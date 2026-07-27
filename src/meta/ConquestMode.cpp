#include "ConquestMode.h"
#include <ctime>
#include <random>
#include <algorithm>
#include <cstdio>
#include "../town/BuildingRegistry.h"

// ── Init / shutdown ──────────────────────────────────────────────────────────

bool ConquestMode::init(const std::string& dbPath)
{
    if (!m_db.open(dbPath)) return false;
    m_hero = m_db.loadHero();
    m_week = isoWeekNumber();

    generateMap(static_cast<uint32_t>(m_week) * 2654435761u); // same seed for the whole week

    // Restore progress if this week's map was already started
    std::string saved = m_db.mapNodeState(m_week);
    if (!saved.empty() && saved.size() == m_nodes.size())
        restoreNodeState(saved);

    m_active = true;
    return true;
}

void ConquestMode::shutdown()
{
    if (m_active && m_hero.exists) m_db.saveHero(m_hero);
    m_db.close();
    m_active = false;
}

// ── Hero / XP ────────────────────────────────────────────────────────────────

void ConquestMode::createHero(const std::string& name, FactionId f, int classId)
{
    m_hero = ConquestHero{};
    m_hero.exists  = true;
    m_hero.name    = name;
    m_hero.faction = f;
    m_hero.classId = classId;
    m_hero.level   = 1;
    m_hero.xp      = 0;
    m_db.saveHero(m_hero);
}

int ConquestMode::xpForLevel(int level)
{
    // Cumulative XP required to REACH `level`: 100 × (level-1)²
    int n = level - 1;
    return 100 * n * n;
}

int ConquestMode::currentLevel() const
{
    int lvl = 1;
    while (m_hero.xp >= xpForLevel(lvl + 1)) ++lvl;
    return lvl;
}

int ConquestMode::grantVictoryRewards(int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= (int)m_nodes.size()) return 0;
    const ConquestNode& n = m_nodes[nodeIndex];

    int nodeTier = 1 + n.depth / 4;                 // rises every 4 depth steps
    if (n.type == ConquestNodeType::Elite) nodeTier += 1;
    if (n.type == ConquestNodeType::Boss)  nodeTier += 2;

    int streak = m_db.winStreak();
    float mult = 1.0f + 0.1f * static_cast<float>(streak);
    int xpGain = static_cast<int>(50.0f * nodeTier * mult);

    int before = currentLevel();
    m_hero.xp += xpGain;
    int after  = currentLevel();
    m_hero.level = after;
    m_db.saveHero(m_hero);
    m_db.setWinStreak(streak + 1);

    // Gold: base by tier, treasure nodes pay extra
    int goldGain = 100 * nodeTier;
    if (n.type == ConquestNodeType::Treasure) goldGain += 250;
    if (n.type == ConquestNodeType::Boss)     goldGain += 400;
    m_db.addGold(goldGain);

    // Chests (Phase 2): side treasures and elites drop Wooden, bosses Golden
    if (n.type == ConquestNodeType::Treasure) grantChest(ChestType::Wooden);
    if (n.type == ConquestNodeType::Elite)    grantChest(ChestType::Wooden);
    if (n.type == ConquestNodeType::Boss)     grantChest(ChestType::Golden);

    // Quest events (Phase 3): treasure nodes don't count as a "battle won".
    reportEvent(QuestEvent::NodeCleared);
    if (n.sideBranch) reportEvent(QuestEvent::SideNodeCleared);
    if (n.type != ConquestNodeType::Treasure) reportEvent(QuestEvent::BattleWon);

    return after - before;
}

void ConquestMode::onDefeat()
{
    m_db.setWinStreak(0);
}

// ── Map generation ───────────────────────────────────────────────────────────

void ConquestMode::generateMap(uint32_t seed)
{
    m_nodes.clear();
    std::mt19937 rng(seed);
    auto chance = [&](float p) {
        return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p;
    };

    // Main chain: 15-18 nodes
    int mainLen = 15 + static_cast<int>(rng() % 4);
    for (int i = 0; i < mainLen; ++i) {
        ConquestNode n;
        n.depth = i;
        n.x = static_cast<float>(i) / static_cast<float>(mainLen - 1);
        n.y = 0.5f;
        if (i == mainLen - 1)                 n.type = ConquestNodeType::Boss;
        else if (i > 2 && i % 5 == 4)         n.type = ConquestNodeType::Elite;
        else                                  n.type = ConquestNodeType::Battle;
        n.state = (i == 0) ? 'A' : 'L';
        m_nodes.push_back(n);
    }
    // Chain links
    for (int i = 0; i + 1 < mainLen; ++i)
        m_nodes[i].next.push_back(i + 1);

    // 2-3 side branches: fork off a mid node, 1-2 nodes long, ending in
    // Treasure, rejoining one step further down the chain.
    int branches = 2 + (chance(0.5f) ? 1 : 0);
    for (int b = 0; b < branches; ++b) {
        int forkAt = 2 + static_cast<int>(rng() % (mainLen - 5));
        float side = (b % 2 == 0) ? 0.22f : 0.78f;

        int fightIdx = -1;
        if (chance(0.6f)) {                     // optional guard fight
            ConquestNode f;
            f.type = ConquestNodeType::Battle;
            f.depth = m_nodes[forkAt].depth + 1;
            f.sideBranch = true;
            f.x = m_nodes[forkAt].x + 0.03f;
            f.y = side;
            f.state = 'L';
            fightIdx = static_cast<int>(m_nodes.size());
            m_nodes.push_back(f);
        }

        ConquestNode t;
        t.type = ConquestNodeType::Treasure;
        t.depth = m_nodes[forkAt].depth + (fightIdx >= 0 ? 2 : 1);
        t.sideBranch = true;
        t.x = m_nodes[forkAt].x + (fightIdx >= 0 ? 0.06f : 0.03f);
        t.y = side;
        t.state = 'L';
        int treasureIdx = static_cast<int>(m_nodes.size());
        m_nodes.push_back(t);

        int rejoin = std::min(forkAt + 2, mainLen - 1);
        if (fightIdx >= 0) {
            m_nodes[forkAt].next.push_back(fightIdx);
            m_nodes[fightIdx].next.push_back(treasureIdx);
        } else {
            m_nodes[forkAt].next.push_back(treasureIdx);
        }
        m_nodes[treasureIdx].next.push_back(rejoin);
    }
}

void ConquestMode::persistNodeState()
{
    std::string s;
    s.reserve(m_nodes.size());
    for (const auto& n : m_nodes) s += n.state;
    m_db.saveMapNodeState(m_week, s);
}

void ConquestMode::restoreNodeState(const std::string& s)
{
    for (size_t i = 0; i < m_nodes.size() && i < s.size(); ++i)
        m_nodes[i].state = s[i];
}

void ConquestMode::clearNode(int index)
{
    if (index < 0 || index >= (int)m_nodes.size()) return;
    m_nodes[index].state = 'C';
    for (int nx : m_nodes[index].next)
        if (nx >= 0 && nx < (int)m_nodes.size() && m_nodes[nx].state == 'L')
            m_nodes[nx].state = 'A';
    persistNodeState();
}

bool ConquestMode::isNodeAvailable(int index) const
{
    return index >= 0 && index < (int)m_nodes.size() && m_nodes[index].state == 'A';
}

int ConquestMode::enemyWeeksForNode(int index) const
{
    if (index < 0 || index >= (int)m_nodes.size()) return 1;
    const ConquestNode& n = m_nodes[index];
    // Depth drives baseline; hero level nudges it so a maxed hero still
    // gets some resistance early in the weekly map.
    int w = 1 + n.depth / 2 + m_hero.level / 4;
    if (n.type == ConquestNodeType::Elite) w += 2;
    if (n.type == ConquestNodeType::Boss)  w += 4;
    return w;
}

FactionId ConquestMode::enemyFactionForNode(int index) const
{
    // Deterministic per node+week so the same node always shows the same enemy.
    uint32_t h = static_cast<uint32_t>(m_week) * 73856093u
               ^ static_cast<uint32_t>(index) * 19349663u;
    return static_cast<FactionId>(h % 9);
}

// ── Week number ──────────────────────────────────────────────────────────────

int ConquestMode::isoWeekNumber()
{
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[8];
    std::strftime(buf, sizeof(buf), "%V", &tmv);      // ISO week 01-53
    int wk = std::atoi(buf);
    return (tmv.tm_year + 1900) * 100 + wk;           // e.g. 202628 — unique per week
}

// ── Chests (Phase 2) ─────────────────────────────────────────────────────────

static const char* chestKey(ConquestMode::ChestType t)
{
    switch (t) {
    case ConquestMode::ChestType::Wooden: return "chest_wooden";
    case ConquestMode::ChestType::Iron:   return "chest_iron";
    case ConquestMode::ChestType::Golden: return "chest_golden";
    case ConquestMode::ChestType::Grand:  return "chest_grand";
    }
    return "chest_wooden";
}

int ConquestMode::chestCount(ChestType t) const
{
    return const_cast<ConquestDB&>(m_db).stateInt(chestKey(t));
}

void ConquestMode::grantChest(ChestType t, int n)
{
    m_db.setStateInt(chestKey(t), m_db.stateInt(chestKey(t)) + n);
}

ConquestMode::ChestResult ConquestMode::openChest(ChestType t, const BuildingRegistry& reg)
{
    ChestResult res;
    int owned = chestCount(t);
    if (owned <= 0) return res;
    m_db.setStateInt(chestKey(t), owned - 1);

    // Non-deterministic on purpose: each chest opening is a fresh roll.
    std::random_device rd;
    std::mt19937 rng(rd());
    auto ri = [&](int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    int drops = 0, maxTier = 2, factions = 1;
    switch (t) {
    case ChestType::Wooden: drops = 3; maxTier = 2; factions = 1; break;
    case ChestType::Iron:   drops = 4; maxTier = 4; factions = 2; break;
    case ChestType::Golden: drops = 5; maxTier = 5; factions = 2; break;
    case ChestType::Grand:  drops = 6; maxTier = 6; factions = 3; break;
    }

    // Pick the faction pool for this chest
    std::vector<FactionId> pool;
    while ((int)pool.size() < factions) {
        FactionId f = static_cast<FactionId>(ri(0, 8));
        bool dup = false;
        for (auto p : pool) if (p == f) dup = true;
        if (!dup) pool.push_back(f);
    }

    for (int d = 0; d < drops; ++d) {
        FactionId f = pool[ri(0, (int)pool.size() - 1)];
        // Tier roll weighted low: tier = 1 + min of two rolls
        int tier = 1 + std::min(ri(0, maxTier - 1), ri(0, maxTier - 1));
        // Grand chest T6 is rare even when rolled: 30% keep, else downgrade
        if (tier == 6 && ri(0, 99) >= 30) tier = 5;
        const UnitDef* u = reg.getUnitDef(f, tier, UpgradePath::None);
        if (!u) continue;
        // Count scales inversely with tier
        int count = std::max(1, ri(1, 7 - tier));
        m_db.collectionAdd(u->id, count);
        res.units.push_back({u->id, count, u->name, tier, f});
    }

    if (t == ChestType::Golden) {
        res.keysFaction = ri(0, 8);
        res.keysGained  = 1;
        m_db.addKeys(res.keysFaction, 1);
    } else if (t == ChestType::Grand) {
        res.keysFaction = ri(0, 8);
        res.keysGained  = 3;
        m_db.addKeys(res.keysFaction, 3);
        res.gemsGained  = ri(50, 100);
        m_db.addGems(res.gemsGained);
    }

    reportEvent(QuestEvent::ChestOpened);   // Phase 3 quest hook
    return res;
}

// ── Quests (Phase 3) ─────────────────────────────────────────────────────────

static long long nowUnix() { return (long long)std::time(nullptr); }

// Midnight (local) after `t`, as unix seconds.
static long long nextMidnight(long long t)
{
    std::time_t tt = (std::time_t)t;
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
    tmv.tm_mday += 1;                       // start of tomorrow
    return (long long)std::mktime(&tmv);
}

void ConquestMode::refreshQuests()
{
    long long now = nowUnix();
    auto rows = m_db.questsAll();

    bool haveDaily = false, haveWeekly = false;
    long long dailyExpiry = 0, weeklyExpiry = 0;
    for (auto& r : rows) {
        if (r.weekly) { haveWeekly = true; weeklyExpiry = r.expiry; }
        else          { haveDaily  = true; dailyExpiry  = r.expiry; }
    }

    std::mt19937 rng((uint32_t)now);
    auto pick = [&](const std::vector<QuestEvent>& pool) {
        return pool[rng() % pool.size()];
    };

    // Daily set: regenerate if missing or expired
    if (!haveDaily || now >= dailyExpiry) {
        m_db.questClear(false);
        long long exp = nextMidnight(now);
        std::vector<QuestEvent> pool = {
            QuestEvent::BattleWon, QuestEvent::NodeCleared,
            QuestEvent::SideNodeCleared, QuestEvent::ChestOpened,
            QuestEvent::MultiFactionWin
        };
        // 3 distinct daily quests
        std::vector<QuestEvent> chosen;
        while (chosen.size() < 3) {
            QuestEvent e = pick(pool);
            bool dup = false; for (auto c : chosen) if (c == e) dup = true;
            if (!dup) chosen.push_back(e);
        }
        for (auto e : chosen) {
            int target = 2;
            switch (e) {
            case QuestEvent::BattleWon:       target = 2 + (rng() % 2); break;
            case QuestEvent::NodeCleared:     target = 3 + (rng() % 3); break;
            case QuestEvent::SideNodeCleared: target = 1 + (rng() % 2); break;
            case QuestEvent::ChestOpened:     target = 2 + (rng() % 2); break;
            case QuestEvent::MultiFactionWin: target = 1; break;
            default: break;
            }
            m_db.questInsert(false, (int)e, 0, target, exp);
        }
    }

    // Weekly set: regenerate if missing or expired
    if (!haveWeekly || now >= weeklyExpiry) {
        m_db.questClear(true);
        long long exp = now + 7 * 24 * 3600;   // 7 days
        std::vector<QuestEvent> pool = {
            QuestEvent::NodeCleared, QuestEvent::ArenaWon, QuestEvent::ChestOpened
        };
        for (auto e : pool) {
            int target = 10;
            switch (e) {
            case QuestEvent::NodeCleared: target = 10; break;
            case QuestEvent::ArenaWon:    target = 5;  break;
            case QuestEvent::ChestOpened: target = 4;  break;
            default: break;
            }
            m_db.questInsert(true, (int)e, 0, target, exp);
        }
    }
}

std::vector<Quest> ConquestMode::quests() const
{
    std::vector<Quest> out;
    for (auto& r : const_cast<ConquestDB&>(m_db).questsAll()) {
        Quest q;
        q.id       = r.id;
        q.weekly   = r.weekly;
        q.event    = (QuestEvent)r.event;
        q.param    = r.param;
        q.progress = r.progress;
        q.target   = r.target;
        q.expiry   = r.expiry;
        q.claimed  = r.claimed;
        q.text     = questText(q.event, q.target, q.weekly);
        out.push_back(q);
    }
    return out;
}

void ConquestMode::reportEvent(QuestEvent e, int count)
{
    for (auto& r : m_db.questsAll()) {
        if (r.claimed) continue;
        if ((QuestEvent)r.event != e) continue;
        int np = std::min(r.target, r.progress + count);
        if (np != r.progress) m_db.questSetProgress(r.id, np);
    }
}

std::string ConquestMode::claimQuest(int questId, const BuildingRegistry& reg)
{
    for (auto& r : m_db.questsAll()) {
        if (r.id != questId) continue;
        if (r.claimed || r.progress < r.target) return "";
        Quest q;
        q.weekly = r.weekly; q.event = (QuestEvent)r.event;
        q.target = r.target;
        QuestReward rw = QuestRewards::forQuest(q);
        switch (rw.kind) {
        case QuestRewardKind::Gold:        m_db.addGold(rw.amount); break;
        case QuestRewardKind::Gems:        m_db.addGems(rw.amount); break;
        case QuestRewardKind::WoodenChest: grantChest(ChestType::Wooden, rw.amount); break;
        case QuestRewardKind::IronChest:   grantChest(ChestType::Iron,   rw.amount); break;
        case QuestRewardKind::Key: {
            std::random_device rd; std::mt19937 rng(rd());
            m_db.addKeys(rng() % 9, rw.amount);
            break;
        }
        }
        m_db.questSetClaimed(r.id, true);
        (void)reg;
        return QuestRewards::describe(rw);
    }
    return "";
}

// ── Gem spending (Phase 3) ───────────────────────────────────────────────────

int ConquestMode::chestGemPrice(ChestType t)
{
    switch (t) {
    case ChestType::Wooden: return 20;
    case ChestType::Iron:   return 50;
    case ChestType::Golden: return 150;
    case ChestType::Grand:  return 400;
    }
    return 50;
}

bool ConquestMode::buyChestWithGems(ChestType t)
{
    int price = chestGemPrice(t);
    if (m_db.gems() < price) return false;
    m_db.addGems(-price);
    grantChest(t, 1);
    return true;
}

bool ConquestMode::spendGems(int amount)
{
    if (m_db.gems() < amount) return false;
    m_db.addGems(-amount);
    return true;
}

bool ConquestMode::exchangeGoldForGems(int gemsWanted)
{
    if (gemsWanted <= 0) return false;
    int cost = gemsWanted * GOLD_PER_GEM;
    if (m_db.gold() < cost) return false;
    m_db.addGold(-cost);
    m_db.addGems(gemsWanted);
    return true;
}

// ── Keys & path upgrades (Phase 4) ───────────────────────────────────────────

int ConquestMode::keyCostForTier(int tier)
{
    switch (tier) {
    case 1: return 1;
    case 2: return 2;
    case 3: return 3;
    case 4: return 5;
    case 5: return 8;
    default: return 99;   // T6 has no paths
    }
}

bool ConquestMode::chooseUnitPath(int faction, int tier, int choice)
{
    if (tier < 1 || tier > 5) return false;
    if (choice != 1 && choice != 2) return false;
    if (m_db.pathChoice(faction, tier) != 0) return false;   // already chosen
    int cost = keyCostForTier(tier);
    if (m_db.keyCount(faction) < cost) return false;
    m_db.addKeys(faction, -cost);
    m_db.setPathChoice(faction, tier, choice);
    return true;
}

bool ConquestMode::respecUnitPath(int faction, int tier, int newChoice)
{
    if (tier < 1 || tier > 5) return false;
    if (newChoice != 1 && newChoice != 2) return false;
    int cur = m_db.pathChoice(faction, tier);
    if (cur == 0) return chooseUnitPath(faction, tier, newChoice);  // not yet chosen
    if (cur == newChoice) return false;                             // no change
    if (m_db.gems() < respecGemCost()) return false;
    m_db.addGems(-respecGemCost());
    m_db.setPathChoice(faction, tier, newChoice);
    return true;
}

int ConquestMode::resolveVariant(int baseDefId, const BuildingRegistry& reg) const
{
    const UnitDef* base = reg.getUnitDef(baseDefId);
    if (!base) return baseDefId;
    if (base->tier < 1 || base->tier > 5) return baseDefId;   // T6 = no paths
    int choice = m_db.pathChoice((int)base->faction, base->tier);
    if (choice == 0) return baseDefId;
    UpgradePath p = (choice == 1) ? UpgradePath::PathA : UpgradePath::PathB;
    const UnitDef* variant = reg.getUnitDef(base->faction, base->tier, p);
    return variant ? variant->id : baseDefId;
}

// ── Arena (Phase 5) ──────────────────────────────────────────────────────────

static int localDayNumber()
{
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return (tmv.tm_year + 1900) * 1000 + tmv.tm_yday;   // unique per calendar day
}

int ConquestMode::teamPower(const BuildingRegistry& reg) const
{
    // power = Σ(count × tier² × pathBonus) + heroLevel×10
    int power = 0;
    for (auto& [defId, count] : m_db.teamGet()) {
        int variantId = resolveVariant(defId, reg);
        const UnitDef* d = reg.getUnitDef(variantId);
        if (!d) d = reg.getUnitDef(defId);
        if (!d) continue;
        float pathBonus = (d->path == UpgradePath::None) ? 1.0f : 1.15f;
        power += (int)(count * d->tier * d->tier * pathBonus);
    }
    power += currentLevel() * 10;
    return power;
}

int ConquestMode::arenaPoints() const
{
    return const_cast<ConquestDB&>(m_db).arenaGet(m_week).points;
}

int ConquestMode::arenaEntriesLeft() const
{
    auto r = const_cast<ConquestDB&>(m_db).arenaGet(m_week);
    int today = localDayNumber();
    if (r.lastEntryDay != today) return ARENA_ENTRIES_PER_DAY;   // fresh day
    return std::max(0, ARENA_ENTRIES_PER_DAY - r.entriesToday);
}

bool ConquestMode::arenaCanEnter() const
{
    if (arenaEntriesLeft() > 0) return true;
    return m_db.gems() >= ARENA_EXTRA_ENTRY_GEMS;
}

bool ConquestMode::arenaConsumeEntry()
{
    auto r = m_db.arenaGet(m_week);
    int today = localDayNumber();
    if (r.lastEntryDay != today) { r.entriesToday = 0; r.lastEntryDay = today; }

    if (r.entriesToday < ARENA_ENTRIES_PER_DAY) {
        r.entriesToday += 1;
        m_db.arenaSet(r);
        return true;
    }
    // Out of free tries — pay gems
    if (m_db.gems() >= ARENA_EXTRA_ENTRY_GEMS) {
        m_db.addGems(-ARENA_EXTRA_ENTRY_GEMS);
        r.entriesToday += 1;
        m_db.arenaSet(r);
        return true;
    }
    return false;
}

int ConquestMode::arenaReportResult(bool won)
{
    auto r = m_db.arenaGet(m_week);
    int delta = won ? (20 + m_arenaStreak * 5) : -10;
    r.points = std::max(0, r.points + delta);
    m_db.arenaSet(r);

    if (won) {
        m_arenaStreak += 1;
        m_db.addGold(150);
        // 3-win streak → Golden chest, then streak resets
        if (m_arenaStreak >= 3) {
            grantChest(ChestType::Golden);
            m_arenaStreak = 0;
        }
        reportEvent(QuestEvent::ArenaWon);
    } else {
        m_arenaStreak = 0;
    }
    return delta;
}

int ConquestMode::arenaOpponentPower(const BuildingRegistry& reg) const
{
    int base = teamPower(reg);
    if (base <= 0) base = 50 + currentLevel() * 10;   // no team set yet
    // Deterministic 0.95-1.15× jitter seeded by week+points so the "ladder"
    // opponent is stable within a sitting but climbs as you win.
    uint32_t seed = (uint32_t)m_week * 2246822519u
                  ^ (uint32_t)arenaPoints() * 3266489917u;
    std::mt19937 rng(seed);
    float mult = 0.95f + std::uniform_real_distribution<float>(0.f, 0.20f)(rng);
    return (int)(base * mult);
}

// ── Cheap recruits (Phase 2 revised) ─────────────────────────────────────────
int ConquestMode::buyRecruits(FactionId faction, int tier, int count, const BuildingRegistry& reg)
{
    if (tier < 1 || tier > 3 || count <= 0) return 0;
    const UnitDef* d = reg.getUnitDef(faction, tier, UpgradePath::None);
    if (!d) return 0;
    int price = recruitGoldPrice(tier);
    int affordable = m_db.gold() / price;
    int buy = std::min(count, affordable);
    if (buy <= 0) return 0;
    m_db.addGold(-buy * price);
    m_db.collectionAdd(d->id, buy);
    return buy;
}
