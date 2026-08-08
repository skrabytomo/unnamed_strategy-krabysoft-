#include "Game.h"
#include "../platform/SteamIntegration.h"
#include "../meta/ScoreDB.h"
#include "WorkerPool.h"
#include <ctime>
#include <chrono>

// TEMP INSTRUMENT (THREADING.md Phase 4 claims the per-step candidate rescan is
// the dominant cost and a bigger win than threading). Measure it rather than
// trust it — the same document's fan-out advice turned out to be a pessimism.
// Accumulated across one doEndTurn, logged and reset at the end of the turn.
static long long g_candNs     = 0;
static long long g_pathNs     = 0;
static int       g_candBuilds = 0;
// Split the remaining A* cost by horizon: the 400-hex cross-map march search
// vs the cheap 60-hex local one. Tells us which to attack next.
static long long g_longNs     = 0;
static int       g_longCalls  = 0;
static int       g_longFails  = 0;  // long searches that came back empty
static long long g_shortNs    = 0;
static int       g_shortCalls = 0;
static int       g_reuseHits  = 0;
static int       g_aiSlices   = 0;  // frames a spread AI round used (Watch mode)
#include "../hero/LevelUpSystem.h"
#include "../hero/SkillRegistry.h"
#include "../hero/HeroClass.h"
#include "../hero/Artifacts.h"
#include "../magic/SpellRegistry.h"
#include "../world/HexGrid.h"
#include "../town/UnitDef.h"
#include "../sim/ArmyBuilder.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <stdio.h>

static constexpr float MOVE_SPEED = 4.0f;


// ── Per-faction combat unit templates ─────────────────────────────────────────
// Fallback used only when a hero has zero recruited units (fresh hero, Arena
// phantom opponent, etc). Delegates to ArmyBuilder — the single source of
// truth for unit stats (BuildingRegistry) — rather than keeping a second,
// separately-maintained roster that silently drifts out of sync on renames.
static std::vector<CombatUnit> makeFactionUnits(FactionId faction, bool isPlayer, int heroLevel = 1)
{
    // Invert ArmyBuilder::heroLevelFromWeeks (level = 1 + floor(sqrt(weeks*2)))
    // to get an approximate week from the hero's level for this one-off fallback.
    int lvl = std::max(1, heroLevel);
    int weeks = std::max(1, ((lvl - 1) * (lvl - 1)) / 2);

    auto army = ArmyBuilder::buildArmy(faction, weeks);
    int nextId = isPlayer ? 1 : 50;
    for (auto& u : army) {
        u.id = nextId++;
        u.isPlayer = isPlayer;
    }
    return army;
}

// Generate mine guard units deterministically from mine position + resource type
static std::vector<CombatUnit> makeMineGuardUnits(const ResourceNode& r, int week = 1)
{
    uint32_t seed = (uint32_t)(r.pos.q * 73856093u)
                  ^ (uint32_t)(r.pos.r * 19349663u)
                  ^ (uint32_t)((int)r.type * 83492791u);
    auto xr = [&]() -> uint32_t {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
    };
    auto rnd = [&](int lo, int hi) -> int {
        return lo + (int)(xr() % (unsigned)(hi - lo + 1));
    };

    // Map resource type → faction hint for sprite display
    // Gold=any(6=IronAssembly), Iron=IronAssembly, FaithStones=HolyOrder,
    // BloodEssence=Bloodsworn, VerdantSap=Thornkin, Mercury=EternalEmpire
    static const int kFactionHint[6] = { 6, 6, 0, 4, 2, 3 }; // indexed by ResourceType
    int rtype = std::clamp((int)r.type, 0, 5);
    int faction = kFactionHint[rtype];

    struct GN { const char* light; const char* heavy; };
    static const GN gNames[6] = {
        {"Iron Mercenary",  "Iron Sentinel"},    // Gold / Iron → IronAssembly look
        {"Iron Custodian",  "Forge Warden"},     // Iron → IronAssembly
        {"Temple Guard",    "Holy Warden"},      // FaithStones → HolyOrder
        {"Blood Hunter",    "Crimson Reaver"},   // BloodEssence → Bloodsworn
        {"Grove Warden",    "Thornborn"},        // VerdantSap → Thornkin
        {"Spectral Keeper", "Wraith Sentinel"},  // Mercury → EternalEmpire
    };
    int ni   = rtype;
    int tier = std::clamp(r.amount, 1, 5);

    // Scale guard counts by week: +15% per week, capped at 3× at week 15
    float weekMult = std::min(3.0f, 1.0f + (week - 1) * 0.15f);

    CombatUnit g1;
    g1.name        = gNames[ni].light;
    g1.factionHint = faction;
    g1.count       = static_cast<int>(std::round(rnd(3 + tier * 2, 6 + tier * 3) * weekMult));
    g1.maxHp       = g1.hp = 5 + tier * 2;
    g1.attack      = 2 + tier;
    g1.defense     = 1 + tier;
    g1.speed       = 4;
    g1.isPlayer    = false;

    CombatUnit g2;
    g2.name        = gNames[ni].heavy;
    g2.factionHint = faction;
    g2.count       = static_cast<int>(std::round(rnd(2 + tier, 4 + tier * 2) * weekMult));
    g2.maxHp       = g2.hp = 8 + tier * 3;
    g2.attack      = 3 + tier;
    g2.defense     = 2 + tier;
    g2.speed       = 3;
    g2.isPlayer    = false;

    return {g1, g2};
}

// Build CombatUnits from hero's actual army; falls back to faction template if army empty
static std::vector<CombatUnit> makeHeroUnits(const Hero& hero,
    const std::vector<UnitDef>& defs, bool isPlayer)
{
    if (hero.eliminated) return {};
    if (hero.army.empty())
        return makeFactionUnits(hero.faction, isPlayer, hero.level);

    std::vector<CombatUnit> out;
    int nextId = isPlayer ? 1 : 50;
    for (const auto& stack : hero.army) {
        if (stack.count <= 0) continue;
        const UnitDef* ud = nullptr;
        for (const auto& d : defs) if (d.id == stack.defId) { ud = &d; break; }
        if (!ud) continue;
        CombatUnit u;
        u.id = nextId++;
        u.defId = ud->id;
        u.name = ud->name;
        u.count = stack.count;
        u.hp = u.maxHp = ud->hp;
        u.attack   = ud->attack;
        u.defense  = ud->defense;
        u.damageMin = ud->damage_min;
        u.damageMax = ud->damage_max;
        u.speed    = ud->speed;
        u.range    = ud->range;
        u.shots    = u.shotsLeft = ud->shots;
        u.flying             = ud->flying;
        u.vampiric           = ud->vampiric;
        u.regenerates        = ud->regenerates;
        u.tags               = ud->tags;
        u.isPlayer           = isPlayer;
        u.hasSecondLife      = ud->hasSecondLife || (hero.faction == FactionId::EternalEmpire && ud->path != UpgradePath::PathB);
        u.secondLifeFullHeal = ud->secondLifeFullHeal;
        u.moraleImmune       = ud->moraleImmune || hasTag(ud->tags, UnitTag::Undead) || hasTag(ud->tags, UnitTag::Mechanical);
        u.rapidEvolution     = ud->rapidEvolution;
        u.adaptationDouble   = ud->adaptationDouble;
        out.push_back(u);
    }
    return out.empty() ? makeFactionUnits(hero.faction, isPlayer, hero.level) : out;
}

// Estimated combat strength: sum(count * hp * attack) per stack
static int heroStrength(const Hero& hero, const std::vector<UnitDef>& defs)
{
    if (hero.eliminated) return 0;
    if (hero.army.empty()) {
        auto units = makeFactionUnits(hero.faction, true, hero.level);
        int s = 0;
        for (auto& u : units) s += u.count * u.hp * u.attack;
        return s;
    }
    int s = 0;
    for (const auto& stack : hero.army) {
        if (stack.count <= 0) continue;
        for (const auto& d : defs)
            if (d.id == stack.defId) { s += stack.count * d.hp * d.attack; break; }
    }
    return s;
}

// Combat strength of a bare stack list (town garrison) — no faction-template
// fallback, an empty garrison is genuinely worth zero.
static int stacksStrength(const std::vector<UnitStack>& stacks, const std::vector<UnitDef>& defs)
{
    int s = 0;
    for (const auto& st : stacks) {
        if (st.count <= 0) continue;
        for (const auto& d : defs)
            if (d.id == st.defId) { s += st.count * d.hp * d.attack; break; }
    }
    return s;
}

// Watch-mode support heroes are identified by their role name; everything
// else in m_heroes counts as a "main" hero the watch driver can fight with.
static bool isWatchSupportName(const std::string& n)
{
    return n == "Supply Courier" || n == "Scout Rider" || n == "Scout Vanguard";
}

// ── Tech scouting #4: strategic town abandonment (AI_ROADMAP) ─────────────────
// A town is a WRITE-OFF when a non-allied hero within striking range (8 hexes)
// fields at least 3x the strength that would defend it (current garrison plus
// extraDefStr — pass a rescuing hero's strength to ask "even with me?"), AND
// the owner holds a better-developed town elsewhere. Pouring gold or a rescue
// hero into that fight is throwing value away; the AI redeploys instead.
// Never true for the owner's last town or most-built town — those fights are
// existential and are always taken.
bool Game::aiTownIsWriteOff(const Town& t, int extraDefStr,
                            const std::vector<UnitDef>& defs) const
{
    int owned = 0;
    size_t bestBuilt = 0;
    for (const auto& o : m_towns)
        if (o.ownerId == t.ownerId) {
            ++owned;
            bestBuilt = std::max(bestBuilt, o.builtBuildings.size());
        }
    if (owned <= 1) return false;                      // last town: fight for it
    if (t.builtBuildings.size() >= bestBuilt) return false; // best town: keep it
    int64_t defStr = (int64_t)stacksStrength(t.garrison, defs) + extraDefStr;
    auto overwhelms = [&](const Hero& h) {
        return !h.eliminated
            && !isAllied(h.ownerId, t.ownerId)
            && HexGrid::distance(h.pos, t.pos) <= 8
            && (int64_t)heroStrength(h, defs) >= std::max<int64_t>(1, defStr) * 3;
    };
    for (const auto& oh : m_enemyHeroes) if (overwhelms(oh)) return true;
    for (const auto& ph : m_heroes)      if (overwhelms(ph)) return true;
    return false;
}

// ── Fair-economy AI helpers ───────────────────────────────────────────────────
// AI sides recruit through the same paid path as the human player
// (Town::recruit — real unit costs, partial-affordable fallback).

// Recruit everything `pool` can afford from a town's dwellings into `into`
// (a hero army or the town garrison). Higher tiers first: better value per
// gold and the 7-slot cap favors quality. Returns units recruited.
static int aiPaidRecruit(Town& town, std::vector<UnitStack>& into,
                         Resources& pool, const std::vector<UnitDef>& unitDefs)
{
    int total = 0;
    for (int tier = 6; tier >= 1; --tier) {
        const DwellingState* dw = nullptr;
        for (const auto& d : town.dwellings) if (d.tier == tier) { dw = &d; break; }
        if (!dw || dw->available <= 0) continue;
        const UnitDef* udef = nullptr;
        for (const auto& u : unitDefs)
            if (u.faction == town.faction && u.tier == tier && u.path == dw->path) {
                udef = &u; break;
            }
        if (!udef) continue;
        // Respect the 7-slot cap before paying
        bool canMerge = false;
        for (const auto& s : into) if (s.defId == udef->id) { canMerge = true; break; }
        if (!canMerge && into.size() >= 7) continue;
        int got = town.recruit(tier, dw->available, pool, unitDefs);
        if (got <= 0) continue;
        if (canMerge) {
            for (auto& s : into) if (s.defId == udef->id) { s.count += got; break; }
        } else {
            into.push_back({udef->id, got});
        }
        total += got;
    }
    return total;
}

// Move a town's garrison into a hero's army (merge; respect the 7-slot cap —
// whatever doesn't fit stays behind as garrison).
static void takeGarrison(Town& town, Hero& hero, const std::vector<UnitDef>& defs)
{
    // Merge same-defId stacks first (hero + garrison combined pool).
    std::vector<UnitStack> pool = hero.army;
    for (const auto& g : town.garrison) {
        bool merged = false;
        for (auto& s : pool) if (s.defId == g.defId) { s.count += g.count; merged = true; break; }
        if (!merged) pool.push_back(g);
    }
    // Per-unit value (hp*attack) to rank stacks — keep the strongest 7 in the
    // hero, deposit the rest back into the garrison so nothing is wasted and a
    // captured town's strong units bump weak fodder out of the field army.
    auto unitVal = [&](int defId)->long long{
        for (const auto& d : defs) if (d.id == defId) return (long long)d.hp * d.attack;
        return 0;
    };
    std::sort(pool.begin(), pool.end(), [&](const UnitStack& a, const UnitStack& b){
        return unitVal(a.defId) > unitVal(b.defId);
    });
    hero.army.clear();
    town.garrison.clear();
    for (size_t i = 0; i < pool.size(); ++i) {
        if (pool[i].count <= 0) continue;
        if (hero.army.size() < 7) hero.army.push_back(pool[i]);
        else                      town.garrison.push_back(pool[i]);  // leftover defends the town
    }
}

// Sites that persist on the map — never consumed by an AI walk-over collect.
// (Blanket `collected = true` used to silently delete dwellings, shipyards,
// quest givers, even the player's quest target.)
static bool isPersistentSite(WorldObjectType t)
{
    switch (t) {
    case WorldObjectType::UnitDwelling:
    case WorldObjectType::Shipyard:
    case WorldObjectType::FishingHouse:
    case WorldObjectType::NeutralOutpost:
    case WorldObjectType::WitchHut:
    case WorldObjectType::Observatory:
    case WorldObjectType::HolyFountain:
    case WorldObjectType::Oasis:
    case WorldObjectType::ArtifactMerchant:
    case WorldObjectType::Arena:
    case WorldObjectType::QuestGiver:
    case WorldObjectType::QuestTarget:
    case WorldObjectType::Barrier:
    case WorldObjectType::ChokeGuard:
    case WorldObjectType::CursedGround:
    case WorldObjectType::Stables:
    case WorldObjectType::TreeOfKnowledge:
        return true;
    default: return false;
    }
}

// AI/watch heroes buy from external dwellings at the same 50g-per-tier rate
// the human pays in the dwelling popup.
static int dwellingPaidRecruit(WorldObject& obj, std::vector<UnitStack>& into,
                               Resources& payer, const std::vector<UnitDef>& udefs)
{
    if (obj.type != WorldObjectType::UnitDwelling || obj.available <= 0) return 0;
    int costPer = std::max(1, obj.value * 50);
    int qty = std::min(payer.get(ResourceType::Gold) / costPer, obj.available);
    if (qty <= 0) return 0;
    for (const auto& ud : udefs) {
        if (ud.faction == static_cast<FactionId>(obj.faction) && ud.tier == obj.value
            && ud.path == UpgradePath::None) {
            bool merged = false;
            for (auto& s : into) if (s.defId == ud.id) { s.count += qty; merged = true; break; }
            if (!merged) {
                if (into.size() >= 7) return 0;
                into.push_back({ud.id, qty});
            }
            payer.add(ResourceType::Gold, -qty * costPer);
            obj.available -= qty;
            return qty;
        }
    }
    return 0;
}

// A landlocked AI hero heads for a shipyard and buys a boat with its side's
// real gold — the only way off an island once every land target is exhausted.
// On the shipyard tile: buy (if affordable). Otherwise: return a path to it.
// Returns true if `outPath` was filled (caller keeps moving); buying returns
// false so the caller re-scores candidates with water now traversable.
// Why a hero that wanted passage didn't get it. Naval failures used to be
// completely silent, which is how "every town built a Shipyard, no hero ever
// sailed" survived a full 80-week game unnoticed. The caller logs this at most
// once per hero per week.
enum class BoatFail { None, NoDock, NoCoast, NoGold, NoPathToDock };
static BoatFail g_lastBoatFail = BoatFail::None;
// Distance to the dock we gave up on, so the log distinguishes "my own dock is
// 4 hexes away and something is blocking the tile" from "it is 90 hexes away
// across an ocean".
static int      g_lastBoatDockDist = -1;
static HexCoord g_lastBoatDockPos{0, 0};      // the dock actually tried
static size_t   g_lastBoatWalkableDocks = 0;  // docks surviving the connectivity filter
static const char* g_lastBoatPathExit = "";   // Pathfinder's exit reason
static const char* boatFailName(BoatFail f)
{
    switch (f) {
        case BoatFail::NoDock:       return "no allied Shipyard town exists";
        case BoatFail::NoCoast:      return "shipyard town has no free adjacent water tile";
        case BoatFail::NoGold:       return "cannot afford a hull";
        case BoatFail::NoPathToDock: return "no land route to the shipyard";
        case BoatFail::None:         break;
    }
    return "ok";
}

// `sameLandmass` is the caller's O(1) land-connectivity test. Without it this
// function spends its whole A* budget on docks it can never walk to: the map
// scatters coastal Shipyard OBJECTS across every island, they sort nearest-
// first by hex distance, and they filled every attempt while the hero's own
// reachable town dock was never tried. That is the actual reason heroes kept
// reporting "no land route to the shipyard" — not a search-budget limit.
static bool aiTryBoat(HexMap& map, std::vector<WorldObject>& objs,
                      const std::vector<HexCoord>& townDocks,
                      Hero& hero, Resources& payer,
                      const Pathfinder::CostFn& costFn,
                      const std::function<bool(HexCoord)>& sameLandmass,
                      std::vector<HexCoord>& outPath)
{
    g_lastBoatFail = BoatFail::None;
    if (hero.onBoat) return false;
    // Nearest boat source: a coastal Shipyard object OR an owned town that
    // built the Shipyard structure.
    // Gather EVERY dock, nearest first. Committing to only the closest one by
    // hex distance meant a single unroutable dock (across a bay, behind
    // mountains) killed naval movement outright even with usable docks
    // further away — measured as "wants passage (1 dock) but no land route
    // to the shipyard", repeating for the rest of the game.
    std::vector<std::pair<int, HexCoord>> allDocks;
    for (auto& obj : objs)
        if (obj.type == WorldObjectType::Shipyard)
            allDocks.push_back({HexGrid::distance(hero.pos, obj.pos), obj.pos});
    for (const auto& dp : townDocks)
        allDocks.push_back({HexGrid::distance(hero.pos, dp), dp});
    if (allDocks.empty()) { g_lastBoatFail = BoatFail::NoDock; return false; }
    // Drop docks we provably cannot walk to (different land component) BEFORE
    // spending any A* on them. This is the fix for the "3 tries all wasted on
    // island shipyards" failure, and it also removes the pathological cost of
    // repeatedly searching for routes that cannot exist.
    {
        std::vector<std::pair<int, HexCoord>> walkable;
        for (const auto& d : allDocks) {
            if (d.first == 0) { walkable.push_back(d); continue; }
            // The dock tile must be one this hero can actually stand on.
            // Coastal Shipyard OBJECTS sit on WATER, so a land hero can never
            // reach one — and the land-connectivity test cannot reject them,
            // because water tiles are absent from the land component map and
            // routeImpossible() answers "unknown, let A* decide" for absent
            // tiles. They therefore passed the filter and then failed as an
            // impassable goal. This was the real "no land route to the
            // shipyard": not a search budget, not connectivity, just a dock
            // standing in the sea.
            if (costFn(d.second) >= 99) continue;
            if (sameLandmass && !sameLandmass(d.second)) continue;
            walkable.push_back(d);
        }
        g_lastBoatWalkableDocks = walkable.size();
        if (!walkable.empty()) allDocks.swap(walkable);
    }
    std::sort(allDocks.begin(), allDocks.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    // Distance to the nearest dock — 0 means the hero is standing on it and
    // boards below; the destination itself is chosen in the reachability loop.
    int bestD = allDocks.front().first;

    if (bestD == 0) {
        // Hull choice: a rich side buys a War hull (it sinks anything it meets
        // on the way over), otherwise the fast Travel ferry, falling back to the
        // cheap Fishing hull when funds are thin.
        int purse = payer.get(ResourceType::Gold);
        BoatType want = BoatType::Travel;
        if (purse >= BOAT_BASE_COST[static_cast<int>(BoatType::War)] + hero.boatCount * 1000 + 4000)
            want = BoatType::War;
        else if (purse < BOAT_BASE_COST[static_cast<int>(BoatType::Travel)] + hero.boatCount * 1000)
            want = BoatType::Fishing;
        int goldCost = BOAT_BASE_COST[static_cast<int>(want)] + hero.boatCount * 1000;
        if (payer.get(ResourceType::Gold) >= goldCost) {
            hero.boatType = want;
            // LAUNCH onto an adjacent water hex as part of boarding. Simply
            // setting onBoat while the hero still stands on the land dock was
            // the real "24 boats bought, never seen crossing water" bug: the
            // hero's very next step was overland, which instantly disembarked
            // it (onBoat=false), burning the boat and re-buying next turn,
            // forever. You board a boat onto the water, not carry it inland.
            HexCoord launch{}; bool haveLaunch = false;
            for (const auto& nb : HexGrid::neighbors(hero.pos)) {
                const HexTile* nt = map.getTile(nb);
                if (nt && nt->terrain == Terrain::Water && !nt->blocked && nt->heroId == 0) {
                    launch = nb; haveLaunch = true; break;
                }
            }
            if (!haveLaunch) {               // dock has no free coast tile
                g_lastBoatFail = BoatFail::NoCoast;
                return false;
            }
            payer.add(ResourceType::Gold, -goldCost);
            if (HexTile* oldT = map.getTile(hero.pos)) oldT->heroId = 0;
            hero.pos     = launch;
            hero.onBoat  = true;
            hero.boatCount += 1;
            if (HexTile* nT = map.getTile(hero.pos)) nT->heroId = hero.id;
            gLog("%s launched a boat at shipyard (-%dg)\n", hero.name.c_str(), goldCost);
        }
        else g_lastBoatFail = BoatFail::NoGold;
        return false;  // re-score: hero is afloat now
    }
    // Walk the dock list nearest-first and take the first one we can actually
    // reach, instead of failing on the closest unroutable one. Capped so a
    // hero with many docks doesn't pay for a dozen A* searches every turn.
    constexpr size_t kMaxDockTries = 3;
    // Budget for the walk to the dock. Pathfinder's defaults (maxCost 999,
    // maxNodes 20000) are tuned for ordinary short hops and are NOT enough
    // here: hex distance badly understates the real walk on a ring or a
    // coastline, so a dock 40 hexes away can be a 200+ hex trek around the
    // land. Measured on a Ring map — "wants passage (1 dock) but no land
    // route to the shipyard [nearest dock 40 hexes, sameLandmass=1]", i.e.
    // connectivity said reachable while A* ran out of nodes. This search
    // only runs when a hero actually wants passage, so the wider budget is
    // affordable.
    // Modest budget on purpose. Two attempts (2026-07-25) blamed these limits
    // for the "no land route to the shipyard" failure and inflated them
    // (maxNodes 120k, then maxCost 4000). Neither produced a single boat, so
    // the limits were never the cause — reverted to a normal budget.
    // Do not raise these without evidence that a REACHABLE dock failed.
    //
    // Correction to an earlier note here: those attempts were also blamed for a
    // "5-6x AI turn slowdown". That was a measurement error — Ring-map turns
    // were compared against Hexagon-map turns. The pre-change Ring baseline was
    // already ~610-740ms (max 772ms), i.e. the same range. Ring maps are simply
    // expensive; nothing here regressed it.
    constexpr int kDockMaxCost  = 1200;
    constexpr int kDockMaxNodes = 30000;
    size_t tries = 0;
    for (const auto& [d, dp] : allDocks) {
        if (d == 0) continue;                 // handled by the bestD==0 branch
        if (++tries > kMaxDockTries) break;
        outPath = Pathfinder::find(map, hero.pos, dp, costFn,
                                   kDockMaxCost, kDockMaxNodes);
        if (!outPath.empty()) return true;
        // Record the dock we actually tried and exactly why it was rejected.
        // The previous log reported connectivity for a DIFFERENT dock than the
        // one that failed, which sent two fix attempts chasing search limits.
        g_lastBoatDockPos  = dp;
        g_lastBoatDockDist = d;
        g_lastBoatPathExit = Pathfinder::exitName(Pathfinder::lastExit());
    }
    g_lastBoatFail = BoatFail::NoPathToDock;
    g_lastBoatDockDist = allDocks.front().first;
    outPath.clear();
    return false;
}

// Starting retinue for a hired hero — same formula the human tavern uses
// (T1 dwelling weeklyGrowth + T2 weeklyGrowth/3), included in the hire fee.
static void giveTavernRetinue(Hero& h, const std::vector<BuildingDef>& buildings,
                              const std::vector<UnitDef>& unitDefs)
{
    for (int tier : {1, 2}) {
        int growth = 0;
        for (const auto& bd : buildings) {
            if (bd.faction == h.faction && bd.tier == tier
                && bd.category == BuildingCategory::UnitDwelling
                && bd.path == UpgradePath::None) {
                growth = (tier == 1) ? bd.weeklyGrowth : bd.weeklyGrowth / 3;
                break;
            }
        }
        if (growth <= 0) continue;
        for (const auto& ud : unitDefs) {
            if (ud.faction == h.faction && ud.tier == tier
                && ud.path == UpgradePath::None) {
                h.army.push_back({ud.id, growth});
                break;
            }
        }
    }
}

// ── File-scope AI constants ───────────────────────────────────────────────────

// Per-faction key resource — used for mine denial (player) and mine focus (enemy)
static const ResourceType kFactionResource[9] = {
    ResourceType::FaithStones,   // HolyOrder
    ResourceType::FaithStones,   // CrimsonWardens
    ResourceType::VerdantSap,    // Thornkin
    ResourceType::Mercury,       // EternalEmpire
    ResourceType::BloodEssence,  // Bloodsworn
    ResourceType::Mercury,       // Voidkin
    ResourceType::Iron,          // IronAssembly
    ResourceType::BloodEssence,  // Amalgamate
    ResourceType::Gold,          // Convergence
};

// Faction-specific build priority order
static const std::vector<int> kBuildOrder[9] = {
    { BID::HO_HALL, BID::HO_T1_BASE, BID::FORT, BID::CITADEL, BID::CASTLE, BID::MARKET,
      BID::MAGE_GUILD, BID::HO_LIGHT_SHRINE, BID::HO_T2_BASE,
      BID::TOWN_HALL, BID::HO_T3_BASE, BID::HO_T3_A,
      BID::MAGE_GUILD_T2, BID::HO_T4_BASE, BID::HO_T4_A,
      BID::CITY_HALL, BID::HO_T5_BASE, BID::HO_T5_A,
      BID::HO_RELIQUARY, BID::HO_T6_BASE, BID::HO_T6_A },
    { BID::CW_HALL, BID::CW_T1, BID::CW_T1_A, BID::MARKET, BID::CW_T2, BID::CW_T2_A,
      BID::FORT, BID::CITADEL, BID::CASTLE, BID::CW_WARDEN_BRAND, BID::CW_T3, BID::CW_T3_A,
      BID::TOWN_HALL, BID::CW_DEATH_ALTAR, BID::CW_T4, BID::CW_T4_A,
      BID::CITY_HALL, BID::CW_T5, BID::CW_T5_A, BID::CW_T6, BID::CW_T6_A },
    { BID::TK_GROVE_HEART, BID::TK_T1, BID::TK_T1_A, BID::TK_SYMBIOSIS_WEB,
      BID::MARKET, BID::TK_T2, BID::TK_T2_A,
      BID::TK_ANCIENT_CIRCLE, BID::FORT, BID::CITADEL, BID::CASTLE, BID::TK_T3, BID::TK_T3_A,
      BID::TOWN_HALL, BID::TK_T4, BID::TK_T4_A,
      BID::CITY_HALL, BID::TK_T5, BID::TK_T5_A, BID::TK_T6, BID::TK_T6_A },
    { BID::EE_THRONE, BID::EE_T1, BID::EE_T1_A, BID::EE_NECROPOLIS,
      BID::FORT, BID::CITADEL, BID::CASTLE, BID::EE_T2, BID::EE_T2_A, BID::MARKET,
      BID::EE_MONUMENT, BID::EE_T3, BID::EE_T3_A,
      BID::TOWN_HALL, BID::EE_T4, BID::EE_T4_A,
      BID::MAGE_GUILD, BID::CITY_HALL, BID::EE_T5, BID::EE_T5_A,
      BID::EE_T6, BID::EE_T6_A },
    { BID::BS_WAR_HALL, BID::BS_T1, BID::BS_T1_A, BID::BS_T2, BID::BS_T2_A,
      BID::FORT, BID::CITADEL, BID::CASTLE, BID::MARKET, BID::BS_T3, BID::BS_T3_A,
      BID::BS_BLOOD_ALTAR, BID::BS_WAR_SHRINE,
      BID::TOWN_HALL, BID::BS_T4, BID::BS_T4_A,
      BID::CITY_HALL, BID::BS_T5, BID::BS_T5_A, BID::BS_T6, BID::BS_T6_A },
    { BID::VK_NEXUS, BID::MARKET, BID::VK_T1, BID::VK_T1_A,
      BID::MAGE_GUILD, BID::VK_T2, BID::VK_T2_A,
      BID::FORT, BID::CITADEL, BID::CASTLE, BID::VK_RIFT_GATE, BID::VK_T3, BID::VK_T3_A,
      BID::TOWN_HALL, BID::VK_VOID_LENS, BID::VK_T4, BID::VK_T4_A,
      BID::CITY_HALL, BID::VK_T5, BID::VK_T5_A, BID::VK_T6, BID::VK_T6_A },
    { BID::IA_FORGE_HALL, BID::FORT, BID::CITADEL, BID::CASTLE, BID::IA_T1, BID::IA_T1_A,
      BID::IA_BLUEPRINT_VAULT, BID::MARKET, BID::IA_T2, BID::IA_T2_A,
      BID::WAREHOUSE, BID::IA_OVERCLOCK, BID::IA_T3, BID::IA_T3_A,
      BID::TOWN_HALL, BID::WAREHOUSE_T2, BID::IA_T4, BID::IA_T4_A,
      BID::CITY_HALL, BID::IA_T5, BID::IA_T5_A, BID::IA_T6, BID::IA_T6_A },
    { BID::AM_GRAFTING_HALL, BID::AM_T1, BID::AM_T1_A, BID::MARKET,
      BID::AM_T2, BID::AM_T2_A, BID::FORT, BID::CITADEL, BID::CASTLE, BID::AM_MERGE_CHAMBER,
      BID::AM_T3, BID::AM_T3_A, BID::TOWN_HALL, BID::AM_FLESH_VAULT,
      BID::AM_T4, BID::AM_T4_A, BID::CITY_HALL,
      BID::AM_T5, BID::AM_T5_A, BID::AM_T6, BID::AM_T6_A },
    { BID::CV_SYNTHESIS_HUB, BID::CV_T1, BID::CV_T1_A, BID::MARKET,
      BID::CV_T2, BID::CV_T2_A, BID::FORT, BID::CITADEL, BID::CASTLE, BID::MAGE_GUILD,
      BID::CV_T3, BID::CV_T3_A, BID::TOWN_HALL,
      BID::CV_T4, BID::CV_T4_A, BID::CV_RESONANCE_WELL, BID::CITY_HALL,
      BID::CV_T5, BID::CV_T5_A, BID::CV_MIRROR_CHAMBER, BID::CV_T6, BID::CV_T6_A },
};

// Returns the ResourceType blocking the first prereqs-met but unaffordable building.
// Returns static_cast<ResourceType>(RESOURCE_COUNT) if nothing is blocked by resources.
static ResourceType aiBlockingResource(const Town& town,
                                        const std::vector<int>& buildOrder,
                                        const std::vector<BuildingDef>& allDefs,
                                        const Resources& res)
{
    for (int bid : buildOrder) {
        if (town.hasBuilding(bid)) continue;
        const BuildingDef* def = nullptr;
        for (const auto& d : allDefs) if (d.id == bid) { def = &d; break; }
        if (!def) continue;
        for (int rt = 0; rt < RESOURCE_COUNT; ++rt) {
            auto type = static_cast<ResourceType>(rt);
            if (def->cost.get(type) > res.get(type))
                return type;
        }
    }
    return static_cast<ResourceType>(RESOURCE_COUNT);
}

// ── World map update ──────────────────────────────────────────────────────────
void Game::watchAiMovePlayerHero()
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    const auto& udefs = m_registry.units();

    hero.movePool = hero.maxMove;

    int myStr = heroStrength(hero, udefs);
    // Gauge against the NEAREST enemy hero, not the globally strongest one.
    // On multi-AI maps the strongest of 7 bots is almost always stronger than
    // you and often on the far side of the map, which pinned strRatio < 0.4
    // (veryWeak) permanently — so the watched hero just cowered and grabbed
    // resources within 6 tiles, never committing to distant mines or fights.
    int nearestEnemyDist = 999;
    int nearestOppStr = 0;
    for (const auto& eh : m_enemyHeroes) {
        int d = HexGrid::distance(hero.pos, eh.pos);
        if (d < nearestEnemyDist) {
            nearestEnemyDist = d;
            nearestOppStr = heroStrength(eh, udefs);
        }
    }
    int bestOppStr = nearestOppStr;
    float strRatio   = bestOppStr > 0 ? (float)myStr / bestOppStr : 99.f;
    bool veryWeak    = strRatio < 0.4f;
    bool softRetreat = strRatio < 0.6f;
    bool dominant    = strRatio >= 1.2f;
    // Only cower when that nearest threat is actually close.
    if (nearestEnemyDist > 10) { veryWeak = false; softRetreat = false; }

    // Component maps for the O(1) unreachable-target reject below. Stamped per
    // turn, so this is free when the enemy-AI block already built them.
    {
        int stamp = m_turns.week() * 100 + m_turns.day();
        if (m_landCompTurn != stamp || m_landComp.empty())
            rebuildLandComponents();
    }

    while (hero.movePool > 0) {
        struct Cand { HexCoord pos; float score; };
        std::vector<Cand> cands;
        auto add = [&](HexCoord pos, float val) {
            if (pos == hero.pos) return;
            int d = std::max(1, HexGrid::distance(hero.pos, pos));
            cands.push_back({pos, val / d});
        };

        if (veryWeak) {
            // Retreat to own town
            for (const auto& t : m_towns)
                if (t.ownerId == 1) add(t.pos, 500.f);
            // Already home → the town candidate is rejected (own position) and the
            // hero would freeze forever; keep grabbing safe nearby resources instead.
            for (const auto& r : m_resources) {
                if (r.ownedBy == 1) continue;
                if (HexGrid::distance(hero.pos, r.pos) <= 6) add(r.pos, 40.f);
            }
        } else {
            // Own town to recruit
            for (const auto& t : m_towns) {
                if (t.ownerId != 1) continue;
                bool hasU = false;
                for (const auto& dw : t.dwellings) if (dw.available > 0) { hasU = true; break; }
                if (hasU && (int)hero.army.size() < 7) add(t.pos, 250.f);
            }
            // Towns — a garrisoned castle is only a target when the garrison
            // fight looks winnable. The required edge decays toward an even
            // fight as weeks pass (135% → 100% by week 35), so a balanced
            // game still ends in sieges instead of a stalemate where neither
            // side ever feels strong enough to attack.
            //
            // The watched player used to score every enemy town a flat 150/200
            // and divide by raw distance — so it "barely played," forever
            // re-grabbing the nearest mine while distant capitals stayed buried
            // under adjacent trinkets. Bring it to PARITY with the enemy AI:
            // value scales with our own strength, a rival on their last town is
            // a kill shot, and a gentler sqrt(dist) falloff lets a strong army
            // commit to the long march instead of orbiting home.
            int reqPct = std::max(100, 135 - m_turns.week());
            float strBoost = 1.f + std::min(8.f, (float)myStr / 60000.f);
            for (const auto& t : m_towns) {
                if (t.ownerId == 1) continue;
                int garStr = stacksStrength(t.garrison, udefs);
                if (garStr > 0 && (int64_t)myStr * 100 < (int64_t)garStr * reqPct) continue;
                float val;
                if (t.ownerId == 0) {
                    val = 150.f;                       // neutral/homeless town
                } else {
                    int rivalTowns = 0;
                    for (const auto& t2 : m_towns)
                        if (t2.ownerId == t.ownerId) ++rivalTowns;
                    val = 600.f * strBoost;
                    if (rivalTowns == 1)      val = 4200.f * strBoost; // elimination
                    else if (rivalTowns == 2) val = 1500.f * strBoost;
                }
                // add() divides by dist; pre-multiply by sqrt(dist) so the
                // effective falloff is 1/sqrt(dist), not 1/dist.
                int d = std::max(1, HexGrid::distance(hero.pos, t.pos));
                add(t.pos, val * std::sqrt((float)d));
            }
            // Resources — prioritise the mine type blocking our next build
            {
                ResourceType neededMineRes = static_cast<ResourceType>(RESOURCE_COUNT);
                const auto& allDefs = m_registry.buildings();
                for (const auto& t : m_towns) {
                    if (t.ownerId != 1) continue;
                    int fi = static_cast<int>(t.faction);
                    if (fi >= 0 && fi < 9) {
                        neededMineRes = aiBlockingResource(t, kBuildOrder[fi], allDefs, m_playerResources);
                        if (static_cast<int>(neededMineRes) < RESOURCE_COUNT) break;
                    }
                }
                int myFidx = static_cast<int>(hero.faction);
                ResourceType myKeyRes = (myFidx >= 0 && myFidx < 9)
                                      ? kFactionResource[myFidx] : ResourceType::Gold;
                for (const auto& r : m_resources) {
                    if (r.ownedBy == 1) continue;
                    bool guarded  = (r.guardId != 0 && !r.guardBeaten);
                    bool valuable = (r.type == neededMineRes)
                                 || (r.type == myKeyRes)
                                 || (r.type == ResourceType::Gold);
                    float val;
                    if (!guarded) {
                        // Free to walk onto — always worth grabbing.
                        val = (r.type == neededMineRes) ? 180.f : 90.f;
                    } else if (valuable) {
                        // Guarded but worth the fight.
                        val = (r.type == neededMineRes) ? 170.f : 110.f;
                    } else {
                        // Guarded and we don't need it — very low priority so the
                        // hero doesn't detour to bleed troops for a useless mine.
                        val = 20.f;
                    }
                    add(r.pos, val);
                }
            }
            // World objects
            for (const auto& obj : m_worldObjects) {
                if (obj.collected) continue;
                float val = 0.f;
                if (obj.type == WorldObjectType::UnitDwelling)
                    val = 160.f;   // free weekly units
                else if (obj.type == WorldObjectType::NeutralOutpost) {
                    int siteStr = std::min(1400, 250 + m_turns.week() * 50);
                    val = (myStr >= siteStr * 14 / 10) ? 150.f : 0.f;
                }
                else if (obj.type == WorldObjectType::ResourceCache ||
                         obj.type == WorldObjectType::Campfire      ||
                         obj.type == WorldObjectType::LavaCrystal)   val = 90.f;
                else if (obj.type == WorldObjectType::ArtifactChest) val = 100.f;
                else if (obj.type == WorldObjectType::TreasureChest) val = 85.f;
                else if (obj.type == WorldObjectType::Stables        ||
                         obj.type == WorldObjectType::TreeOfKnowledge||
                         obj.type == WorldObjectType::WitchHut       ||
                         obj.type == WorldObjectType::Landmark)       val = 70.f;
                else if (obj.type == WorldObjectType::XPShrine    ||
                         obj.type == WorldObjectType::ForestShrine ||
                         obj.type == WorldObjectType::StatShrine   ||
                         obj.type == WorldObjectType::SpellScroll  ||
                         obj.type == WorldObjectType::SwampAltar   ||
                         obj.type == WorldObjectType::Observatory)  val = 50.f;
                if (val > 0.f) add(obj.pos, val);
            }
            // Enemy heroes — same sqrt-damped, week-scaled hunt value as the enemy
            // AI, so the watched player hero also commits to fights late game
            // instead of orbiting mines forever (Watch AI stalemate).
            if (!softRetreat && !m_enemyHeroes.empty()) {
                for (const auto& eh : m_enemyHeroes) {
                    int dist = HexGrid::distance(hero.pos, eh.pos);
                    if (dominant || dist <= 8) {
                        float huntVal = 300.f + m_turns.week() * 10.f;
                        cands.push_back({eh.pos,
                            huntVal / std::sqrt((float)std::max(1, dist))});
                    }
                }
            }
        }

        if (cands.empty()) break;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b){ return a.score > b.score; });

        auto costFn = [&](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
            int base = hero.moveCost(t->terrain);
            if (m_roadHexes.count(c)) base = std::max(1, base / 2);
            return base;
        };
        // Fall through to lower-scored candidates when the best one is
        // unreachable — otherwise the hero freezes for the rest of the game.
        // Cap A* exploration to a horizon: on XL maps unbounded search over
        // the whole grid, per hero per turn, was the main Watch-mode lag
        // source. A distant target scores low anyway and is re-evaluated next
        // turn as the hero closes in, so we lose nothing by not pathing to the
        // far corner of the map in one shot.
        constexpr int kAiPathHorizon = 60;   // ~ a few turns of movement
        std::vector<HexCoord> path;
        for (size_t ci = 0; ci < cands.size() && ci < 10; ++ci) {
            // Same O(1) component fast-reject as the enemy-AI loop: a target on
            // another landmass (or, afloat, behind a mountain wall) cannot be
            // reached by this search, so don't burn a doomed A* proving it.
            if (routeImpossible(hero.onBoat, hero.pos, cands[ci].pos)) continue;
            path = Pathfinder::find(m_map, hero.pos, cands[ci].pos, costFn, kAiPathHorizon);
            if (!path.empty()) break;
        }
        // Everything unreachable by land: buy passage across the water. Fills
        // `path` with a dock route, or leaves it empty (fall through to the
        // anti-idle sweep below instead of freezing the watched hero).
        if (path.empty()) {
            std::vector<HexCoord> docks;
            for (const auto& t : m_towns)
                if (t.ownerId == 1 && t.hasBuilding(BID::TOWN_SHIPYARD))
                    docks.push_back(t.pos);
            auto sameLandmass = [this, &hero](HexCoord c) {
                return !routeImpossible(hero.onBoat, hero.pos, c);
            };
            aiTryBoat(m_map, m_worldObjects, docks, hero,
                      m_playerResources, costFn, sameLandmass, path);
        }

        // ── ANTI-IDLE FALLBACK (mirror of aiTakeHeroTurn) ────────────────
        // The scored top-10 can all prove unreachable while reachable free
        // mines sit just past the cutoff — the watched side then idled and its
        // economy starved (measured: watched town reached only 10/24 buildings
        // vs the AI towns' ~20). Sweep every unclaimed mine / free pickup by
        // nearest distance and walk to the first one actually reachable.
        if (path.empty()) {
            struct FreeGoal { HexCoord pos; int d; };
            std::vector<FreeGoal> freebies;
            for (const auto& r : m_resources) {
                if (r.ownedBy == 1) continue;                     // already watched-owned
                if (r.guardId != 0 && !r.guardBeaten) continue;   // still guarded
                freebies.push_back({r.pos, HexGrid::distance(hero.pos, r.pos)});
            }
            for (const auto& obj : m_worldObjects) {
                if (obj.collected) continue;
                if (obj.type == WorldObjectType::ResourceCache ||
                    obj.type == WorldObjectType::Campfire      ||
                    obj.type == WorldObjectType::LavaCrystal   ||
                    obj.type == WorldObjectType::TreasureChest ||
                    obj.type == WorldObjectType::ArtifactChest ||
                    obj.type == WorldObjectType::UnitDwelling)
                    freebies.push_back({obj.pos, HexGrid::distance(hero.pos, obj.pos)});
            }
            std::sort(freebies.begin(), freebies.end(),
                      [](const FreeGoal& a, const FreeGoal& b){ return a.d < b.d; });
            int tried = 0;
            for (const auto& g : freebies) {
                if (g.pos == hero.pos) continue;
                if (routeImpossible(hero.onBoat, hero.pos, g.pos)) continue;
                if (++tried > 24) break;
                std::vector<HexCoord> fp =
                    Pathfinder::find(m_map, hero.pos, g.pos, costFn, kAiPathHorizon);
                if (!fp.empty()) { path = std::move(fp); break; }
            }
        }

        if (path.empty()) break;

        HexCoord next = path[0];
        const HexTile* nt = m_map.getTile(next);
        if (!nt) break;
        int cost = hero.moveCost(nt->terrain);
        if (hero.movePool < cost) break;

        if (HexTile* old = m_map.getTile(hero.pos)) old->heroId = 0;
        hero.pos = next;
        hero.movePool -= cost;
        if (HexTile* nT = m_map.getTile(hero.pos)) nT->heroId = hero.id;
        if (hero.onBoat && nt->terrain != Terrain::Water)
            hero.onBoat = false;  // disembark

        // Claim resource — but guarded mines require beating the guard first.
        // (Was an unconditional r.ownedBy = 1, so the watched hero grabbed every
        // guarded mine for free and never fought a single one.)
        if (nt->resourceId != 0) {
            for (auto& r : m_resources) {
                if (r.id != nt->resourceId) continue;
                if (r.ownedBy == 1u) break;                 // already ours
                if (r.guardId != 0 && !r.guardBeaten) {
                    // Only fight a mine guard if the mine is WORTH the casualties.
                    // Cheap/free-ish targets: our own faction resource + gold
                    // (low guard, high value). Otherwise only bother if this
                    // resource is currently blocking our town's build queue.
                    // Off-resource mines we don't need aren't worth bleeding for.
                    int myFidx = (int)hero.faction;
                    ResourceType myKeyRes = (myFidx >= 0 && myFidx < 9)
                                          ? kFactionResource[myFidx] : ResourceType::Gold;
                    ResourceType blockingRes = static_cast<ResourceType>(RESOURCE_COUNT);
                    for (auto& t : m_towns) {
                        if (t.ownerId != 1u) continue;
                        blockingRes = aiBlockingResource(t, kBuildOrder[std::clamp(myFidx,0,8)],
                                                         m_registry.buildings(), m_playerResources);
                        if (static_cast<int>(blockingRes) < RESOURCE_COUNT) break;
                    }
                    bool worthwhile = (r.type == myKeyRes)
                                   || (r.type == ResourceType::Gold)
                                   || (r.type == blockingRes);

                    int myS = heroStrength(hero, udefs);
                    int guardStr = std::min(900, 150 + m_turns.week() * 35);
                    // Gold & own faction resource are lightly guarded / high value,
                    // so a 1.3x margin is fine; other worthwhile mines need 1.6x.
                    bool cheap = (r.type == myKeyRes || r.type == ResourceType::Gold);
                    int needNum = cheap ? 13 : 16;
                    if (worthwhile && myS * 10 >= guardStr * needNum) {
                        r.guardBeaten = true;
                        r.ownedBy     = 1u;
                        // Casualties scale INVERSELY with how outmatched the guard
                        // is: a huge margin costs almost nothing; a marginal fight
                        // costs more. Cheap mines cost even less.
                        float margin = guardStr > 0 ? (float)myS / guardStr : 9.f;
                        int divisor = cheap ? 40 : (margin >= 3.f ? 30 : margin >= 2.f ? 20 : 12);
                        int bigIdx = 0;
                        for (int i = 1; i < (int)hero.army.size(); ++i)
                            if (hero.army[i].count > hero.army[bigIdx].count) bigIdx = i;
                        if (!hero.army.empty())
                            hero.army[bigIdx].count =
                                std::max(1, hero.army[bigIdx].count - hero.army[bigIdx].count / divisor);
                        gLog("Watch hero %s cleared %s mine guard (week %d)\n",
                             hero.name.c_str(), resourceName(r.type), m_turns.week());
                    }
                    // Not worthwhile or too weak — leave it guarded and move on
                    // instead of bleeding troops for a mine we don't need.
                    break;
                }
                r.ownedBy = 1u;   // unguarded or already-cleared
                break;
            }
        }
        // Claim or visit town
        if (nt->townId != 0) {
            for (auto& t : m_towns) {
                if (t.id != nt->townId) continue;
                if (t.ownerId != 1) {
                    // Garrisoned castle: fight for it like everyone else —
                    // previously the watch hero flipped ANY town by walking
                    // in, making sieges pointless in watch mode.
                    if (!t.garrison.empty()) {
                        Hero garrisonHero;
                        garrisonHero.id      = 0;
                        garrisonHero.name    = t.name + " Garrison";
                        garrisonHero.faction = t.faction;
                        garrisonHero.army    = t.garrison;
                        m_lastCombatEnemyId    = 0;
                        m_pendingTownCaptureId = t.id;
                        auto pUnits = makeHeroUnits(hero, udefs, true);
                        auto gUnits = makeHeroUnits(garrisonHero, udefs, false);
                        m_fromBattleSim    = true;
                        m_simAutoPlay      = true;
                        m_simAutoPlayTimer = 0.f;
                        enterCombat(hero, pUnits, garrisonHero, gUnits);
                        return;
                    }
                    uint32_t prevOwner = t.ownerId;
                    t.ownerId = 1;
                    m_campaign.onTownCaptured(t.id, prevOwner);
                }
                // Recruit from now-owned town — paid from the watched side's
                // real resources, same rules as a human player
                if (t.ownerId == 1) {
                    takeGarrison(t, hero, udefs);
                    aiPaidRecruit(t, hero.army, m_playerResources, udefs);
                    // Upgrade base-tier stacks to PathA when PathA dwelling is built
                    for (const auto& dw : t.dwellings) {
                        if (dw.path != UpgradePath::PathA) continue;
                        const UnitDef* pathADef = nullptr;
                        const UnitDef* baseDef  = nullptr;
                        for (const auto& ud : udefs) {
                            if (ud.faction == t.faction && ud.tier == dw.tier) {
                                if (ud.path == UpgradePath::PathA) pathADef = &ud;
                                else if (ud.path == UpgradePath::None) baseDef = &ud;
                            }
                        }
                        if (!pathADef || !baseDef) continue;
                        for (auto& s : hero.army)
                            if (s.defId == baseDef->id) s.defId = pathADef->id;
                    }
                }
                break;
            }
        }
        // Collect world objects (persistent sites are used, not consumed)
        for (auto& obj : m_worldObjects) {
            if (obj.collected || obj.pos != hero.pos) continue;
            if (isPersistentSite(obj.type)) {
                if (obj.type == WorldObjectType::UnitDwelling) {
                    obj.linkedId = 1;  // captured for the watched side
                    dwellingPaidRecruit(obj, hero.army, m_playerResources, udefs);
                }
                continue;
            }
            obj.collected = true;
        }

        // Combat: stepped onto enemy hero
        for (auto& eHero : m_enemyHeroes) {
            if (eHero.pos != hero.pos) continue;
            m_lastCombatEnemyId = eHero.id;
            auto pUnits = makeHeroUnits(hero,  udefs, true);
            auto eUnits = makeHeroUnits(eHero, udefs, false);
            // Auto-resolve combat in watch AI mode
            m_fromBattleSim = true;
            m_simAutoPlay   = true;
            m_simAutoPlayTimer = 0.f;
            enterCombat(hero, pUnits, eHero, eUnits);
            return;
        }
    }
}

// Drives a Watch AI support hero (Scout Rider/Vanguard or Supply Courier).
// Support heroes never engage enemies — they explore/claim loot or shuttle
// freshly recruited troops from town to the main hero's army.
void Game::watchAiMoveSupportHero(Hero& hero, bool isCourier)
{
    const auto& udefs = m_registry.units();
    hero.movePool = hero.maxMove;

    auto isEnemyTile = [&](HexCoord c) -> bool {
        for (const auto& eh : m_enemyHeroes) if (eh.pos == c) return true;
        return false;
    };
    auto costFn = [&](HexCoord c) -> int {
        const HexTile* t = m_map.getTile(c);
        if (!t || !hero.canEnter(t->terrain) || t->blocked || isEnemyTile(c)) return 999;
        int base = hero.moveCost(t->terrain);
        if (m_roadHexes.count(c)) base = std::max(1, base / 2);
        return base;
    };
    auto stepToward = [&](HexCoord goal) {
        while (hero.movePool > 0) {
            auto path = Pathfinder::find(m_map, hero.pos, goal, costFn);
            if (path.empty()) break;
            HexCoord next = path[0];
            const HexTile* nt = m_map.getTile(next);
            if (!nt) break;
            int cost = hero.moveCost(nt->terrain);
            if (hero.movePool < cost) break;
            if (HexTile* old = m_map.getTile(hero.pos)) old->heroId = 0;
            hero.pos = next;
            hero.movePool -= cost;
            if (HexTile* nh = m_map.getTile(hero.pos)) nh->heroId = hero.id;
            if (hero.pos == goal) break;
        }
    };

    if (isCourier) {
        // Standing on our own town: pick up the garrison and buy any fresh
        // recruits the side can afford (paid, same rules as a human player).
        for (auto& t : m_towns) {
            if (t.ownerId != 1 || t.pos != hero.pos) continue;
            takeGarrison(t, hero, udefs);
            aiPaidRecruit(t, hero.army, m_playerResources, udefs);
            break;
        }

        // Deliver to the first REAL hero, not blindly to m_heroes[0] — after
        // the main dies, slot 0 can be another support hero.
        Hero* mainHero = nullptr;
        for (auto& h : m_heroes)
            if (!isWatchSupportName(h.name)) { mainHero = &h; break; }

        if (mainHero && !hero.army.empty()) {
            if (hero.pos == mainHero->pos) {
                // Transfer the whole delivery into the main hero's army.
                for (auto& s : hero.army) {
                    bool merged = false;
                    for (auto& ms : mainHero->army)
                        if (ms.defId == s.defId) { ms.count += s.count; merged = true; break; }
                    if (!merged && mainHero->army.size() < 7) mainHero->army.push_back(s);
                }
                hero.army.clear();
                gLog("Supply Courier delivered troops to %s (week %d)\n",
                     mainHero->name.c_str(), m_turns.week());
            } else {
                stepToward(mainHero->pos);
            }
        } else {
            // Nothing to carry — head home and wait for the next recruit cycle.
            for (auto& t : m_towns) {
                if (t.ownerId != 1) continue;
                if (t.pos != hero.pos) stepToward(t.pos);
                break;
            }
        }
        return;
    }

    // ── Scout role: explore, claim loot/resources/neutral towns, never fight ──
    struct Cand { HexCoord pos; float score; };
    std::vector<Cand> cands;
    auto add = [&](HexCoord pos, float val) {
        if (pos == hero.pos || isEnemyTile(pos)) return;
        int d = std::max(1, HexGrid::distance(hero.pos, pos));
        cands.push_back({pos, val / d});
    };
    for (const auto& r : m_resources) if (r.ownedBy != 1) add(r.pos, 100.f);
    for (const auto& obj : m_worldObjects) if (!obj.collected) add(obj.pos, 90.f);
    for (const auto& t : m_towns)
        if (t.ownerId == 0 && t.garrison.empty()) add(t.pos, 70.f);
    if (cands.empty()) return;
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b){ return a.score > b.score; });

    // Pick the best-scored candidate that is actually reachable; an island
    // resource at the top of the list would otherwise pin the scout in place.
    HexCoord goal = cands[0].pos;
    for (size_t ci = 0; ci < cands.size() && ci < 8; ++ci) {
        if (!Pathfinder::find(m_map, hero.pos, cands[ci].pos, costFn).empty()) {
            goal = cands[ci].pos;
            break;
        }
    }

    while (hero.movePool > 0) {
        HexCoord before = hero.pos;
        stepToward(goal);

        const HexTile* nt = m_map.getTile(hero.pos);
        if (nt) {
            if (nt->resourceId != 0)
                for (auto& r : m_resources) if (r.id == nt->resourceId) { r.ownedBy = 1; break; }
            for (auto& obj : m_worldObjects) {
                if (obj.collected || obj.pos != hero.pos) continue;
                if (isPersistentSite(obj.type)) {
                    // Scouts claim dwellings for the side but don't recruit
                    if (obj.type == WorldObjectType::UnitDwelling) obj.linkedId = 1;
                    continue;
                }
                obj.collected = true;
            }
            if (nt->townId != 0)
                for (auto& t : m_towns)
                    if (t.id == nt->townId && t.ownerId == 0 && t.garrison.empty())
                        t.ownerId = 1;
        }
        if (hero.pos == before || hero.pos == goal) break;
    }
}

void Game::updateWorldMap(float dt)
{
    m_mapTime += dt;
    m_hexRenderer.update(dt);

    // Watch AI auto-advance end-turn
    if (m_watchingAI) {
        // Auto-dismiss any blocking modals so the sim can continue
        if (m_showVictory || m_showDefeat) {
            m_watchingAI  = false;
            m_fogDisabled = false;
            return;
        }
        if (m_showCombatResult)        m_showCombatResult        = false;
        if (m_showTownLostPopup)       m_showTownLostPopup       = false;
        if (m_showCapturePopup)        m_showCapturePopup        = false;
        if (m_showWeekSummary)         m_showWeekSummary         = false;
        if (m_showEncounterPrompt) {
            // Auto-accept encounters (fight neutral stacks)
            if (m_encounterOnAccept) { m_encounterOnAccept(); m_encounterOnAccept = nullptr; }
            m_showEncounterPrompt = false;
        }
        if (m_showDwellingPopup) {
            // Auto-recruit from a standalone dwelling — paid at player rates,
            // and the dwelling is claimed for the watched side (+1 town growth)
            if (!m_heroes.empty() && m_pendingObjId != 0) {
                Hero& dh = m_heroes[m_activeHeroIdx];
                for (auto& obj : m_worldObjects) {
                    if (obj.id != m_pendingObjId) continue;
                    obj.linkedId = 1;
                    dwellingPaidRecruit(obj, dh.army, m_playerResources,
                                        m_registry.units());
                    break;
                }
            }
            m_showDwellingPopup = false;
        }
        if (m_showQuestPopup)          m_showQuestPopup          = false;
        if (m_showTreasureChestPopup)  m_showTreasureChestPopup  = false;
        if (m_showCryptPopup)          m_showCryptPopup          = false;
        if (m_showUtopiaPopup)         m_showUtopiaPopup         = false;
        if (m_showStatShrinePopup)     m_showStatShrinePopup     = false;
        if (m_showTreeKnowledgePopup)  m_showTreeKnowledgePopup  = false;
        if (m_showShipyardPopup)       m_showShipyardPopup       = false;
        if (m_showSiegeCampPrompt)     m_showSiegeCampPrompt     = false;
        if (m_showMineInfoPopup)       m_showMineInfoPopup       = false;
        if (m_showHeroSheetPopup)      m_showHeroSheetPopup      = false;
        if (m_showLevelUpModal && !m_levelUpOffers.empty() && !m_heroes.empty()) {
            // Auto-pick first skill offer
            Hero& lvlHero = m_heroes[m_activeHeroIdx];
            const auto& offer = m_levelUpOffers[0];
            int prevTier = 0;
            if (const SkillInstance* existing = lvlHero.skills.getSkill(offer.skillId))
                prevTier = static_cast<int>(existing->tier);
            LevelUpSystem::applyOffer(offer, lvlHero.skills);
            // Apply passive skill bonuses
            if (const SkillDef* sd = findSkillDef(offer.skillId)) {
                int v = offer.isUpgrade ? (sd->values[prevTier+1] - sd->values[prevTier]) : sd->values[0];
                if (sd->effectType == SkillEffectType::MovementBonus) {
                    lvlHero.maxMove += v; lvlHero.movePool = std::max(lvlHero.movePool, lvlHero.maxMove);
                } else if (sd->effectType == SkillEffectType::VisionBonus) {
                    lvlHero.visionRange += v;
                }
            }
            m_levelUpOffers.clear();
            if (m_pendingLevelUps > 1) {
                m_pendingLevelUps--;
                const HeroClassDef* ncls = m_classRegistry.getClass(lvlHero.classId);
                if (ncls) {
                    std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                    m_levelUpOffers = LevelUpSystem::generateOffers(
                        *ncls, lvlHero.skills, lvlHero.level, allSkills, lvlHero.faction);
                }
                if (m_levelUpOffers.empty())
                    m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
            } else {
                m_pendingLevelUps = 0;
                m_showLevelUpModal = false;
            }
        }

        // Paused: skip all turn processing but keep rendering, so the HUD stays
        // responsive. At 8x the turn work saturates the frame and the UI became
        // near-impossible to click — pausing gives you the controls back.
        if (m_watchAIPaused) return;

        // A spread AI round is in flight (THREADING.md Phase 3): run one
        // budgeted slice, then yield so this frame still renders. The day
        // timer below is intentionally frozen until the round completes —
        // a new day must not start while heroes from the last one are
        // still moving.
        if (m_aiTurn.active) {
            aiTurnStep();
            return;
        }

        m_watchAITimer -= dt;
        if (m_watchAITimer <= 0.f) {
            // Guard the divisor: a 0 (or negative) speed — reachable by
            // ctrl+click text entry on the slider — turned this into inf/NaN
            // and wedged the timer permanently.
            m_watchAISpeed = std::clamp(m_watchAISpeed, 0.25f, 8.0f);
            m_watchAITimer = 1.0f / m_watchAISpeed;

            // Watch game over (free-for-all): every ownerId is an independent
            // player, NOT a Blue-vs-Red team. The old 2-side model summed all
            // AI players into one "Red" side, so on an 8-player map it declared
            // a landslide on turn 1 (7 towns vs 1). End only when a single
            // player is left standing, or the watched player (owner 1) is out.
            {
                const auto& udefs = m_registry.units();

                // Per-owner strength = own heroes' + own towns' garrisons.
                auto ownerStrength = [&](uint32_t owner) -> long long {
                    long long s = 0;
                    if (owner == 1u) {
                        for (const auto& h : m_heroes) s += heroStrength(h, udefs);
                    } else {
                        for (const auto& h : m_enemyHeroes)
                            if (h.ownerId == owner) s += heroStrength(h, udefs);
                    }
                    for (const auto& t : m_towns)
                        if (t.ownerId == owner) s += stacksStrength(t.garrison, udefs);
                    return s;
                };
                auto ownerTowns = [&](uint32_t owner) {
                    int n = 0;
                    for (const auto& t : m_towns) if (t.ownerId == owner) ++n;
                    return n;
                };

                // Collect every player id that owns anything (town or army).
                std::vector<uint32_t> owners;
                auto note = [&](uint32_t o){
                    if (std::find(owners.begin(), owners.end(), o) == owners.end())
                        owners.push_back(o);
                };
                for (const auto& t : m_towns) note(t.ownerId);
                if (!m_heroes.empty()) note(1u);
                for (const auto& h : m_enemyHeroes) note(h.ownerId);

                // Which owners are still alive (have a town or any strength)?
                std::vector<uint32_t> alive;
                for (uint32_t o : owners)
                    if (ownerTowns(o) > 0 || ownerStrength(o) > 0)
                        alive.push_back(o);

                // Last player standing — the game runs until ONE owner remains,
                // whoever it is. It used to ALSO end the moment the watched
                // player (owner 1) died, so every game "ended" early the instant
                // P1 was knocked out instead of playing on to a real conqueror.
                bool gameOver = (alive.size() <= 1);

                // Early stop: one surviving player dwarfs ALL other survivors
                // combined (>=6×) past week 5 — the outcome is settled. This is
                // the per-player replacement for the old team-sum dominance.
                uint32_t dominantOwner = 0;
                if (!gameOver && alive.size() >= 2 && m_turns.week() > 5) {
                    long long total = 0;
                    std::vector<std::pair<uint32_t,long long>> strs;
                    for (uint32_t o : alive) {
                        long long s = ownerStrength(o);
                        strs.push_back({o, s});
                        total += s;
                    }
                    for (auto& [o, s] : strs) {
                        long long rest = total - s;
                        if (rest > 0 && s > rest * 6) { dominantOwner = o; gameOver = true; break; }
                    }
                }

                // Hard backstop for the spectator harness: if a game hasn't
                // resolved by week 80 (naval invasion now lets island players be
                // reached, but a big multi-player XL map can still grind on),
                // declare the strongest survivor the winner so the watch run
                // always terminates instead of running forever. Real (non-watch)
                // games are unaffected — this whole block is watch-only.
                if (!gameOver && m_turns.week() >= 80 && !alive.empty()) {
                    long long bestStr = -1;
                    for (uint32_t o : alive) {
                        long long s = ownerStrength(o);
                        if (s > bestStr) { bestStr = s; dominantOwner = o; }
                    }
                    gameOver = true;
                }

                if (gameOver) {
                    uint32_t winner = dominantOwner ? dominantOwner
                                    : (alive.empty() ? 0u : alive.front());
                    gLog("=== WATCH GAME OVER (week %d) — %s wins "
                         "(%zu players left, winner owner %u: str %lld / %d towns) ===\n",
                         m_turns.week(),
                         winner == 1u ? "BLUE (watched)" : "an AI player",
                         alive.size(), winner,
                         winner ? ownerStrength(winner) : 0,
                         winner ? ownerTowns(winner) : 0);
                    // --watch-ai-test self-termination: this watch-only
                    // game-over path never raises m_showVictory/m_showDefeat
                    // (it drops to the main menu), so without this a headless
                    // test run would sit at the menu forever after the game
                    // resolved. End the process here with the [WATCH-AI] tag.
                    if (m_watchAiAutoExit) {
                        gLog("[WATCH-AI] game over (week %d): winner owner %u\n",
                             m_turns.week(), winner);
                        m_running = false;
                    }
                    m_watchingAI  = false;
                    m_fogDisabled = false;
                    m_state       = GameState::MainMenu;
                    m_menuMode    = 0;
                    return;
                }
            }

            if (!m_showCombatResult && !m_showLevelUpModal) {
                // Consolidate: watched-side extra heroes standing next to the
                // main hero (index 0) dump their army into it — same one-fat-
                // stack behaviour as the enemy AI, so Watch mirrors real play.
                if (m_heroes.size() > 1) {
                    Hero& main = m_heroes[0];
                    for (size_t fi = 1; fi < m_heroes.size(); ++fi) {
                        Hero& feeder = m_heroes[fi];
                        if (HexGrid::distance(feeder.pos, main.pos) > 1) continue;
                        std::sort(feeder.army.begin(), feeder.army.end(),
                                  [](const UnitStack& a, const UnitStack& b){ return a.count > b.count; });
                        for (auto it = feeder.army.begin(); it != feeder.army.end();) {
                            bool merged = false;
                            for (auto& s : main.army)
                                if (s.defId == it->defId) { s.count += it->count; merged = true; break; }
                            if (!merged && main.army.size() < 7) { main.army.push_back(*it); merged = true; }
                            it = merged ? feeder.army.erase(it) : ++it;
                        }
                    }
                }
                // Drive EVERY watched-side hero once per game-day (a real
                // roster, mirroring the enemy). A hero is marked moved before
                // it acts, so if it enters combat it isn't re-driven when the
                // fight returns control here next tick.
                bool allMoved = true;
                for (size_t hi = 0; hi < m_heroes.size(); ++hi) {
                    uint32_t hid = m_heroes[hi].id;
                    bool moved = false;
                    for (uint32_t m : m_watchMovedThisDay) if (m == hid) { moved = true; break; }
                    if (moved) continue;
                    m_watchMovedThisDay.push_back(hid);
                    m_activeHeroIdx = static_cast<int>(hi);
                    watchAiMovePlayerHero();
                    if (m_state != GameState::WorldMap) { allMoved = false; break; }
                }
                // All watched heroes moved → end the day (runs enemy AI, income,
                // weekly hiring) and start a fresh move cycle.
                if (allMoved && m_state == GameState::WorldMap) {
                    m_watchMovedThisDay.clear();
                    doEndTurn();
                }
            }
        }
        return;
    }

    const auto& mouse = m_input.mouse();

    if (mouse.wheelY != 0.0f)
        m_camera.zoomBy(mouse.wheelY > 0 ? 1.12f : 0.88f);

    if (mouse.middle)
        m_camera.pan(-static_cast<float>(mouse.dx), -static_cast<float>(mouse.dy));

    const float PAN = 200.0f * dt;
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (m_input.keyHeld(SDLK_LEFT))  m_camera.pan(-PAN, 0);
        if (m_input.keyHeld(SDLK_RIGHT)) m_camera.pan( PAN, 0);
        if (m_input.keyHeld(SDLK_UP))    m_camera.pan(0, -PAN);
        if (m_input.keyHeld(SDLK_DOWN))  m_camera.pan(0,  PAN);
    }

    // Lambda that clamps camera so map edge never scrolls past viewport edge
    auto clampCamera = [this]() {
        const float hs    = m_hexRenderer.grid().hexSize();
        const float R     = static_cast<float>(m_map.radius());
        const float zoom  = m_camera.zoom();
        const float halfW = static_cast<float>(m_width)  / (2.0f * zoom);
        const float halfH = static_cast<float>(m_height) / (2.0f * zoom);
        const float mapExtX = R * hs * 1.5f + hs;
        const float mapExtY = R * hs * 1.732f + hs;
        const float limX = std::max(0.0f, mapExtX - halfW);
        const float limY = std::max(0.0f, mapExtY - halfH);
        float cx = std::clamp(m_camera.x(), -limX, limX);
        float cy = std::clamp(m_camera.y(), -limY, limY);
        if (cx != m_camera.x() || cy != m_camera.y())
            m_camera.setPosition(cx, cy);
    };
    clampCamera();

    {
        float wx, wy;
        m_camera.screenToWorld(static_cast<float>(mouse.x),
                               static_cast<float>(mouse.y), wx, wy);
        HexCoord h = m_hexRenderer.grid().worldToHex(wx, wy);
        m_hovered = m_map.inBounds(h) ? h : HexCoord{-999,-999};
    }

    // Cursor: fight if enemy hovered, otherwise arrow
    if (m_cursorArrow && m_cursorFight) {
        bool fight = false;
        if (m_map.inBounds(m_hovered)) {
            const HexTile* ht = m_map.getTile(m_hovered);
            if (ht && ht->visible) {
                for (const auto& e : m_enemyHeroes)
                    if (e.id == ht->heroId) { fight = true; break; }
                if (!fight && ht->townId != 0)
                    for (const auto& t : m_towns)
                        if (t.id == ht->townId && t.ownerId > static_cast<uint32_t>(m_numHumanPlayers)) { fight = true; break; }
            }
        }
        SDL_SetCursor(fight ? m_cursorFight : m_cursorArrow);
    }

    if (mouse.leftDown && !ImGui::GetIO().WantCaptureMouse) {
        bool uiHandled = m_worldHUD.onMouseDown(
            static_cast<float>(mouse.x), static_cast<float>(mouse.y));

        // Screen-space hero click: reliable regardless of hex-coordinate rounding
        // First click → center camera + select hero; second click on same hero → inspect
        if (!uiHandled) {
            bool heroClickHandled = false;

            // All human players (incl. hot-seat P2 after handoff swap) control
            // m_heroes through the same path — no per-player special casing.
            {
                // Normal hero selection — but ONLY for the already-active hero
                // (click it again to inspect). Clicking a DIFFERENT own hero
                // falls through to onTileClicked below so the active hero paths
                // onto that tile instead of instantly switching control — the
                // hero-click radius here is nearly a full hex at default zoom,
                // so without this it swallowed every attempt to walk one hero
                // onto another to trigger the trade/exchange screen, making
                // hero-to-hero trading unreachable. Switching which hero you
                // control is still done via the sidebar hero list.
                for (int hi = 0; hi < static_cast<int>(m_heroes.size()); ++hi) {
                    const Hero& h = m_heroes[hi];
                    float wx, wy;
                    m_hexRenderer.grid().hexToWorld(h.pos, wx, wy);
                    float sx, sy;
                    m_camera.worldToScreen(wx, wy, sx, sy);
                    float dx = static_cast<float>(mouse.x) - sx;
                    float dy = static_cast<float>(mouse.y) - sy;
                    if (dx * dx + dy * dy < 20.0f * 20.0f) {
                        if (hi != m_activeHeroIdx) break;  // let it fall through to movement
                        if (m_heroClickTarget == hi) {
                            m_showHeroInspect = true;
                            m_heroClickTarget = -1;
                        } else {
                            m_heroClickTarget = hi;
                        }
                        heroClickHandled = true;
                        uiHandled = true;
                        break;
                    }
                }
                if (!heroClickHandled) m_heroClickTarget = -1;
            }
        }

        // Minimap click: pan camera to clicked map position
        if (!uiHandled && m_map.radius() > 0) {
            constexpr float MINI_W = 150.0f, MINI_H = 150.0f, PAD = 10.0f;
            const float mm_left = PAD;
            const float mm_top  = static_cast<float>(m_height) - MINI_H - PAD;
            const float mx = static_cast<float>(mouse.x);
            const float my = static_cast<float>(mouse.y);
            if (mx >= mm_left && mx <= mm_left + MINI_W &&
                my >= mm_top  && my <= mm_top  + MINI_H) {
                const float mm_cx  = mm_left + MINI_W * 0.5f;
                const float mm_cy  = mm_top  + MINI_H * 0.5f;
                const float R      = static_cast<float>(m_map.radius());
                const float scaleX = MINI_W * 0.5f / R;
                const float scaleY = MINI_H * 0.5f / R;
                const float hs     = m_hexRenderer.grid().hexSize();
                float q_f   = (mx - mm_cx) / scaleX;
                float rq_f  = (my - mm_cy) / scaleY;
                m_camera.setPosition(hs * 1.5f * q_f, hs * 1.7320508f * rq_f);
                clampCamera();
                uiHandled = true;
            }
        }

        if (!uiHandled && m_map.inBounds(m_hovered))
            onTileClicked(m_hovered);
        else if (!uiHandled)
            m_selected = {-999,-999};
    }

    // Right-click on a mine tile — show guard/income info popup
    if (mouse.rightDown && !ImGui::GetIO().WantCaptureMouse) {
        m_showMineInfoPopup  = false;
        m_showHeroSheetPopup = false;
        if (m_map.inBounds(m_hovered)) {
            const HexTile* ht = m_map.getTile(m_hovered);
            // A hero standing on the tile takes precedence over the mine beneath.
            if (ht && ht->heroId != 0) {
                m_heroSheetId        = ht->heroId;
                m_showHeroSheetPopup = true;
            } else if (ht && ht->resourceId != 0) {
                m_mineInfoId        = ht->resourceId;
                m_showMineInfoPopup = true;
            }
        }
    }

    if (mouse.leftUp)
        m_worldHUD.onMouseUp(static_cast<float>(mouse.x), static_cast<float>(mouse.y));

    m_worldHUD.onMouseMove(static_cast<float>(mouse.x),
                           static_cast<float>(mouse.y));

    updateHeroMovement(dt);

    // Update particles
    m_particles.update(dt);

    if (m_playerTurnBannerT > 0.0f)
        m_playerTurnBannerT -= dt;

    // Advance pickup effects (float upward, fade out)
    for (auto& e : m_pickupEffects) e.t -= dt;
    m_pickupEffects.erase(
        std::remove_if(m_pickupEffects.begin(), m_pickupEffects.end(),
            [](const PickupEffect& ef){ return ef.t <= 0.0f; }),
        m_pickupEffects.end());

    // Update world-map hero animators (lazy-init on first seen)
    auto initAnim = [&](const Hero& h, bool mirror) {
        if (m_heroMapAnimators.find(h.id) == m_heroMapAnimators.end()) {
            SpriteAnimator a;
            a.faction  = std::min(static_cast<int>(h.faction), NUM_FACTIONS - 1);
            a.tier     = 1; a.mirror = mirror;
            // Prefer the dedicated hero figure's frame count where one exists.
            a.numCols  = m_heroTex[a.faction].ok()
                         ? m_heroTexCols[a.faction]
                         : m_unitTexCols[a.faction][0];
            a.setState(AnimState::Idle);
            m_heroMapAnimators[h.id] = a;
        }
        m_heroMapAnimators[h.id].update(dt);
    };
    for (const auto& h : m_heroes)      initAnim(h, false);
    for (const auto& h : m_enemyHeroes) initAnim(h, true);
    for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
        if (pi == m_currentPlayerIdx) continue;
        for (const auto& h : m_players[pi].heroes) initAnim(h, true);
    }

    if (m_input.keyDown(SDLK_F6)) m_showHideoutScreen   = !m_showHideoutScreen;
    if (m_input.keyDown(SDLK_F7)) m_showArtifactPanel   = !m_showArtifactPanel;
    if (m_input.keyDown(SDLK_F8)) m_showHeroInspect     = !m_showHeroInspect;
    if (m_input.keyDown(SDLK_m))  m_showMinimap         = !m_showMinimap;

    // G — toggle garrison (hero digs in, blocks passage until defeated)
    if (m_input.keyDown(SDLK_g) && !m_heroes.empty()) {
        Hero& h = m_heroes[m_activeHeroIdx];
        h.isGarrisoned = !h.isGarrisoned;
        gLog("Hero %s %s garrison\n", h.name.c_str(),
               h.isGarrisoned ? "dug in at" : "left");
    }

    // F — Build Fishing House (hero recently disembarked from a boat on land)
    if (m_input.keyDown(SDLK_f) && !m_heroes.empty()) {
        Hero& h = m_heroes[m_activeHeroIdx];
        const HexTile* ft = m_map.getTile(h.pos);
        bool onLand = ft && ft->terrain != Terrain::Water;
        bool hasBoatHistory = h.boatCount > 0;  // built at least one boat this game
        if (onLand && hasBoatHistory) {
            int buildCost = 500;
            if (m_playerResources.get(ResourceType::Gold) >= buildCost) {
                // Check no existing FishingHouse on this tile
                bool already = false;
                for (const auto& wo : m_worldObjects)
                    if (wo.pos == h.pos && wo.type == WorldObjectType::FishingHouse)
                    { already = true; break; }
                if (!already) {
                    m_playerResources.add(ResourceType::Gold, -buildCost);
                    WorldObject fh;
                    fh.id          = m_nextObjId++;
                    fh.type        = WorldObjectType::FishingHouse;
                    fh.pos         = h.pos;
                    fh.faction     = currentPlayerId();   // owned by current player
                    fh.value       = 150;                 // daily gold
                    m_worldObjects.push_back(fh);
                    pushPickupEffect(h.pos, "Fishing House built! (+150g/day)", IM_COL32(80, 220, 120, 255));
                    m_audio.playSound("pickup");
                }
            } else {
                pushPickupEffect(h.pos, "Need 500 Gold!", IM_COL32(255, 80, 80, 255));
            }
        }
    }

    // Tab — cycle to next player hero
    if (m_input.keyDown(SDLK_TAB) && !m_heroes.empty()) {
        m_activeHeroIdx = (m_activeHeroIdx + 1) % static_cast<int>(m_heroes.size());
        const Hero& nextHero = m_heroes[m_activeHeroIdx];
        float hx2, hy2;
        m_hexRenderer.grid().hexToWorld(nextHero.pos, hx2, hy2);
        m_camera.setPosition(hx2, hy2);
        clampCamera();
        m_selected = {-999, -999};
        auto costFn2 = [this, &nextHero](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            if (!t || !nextHero.canEnter(t->terrain) || t->blocked) return 999;
            int base = nextHero.moveCost(t->terrain);
            if (m_roadHexes.count(c)) base = std::max(1, base / 2);
            return base;
        };
        m_reachable = Pathfinder::reachable(m_map, nextHero.pos, costFn2, nextHero.movePool);
    }

    if (m_input.keyDown(SDLK_SPACE)) {
        doEndTurn();
    }
}

// ── End Turn — full turn logic (SPACE key + HUD button) ───────────────────────
// ── AI hero XP + auto level-up (stats and class skills, no UI) ───────────────
// Used by shrine pickups during the AI turn and by exitCombat when an AI hero
// wins a battle (AI heroes previously gained zero XP from victories and never
// levelled outside the two map shrines).
void Game::aiHeroAwardXp(Hero& hero, int xp)
{
    if (xp <= 0) return;

    auto applySkillBonus = [](Hero& h, const SkillDef* def, int v) {
        if (!def) return;
        if (def->effectType == SkillEffectType::MovementBonus) {
            h.maxMove += v;
            h.movePool = std::max(h.movePool, h.maxMove);
        } else if (def->effectType == SkillEffectType::VisionBonus) {
            h.visionRange += v;
        } else if (def->effectType == SkillEffectType::MagicSchoolBonus) {
            if      (def->statName == "lightPower")  h.lightPower  += v;
            else if (def->statName == "bloodPower")  h.bloodPower  += v;
            else if (def->statName == "deathPower")  h.deathPower  += v;
            else if (def->statName == "naturePower") h.naturePower += v;
            else if (def->statName == "forgePower")  h.forgePower  += v;
            else if (def->statName == "fleshPower")  h.fleshPower  += v;
        }
    };
    // Prioritise upgrading an existing skill, else learn the next pool skill.
    auto learnNextSkill = [&](Hero& h) {
        const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
        if (!cls || cls->skillPool.empty()) return;
        for (int sid : cls->skillPool) {
            if (SkillInstance* s = h.skills.getSkill(sid)) {
                if (s->canUpgrade()) {
                    int prevTierIdx = static_cast<int>(s->tier);
                    s->upgrade();
                    if (const SkillDef* def = findSkillDef(sid)) {
                        int delta = def->values[prevTierIdx + 1] - def->values[prevTierIdx];
                        applySkillBonus(h, def, delta);
                    }
                    return;
                }
            }
        }
        for (int sid : cls->skillPool) {
            if (!h.skills.hasSkill(sid) && h.skills.canLearn(sid)) {
                h.skills.learn(sid);
                if (const SkillDef* def = findSkillDef(sid))
                    applySkillBonus(h, def, def->values[0]);
                return;
            }
        }
    };

    int prevLevel = hero.level;
    if (hero.addXp(xp) && hero.level > prevLevel) {
        int gained = hero.level - prevLevel;
        hero.attack  += (gained + 1) / 2;   // mirror player level gains, alternating
        hero.defense += gained / 2;
        for (int g = 0; g < gained; ++g) learnNextSkill(hero);
        gLog("AI hero %s reached level %d\n", hero.name.c_str(), hero.level);
    }
}

// ── AI artifact auto-equip ───────────────────────────────────────────────────
// AI heroes used to hoard picked-up artifacts in artifactInventory forever
// (combat only reads the equipped set), so they got zero benefit. Equip into a
// free slot, or swap if the new piece's raw stat sum beats what's in its slot.
void Game::aiEquipOrStashArtifact(Hero& hero, int artifactId)
{
    const ArtifactDef* def = m_artifactRegistry.getDef(artifactId);
    if (!def) { hero.artifactInventory.push_back(artifactId); return; }

    auto statSum = [](const ArtifactBonus& b) {
        return b.attack + b.defense + b.lightPower + b.bloodPower + b.deathPower
             + b.naturePower + b.forgePower + b.fleshPower
             + b.moveBonus + b.visionBonus + b.manaBonus + b.hpBonus;
    };

    int cur = hero.artifacts.getEquipped(def->slot);
    if (cur == 0) {
        hero.artifacts.equip(artifactId, def->slot);
        return;
    }
    const ArtifactDef* curDef = m_artifactRegistry.getDef(cur);
    if (curDef && statSum(def->bonus) > statSum(curDef->bonus)) {
        hero.artifacts.equip(artifactId, def->slot);      // upgrade
        hero.artifactInventory.push_back(cur);            // stash the old one
    } else {
        hero.artifactInventory.push_back(artifactId);     // keep current, stash new
    }
}

// Wall-clock anchor for the [PERF] total — set in doEndTurn(), read in
// doEndTurnPost() (which, in Watch mode, runs several frames later).
static std::chrono::steady_clock::time_point g_turnT0;

void Game::doEndTurn()
{
    // Total wall time of the whole end-turn (income + AI + sieges…) — this is
    // the actual frame stall the [PERF] line's cand/path split lives inside.
    g_turnT0 = std::chrono::steady_clock::now();
    // Prune any AI heroes eliminated by an AI-vs-AI field battle since the
    // last call — done first, before any early-return path below, so an
    // elimination never lingers with a stale `heroId` left on the map.
    for (auto it = m_enemyHeroes.begin(); it != m_enemyHeroes.end(); ) {
        if (it->eliminated) {
            uint32_t deadId = it->id;
            m_map.forEach([&](HexTile& ht){ if (ht.heroId == deadId) ht.heroId = 0; });
            it = m_enemyHeroes.erase(it);
        } else {
            ++it;
        }
    }

    // ── Hotseat: non-last player ends turn → switch to next player ───────────────
    if (m_numHumanPlayers >= 2 && m_currentPlayerIdx < m_numHumanPlayers - 1) {
        int nextIdx = m_currentPlayerIdx + 1;
        // Discard stale encounter/popup state
        m_showEncounterPrompt    = false;
        m_encounterOnAccept      = nullptr;
        m_encounterOnDecline     = nullptr;
        m_showWeekSummary        = false;
        m_weekChoiceOptions.clear();
        m_showLevelUpModal       = false;
        m_pendingLevelUps        = 0;
        m_levelUpOffers.clear();
        m_showCryptPopup         = false;
        m_pendingCryptId         = 0;
        m_pendingPandoraId       = 0;
        m_showUtopiaPopup        = false;
        m_pendingUtopiaId        = 0;
        m_showDwellingPopup      = false;
        m_showStatShrinePopup    = false;
        m_showTreasureChestPopup = false;
        m_showTreeKnowledgePopup = false;
        m_showQuestPopup         = false;
        m_showUnitExchange       = false;
        m_showCapturePopup       = false;
        m_showTownLostPopup      = false;
        m_showFoundCityPopup     = false;
        m_showTownPortalPopup    = false;
        m_pendingObjId           = 0;

        // Store current player's live state
        m_players[m_currentPlayerIdx].heroes        = m_heroes;
        m_players[m_currentPlayerIdx].resources     = m_playerResources;
        m_players[m_currentPlayerIdx].activeHeroIdx = m_activeHeroIdx;
        for (const auto& h : m_players[m_currentPlayerIdx].heroes)
            if (HexTile* ht = m_map.getTile(h.pos)) ht->heroId = 0;

        // Load next player's state
        m_heroes          = m_players[nextIdx].heroes;
        m_playerResources = m_players[nextIdx].resources;
        m_activeHeroIdx   = m_players[nextIdx].activeHeroIdx;
        for (const auto& h : m_heroes)
            if (HexTile* ht = m_map.getTile(h.pos)) ht->heroId = h.id;
        if (m_activeHeroIdx >= static_cast<int>(m_heroes.size())) m_activeHeroIdx = 0;

        // Check if next player was already eliminated
        {
            uint32_t nextId = static_cast<uint32_t>(nextIdx + 1);
            bool hasTowns = false;
            for (const auto& t : m_towns)
                if (t.ownerId == nextId) { hasTowns = true; break; }
            if (m_heroes.empty() && !hasTowns) {
                m_victoryMessage = "Player " + std::to_string(m_currentPlayerIdx + 1)
                                 + " wins! Player " + std::to_string(nextIdx + 1)
                                 + " has been eliminated.";
                m_showVictory = true;
                m_audio.playSound("victory");
                // Restore current player for the victory screen
                m_heroes          = m_players[m_currentPlayerIdx].heroes;
                m_playerResources = m_players[m_currentPlayerIdx].resources;
                m_activeHeroIdx   = m_players[m_currentPlayerIdx].activeHeroIdx;
                m_worldHUD.setCurrentPlayerId(m_currentPlayerIdx + 1);
                FogOfWar::hideAll(m_map);
                if (!m_heroes.empty()) FogOfWar::updateVision(m_map, m_heroes);
                return;
            }
        }

        for (auto& h : m_heroes) { h.movePool = h.maxMove; h.path.clear(); h.pathStep = 0; }
        FogOfWar::hideAll(m_map);
        if (!m_heroes.empty()) FogOfWar::updateVision(m_map, m_heroes);
        if (!m_heroes.empty() && m_activeHeroIdx < static_cast<int>(m_heroes.size())) {
            float hx2, hy2;
            m_hexRenderer.grid().hexToWorld(m_heroes[m_activeHeroIdx].pos, hx2, hy2);
            m_camera.setPosition(hx2, hy2);
        }

        m_currentPlayerIdx     = nextIdx;
        m_showPlayerTurnBanner = true;
        m_playerTurnBannerT    = 2.5f;
        if (m_hotSeatMode) m_hotSeatHandoff = true;  // pass-the-device privacy screen
        m_reachable.clear();
        m_selected = {-999, -999};
        m_worldHUD.setCurrentPlayerId(currentPlayerId());
        uint32_t nextCid = static_cast<uint32_t>(nextIdx + 1);
        m_cachedWeeklyIncome = m_turns.calculateWeeklyIncome(m_towns, nextCid);
        for (const auto& r : m_resources)
            if (r.ownedBy == nextCid) m_cachedWeeklyIncome.add(r.type, mineYield(r));

        // Deliver deferred notifications for this player
        auto& notifs = m_playerNotifs[nextIdx];
        if (notifs.townLost) {
            m_lostTownName      = notifs.townName;
            m_showTownLostPopup = true;
            notifs.townLost = false; notifs.townName.clear();
        }
        if (notifs.defeated) {
            m_finalDefeat = true; m_showDefeat = true;
            notifs.defeated = false;
        }
        if (notifs.weekSummary) {
            m_weekSummaryIncome = notifs.weekIncome;
            m_weekSummaryWeek   = notifs.weekNum;
            m_weeklyEventHeadline.clear(); m_weeklyEventBody.clear(); m_weekChoiceOptions.clear();
            m_showWeekSummary   = true;
            notifs.weekSummary  = false;
        }
        return;
    }

    // ── Hotseat: last player ends turn → restore P1 then run full turn ───────────
    bool lastPlayerEndedTurn = false;
    if (m_numHumanPlayers >= 2 && m_currentPlayerIdx == m_numHumanPlayers - 1) {
        int lastIdx = m_currentPlayerIdx;
        m_showEncounterPrompt    = false;
        m_encounterOnAccept      = nullptr;
        m_encounterOnDecline     = nullptr;
        m_showWeekSummary        = false;
        m_weekChoiceOptions.clear();
        m_showLevelUpModal       = false;
        m_pendingLevelUps        = 0;
        m_levelUpOffers.clear();
        m_showCryptPopup         = false;
        m_pendingCryptId         = 0;
        m_pendingPandoraId       = 0;
        m_showUtopiaPopup        = false;
        m_pendingUtopiaId        = 0;
        m_showDwellingPopup      = false;
        m_showStatShrinePopup    = false;
        m_showTreasureChestPopup = false;
        m_showTreeKnowledgePopup = false;
        m_showQuestPopup         = false;
        m_showUnitExchange       = false;
        m_showCapturePopup       = false;
        m_showTownLostPopup      = false;
        m_showFoundCityPopup     = false;
        m_showTownPortalPopup    = false;
        m_pendingObjId           = 0;

        // Store last player's state
        m_players[lastIdx].heroes        = m_heroes;
        m_players[lastIdx].resources     = m_playerResources;
        m_players[lastIdx].activeHeroIdx = m_activeHeroIdx;
        for (const auto& h : m_players[lastIdx].heroes)
            if (HexTile* ht = m_map.getTile(h.pos)) ht->heroId = 0;

        // Restore P1 (idx 0)
        m_heroes          = m_players[0].heroes;
        m_playerResources = m_players[0].resources;
        m_activeHeroIdx   = m_players[0].activeHeroIdx;
        for (const auto& h : m_heroes)
            if (HexTile* ht = m_map.getTile(h.pos)) ht->heroId = h.id;
        if (m_activeHeroIdx >= static_cast<int>(m_heroes.size())) m_activeHeroIdx = 0;
        m_currentPlayerIdx = 0;
        m_reachable.clear();
        m_selected = {-999, -999};
        m_worldHUD.setCurrentPlayerId(1);

        // Check if P1 was eliminated while last player was playing
        {
            bool p1HasTowns = false;
            for (const auto& t : m_towns)
                if (t.ownerId == 1u) { p1HasTowns = true; break; }
            if (m_heroes.empty() && !p1HasTowns) {
                m_victoryMessage = "Player " + std::to_string(lastIdx + 1)
                                 + " wins! Player 1 has been eliminated.";
                m_showVictory = true;
                m_audio.playSound("victory");
                m_heroes          = m_players[lastIdx].heroes;
                m_playerResources = m_players[lastIdx].resources;
                m_activeHeroIdx   = m_players[lastIdx].activeHeroIdx;
                m_currentPlayerIdx = lastIdx;
                m_worldHUD.setCurrentPlayerId(lastIdx + 1);
                FogOfWar::hideAll(m_map);
                if (!m_heroes.empty()) FogOfWar::updateVision(m_map, m_heroes);
                return;
            }
        }

        lastPlayerEndedTurn = true;
        if (m_hotSeatMode) m_hotSeatHandoff = true;  // device passes back to Player 1

        m_cachedWeeklyIncome = m_turns.calculateWeeklyIncome(m_towns, 1);
        for (const auto& r : m_resources)
            if (r.ownedBy == 1u) m_cachedWeeklyIncome.add(r.type, mineYield(r));

        FogOfWar::hideAll(m_map);
        if (!m_heroes.empty()) FogOfWar::updateVision(m_map, m_heroes);
        if (!m_heroes.empty() && m_activeHeroIdx < static_cast<int>(m_heroes.size())) {
            float hx2, hy2;
            m_hexRenderer.grid().hexToWorld(m_heroes[m_activeHeroIdx].pos, hx2, hy2);
            m_camera.setPosition(hx2, hy2);
        }
    }

    // Reset per-day build limit for all towns
    for (auto& t : m_towns) t.builtToday = 0;

    // (Hot-seat alternation is handled entirely by the N-player handoff above;
    //  the legacy m_hotSeatP2Turn system was removed.)

    // FishingHouse daily income (+150 gold per player-owned house)
    for (const auto& wo : m_worldObjects) {
        if (wo.type != WorldObjectType::FishingHouse || wo.collected) continue;
        int ownerIdx = wo.faction - 1;  // faction stores 1-based playerId
        if (ownerIdx == m_currentPlayerIdx) {
            m_playerResources.add(ResourceType::Gold, 150);
        } else if (ownerIdx >= 0 && ownerIdx < (int)m_players.size()) {
            m_players[ownerIdx].resources.add(ResourceType::Gold, 150);
        }
    }

    // Fishing-hull daily income: the hull's whole pitch is "earns gold each
    // day spent at sea", but until now that existed only in its tooltip.
    // "At sea" = aboard and on a Water tile — parked at the dock earns nothing.
    {
        constexpr int kFishingBoatGold = 100;   // below a FishingHouse's 150 —
                                                // the boat also moves you
        auto fishingAtSea = [&](const Hero& h) {
            if (!h.onBoat || h.boatType != BoatType::Fishing) return false;
            const HexTile* t = m_map.getTile(h.pos);
            return t && t->terrain == Terrain::Water;
        };
        for (const auto& h : m_heroes)
            if (fishingAtSea(h)) m_playerResources.add(ResourceType::Gold, kFishingBoatGold);
        for (int pi = 0; pi < (int)m_players.size(); ++pi) {
            if (pi == m_currentPlayerIdx) continue;   // active player uses m_heroes
            for (const auto& h : m_players[pi].heroes)
                if (fishingAtSea(h))
                    m_players[pi].resources.add(ResourceType::Gold, kFishingBoatGold);
        }
        for (const auto& h : m_enemyHeroes)
            if (fishingAtSea(h))
                aiResources(h.ownerId).add(ResourceType::Gold, kFishingBoatGold);
    }

    // Restore hero movement pools and daily mana regen for enemy heroes.
    // In hot-seat this runs after the LAST player's handoff, so m_heroes is P1's
    // fresh-day roster; other humans' heroes were reset during their own handoff.
    {
        for (auto& h : m_heroes)      h.movePool = h.maxMove;
        for (auto& h : m_enemyHeroes) {
            h.movePool = h.maxMove;
            int manaRegen = std::max(2, 2 + h.maxMana / 10);
            h.mana = std::min(h.maxMana, h.mana + manaRegen);
        }
    }


        // Enemy hero AI — omniscient (full map visibility, no fog), faction-optimal.
        // Runs once per full round: immediately in single-player, or after the last
        // human ends their turn in hot-seat (m_enemyHeroes is pure AI in all modes).
        //
        // THREADING.md Phase 3 groundwork — the monolithic AI block is now three
        // seams: aiTurnSetup() builds the shared round state, aiTakeHeroTurn(ehi)
        // runs one hero's whole turn, doEndTurnPost() is everything that used to
        // follow the loop. In Watch mode the roster is processed a few heroes per
        // FRAME from updateWorldMap() (m_aiTurn.active) so the render loop keeps
        // drawing instead of freezing 0 FPS for the whole AI round. Normal play
        // drains the loop synchronously below — identical order, same thread, so
        // no new race surface exists in either mode.
        if ((m_numHumanPlayers <= 1 || lastPlayerEndedTurn) && !m_heroes.empty()) {
            aiTurnSetup();
            m_aiTurn.nextHero = 0;
            if (m_watchingAI) {
                m_aiTurn.active = true;
                m_aiTurn.lastPlayerEndedTurn = lastPlayerEndedTurn;
                return;   // updateWorldMap() steps the heroes and runs the post
            }
            while (m_aiTurn.nextHero < static_cast<int>(m_enemyHeroes.size())) {
                if (!aiTakeHeroTurn(m_aiTurn.nextHero++))
                    return;   // combat vs the player aborts the round (as before)
            }
        }

        doEndTurnPost(lastPlayerEndedTurn);
}

// ── THREADING.md Phase 3 groundwork: the AI round, split out of doEndTurn ────
// Shared state for one AI round. Everything here is main-thread only; the
// split exists so Watch mode can spread heroes across frames today and so the
// per-hero planning has a clean seam for a worker-thread AiPlanner tomorrow.
void Game::aiTurnSetup()
{
    AiTurnState& S = m_aiTurn;
    S.combatTriggered = false;
    S.allHeroesForTargeting.clear();
    S.aiNeededResByOwner.clear();

    Hero& playerHero = m_heroes[m_activeHeroIdx];
    const auto& unitDefs = m_registry.units();

    // ── Omniscient threat state ──────────────────────────────────────────────
    S.plStr = heroStrength(playerHero, unitDefs);
    // Weak player = just fought a battle (army below expected for this week)
    S.playerIsWeak = (S.plStr > 0 && S.plStr < m_turns.week() * 350);

    // Player's key faction resource — AI denies these mines first
    int plFidx = static_cast<int>(playerHero.faction);
    S.denialRes = (plFidx >= 0 && plFidx < 9)
                ? kFactionResource[plFidx] : ResourceType::Gold;

    // Resource blocking each AI player's own build queue — its mines get top
    // priority (same 180 weighting the watch hero uses). Per-owner: each AI
    // player has its own economy/build queue, not one shared "team" queue.
    {
        const auto& allDefs = m_registry.buildings();
        for (const auto& t : m_towns) {
            if (!isAiOwner(t.ownerId)) continue;
            if (S.aiNeededResByOwner.count(t.ownerId)) continue;
            int tfi = static_cast<int>(t.faction);
            if (tfi < 0 || tfi >= 9) continue;
            ResourceType need = aiBlockingResource(t, kBuildOrder[tfi], allDefs, aiResources(t.ownerId));
            if (static_cast<int>(need) < RESOURCE_COUNT) S.aiNeededResByOwner[t.ownerId] = need;
        }
    }

    // ── Tech scouting #3: resource-hoarding prediction (AI_ROADMAP) ──────────
    // Read every owner's treasury against the big purchases still open to
    // them (City Hall → faction Capitol chain, or Castle walls). An owner
    // sitting at 50–99% of such a cost is saving up — the raid window where
    // cutting their gold line forces emergency spending. Below 50% a raid
    // barely matters; at 100% they buy next tick and the window is gone.
    S.hoardingOwners.clear();
    {
        const auto& allDefs = m_registry.buildings();
        auto goldCost = [&](int bid) -> int {
            for (const auto& d : allDefs)
                if (d.id == bid) return d.cost.get(ResourceType::Gold);
            return 0;
        };
        for (const auto& t : m_towns) {
            if (t.ownerId == 0 || S.hoardingOwners.count(t.ownerId)) continue;
            int gold = (t.ownerId == 1)
                     ? m_playerResources.get(ResourceType::Gold)
                     : (isAiOwner(t.ownerId)
                            ? (int)aiResources(t.ownerId).get(ResourceType::Gold)
                            : -1);
            if (gold < 0) continue;
            int tfi = static_cast<int>(t.faction);
            int cheapest = 0;
            auto consider = [&](int bid) {
                int c = goldCost(bid);
                if (c > 0 && (cheapest == 0 || c < cheapest)) cheapest = c;
            };
            if (t.hasBuilding(BID::TOWN_HALL) && !t.hasBuilding(BID::CITY_HALL))
                consider(BID::CITY_HALL);
            if (tfi >= 0 && tfi < 9 && t.hasBuilding(BID::CITY_HALL)
                && !t.hasBuilding(13 + tfi))            // faction Capitol
                consider(13 + tfi);
            if (t.hasBuilding(BID::CITADEL) && !t.hasBuilding(BID::CASTLE))
                consider(BID::CASTLE);
            if (cheapest > 0 && gold * 2 >= cheapest && gold < cheapest) {
                S.hoardingOwners.insert(t.ownerId);
                if (m_turns.day() == 1)   // once a week, not every day
                    gLog("[SCOUT] P%u is hoarding (%dg toward a %dg purchase) — "
                         "gold mines marked for raids\n", t.ownerId, gold, cheapest);
            }
        }
    }

    // Difficulty tunes how boldly the AI commits. Hard attacks at a lower
    // strength ratio and retreats less readily; Easy is more timid.
    int diffIdx = std::clamp(m_newGameDifficulty, 0, 2);
    static const int kAggrPct[3]    = { 6, 5, 4 };  // Easy 60% / Normal 50% / Hard 40%
    static const int kRetreatPct[3] = { 5, 4, 3 };  // Easy 50% / Normal 40% / Hard 30%
    S.aggrPct    = kAggrPct[diffIdx];
    S.retreatPct = kRetreatPct[diffIdx];

    // ── Gather every hero on the map (human AND rival AI) so each bot
    //    hunts/fears whichever hero is nearest, human or bot alike. Positions
    //    are captured once per round (they were under the serial loop too). ──
    for (const auto& h : m_heroes)
        S.allHeroesForTargeting.push_back(
            {h.pos, heroStrength(h, unitDefs), static_cast<uint32_t>(m_currentPlayerIdx + 1)});
    for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
        if (pi == m_currentPlayerIdx) continue;  // that roster is live in m_heroes
        for (const auto& h : m_players[pi].heroes)
            S.allHeroesForTargeting.push_back(
                {h.pos, heroStrength(h, unitDefs), static_cast<uint32_t>(pi + 1)});
    }
    for (const auto& h : m_enemyHeroes)
        S.allHeroesForTargeting.push_back({h.pos, heroStrength(h, unitDefs), h.ownerId});

    // Land connectivity, once per turn — terrain is static within a turn.
    // Every hero's pathfinding fast-reject reads this, so it must be built
    // before any planning and never mutated during it.
    {
        int stamp = m_turns.week() * 100 + m_turns.day();
        if (m_landCompTurn != stamp || m_landComp.empty())
            rebuildLandComponents();
    }

    // ── Strength-based weekly roles, computed PER PLAYER ─────────────────────
    //    Each AI owner gets its own raider/economic/defender split, so all AI
    //    players play the game (a global ranking starved everyone but one).
    S.heroRank.assign(m_enemyHeroes.size(), 0);  // 0=raider,1=economic,2+=defender
    {
        std::vector<uint32_t> owners;
        for (const auto& eh : m_enemyHeroes)
            if (std::find(owners.begin(), owners.end(), eh.ownerId) == owners.end())
                owners.push_back(eh.ownerId);
        for (uint32_t owner : owners) {
            std::vector<int> idxs;
            for (int i = 0; i < (int)m_enemyHeroes.size(); ++i)
                if (m_enemyHeroes[i].ownerId == owner) idxs.push_back(i);
            std::sort(idxs.begin(), idxs.end(), [&](int a, int b){
                return heroStrength(m_enemyHeroes[a], unitDefs)
                     > heroStrength(m_enemyHeroes[b], unitDefs);
            });
            for (size_t r = 0; r < idxs.size(); ++r)
                S.heroRank[idxs[r]] = (int)r;
        }
    }

    // ── Consolidate armies into each player's raider ─────────────────────────
    //    genre opening: extra heroes act as army shuttles — any non-raider hero
    //    adjacent to its player's raider dumps its army into it (7-slot cap,
    //    largest stacks first), so the AI fields one fat stack.
    for (int ri = 0; ri < (int)m_enemyHeroes.size(); ++ri) {
        if (S.heroRank[ri] != 0 || m_enemyHeroes[ri].eliminated) continue;
        Hero& raider = m_enemyHeroes[ri];
        for (int fi = 0; fi < (int)m_enemyHeroes.size(); ++fi) {
            if (fi == ri) continue;
            Hero& feeder = m_enemyHeroes[fi];
            if (feeder.eliminated) continue;
            if (feeder.ownerId != raider.ownerId) continue;
            int d = HexGrid::distance(feeder.pos, raider.pos);
            if (d > 1) continue;   // must be adjacent or same tile
            std::sort(feeder.army.begin(), feeder.army.end(),
                      [](const UnitStack& a, const UnitStack& b){ return a.count > b.count; });
            for (auto it = feeder.army.begin(); it != feeder.army.end();) {
                bool merged = false;
                for (auto& s : raider.army)
                    if (s.defId == it->defId) { s.count += it->count; merged = true; break; }
                if (!merged && raider.army.size() < 7) {
                    raider.army.push_back(*it); merged = true;
                }
                it = merged ? feeder.army.erase(it) : ++it;
            }
        }
    }
}

// One enemy hero's entire turn. Body moved VERBATIM (reindented) from the
// doEndTurn() per-hero loop; the aliases below keep every identifier the body
// referenced resolving to the same thing it did as a loop body, so the diff
// is a pure move. Returns false when combat against the player was triggered
// — the caller must abort the rest of the AI round, exactly as the old
// `if (combatTriggered) return;` did from inside doEndTurn().
bool Game::aiTakeHeroTurn(int ehi)
{
    AiTurnState& S = m_aiTurn;
    auto& eHero = m_enemyHeroes[ehi];
    if (eHero.eliminated) return true; // lost a field battle earlier this pass — skip, round continues
    // [DBG crash-hunt] Breadcrumb for the 'vector<UnitStack> operator[] assert'
    // crash reported after 'Day N Week 1' with zero other log output in
    // between — extensive static audit of every UnitStack-vector access in
    // the codebase found nothing unsafe, so this pins down WHICH hero/state
    // is live when it happens next, instead of guessing blind again.
    gLog("[DBG turn] ehi=%d P%u %s army=%zu pos=(%d,%d)\n",
         ehi, eHero.ownerId, eHero.name.c_str(), eHero.army.size(),
         eHero.pos.q, eHero.pos.r);
    Hero& playerHero        = m_heroes[m_activeHeroIdx];
    const auto& unitDefs    = m_registry.units();
    bool& combatTriggered   = S.combatTriggered;
    const int  plStr        = S.plStr;
    const bool playerIsWeak = S.playerIsWeak;
    const ResourceType denialRes = S.denialRes;
    const int aggrPct       = S.aggrPct;
    const int retreatPct    = S.retreatPct;
    auto& allHeroesForTargeting = S.allHeroesForTargeting;
    auto& heroRank          = S.heroRank;
    auto& aiNeededResByOwner = S.aiNeededResByOwner;
    auto& hoardingOwners    = S.hoardingOwners;
    using RivalHero = AiTurnState::RivalHero;
    // Nearest RIVAL (any owner other than selfOwnerId or an ally) — human or bot.
    auto nearestRival = [&](HexCoord from, uint32_t selfOwnerId, int& outStr) -> const RivalHero* {
        const RivalHero* best = nullptr; int bestD = INT32_MAX;
        for (const auto& hh : allHeroesForTargeting) {
            if (isAllied(hh.ownerId, selfOwnerId)) continue;
            int d = HexGrid::distance(from, hh.pos);
            if (d < bestD) { bestD = d; best = &hh; }
        }
        if (best) outStr = best->str;
        return best;
    };
    // NOTE: combat no longer aborts the roster — once one hero enters
    // combat, the rest still take their turn; they just can't start a
    // second fight (the player-tile is blocked and untargeted below).

    // ── Strength-based role (see byStrength above) ────────────────────
    bool isRaider   = (heroRank[ehi] == 0);
    bool isDefender = (heroRank[ehi] >= 2);

    // Recruit from one of THIS player's own towns within 1 tile —
    // paid from its own pool at real unit costs, plus pick up the
    // garrison (which weekly recruitment fills). Was "any AI
    // town" — a rival AI player's town, not just a teammate's.
    for (auto& t : m_towns) {
        if (t.ownerId != eHero.ownerId) continue;
        if (HexGrid::distance(eHero.pos, t.pos) > 1) continue;
        takeGarrison(t, eHero, unitDefs);
        aiPaidRecruit(t, eHero.army, aiResources(eHero.ownerId), unitDefs);
        // Field-upgrade base-tier stacks to whichever upgrade path the
        // town has built (previously only the watch-mode player did this,
        // so enemy armies stayed base-tier forever).
        for (const auto& dw : t.dwellings) {
            if (dw.path == UpgradePath::None) continue;
            const UnitDef* pathDef = nullptr;
            const UnitDef* baseDef = nullptr;
            for (const auto& ud : unitDefs) {
                if (ud.faction == t.faction && ud.tier == dw.tier) {
                    if (ud.path == dw.path)                 pathDef = &ud;
                    else if (ud.path == UpgradePath::None)  baseDef = &ud;
                }
            }
            if (!pathDef || !baseDef) continue;
            for (auto& s : eHero.army)
                if (s.defId == baseDef->id) s.defId = pathDef->id;
        }
    }

    int eiStr = heroStrength(eHero, unitDefs);

    // ── Owner's town count: drives desperation + world spells ────
    int ownerTownCount = 0;
    for (const auto& t : m_towns)
        if (t.ownerId == eHero.ownerId) ++ownerTownCount;
    // A player with NO town is on a death clock — the weekly decay
    // below starves its heroes out after 6 townless weeks. Sitting
    // around guarantees elimination, so go all-in: max aggression
    // and beeline the nearest enemy town. Better to die attacking
    // than to be disbanded having never tried.
    const bool townlessDesperate = (ownerTownCount == 0);

    // Found City, as a callable: a bot clears a Utopia mid-move and
    // has walked on by the next turn, so a turn-start-only check
    // never fired. This runs BOTH at turn start and immediately
    // after a Utopia is cleared, while the hero is still stood on it.
    auto tryFoundCity = [&]() -> bool {
        if (eHero.level < 10 || eHero.mana < 15) return false;
        WorldObject* ut = nullptr;
        for (auto& obj : m_worldObjects)
            if (obj.type == WorldObjectType::Utopia
                && obj.pos == eHero.pos && obj.collected) { ut = &obj; break; }
        if (!ut) return false;
        Resources cost;
        cost.set(ResourceType::Gold,         10000);
        cost.set(ResourceType::Iron,            10);
        cost.set(ResourceType::FaithStones,     10);
        cost.set(ResourceType::BloodEssence,    10);
        cost.set(ResourceType::VerdantSap,      10);
        cost.set(ResourceType::Mercury,         10);
        Resources& res = aiResources(eHero.ownerId);
        if (!res.canAfford(cost)) return false;
        res.spend(cost);
        eHero.mana -= 15;
        uint32_t newId = 1;
        for (const auto& t : m_towns) newId = std::max(newId, t.id + 1);
        Town nt;
        nt.id      = newId;
        nt.name    = eHero.name + "'s Settlement";
        nt.faction = eHero.faction;
        nt.pos     = eHero.pos;
        nt.ownerId = eHero.ownerId;
        if (HexTile* ht = m_map.getTile(nt.pos)) ht->townId = nt.id;
        m_towns.push_back(nt);
        uint32_t utId = ut->id;
        m_worldObjects.erase(
            std::remove_if(m_worldObjects.begin(), m_worldObjects.end(),
                [utId](const WorldObject& o){ return o.id == utId; }),
            m_worldObjects.end());
        gLog("P%u %s founded a city on a cleared Utopia (week %d)\n",
             eHero.ownerId, eHero.name.c_str(), m_turns.week());
        return true;
    };

    // ── AI world-map spells ──────────────────────────────────────
    // The AI previously never cast ANY world spell (castWorldSpell
    // is only reachable from the human UI panel), which is why it
    // never founded a single Utopia city and never repositioned.
    {
        auto knows = [&](int sid) {
            for (int s : eHero.knownSpells) if (s == sid) return true;
            return false;
        };
        // FOUND CITY — a cleared Utopia under the hero becomes a
        // town. A level-10 AI hero picks the spell up here: it has
        // no spellbook UI to learn it from, so without this the
        // Utopia->town mechanic is unreachable for bots entirely.
        // Base heroes cap at maxMana=10 but Found City costs 15, so
        // the precondition was literally unsatisfiable — lift the
        // ceiling along with the grant.
        if (eHero.level >= 10 && !knows(SPL::FOUND_CITY)) {
            eHero.knownSpells.push_back(SPL::FOUND_CITY);
            if (eHero.maxMana < 20) eHero.maxMana = 20;
        }
        // Town Portal is cheap (8 mana) and within a mid-level bot's
        // reach, but bots only ever learned spells from rare scrolls.
        if (eHero.level >= 5 && !knows(SPL::TOWN_PORTAL))
            eHero.knownSpells.push_back(SPL::TOWN_PORTAL);
        if (tryFoundCity()) ++ownerTownCount;
        // TOWN PORTAL — jump home to defend a threatened town when
        // far away and still holding a full move.
        if (knows(SPL::TOWN_PORTAL) && eHero.mana >= 8
            && eHero.movePool >= eHero.maxMove && ownerTownCount > 0) {
            const Town* threatened = nullptr;
            for (const auto& t : m_towns) {
                if (t.ownerId != eHero.ownerId) continue;
                if (HexGrid::distance(eHero.pos, t.pos) <= 10) continue;
                bool underThreat = false;
                for (const auto& oh : m_enemyHeroes) {
                    if (oh.eliminated || isAllied(oh.ownerId, eHero.ownerId)) continue;
                    if (HexGrid::distance(oh.pos, t.pos) <= 6) { underThreat = true; break; }
                }
                if (!underThreat && !m_heroes.empty()
                    && !isAllied(static_cast<uint32_t>(currentPlayerId()), eHero.ownerId)
                    && HexGrid::distance(m_heroes[0].pos, t.pos) <= 6)
                    underThreat = true;
                // Tech scouting #4: don't teleport home just to die — if the
                // attacker overwhelms even garrison+this hero, write the town
                // off and keep fighting where the hero actually matters.
                if (underThreat && aiTownIsWriteOff(t, eiStr, unitDefs)) {
                    gLog("[SCOUT] P%u %s lets %s fall — defense is hopeless, "
                         "redeploying\n", eHero.ownerId, eHero.name.c_str(),
                         t.name.c_str());
                    underThreat = false;
                }
                if (underThreat) { threatened = &t; break; }
            }
            if (threatened) {
                if (HexTile* oldT = m_map.getTile(eHero.pos)) oldT->heroId = 0;
                eHero.pos    = threatened->pos;
                eHero.mana  -= 8;
                eHero.onBoat = false;
                eHero.marchPath.clear(); eHero.marchPathIdx = 0;
                if (HexTile* nT = m_map.getTile(eHero.pos)) nT->heroId = eHero.id;
                gLog("P%u %s cast Town Portal to defend %s (week %d)\n",
                     eHero.ownerId, eHero.name.c_str(),
                     threatened->name.c_str(), m_turns.week());
            }
        }
    }

    // Strength gauge vs the NEAREST rival hero to this AI hero —
    // human or bot alike (was always the single active player
    // hero, so hot-seat/rival-AI opponents nearer by were ignored).
    int nearHumanStr = plStr;
    const RivalHero* target = nearestRival(eHero.pos, eHero.ownerId, nearHumanStr);
    HexCoord targetPos = target ? target->pos : playerHero.pos;
    if (nearHumanStr <= 0) nearHumanStr = plStr;

    // ── Tech scouting: respect magic tech (AI_ROADMAP "Psychic Bundle" #2) ──
    // The AI reads the hunted rival's built tech: a T3/T4 mage guild means
    // their hero fights with heavily discounted spells, so raw army strength
    // understates them. Count the caster as effectively stronger (+15%/+25%)
    // — this flows into aggressive/veryWeak/strRatio/dominant below, so the
    // AI wants a real edge before engaging a mage and disengages earlier.
    if (target) {
        int guildTier = 0;
        for (const auto& tw : m_towns) {
            if (tw.ownerId != target->ownerId) continue;
            if (tw.hasBuilding(BID::MAGE_GUILD_T4)) { guildTier = 4; break; }
            if (tw.hasBuilding(BID::MAGE_GUILD_T3))   guildTier = 3;
        }
        if      (guildTier == 4) nearHumanStr = nearHumanStr * 5 / 4;
        else if (guildTier == 3) nearHumanStr = nearHumanStr * 23 / 20;
    }

    // Raider: attack at the difficulty-scaled ratio OR when the opponent
    // is wounded; Economic: only at 1.5×; Defender: never.
    // ── Personality modifies aggression per this hero's owner ─────
    // Warrior commits at a lower ratio; Builder/Mage need a bigger
    // edge before attacking; Explorer is timid but roams (handled in
    // candidate scoring below).
    int ownerAggr    = aggrPct;
    int ownerRetreat = retreatPct;
    AiPersonality persona = m_aiPersonality[std::min<uint32_t>(eHero.ownerId, 9)];
    switch (persona) {
        case AiPersonality::Warrior:  ownerAggr -= 2; ownerRetreat -= 1; break;
        case AiPersonality::Explorer: ownerAggr += 1;                    break;
        case AiPersonality::Builder:  ownerAggr += 2; ownerRetreat += 1; break;
        case AiPersonality::Mage:
            // calm early, escalates late
            ownerAggr += (m_turns.week() < 12 ? 3 : -2);
            break;
    }
    ownerAggr    = std::max(2, ownerAggr);
    ownerRetreat = std::max(1, ownerRetreat);

    // Rich-economy override: a bot sitting on a big treasury has
    // already "won" economically and should convert that lead into
    // military pressure instead of hoarding. The more gold banked,
    // the more aggressive — this fixes the "3M gold, tiny army,
    // never attacks, game drags to week 43" failure.
    int ownerGold = (int)aiResources(eHero.ownerId).get(ResourceType::Gold);
    if (ownerGold > 100000)      ownerAggr = std::max(2, ownerAggr - 5);
    else if (ownerGold > 40000)  ownerAggr = std::max(2, ownerAggr - 3);
    else if (ownerGold > 15000)  ownerAggr = std::max(2, ownerAggr - 1);

    bool aggressive = isDefender ? false
                    : isRaider   ? (playerIsWeak || eiStr * 10 >= nearHumanStr * ownerAggr)
                    :              (eiStr * 10 >= nearHumanStr * 15);
    // A very rich player commits even its non-raider heroes.
    if (ownerGold > 40000 && !isDefender
        && eiStr * 10 >= nearHumanStr * 9)
        aggressive = true;
    // Late-game escalation: past week 20 any non-defender at >=80%
    // relative strength commits — prevents the endless "both sides farm
    // mines and never engage" stalemate once the map is exhausted.
    if (m_turns.week() >= 20 && !isDefender
        && eiStr * 10 >= nearHumanStr * 8)
        aggressive = true;
    // Townless = doomed anyway: always commit, never hold back.
    if (townlessDesperate) aggressive = true;
    // Retreat when very weak regardless of role (difficulty + persona)
    bool veryWeak   = (eiStr * 10 <  nearHumanStr * ownerRetreat);
    // ...and never retreat/cower when there's no home to run to.
    if (townlessDesperate) veryWeak = false;

    // Graduated retreat thresholds
    float strRatio = nearHumanStr > 0 ? (float)eiStr / nearHumanStr : 99.f;
    bool softRetreat = strRatio < 0.6f;
    bool dominant    = strRatio >= 1.2f;
    // A strong opponent on the other side of the map is no reason
    // to hide at home — cowering locked every AI hero in its
    // castle all game (deposit + movePool=0 daily) whenever one
    // human hero snowballed. Retreat instincts only apply when
    // the threat can plausibly reach us.
    int threatDist = target ? HexGrid::distance(eHero.pos, target->pos) : 999;
    if (threatDist > 10) { veryWeak = false; softRetreat = false; }
    // GhostWalk is now a soft penalty (halved target score) rather than
    // a hard exemption that made a GhostWalk player un-huntable forever.
    float ghostMult = playerHero.ghostWalkSpecialty ? 0.5f : 1.0f;

    // Pinned by siege camp: enemy hero can't leave their besieged town
    bool pinnedBySiege = false;
    for (const auto& t : m_towns) {
        if (t.ownerId != eHero.ownerId || !t.underSiege) continue;
        if (HexGrid::distance(eHero.pos, t.pos) <= 1) { pinnedBySiege = true; break; }
    }
    if (pinnedBySiege) { eHero.movePool = 0; }

    // Snap camera to this enemy hero so Watch AI is visible
    if (m_watchingAI) {
        float ewx, ewy;
        m_hexRenderer.grid().hexToWorld(eHero.pos, ewx, ewy);
        m_camera.setPosition(ewx, ewy);
    }

    // Per-turn memo of goals a search already proved unreachable
    // (value = the horizon that failed). The candidate list is
    // rebuilt every move step, so without this the same doomed
    // searches re-ran once per step — the residual ~1.2 s turn
    // spikes after the O(1) component reject (targets on the SAME
    // landmass but walled off or beyond the horizon, which the
    // reject can't see). Trades a sliver of mid-turn reactivity —
    // a dead end stays "dead" until next turn even if the hero
    // walks toward it — for not re-proving it ~100× per turn.
    // Local to this hero's turn: never carried across turns.
    std::unordered_map<HexCoord, int, HexCoordHash> failedTargets;
    bool memoOnBoat = eHero.onBoat;
    // marchGoalTurns is "turns spent committed" (the 60-turn lock
    // timeout) but was incremented once per MOVE STEP, so the lock
    // really expired after ~60 steps ≈ 6-8 turns — every cross-map
    // march (~15+ turns) timed out mid-march, re-locked, and re-ran
    // the 400-hex search. Count it once per hero-turn.
    bool marchTurnCounted = false;

    while (eHero.movePool > 0) {
        // Persona weight biases: mineMul favours resources, attackMul
        // favours enemy heroes/towns. Warrior hunts, Explorer grabs
        // mines, Builder/Mage lean economic/defensive.
        float mineMul = 1.f, attackMul = 1.f;
        switch (persona) {
            case AiPersonality::Explorer: mineMul = 1.8f; attackMul = 0.7f; break;
            case AiPersonality::Warrior:  mineMul = 0.8f; attackMul = 1.8f; break;
            case AiPersonality::Builder:  mineMul = 1.2f; attackMul = 0.7f; break;
            case AiPersonality::Mage:
                mineMul   = 1.2f;
                attackMul = (m_turns.week() < 12 ? 0.6f : 1.6f);
                break;
        }

        // TEMP INSTRUMENT: is the per-step candidate rescan really
        // the dominant cost, as THREADING.md Phase 4 claims? Measure
        // before optimising — the same doc's fan-out advice was wrong.
        auto tCandStart = std::chrono::steady_clock::now();
        // Score-based candidate selection: value / distance
        struct Cand { HexCoord pos; float score; };
        std::vector<Cand> cands;
        auto add = [&](HexCoord pos, float val) {
            if (pos == eHero.pos) return; // don't target current position
            int d = std::max(1, HexGrid::distance(eHero.pos, pos));
            cands.push_back({pos, val / d});
        };
        // Persona-weighted variants for resource / attack candidates.
        auto addMine   = [&](HexCoord pos, float val){ add(pos, val * mineMul); };
        auto addAttack = [&](HexCoord pos, float val){ add(pos, val * attackMul); };

        if (veryWeak) {
            for (const auto& t : m_towns)
                if (t.ownerId == eHero.ownerId) add(t.pos, 500.f);
            // A weak hero that's already home has no retreat target left
            // (add() rejects a candidate equal to its own position), so
            // it would otherwise freeze in place indefinitely every turn
            // until it happens to out-level the player. Give it safe,
            // nearby (within 4 hexes) unclaimed resources so it keeps
            // doing something productive instead of statue-standing.
            for (const auto& r : m_resources) {
                if (isAllied(r.ownedBy, eHero.ownerId)) continue;   // skip own/ally mine
                if (HexGrid::distance(eHero.pos, r.pos) <= 6) add(r.pos, 40.f);
            }
        } else if (isDefender) {
            for (const auto& r : m_resources)
                if (!isAllied(r.ownedBy, eHero.ownerId)) addMine(r.pos, 100.f);
            for (const auto& t : m_towns)
                if (t.ownerId == eHero.ownerId) add(t.pos, 80.f);
            // Defenders actively intercept any RIVAL hero (human or
            // bot — allies excluded) threatening an owned town
            // (previously defenders just wandered for resources
            // and never protected anything).
            for (const auto& t : m_towns) {
                if (t.ownerId != eHero.ownerId) continue;
                for (const auto& hh : allHeroesForTargeting) {
                    if (isAllied(hh.ownerId, eHero.ownerId)) continue;
                    if (HexGrid::distance(hh.pos, t.pos) <= 6)
                        addAttack(hh.pos, 250.f * ghostMult);
                }
            }
        } else {
            // ── Supply-chain shuttle for non-raider heroes ──────────
            // A clean two-state ferry: carrying troops → deliver to
            // the raider (dominant pull, no detours); empty-handed →
            // go to own town and grab the piled-up garrison. This is
            // the genre scout-chain that keeps the raider on the front
            // line instead of breaking off to collect units itself.
            bool didShuttle = false;
            if (!isRaider) {
                int carried = 0;
                for (const auto& s : eHero.army) carried += s.count;

                // Find this player's raider.
                const Hero* raider = nullptr;
                for (int rj = 0; rj < (int)m_enemyHeroes.size(); ++rj) {
                    if (heroRank[rj] != 0) continue;
                    const Hero& r = m_enemyHeroes[rj];
                    if (!r.eliminated && r.ownerId == eHero.ownerId) { raider = &r; break; }
                }

                if (carried > 0 && raider) {
                    // Deliver: head straight for the raider. Weight
                    // scales with cargo so a full ferry is urgent.
                    add(raider->pos, 500.f + std::min(500.f, carried * 4.f));
                    didShuttle = true;
                } else {
                    // Empty: go to the OWN town with the most garrison
                    // waiting to be picked up.
                    const Town* bestTown = nullptr; int bestG = 0;
                    for (const auto& t : m_towns) {
                        if (t.ownerId != eHero.ownerId) continue;
                        int g = 0;
                        for (const auto& gs : t.garrison) g += gs.count;
                        for (const auto& dw : t.dwellings) if (dw.available > 0) g += dw.available;
                        if (g > bestG) { bestG = g; bestTown = &t; }
                    }
                    if (bestTown && bestG > 0) {
                        add(bestTown->pos, 350.f + std::min(400.f, bestG * 4.f));
                        didShuttle = true;
                    }
                }
            }

            // Own town to recruit / collect garrison (raiders + any
            // hero not already committed to a shuttle run).
            if (!didShuttle)
            for (const auto& t : m_towns) {
                if (t.ownerId != eHero.ownerId) continue;
                int garrisonUnits = 0;
                for (const auto& gs : t.garrison) garrisonUnits += gs.count;
                bool dwellUnits = false;
                for (const auto& dw : t.dwellings) if (dw.available > 0) { dwellUnits = true; break; }
                float pull = 0.f;
                if (garrisonUnits > 0) pull = 200.f + std::min(600.f, garrisonUnits * 4.f);
                else if (dwellUnits && (int)eHero.army.size() < 7) pull = 250.f;
                if (pull > 0.f) add(t.pos, pull);
            }
            // Towns — capture ANY rival town (enemy AI or human).
            // Neutral towns a lesser target. This is how players get
            // eliminated — previously only human towns were targeted
            // (ownerId <= numHumanPlayers), so AI-vs-AI games never
            // resolved: nobody ever took anyone's town.
            //
            // Key fix: town value scales with THIS hero's strength and
            // uses a gentler distance falloff (applied via addTownGoal
            // below), so a strong army commits to the long march to an
            // enemy capital instead of forever re-grabbing the nearest
            // mine (score = val/dist buried distant towns under swarms
            // of adjacent mines — the "everyone stuck at 1 town, no
            // eliminations at week 49 despite million-str armies" bug).
            long long myStr = heroStrength(eHero, unitDefs);
            float strBoost = 1.f + std::min(8.f, (float)myStr / 60000.f);
            for (const auto& t : m_towns) {
                if (t.ownerId == 0) {
                    // Homeless: an unowned town is a free home — grab
                    // it over anything else on the map.
                    addAttack(t.pos, townlessDesperate ? 20000.f : 150.f);
                } else if (!isAllied(t.ownerId, eHero.ownerId)) {
                    int rivalTowns = 0;
                    for (const auto& t2 : m_towns)
                        if (t2.ownerId == t.ownerId) ++rivalTowns;
                    float val = 600.f * strBoost;
                    // Finish the fight. A rival on their last town is one
                    // capture away from elimination — swarm it so the game
                    // actually resolves instead of grinding to the week-80
                    // backstop. A rival down to two towns is next in line.
                    if (rivalTowns == 1)      val = 4200.f * strBoost; // elimination kill shot
                    else if (rivalTowns == 2) val = 1500.f * strBoost; // press the advantage
                    // Suicide run: with no town of our own we're dead
                    // in 6 weeks regardless, so ANY enemy town
                    // outweighs every mine/chest on the map. Take one
                    // or die trying.
                    if (townlessDesperate) val = 20000.f;
                    // ── Tech scouting: the pre-wall strike window ─────
                    // (AI_ROADMAP "Psychic Bundle" #2.) The AI reads the
                    // defender's fort tech: no Fort yet = an open-field
                    // capture — hit that window before the walls go up.
                    // Castle walls = a costly grind — prefer softer
                    // targets. Kill shots (last town) and desperation
                    // keep their absolute priority untouched.
                    else if (rivalTowns > 1) {
                        if      (!t.hasBuilding(BID::FORT))    val *= 1.35f;
                        else if (t.hasBuilding(BID::CASTLE))   val *= 0.85f;
                    }
                    // Gentler distance penalty for towns: use sqrt(dist)
                    // instead of dist so a strong army will cross the
                    // map for the kill. add() divides by dist, so we
                    // pre-multiply by sqrt(dist) to soften it.
                    int d = std::max(1, HexGrid::distance(eHero.pos, t.pos));
                    addAttack(t.pos, val * std::sqrt((float)d));
                }
            }
            // Resources — deny player's key resource and favour own faction's
            {
                int eFidx = static_cast<int>(eHero.faction);
                ResourceType enemyKeyRes = (eFidx >= 0 && eFidx < 9)
                                         ? kFactionResource[eFidx] : ResourceType::Gold;
                auto neededIt = aiNeededResByOwner.find(eHero.ownerId);
                ResourceType myNeededRes = (neededIt != aiNeededResByOwner.end())
                                         ? neededIt->second : static_cast<ResourceType>(RESOURCE_COUNT);
                for (const auto& r : m_resources) {
                    // Skip mines THIS player or an ALLY already
                    // holds — every non-allied AI player runs its
                    // own economy, so a rival bot's mine is a
                    // legitimate raid target, not team property.
                    if (isAllied(r.ownedBy, eHero.ownerId)) continue;
                    // Unclaimed/rival mines are worth taking — they're
                    // the income engine. Unguarded ones are free money
                    // and always attractive; guarded ones the arrival
                    // check gates on strength.
                    bool guarded = (r.guardId != 0 && !r.guardBeaten);
                    float val = guarded ? 100.f : 120.f;
                    if (r.type == denialRes)    val = std::max(val, 130.f);
                    if (r.type == enemyKeyRes)  val = std::max(val, 110.f);
                    // Mine type blocking our own build queue wins
                    if (r.type == myNeededRes)  val = std::max(val, 180.f);
                    // Tech scouting #3: this mine's owner is one purchase
                    // away from a big building — cut their gold line NOW
                    // to force emergency spending (see aiTurnSetup).
                    if (r.ownedBy != 0 && r.type == ResourceType::Gold
                        && hoardingOwners.count(r.ownedBy))
                        val = std::max(val, 200.f);
                    addMine(r.pos, val);
                }
            }
            // World objects
            for (const auto& obj : m_worldObjects) {
                if (obj.collected) continue;
                float val = 0.f;
                // ── Free army & growth — highest map priority ─────
                if (obj.type == WorldObjectType::UnitDwelling)
                    val = 160.f;   // weekly recruitable units — huge
                // ── Naval objects — only worth anything afloat ──
                // Without scoring these the AI only ever bumped into
                // them by luck (25 salvage points scattered over
                // 8000+ sea tiles = never), so the sea stayed empty
                // of purpose. A hero already on a boat now detours
                // for salvage and beacons on its way across.
                else if (obj.type == WorldObjectType::Flotsam)
                    val = eHero.onBoat ? 170.f : 0.f;
                else if (obj.type == WorldObjectType::Shipwreck)
                    val = eHero.onBoat ? 220.f : 0.f;
                else if (obj.type == WorldObjectType::SeaMonsterLair)
                    val = eHero.onBoat ? 200.f : 0.f;
                else if (obj.type == WorldObjectType::Lighthouse)
                    // Permanent fleet-wide speed/vision — worth a
                    // real detour, and worth taking off a rival.
                    val = (eHero.onBoat
                           && obj.faction != static_cast<int>(eHero.ownerId))
                          ? 320.f : 0.f;
                else if (obj.type == WorldObjectType::NeutralOutpost) {
                    // Guarded, but capture gives permanent weekly
                    // production — very worth it once beatable.
                    int siteStr = std::min(1400, 250 + m_turns.week() * 50);
                    val = (eiStr >= siteStr * 14 / 10) ? 150.f : 0.f;
                }
                // ── Free resources / gold pickups ─────────────────
                else if (obj.type == WorldObjectType::ResourceCache ||
                         obj.type == WorldObjectType::Campfire      ||
                         obj.type == WorldObjectType::LavaCrystal)
                    val = 90.f;
                // ── Chests & artifacts ───────────────────────────
                else if (obj.type == WorldObjectType::ArtifactChest)  val = 100.f;
                else if (obj.type == WorldObjectType::TreasureChest)  val = 85.f;
                // ── Permanent hero upgrades ──────────────────────
                else if (obj.type == WorldObjectType::Stables        ||
                         obj.type == WorldObjectType::TreeOfKnowledge ||
                         obj.type == WorldObjectType::WitchHut        ||
                         obj.type == WorldObjectType::Landmark)       val = 70.f;
                // ── Shrines / scrolls ────────────────────────────
                else if (obj.type == WorldObjectType::XPShrine   ||
                         obj.type == WorldObjectType::ForestShrine||
                         obj.type == WorldObjectType::StatShrine  ||
                         obj.type == WorldObjectType::SpellScroll ||
                         obj.type == WorldObjectType::SwampAltar  ||
                         obj.type == WorldObjectType::Observatory)  val = 50.f;
                // ── Guarded high-value sites ─────────────────────
                else if (obj.type == WorldObjectType::Crypt      ||
                         obj.type == WorldObjectType::Utopia     ||
                         obj.type == WorldObjectType::PandoraBox ||
                         obj.type == WorldObjectType::BanditCamp) {
                    int siteStr = std::min(2200, 350 + m_turns.week() * 80);
                    if (obj.type == WorldObjectType::Utopia)     siteStr *= 2;
                    if (obj.type == WorldObjectType::PandoraBox) siteStr = siteStr * 3 / 2;
                    if (eiStr >= siteStr * 14 / 10) val = 110.f;
                }
                // Explorer persona loves map content — bump non-combat grabs.
                if (persona == AiPersonality::Explorer && val > 0.f && val < 120.f)
                    val *= 1.4f;
                if (val > 0.f) add(obj.pos, val);
            }
            // Nearest human hero. The old score (300/dist) meant a hero
            // 30 hexes away NEVER outbid a mine 2 hexes away, at any
            // strength — both sides orbited trinkets forever and the game
            // stalemated. Hunt value now grows with the week and decays
            // with sqrt(dist), so late-game confrontation always wins out.
            if (!softRetreat && !combatTriggered) {
                int dist = HexGrid::distance(eHero.pos, targetPos);
                if (dominant || dist <= 8 || isRaider) {
                    float huntVal = 300.f + m_turns.week() * 10.f;
                    cands.push_back({targetPos,
                        huntVal * ghostMult / std::sqrt((float)std::max(1, dist))});
                }
            }
        }

        if (cands.empty()) break;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b){ return a.score > b.score; });
        g_candNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - tCandStart).count();
        ++g_candBuilds;

        // ── AI target-lock ──────────────────────────────────────────
        // If this hero already committed to marching on an enemy town,
        // keep heading there rather than re-evaluating every turn (the
        // oscillation bug: a strong hero "eyeing" 6 towns at 200+ hexes
        // and committing to none). Drop the lock when the town is no
        // longer an enemy town, on arrival, or after a timeout.
        auto townEnemyAt = [&](HexCoord p)->bool{
            for (const auto& t : m_towns)
                if (t.pos == p && t.ownerId != 0 && !isAllied(t.ownerId, eHero.ownerId))
                    return true;
            return false;
        };
        if (eHero.hasMarchGoal &&
            (!townEnemyAt(eHero.marchGoal) || eHero.marchGoalTurns > 60)) {
            eHero.hasMarchGoal = false; eHero.marchGoalTurns = 0;
            eHero.marchPath.clear(); eHero.marchPathIdx = 0;
        }
        // Commit to a new goal if the top candidate is an enemy town.
        if (!eHero.hasMarchGoal && townEnemyAt(cands[0].pos)) {
            eHero.hasMarchGoal = true;
            eHero.marchGoal = cands[0].pos;
            eHero.marchGoalTurns = 0;
            for (const auto& t : m_towns)
                if (t.pos == cands[0].pos) {
                    // Tag commits where tech scouting saw an open window,
                    // so seeded verification runs can grep the behavior.
                    const char* scout = (!t.hasBuilding(BID::FORT))
                                      ? " [SCOUT: pre-wall window]" : "";
                    gLog("P%u %s committing to march on %s (P%u) dist %d%s\n",
                         eHero.ownerId, eHero.name.c_str(), t.name.c_str(),
                         t.ownerId, HexGrid::distance(eHero.pos, t.pos), scout);
                    break;
                }
        }
        // While locked, force the committed town to the front so the
        // hero keeps marching there instead of chasing a nearer mine.
        if (eHero.hasMarchGoal) {
            if (!marchTurnCounted) {
                eHero.marchGoalTurns++;
                marchTurnCounted = true;
            }
            bool present = false;
            for (auto& c : cands) if (c.pos == eHero.marchGoal) { c.score = 1e9f; present = true; break; }
            if (!present) cands.insert(cands.begin(), {eHero.marchGoal, 1e9f});
            std::sort(cands.begin(), cands.end(),
                      [](const Cand& a, const Cand& b){ return a.score > b.score; });
        }

        auto costFn = [this, &eHero, aggressive, combatTriggered,
                       &playerHero](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            if (!t || !eHero.canEnter(t->terrain) || t->blocked) return 999;
            // One combat per day: block the player's tile once a fight is queued
            if (combatTriggered && c == playerHero.pos) return 999;
            // Only block passage through RIVAL player towns. An allied town is
            // friendly ground and must stay enterable — an AI allied with the
            // watched player (Watch AI puts slots 0 and 1 on one team) has its
            // ally's town as its only Shipyard, and blocking it here made that
            // dock an impassable goal. That is the actual reason AI heroes
            // never sailed: not scoring, not connectivity, not search budget —
            // they were forbidden from standing on their own ally's dock.
            if (!aggressive && t->townId != 0) {
                for (const auto& town : m_towns)
                    if (town.id == t->townId && town.ownerId > 0
                        && town.ownerId <= static_cast<uint32_t>(m_numHumanPlayers)
                        && !isAllied(town.ownerId, eHero.ownerId)) return 999;
            }
            int base = eHero.moveCost(t->terrain);
            if (m_roadHexes.count(c)) base = std::max(1, base / 2);
            return base;
        };
        // Try candidates in score order until one is actually
        // reachable — an unreachable top target (e.g. across
        // water) used to freeze the hero for the whole game.
        // Horizon cap (see watched-hero note above) keeps XL-map
        // multi-hero turns from exploring the whole grid per hero.
        constexpr int kAiPathHorizon = 60;
        std::vector<HexCoord> path;
        // First: is the SINGLE best target reachable by land? If not,
        // and it's high-value (enemy town / strong hunt), prefer
        // buying a boat to reach it over settling for lesser land
        // grabs — otherwise ships never get used on maps that always
        // have some nearby land mine.
        // Track whether the single best target (cands[0]) was
        // reachable. We get this for free from the loop below — no
        // extra pathfind (the old extra A* per hero per turn was a
        // major CPU cost on XL maps).
        bool bestReachable = false;
        // Path to the best reachable candidate. Use a generous
        // horizon ONLY when the top target is an enemy town (worth a
        // long cross-map march — the 60-hex cap made distant enemy
        // capitals permanently unreachable, why towns were never
        // captured even 1v1). Everything else keeps the cheap horizon
        // so per-hero pathfinding cost stays low.
        bool topIsEnemyTown = false;
        if (!cands.empty()) {
            for (const auto& t : m_towns)
                if (t.pos == cands[0].pos && t.ownerId != 0
                    && !isAllied(t.ownerId, eHero.ownerId)) { topIsEnemyTown = true; break; }
        }
        // Reuse a cached march path when locked onto a town goal so we
        // don't rerun the expensive 400-hex A* every turn (that was the
        // main-thread freeze). Recompute only if the cache is empty or
        // the hero has strayed off it.
        path.clear();
        auto tPathStart = std::chrono::steady_clock::now();
        if (eHero.hasMarchGoal && topIsEnemyTown
            && cands[0].pos == eHero.marchGoal
            && !eHero.marchPath.empty()
            && eHero.marchPathIdx < eHero.marchPath.size()) {
            // Follow the cached path from the current position.
            path.assign(eHero.marchPath.begin() + eHero.marchPathIdx,
                        eHero.marchPath.end());
            // The cached path IS a route to cands[0] (the locked
            // goal), so the top target is reachable. Leaving this
            // false made wantBoatForBestTarget fire on every step
            // (the lock's forced 1e9 score passes any threshold):
            // aiTryBoat swapped in a dock path, the hero stepped
            // off the march path, the divergence check wiped the
            // cache — and the 400-hex search re-ran EVERY turn for
            // EVERY marching hero while they yo-yoed toward docks.
            bestReachable = true;
        } else {
            // THREADING.md Phase 2 — parallel candidate A* fan-out.
            //
            // The effective top candidate is still tried SERIALLY
            // first. A plain "fan all 10 out" would be a pessimism:
            // the serial try short-circuits on a hit, so when the
            // leader is reachable (the common case) it costs exactly
            // one A*, while a blind fan-out costs ten. Only the
            // failure path is overlapped.
            //
            // Taking the LOWEST reachable index keeps the winner
            // identical to a serial scan: Pathfinder::find is pure,
            // costFn only reads m_map/m_towns/m_roadHexes, and
            // nothing mutates during the fan-out — so completion
            // order cannot change which candidate wins.
            int chosenIdx = -1;
            int nCand = static_cast<int>(std::min<size_t>(cands.size(), 10));
            // Boarding or disembarking changes what is reachable, so
            // unreachability proofs gathered in the other movement
            // mode no longer hold — the per-turn memo AND the
            // cross-turn march-fail stamp both expire.
            if (eHero.onBoat != memoOnBoat) {
                failedTargets.clear();
                eHero.marchFailTurn = -1000000;
                memoOnBoat = eHero.onBoat;
            }
            // Linear turn counter for the cross-turn retry cadence
            // (the week*100+day stamp used elsewhere isn't linear).
            const int linTurn = m_turns.week() * 7 + m_turns.day();
            // Retry cadence for a march goal already PROVEN unreachable.
            // Was 4, which a week-39 log showed costing 3-5 failed 400-hex
            // searches EVERY turn at ~128 ms each — 400-650 ms of the worst
            // turns spent re-proving the same dead ends.
            // 16 is safe rather than merely cheaper: the one event that really
            // changes reachability (boarding/leaving a boat) clears the memo
            // explicitly above, regardless of this cadence. This only throttles
            // re-proving a dead end that nothing has changed about.
            constexpr int kMarchRetryTurns = 16;
            // Generous horizon ONLY for a top-candidate enemy town
            // (worth a cross-map march); everything else stays cheap.
            auto horizonFor = [&](int ci) {
                return (ci == 0 && topIsEnemyTown) ? 400 : kAiPathHorizon;
            };
            // Known unreachable = another component (O(1) flood-fill
            // lookup; the amphibious map covers onBoat heroes) OR a
            // search this turn already failed at >= the horizon we
            // would use now. Both are read-only queries, safe from
            // the fan-out workers below.
            auto knownUnreachable = [&](int ci) {
                if (routeImpossible(eHero.onBoat, eHero.pos,
                                    cands[static_cast<size_t>(ci)].pos))
                    return true;
                // Cross-turn throttle: this march goal's 400-hex
                // search failed recently; don't re-prove it yet.
                if (ci == 0 && eHero.hasMarchGoal
                    && cands[0].pos == eHero.marchGoal
                    && eHero.marchFailGoal == eHero.marchGoal
                    && linTurn - eHero.marchFailTurn < kMarchRetryTurns)
                    return true;
                auto it = failedTargets.find(cands[static_cast<size_t>(ci)].pos);
                return it != failedTargets.end() && it->second >= horizonFor(ci);
            };
            // Effective top candidate: the first not already known
            // unreachable. Once a fallback is chosen, later steps of
            // the same turn land here again instead of re-proving
            // the doomed leader with a fresh A* per step (that
            // re-proving was the residual ~1.2 s spike).
            int effTop = -1;
            for (int ci = 0; ci < nCand; ++ci)
                if (!knownUnreachable(ci)) { effTop = ci; break; }
            // Reuse this turn's path when we're still heading to the
            // effective top target and haven't strayed off it. This
            // is the 99%: without it a full A* ran on EVERY move
            // step to the very same destination, ~85-100 times/turn.
            if (effTop >= 0 && eHero.hasStepPath
                && eHero.stepPathGoal == cands[static_cast<size_t>(effTop)].pos
                && eHero.stepPathIdx < eHero.stepPath.size()
                // The remaining path must actually continue from where
                // the hero IS. Position can change without a move —
                // a boat launch or Town Portal teleports it — and
                // blindly resuming would teleport it along a stale route.
                && HexGrid::distance(eHero.pos,
                       eHero.stepPath[eHero.stepPathIdx]) == 1) {
                path.assign(eHero.stepPath.begin() + eHero.stepPathIdx,
                            eHero.stepPath.end());
                // bestReachable means "cands[0] itself" — a reused
                // path to a fallback must not suppress the boat
                // consideration for the real top target.
                bestReachable = (effTop == 0);
                ++g_reuseHits;
            }
            if (!path.empty()) {
                // cached — nothing to search
            } else if (effTop >= 0) {
                int h0 = horizonFor(effTop);
                auto tA = std::chrono::steady_clock::now();
                path = Pathfinder::find(m_map, eHero.pos,
                                        cands[static_cast<size_t>(effTop)].pos,
                                        costFn, h0);
                {
                    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - tA).count();
                    if (h0 > kAiPathHorizon) {
                        g_longNs += ns; ++g_longCalls;
                        if (path.empty()) ++g_longFails;
                    }
                    else                     { g_shortNs += ns; ++g_shortCalls; }
                }
                if (!path.empty()) {
                    bestReachable = (effTop == 0);
                    chosenIdx     = effTop;
                    if (effTop == 0 && topIsEnemyTown) {
                        eHero.marchPath    = path;
                        eHero.marchPathIdx = 0;
                    }
                } else {
                    // Memoise BEFORE the fan-out so the workers'
                    // read-only view of failedTargets is settled.
                    int& fh = failedTargets[cands[static_cast<size_t>(effTop)].pos];
                    fh = std::max(fh, h0);
                    // A failed MARCH search also stamps the hero so
                    // the retry happens on a cadence, not per turn.
                    if (effTop == 0 && eHero.hasMarchGoal
                        && cands[0].pos == eHero.marchGoal
                        && h0 > kAiPathHorizon) {
                        eHero.marchFailGoal = eHero.marchGoal;
                        eHero.marchFailTurn = linTurn;
                    }
                    if (effTop + 1 < nCand) {
                        // Leader unreachable: search the rest at once.
                        std::vector<std::vector<HexCoord>> results(
                            static_cast<size_t>(nCand));
                        // Which candidates actually ran a search —
                        // distinguishes "failed" (worth memoising)
                        // from "skipped". Distinct elements per
                        // worker, so no data race.
                        std::vector<char> searched(
                            static_cast<size_t>(nCand), 0);
                        WorkerPool::instance().parallelFor(
                            nCand - 1 - effTop, [&](int k) {
                            int ci = effTop + 1 + k;
                            if (knownUnreachable(ci)) return;
                            searched[static_cast<size_t>(ci)] = 1;
                            results[static_cast<size_t>(ci)] =
                                Pathfinder::find(m_map, eHero.pos,
                                                 cands[static_cast<size_t>(ci)].pos,
                                                 costFn, kAiPathHorizon);
                        });
                        for (int ci = effTop + 1; ci < nCand; ++ci) {
                            if (!results[static_cast<size_t>(ci)].empty()) {
                                if (chosenIdx < 0) {
                                    path = std::move(
                                        results[static_cast<size_t>(ci)]);
                                    chosenIdx = ci;
                                }
                            } else if (searched[static_cast<size_t>(ci)]) {
                                // Failed search → memoise so later
                                // steps this turn skip it in O(1).
                                int& f = failedTargets[
                                    cands[static_cast<size_t>(ci)].pos];
                                f = std::max(f, kAiPathHorizon);
                            }
                        }
                    }
                }
            }
            // Remember the freshly computed path so the remaining
            // steps of this turn can walk it instead of re-running A*.
            if (chosenIdx >= 0 && !path.empty()) {
                eHero.stepPath     = path;
                eHero.stepPathIdx  = 0;
                eHero.stepPathGoal = cands[static_cast<size_t>(chosenIdx)].pos;
                eHero.hasStepPath  = true;
            }
        }
        g_pathNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - tPathStart).count();
        // An enemy town ALWAYS justifies passage, whatever its score.
        // Scores are distance-diluted (add() divides by hex distance), so an
        // overseas capital 50 hexes away scores ~600/sqrt(50) = 85 — under the
        // flat 150 gate, which silently made naval conquest impossible beyond
        // ~16 hexes. Measured 2026-07-25 on a Ring map: all six towns built
        // Shipyards, heroes wanted passage all game, and not one boat was ever
        // bought — every AI farmed its own island to the week-80 backstop with
        // 5 of 6 players still alive. The score gate still guards non-town
        // targets so heroes don't buy boats to chase a distant mine.
        bool wantBoatForBestTarget =
            (!cands.empty() && !eHero.onBoat && !bestReachable
             && (topIsEnemyTown || cands[0].score >= 150.f));
        // Everything unreachable by land, OR the best target needs a
        // boat: buy passage across the water.
        if (path.empty() || wantBoatForBestTarget) {
            std::vector<HexCoord> docks;
            for (const auto& t : m_towns)
                // Own docks only — a rival AI player's shipyard
                // isn't this hero's to use.
                // Own OR allied shipyard towns. Restricting to the
                // hero's own owner left most bots with docks=0
                // (measured: heroes wanted a boat 1065 times in one
                // game and could not name a single dock), so naval
                // was dead on arrival for anyone who hadn't
                // personally built a coastal town.
                if (isAllied(t.ownerId, eHero.ownerId)
                    && t.hasBuilding(BID::TOWN_SHIPYARD))
                    docks.push_back(t.pos);
            std::vector<HexCoord> boatPath;
            bool wasOnBoat = eHero.onBoat;
            // The target that motivated buying passage in the first
            // place. Must be latched BEFORE boarding, because the
            // moment the hero is floating offshore a fresh scoring
            // pass sees coastal land junk (a mine two hexes away)
            // and happily sends it back to the beach.
            HexCoord seaTarget{};
            bool haveSeaTarget = false;
            if (!cands.empty()) { seaTarget = cands[0].pos; haveSeaTarget = true; }
            auto sameLandmass = [this, &eHero](HexCoord c) {
                return !routeImpossible(eHero.onBoat, eHero.pos, c);
            };
            bool gotBoatPath = aiTryBoat(m_map, m_worldObjects, docks, eHero,
                                         aiResources(eHero.ownerId), costFn,
                                         sameLandmass, boatPath);
            // Surface WHY passage failed — once per hero per week, so a stalled
            // naval game says so in the log instead of looking like the AI
            // simply chose not to sail.
            if (!gotBoatPath && !eHero.onBoat && wantBoatForBestTarget
                && g_lastBoatFail != BoatFail::None
                && eHero.boatFailWeek != m_turns.week()) {
                eHero.boatFailWeek = m_turns.week();
                gLog("[NAVAL] P%u %s at (%d,%d) wants passage: %s"
                     " [towndocks=%zu walkable=%zu triedDock=(%d,%d) dist=%d"
                     " pathfinder=%s]\n",
                     eHero.ownerId, eHero.name.c_str(), eHero.pos.q, eHero.pos.r,
                     boatFailName(g_lastBoatFail),
                     docks.size(), g_lastBoatWalkableDocks,
                     g_lastBoatDockPos.q, g_lastBoatDockPos.r,
                     g_lastBoatDockDist,
                     g_lastBoatPathExit[0] ? g_lastBoatPathExit : "n/a");
            }
            if (gotBoatPath) {
                // Success side: is the hero actually CLOSING on the dock, or
                // re-deciding every turn and never arriving? Logging only
                // failures could not tell those apart. Distance should shrink
                // week over week; a flat number means oscillation.
                if (eHero.boatFailWeek != m_turns.week()) {
                    eHero.boatFailWeek = m_turns.week();
                    gLog("[NAVAL] P%u %s walking to dock: %zu steps queued,"
                         " dock %d hexes away\n",
                         eHero.ownerId, eHero.name.c_str(), boatPath.size(),
                         boatPath.empty()
                             ? -1
                             : HexGrid::distance(eHero.pos, boatPath.back()));
                }
                path = boatPath;   // head to the dock
            } else if (!wasOnBoat && eHero.onBoat) {
                // Just BOARDED at the dock this call. aiTryBoat returns
                // false here expecting a re-score, but the stale land
                // `path` (a fallback target) would walk the hero back
                // onto land and instantly disembark — wasting the boat
                // and re-buying every turn (the "28 boats bought, never
                // seen crossing water" bug). Drop the land path and
                // re-score now that water is traversable so the hero
                // actually sails toward the island target.
                eHero.marchPath.clear(); eHero.marchPathIdx = 0;
                // Commit to the crossing: lock the overseas target as
                // the march goal so the re-score forces it to the
                // front of the candidates. Without this the hero
                // launched, immediately re-picked a nearby land
                // target, stepped ashore and disembarked — 6 boats
                // launched and 0 water tiles crossed in a real game.
                if (haveSeaTarget) {
                    eHero.marchGoal      = seaTarget;
                    eHero.hasMarchGoal   = true;
                    eHero.marchGoalTurns = 0;
                }
                continue;
            } else if (path.empty()) {
                // No boat and nothing from the scored list — fall through to
                // the anti-idle sweep below instead of freezing for the game.
            }
        }

        // ── ANTI-IDLE FALLBACK ───────────────────────────────────────────
        // The scored candidate list is capped at the top ~10, and every one
        // of them can prove unreachable (across water, walled off, or beyond
        // the cheap horizon) while perfectly reachable free mines sit just
        // past that cutoff. Rather than break and idle for weeks, sweep EVERY
        // unclaimed mine / free pickup by nearest distance and walk to the
        // first one actually reachable. An all-seeing bot must never sit on
        // unspent movement while free income is grabbable.
        if (path.empty()) {
            struct FreeGoal { HexCoord pos; int d; };
            std::vector<FreeGoal> freebies;
            for (const auto& r : m_resources) {
                if (isAllied(r.ownedBy, eHero.ownerId)) continue;      // own/ally already
                if (r.guardId != 0 && !r.guardBeaten)  continue;       // still guarded → main logic gates it
                freebies.push_back({r.pos, HexGrid::distance(eHero.pos, r.pos)});
            }
            for (const auto& obj : m_worldObjects) {
                if (obj.collected) continue;
                // Grab-and-go pickups only — no guarded/naval/combat sites here.
                if (obj.type == WorldObjectType::ResourceCache ||
                    obj.type == WorldObjectType::Campfire      ||
                    obj.type == WorldObjectType::LavaCrystal   ||
                    obj.type == WorldObjectType::TreasureChest ||
                    obj.type == WorldObjectType::ArtifactChest ||
                    obj.type == WorldObjectType::UnitDwelling)
                    freebies.push_back({obj.pos, HexGrid::distance(eHero.pos, obj.pos)});
            }
            std::sort(freebies.begin(), freebies.end(),
                      [](const FreeGoal& a, const FreeGoal& b){ return a.d < b.d; });
            int tried = 0;
            for (const auto& g : freebies) {
                if (g.pos == eHero.pos) continue;
                if (routeImpossible(eHero.onBoat, eHero.pos, g.pos)) continue;  // O(1) other-component reject
                if (failedTargets.count(g.pos)) continue;                       // already proven dead this turn
                if (++tried > 24) break;                                        // bound the extra A* work
                std::vector<HexCoord> fp =
                    Pathfinder::find(m_map, eHero.pos, g.pos, costFn, kAiPathHorizon);
                if (!fp.empty()) {
                    path = std::move(fp);
                    eHero.stepPath     = path;
                    eHero.stepPathIdx  = 0;
                    eHero.stepPathGoal = g.pos;
                    eHero.hasStepPath  = true;
                    break;
                }
                failedTargets[g.pos] = kAiPathHorizon;   // remember the miss for later steps
            }
        }

        if (path.empty()) {
            // Genuinely nothing reachable — truly boxed in (islanded with no
            // dock, or every free objective walled off). Log it so idle heroes
            // are visible in the session log instead of silently doing nothing.
            gLog("  [IDLE] P%u %s stuck at (%d,%d) wk%d d%d — no reachable objective, %d move left\n",
                 eHero.ownerId, eHero.name.c_str(), eHero.pos.q, eHero.pos.r,
                 m_turns.week(), m_turns.day(), eHero.movePool);
            break;
        }

        HexCoord next = path[0];
        const HexTile* nextTile = m_map.getTile(next);
        if (!nextTile) break;
        int cost = eHero.moveCost(nextTile->terrain);
        if (eHero.movePool < cost) break;

        // Move
        if (HexTile* old = m_map.getTile(eHero.pos)) old->heroId = 0;
        eHero.pos = next;
        eHero.movePool -= cost;
        if (HexTile* nT = m_map.getTile(eHero.pos)) nT->heroId = eHero.id;
        if (eHero.onBoat && nextTile->terrain == Terrain::Water)
            gLog("  [SAIL] %s crossing water at (%d,%d) wk%d\n",
                 eHero.name.c_str(), next.q, next.r, m_turns.week());
        if (eHero.onBoat && nextTile->terrain != Terrain::Water)
            eHero.onBoat = false;  // disembark
        // If following the cached march path and we stepped onto its
        // next tile, advance the index; if we diverged, invalidate it
        // so it's recomputed next turn.
        if (eHero.hasMarchGoal && !eHero.marchPath.empty()
            && eHero.marchPathIdx < eHero.marchPath.size()) {
            if (eHero.marchPath[eHero.marchPathIdx] == eHero.pos)
                eHero.marchPathIdx++;
            else { eHero.marchPath.clear(); eHero.marchPathIdx = 0; }
        }
        // Same bookkeeping for the general per-turn path cache: walk
        // it forward when we landed on its next tile, drop it the
        // moment we diverge (boat launch, combat shove, blocked tile)
        // so the next step re-plans honestly instead of following a
        // path that no longer starts where the hero is.
        if (eHero.hasStepPath && eHero.stepPathIdx < eHero.stepPath.size()) {
            if (eHero.stepPath[eHero.stepPathIdx] == eHero.pos) {
                eHero.stepPathIdx++;
                if (eHero.stepPathIdx >= eHero.stepPath.size())
                    eHero.hasStepPath = false;   // arrived
            } else {
                eHero.stepPath.clear();
                eHero.stepPathIdx = 0;
                eHero.hasStepPath = false;
            }
        }

        // Combat with a RIVAL AI hero? Real AI-vs-AI hostility:
        // resolve it off-screen (no human is present to fight it
        // out on the battlefield) instead of silently letting two
        // bots' heroes phase through each other on the same tile.
        {
            bool fought = false;
            for (int oj = 0; oj < static_cast<int>(m_enemyHeroes.size()); ++oj) {
                if (oj == ehi) continue;
                Hero& other = m_enemyHeroes[oj];
                if (other.eliminated || isAllied(other.ownerId, eHero.ownerId)) continue;
                if (other.pos != eHero.pos) continue;

                int otherStr = heroStrength(other, unitDefs);
                bool eWins = eiStr >= otherStr; // ties favour the mover
                // Naval engagement: a War hull rams and sinks any
                // lesser boat regardless of the armies aboard —
                // that is the entire point of paying for one.
                if (eHero.onBoat && other.onBoat) {
                    bool eWar = (eHero.boatType == BoatType::War);
                    bool oWar = (other.boatType == BoatType::War);
                    if (eWar != oWar) {
                        eWins = eWar;
                        gLog("%s (war boat) rammed and sank %s at sea (week %d)\n",
                             eWar ? eHero.name.c_str() : other.name.c_str(),
                             eWar ? other.name.c_str() : eHero.name.c_str(),
                             m_turns.week());
                    }
                }
                Hero& winner = eWins ? eHero : other;
                Hero& loser  = eWins ? other  : eHero;
                int winnerStr = eWins ? eiStr : otherStr;
                int loserStr  = eWins ? otherStr : eiStr;

                if (!winner.army.empty()) {
                    int bigIdx = 0;
                    for (int i = 1; i < (int)winner.army.size(); ++i)
                        if (winner.army[i].count > winner.army[bigIdx].count) bigIdx = i;
                    // Close fights cost the winner more than a rout.
                    int lossPct = (winnerStr > 0 && loserStr * 10 >= winnerStr * 7) ? 20 : 8;
                    winner.army[bigIdx].count = std::max(1, winner.army[bigIdx].count
                                                - winner.army[bigIdx].count * lossPct / 100);
                }
                loser.army.clear();
                loser.eliminated = true;
                if (HexTile* ct = m_map.getTile(eHero.pos)) ct->heroId = winner.id;
                // Killing a rival hero awarded nothing either.
                aiHeroAwardXp(winner, 200 + loserStr / 8);
                gLog("%s defeated rival %s in the field (week %d)\n",
                     winner.name.c_str(), loser.name.c_str(), m_turns.week());
                fought = true;
                break;
            }
            if (fought && eHero.eliminated) { eHero.movePool = 0; break; }
        }

        // Combat with player? (only the first collision per day fights)
        if (eHero.pos == playerHero.pos && !combatTriggered
            && !isAllied(static_cast<uint32_t>(currentPlayerId()), eHero.ownerId)) {
            m_lastCombatEnemyId = eHero.id;
            auto pUnits = makeHeroUnits(playerHero, unitDefs, true);
            auto eUnits = makeHeroUnits(eHero, unitDefs, false);
            if (m_watchingAI) {
                m_fromBattleSim = true;
                m_simAutoPlay   = true;
                m_simAutoPlayTimer = 0.f;
            }
            enterCombat(playerHero, pUnits, eHero, eUnits);
            combatTriggered = true;
            break;
        }

        // Collect world objects — apply meaningful effects to enemy hero
        for (auto& obj : m_worldObjects) {
            // ── Naval objects (AI) ───────────────────────────────
            // Lighthouses are capturable repeatedly, so they are
            // checked before the generic `collected` guard.
            if (obj.type == WorldObjectType::Lighthouse
                && obj.pos == eHero.pos && eHero.onBoat) {
                if (obj.faction != static_cast<int>(eHero.ownerId)) {
                    obj.faction   = static_cast<int>(eHero.ownerId);
                    obj.collected = true;
                    refreshLighthouseBoosts();
                    gLog("P%u %s captured a Lighthouse (week %d)\n",
                         eHero.ownerId, eHero.name.c_str(), m_turns.week());
                }
                continue;
            }
            // Flotsam drifts — a passing crew scoops it up without
            // having to land on the exact hex. Needed in practice:
            // a hero with a locked overseas march goal sails a
            // straight line, so requiring an exact tile hit meant
            // salvage was never collected on a 40k-tile ocean.
            if (obj.type == WorldObjectType::Flotsam && !obj.collected
                && eHero.onBoat && obj.pos != eHero.pos
                && HexGrid::distance(obj.pos, eHero.pos) <= 1) {
                obj.collected = true;
                int g = 300 + (obj.value % 700);
                aiResources(eHero.ownerId).add(ResourceType::Gold, g);
                gLog("P%u %s salvaged drifting flotsam (+%dg, week %d)\n",
                     eHero.ownerId, eHero.name.c_str(), g, m_turns.week());
                continue;
            }
            if (obj.collected || obj.pos != eHero.pos) continue;
            if (obj.type == WorldObjectType::Flotsam
                || obj.type == WorldObjectType::Shipwreck
                || obj.type == WorldObjectType::SeaMonsterLair) {
                if (!eHero.onBoat) continue;   // can't salvage on foot
                bool guarded = (obj.type != WorldObjectType::Flotsam);
                if (guarded) {
                    // Week-scaled sea guardians; only engage if strong.
                    int siteStr = std::min(2400, 400 + m_turns.week() * 90);
                    if (obj.type == WorldObjectType::SeaMonsterLair)
                        siteStr = siteStr * 3 / 2;
                    if (eiStr < siteStr * 14 / 10) continue;
                    if (!eHero.army.empty()) {
                        int bigIdx = 0;
                        for (int i = 1; i < (int)eHero.army.size(); ++i)
                            if (eHero.army[i].count > eHero.army[bigIdx].count) bigIdx = i;
                        eHero.army[bigIdx].count =
                            std::max(1, eHero.army[bigIdx].count -
                                     eHero.army[bigIdx].count * 12 / 100);
                    }
                    aiHeroAwardXp(eHero, siteStr / 3);
                }
                obj.collected = true;
                int gold = guarded ? 800 + (obj.value % 900)
                                   : 300 + (obj.value % 700);
                aiResources(eHero.ownerId).add(ResourceType::Gold, gold);
                gLog("P%u %s salvaged %s at sea (+%dg, week %d)\n",
                     eHero.ownerId, eHero.name.c_str(),
                     obj.type == WorldObjectType::Flotsam ? "flotsam"
                     : obj.type == WorldObjectType::Shipwreck ? "a shipwreck"
                     : "a monster lair",
                     gold, m_turns.week());
                continue;
            }
            // Guarded sites resolve as an off-screen fight, and only when
            // strong enough — walking over one no longer silently deletes
            // it (previous behavior collected EVERYTHING unconditionally).
            if (obj.type == WorldObjectType::Crypt      ||
                obj.type == WorldObjectType::Utopia     ||
                obj.type == WorldObjectType::PandoraBox ||
                obj.type == WorldObjectType::BanditCamp) {
                int siteStr = std::min(2200, 350 + m_turns.week() * 80);
                bool utopia  = (obj.type == WorldObjectType::Utopia);
                bool pandora = (obj.type == WorldObjectType::PandoraBox);
                if (utopia)  siteStr *= 2;
                if (pandora) siteStr = siteStr * 3 / 2;
                if (eiStr < siteStr * 14 / 10) continue;  // too risky — leave it
                obj.collected = true;
                // Casualties: ~15% off the largest stack (harder than a mine guard)
                if (!eHero.army.empty()) {
                    int bigIdx = 0;
                    for (int i = 1; i < (int)eHero.army.size(); ++i)
                        if (eHero.army[i].count > eHero.army[bigIdx].count) bigIdx = i;
                    eHero.army[bigIdx].count =
                        std::max(1, eHero.army[bigIdx].count -
                                 eHero.army[bigIdx].count * 15 / 100);
                }
                aiHeroAwardXp(eHero, siteStr / 4);
                if (utopia && obj.value > 0)
                    aiEquipOrStashArtifact(eHero, obj.value);
                // (naval sites are handled in their own branch below)
                // Just cleared a Utopia and standing on it — this is
                // the only moment the Found City precondition holds,
                // since the hero walks on before its next turn.
                // (tryFoundCity erases the object, so bail out of
                // this loop rather than keep iterating it.)
                if (utopia && tryFoundCity()) break;
                if (pandora) {
                    // Same reward table as the player, seed-rolled
                    uint32_t seed = static_cast<uint32_t>(obj.value);
                    if (seed % 4 == 1) {
                        const auto& arts = m_artifactRegistry.artifacts();
                        if (!arts.empty())
                            aiEquipOrStashArtifact(eHero, arts[seed % arts.size()].id);
                    } else if (seed % 4 == 2) {
                        eHero.attack += 2; eHero.defense += 2;
                    }
                    // gold/resource rolls are meaningless to the AI (infinite
                    // build budget) — XP above covers the value instead
                }
                gLog("Enemy %s cleared %s (week %d)\n", eHero.name.c_str(),
                     utopia  ? "Utopia" :
                     pandora ? "Pandora's Box" :
                     (obj.type == WorldObjectType::Crypt ? "Crypt" : "Bandit Camp"),
                     m_turns.week());
                continue;
            }
            // Persistent sites are used, not consumed
            if (isPersistentSite(obj.type)) {
                if (obj.type == WorldObjectType::UnitDwelling) {
                    // Capture: this AI PLAYER (not "the AI team")
                    // gains +1 weekly growth of this tier in its
                    // matching towns, and buys out the available
                    // pool from its own pocket.
                    obj.linkedId = eHero.ownerId;
                    dwellingPaidRecruit(obj, eHero.army,
                                        aiResources(eHero.ownerId), unitDefs);
                }
                continue;
            }
            obj.collected = true;
            if (obj.type == WorldObjectType::XPShrine) {
                aiHeroAwardXp(eHero, obj.value);
                gLog("Enemy %s gained %d XP from shrine\n",
                       eHero.name.c_str(), obj.value);
            } else if (obj.type == WorldObjectType::SpellScroll) {
                bool already = false;
                for (int sid : eHero.knownSpells)
                    if (sid == obj.value) { already = true; break; }
                if (!already) eHero.knownSpells.push_back(obj.value);
            } else if (obj.type == WorldObjectType::StatShrine) {
                // Alternate ATK and DEF based on current stats
                if (eHero.attack <= eHero.defense) eHero.attack++;
                else eHero.defense++;
            } else if (obj.type == WorldObjectType::ArtifactChest) {
                aiEquipOrStashArtifact(eHero, obj.value);
            } else if (obj.type == WorldObjectType::ForestShrine) {
                aiHeroAwardXp(eHero, obj.value);
                gLog("Enemy %s gained %d XP from forest shrine\n",
                       eHero.name.c_str(), obj.value);
            } else if (obj.type == WorldObjectType::SwampAltar) {
                bool already = false;
                for (int sid : eHero.knownSpells)
                    if (sid == obj.value) { already = true; break; }
                if (!already) eHero.knownSpells.push_back(obj.value);
            } else if (obj.type == WorldObjectType::TreasureChest) {
                // AI always takes gold from multi-choice chests
                // (no popup, instant collect)
                gLog("Enemy %s looted chest: +%d gold\n",
                       eHero.name.c_str(), obj.value);
            }
        }

        // Claim resource node (mine control). If a guard is present, the
        // AI now fights it off-screen when comfortably stronger (was
        // guardBeaten-only, so guarded mines were unclaimable by the AI).
        if (nextTile->resourceId != 0) {
            for (auto& r : m_resources) {
                // (was `break` — bailed at the first non-matching
                // node, so the AI claimed 0 mines in every game)
                if (r.id != nextTile->resourceId) continue;
                // An ally's mine — leave ownership alone, don't
                // silently steal it by walking onto it.
                if (r.ownedBy != 0 && r.ownedBy != eHero.ownerId
                    && isAllied(r.ownedBy, eHero.ownerId)) break;
                if (!r.guardBeaten && r.guardId != 0) {
                    // Guard strength scales with the week but is kept
                    // low and capped so a hero with a modest army can
                    // clear nearby mines early — the AI's economy
                    // depends on them. Require a 1.5x margin so it
                    // doesn't gut its army on a marginal fight.
                    int guardStr = std::min(900, 150 + m_turns.week() * 35);
                    if (eiStr >= guardStr * 3 / 2) {
                        r.guardBeaten = true;
                        // Casualties: shave ~7% off the hero's largest stack.
                        int bigIdx = 0;
                        for (int i = 1; i < (int)eHero.army.size(); ++i)
                            if (eHero.army[i].count > eHero.army[bigIdx].count) bigIdx = i;
                        if (!eHero.army.empty())
                            eHero.army[bigIdx].count =
                                std::max(1, eHero.army[bigIdx].count -
                                         eHero.army[bigIdx].count / 14);
                        // Mine guards are the AI's most common fight
                        // (~1000 a game) and awarded ZERO xp, which
                        // is why bot heroes sat at level 2 all game
                        // — gating Town Portal (L5), Found City
                        // (L10) and every level-up stat behind a
                        // bar they could never reach.
                        aiHeroAwardXp(eHero, 40 + m_turns.week() * 8);
                        gLog("Enemy %s beat mine guard (week %d)\n",
                             eHero.name.c_str(), m_turns.week());
                    }
                }
                // Unguarded mines are claimable outright (guardBeaten
                // is only meaningful when a guard exists)
                if (r.guardId == 0 || r.guardBeaten) r.ownedBy = eHero.ownerId;
                break;
            }
        }

        // Capture neutral towns / siege player towns / garrison at own towns
        if (nextTile->townId != 0) {
            for (auto& t : m_towns) {
                if (t.id != nextTile->townId) continue;
                if (t.ownerId == eHero.ownerId && veryWeak && !eHero.army.empty()) {
                    // Retreating hero deposits their smallest stack as garrison
                    int weakIdx = 0;
                    for (int i = 1; i < (int)eHero.army.size(); ++i)
                        if (eHero.army[i].count < eHero.army[weakIdx].count) weakIdx = i;
                    auto& stack = eHero.army[weakIdx];
                    int deposit = stack.count / 2;
                    if (deposit > 0 && t.garrison.size() < 7) {
                        bool merged = false;
                        for (auto& gs : t.garrison)
                            if (gs.defId == stack.defId) { gs.count += deposit; merged = true; break; }
                        if (!merged) t.garrison.push_back({stack.defId, deposit});
                        stack.count -= deposit;
                        if (stack.count == 0)
                            eHero.army.erase(eHero.army.begin() + weakIdx);
                    }
                    eHero.movePool = 0; // done retreating for this turn
                } else if (t.ownerId == eHero.ownerId) {
                    // Stepped into one of its OWN towns — pick up
                    // the garrison and recruit, keep moving.
                    takeGarrison(t, eHero, unitDefs);
                    aiPaidRecruit(t, eHero.army, aiResources(eHero.ownerId), unitDefs);
                } else if (t.ownerId == 0) {
                    t.ownerId = eHero.ownerId;
                    gLog("Enemy %s captured %s\n", eHero.name.c_str(), t.name.c_str());
                } else if (isAllied(t.ownerId, eHero.ownerId)) {
                    // An ally's town (bot or human) is never a
                    // hostile target — no siege, no reaching into
                    // its garrison/economy. Just pass through.
                } else if (isAiOwner(t.ownerId)) {
                    // A RIVAL bot's town — real AI-vs-AI hostility.
                    // No human is present to play this out on the
                    // battlefield, so resolve it off-screen the
                    // same way mine-guard fights are resolved
                    // above: compare strength, a decisive attacker
                    // takes the town, otherwise it bounces off
                    // with losses (mirrors the human-town "empty
                    // garrison" stop-here behaviour below).
                    int garrStr  = stacksStrength(t.garrison, unitDefs);
                    // Fort ladder gives escalating defensive strength
                    // so a fortified town is genuinely hard to take —
                    // defending a Castle is as valid as attacking one.
                    float fortMul = 0.f;
                    if (t.hasBuilding(BID::CASTLE))      fortMul = 1.5f;
                    else if (t.hasBuilding(BID::CITADEL)) fortMul = 1.0f;
                    else if (t.hasBuilding(BID::FORT))    fortMul = 0.6f;
                    if (t.hasBuilding(BID::BASTION))     fortMul += 0.75f;
                    int fortBonus = (int)(garrStr * fortMul);
                    // Walls also give a flat floor so an empty town
                    // still resists a token force.
                    int wallFloor = t.hasBuilding(BID::CASTLE) ? 800
                                  : t.hasBuilding(BID::CITADEL) ? 500
                                  : t.hasBuilding(BID::FORT) ? 300 : 100;
                    int defStr = garrStr + fortBonus + wallFloor;
                    int bigIdx = 0;
                    for (int i = 1; i < (int)eHero.army.size(); ++i)
                        if (eHero.army[i].count > eHero.army[bigIdx].count) bigIdx = i;
                    // Attacker needs ~1.15x the defender's effective
                    // strength to storm a rival town (was 1.3x). Eased so
                    // a committed army finishes sieges instead of bouncing
                    // off Castles for weeks and stalling the game.
                    if (eiStr * 100 >= defStr * 115) {
                        if (!eHero.army.empty())
                            eHero.army[bigIdx].count = std::max(1, eHero.army[bigIdx].count
                                                        - eHero.army[bigIdx].count / 10);
                        t.garrison.clear();
                        t.ownerId = eHero.ownerId;
                        // Taking a defended town is the biggest feat
                        // on the map — it awarded no xp at all.
                        aiHeroAwardXp(eHero, 400 + defStr / 4);
                        gLog("Enemy %s stormed rival town %s (week %d)\n",
                             eHero.name.c_str(), t.name.c_str(), m_turns.week());
                    } else {
                        if (!eHero.army.empty())
                            eHero.army[bigIdx].count = std::max(1, eHero.army[bigIdx].count
                                                        - eHero.army[bigIdx].count / 6);
                        eHero.movePool = 0; // assault failed, stops here
                    }
                } else if (t.ownerId > 0 && t.ownerId <= static_cast<uint32_t>(m_numHumanPlayers)) {
                    // Assault on a human/watched player's town: ALWAYS
                    // fight it for real on the battlefield. There is NO
                    // off-screen strength comparison and NO walk-in
                    // capture — the town only changes hands if the
                    // attacker beats the garrison in an actual siege.
                    // Watch mode plays it on-screen too (auto-played,
                    // prep auto-picked).
                    if (!combatTriggered
                        && (t.ownerId == static_cast<uint32_t>(currentPlayerId())
                            || m_watchingAI)
                        && !t.garrison.empty()) {
                        m_pendingTownDefenseId = t.id;
                        m_defenseAttackerId    = eHero.id;
                        eHero.movePool = 0;
                        combatTriggered = true;
                        if (!m_watchingAI && t.hasBuilding(BID::BASTION)) {
                            m_showDefensePrepPopup = true;  // battle starts on choice
                        } else {
                            startTownDefenseBattle(-1);
                        }
                        break;
                    }
                    // Empty garrison, or a battle already triggered this
                    // turn: the enemy cannot take the town by walking in.
                    // It simply stops here — no off-screen capture.
                    eHero.movePool = 0;
                }
                break;
            }
        }
    }
    // Combat vs the player interrupts the whole AI round (both the
    // remaining heroes and doEndTurnPost) — same semantics as when this
    // was the last statement of the hero loop inside doEndTurn().
    if (combatTriggered) return false;
    return true;
}

// One frame's slice of a spread AI round (Watch mode). Runs heroes until the
// budget is spent, then yields so the frame can render; finishes the round
// with doEndTurnPost() once the roster is done. A single hero can exceed the
// budget on its own (e.g. a failing 400-hex march search) — the budget only
// stops several such heroes stacking into one frame.
void Game::aiTurnStep()
{
    constexpr double kFrameBudgetMs = 8.0;
    ++g_aiSlices;
    auto t0 = std::chrono::steady_clock::now();
    while (m_aiTurn.nextHero < static_cast<int>(m_enemyHeroes.size())) {
        if (!aiTakeHeroTurn(m_aiTurn.nextHero++)) {
            // Combat interrupted the round — abandon the rest, exactly like
            // the synchronous path (doEndTurnPost intentionally not run).
            m_aiTurn.active = false;
            return;
        }
        double ms = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - t0).count() / 1e6;
        if (ms > kFrameBudgetMs) break;
    }
    if (m_aiTurn.nextHero >= static_cast<int>(m_enemyHeroes.size())) {
        m_aiTurn.active = false;
        doEndTurnPost(m_aiTurn.lastPlayerEndedTurn);
    }
}

// Everything doEndTurn() used to do AFTER the enemy-hero loop: specialty
// terrain spread, siege resolution, day/week advancement, weekly hiring.
// In Watch mode this runs frames after doEndTurn() returned (so the [PERF]
// total there includes render time between slices, not pure CPU).
void Game::doEndTurnPost(bool lastPlayerEndedTurn)
{
    // Snap camera back to player hero after enemy turns (Watch AI)
    if (m_watchingAI && !m_heroes.empty()) {
        float pwx, pwy;
        m_hexRenderer.grid().hexToWorld(m_heroes[m_activeHeroIdx].pos, pwx, pwy);
        m_camera.setPosition(pwx, pwy);
    }

    // Infestation specialty (Flesh Architect/Amalgamate): FleshZone spreads each turn
    auto applyInfestation = [&](std::vector<Hero>& heroList) {
        for (auto& hero : heroList) {
            if (!hero.infestationSpecialty) continue;
            constexpr int INFEST_RADIUS = 2;
            std::vector<HexCoord> toInfest;
            for (const auto& coord : m_map.coords()) {
                if (HexGrid::distance(hero.pos, coord) > INFEST_RADIUS) continue;
                HexTile* t = m_map.getTile(coord);
                if (!t || t->terrain == Terrain::FleshZone) continue;
                bool adjacentFlesh = false;
                for (const auto& nb : HexGrid::neighbors(coord)) {
                    const HexTile* nt = m_map.getTile(nb);
                    if (nt && nt->terrain == Terrain::FleshZone) { adjacentFlesh = true; break; }
                }
                Terrain ter = t->terrain;
                if (adjacentFlesh && (ter == Terrain::Plains || ter == Terrain::Wasteland
                                      || ter == Terrain::Corrupted || ter == Terrain::Barren)) {
                    toInfest.push_back(coord);
                }
            }
            if (!toInfest.empty()) {
                int converted = 0;
                for (const auto& c : toInfest) {
                    if (converted >= 2) break;
                    HexTile* t = m_map.getTile(c);
                    if (t) { t->terrain = Terrain::FleshZone; converted++; }
                }
                if (converted > 0) {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "Infestation: +%d FleshZone", converted);
                    pushPickupEffect(hero.pos, buf, IM_COL32(180, 100, 60, 255));
                }
            }
        }
    };
    applyInfestation(m_heroes);
    applyInfestation(m_enemyHeroes);

    // BlightAura specialty (Blight Caller/Voidkin): Sacred terrain near the hero
    // is passively corrupted each turn. Applies to both player and enemy heroes.
    auto applyBlightAura = [&](std::vector<Hero>& heroList) {
        for (auto& hero : heroList) {
            if (!hero.blightAuraSpecialty) continue;
            constexpr int BLIGHT_RADIUS = 3;
            int corrupted = 0;
            for (const auto& coord : m_map.coords()) {
                if (HexGrid::distance(hero.pos, coord) > BLIGHT_RADIUS) continue;
                HexTile* t = m_map.getTile(coord);
                if (t && t->terrain == Terrain::Sacred) {
                    t->terrain = Terrain::Corrupted;
                    corrupted++;
                }
            }
            if (corrupted > 0) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "BlightAura: %d Sacred → Corrupted", corrupted);
                pushPickupEffect(hero.pos, buf, IM_COL32(160, 80, 200, 255));
            }
        }
    };
    applyBlightAura(m_heroes);
    applyBlightAura(m_enemyHeroes);

    // ── Siege camp resolution ─────────────────────────────────────────────
    // Update underSiege flag for every town
    for (auto& t : m_towns) t.underSiege = false;
    for (const auto& h : m_heroes) {
        if (!h.isSiegeCamping || h.siegeTargetTownId == 0) continue;
        for (auto& t : m_towns)
            if (t.id == h.siegeTargetTownId) t.underSiege = true;
    }
    // Trigger siege combat for any town that has camped heroes this turn.
    // (Fortify's actual bonuses are read and cleared inside
    // triggerSiegeCombat itself, once combat is confirmed to start.)
    for (auto& t : m_towns) {
        if (!t.underSiege) continue;
        triggerSiegeCombat(t.id);
        // triggerSiegeCombat may change game state; stop processing if combat started
        if (m_state == GameState::Combat) return;
    }

    // TEMP INSTRUMENT: per-turn cost split, candidate rescan vs pathfinding.
    if (g_candBuilds > 0) {
        double totalMs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - g_turnT0).count() / 1e6;
        gLog("  [PERF] turn: total=%.1fms steps=%d cand=%.1fms path=%.1fms | long(400)=%d (fail %d) %.1fms "
             "short(60)=%d %.1fms | reuse-hits=%d slices=%d\n",
             totalMs, g_candBuilds, g_candNs / 1e6, g_pathNs / 1e6,
             g_longCalls, g_longFails, g_longNs / 1e6, g_shortCalls, g_shortNs / 1e6,
             g_reuseHits, g_aiSlices);
        g_candNs = g_pathNs = g_longNs = g_shortNs = 0;
        g_candBuilds = g_longCalls = g_longFails = g_shortCalls = g_reuseHits = 0;
        g_aiSlices = 0;
    }

    bool newWeek = m_turns.endTurn(m_towns, m_heroes,
                                   m_playerResources, m_registry,
                                   static_cast<uint32_t>(currentPlayerId()));
    if (newWeek) {
        // Capture income totals for week summary popup before adding them
        m_weekSummaryIncome = m_turns.calculateWeeklyIncome(m_towns, currentPlayerId());
        for (const auto& r : m_resources)
            if (r.ownedBy == static_cast<uint32_t>(currentPlayerId())) m_weekSummaryIncome.add(r.type, mineYield(r));
        m_cachedWeeklyIncome = m_weekSummaryIncome;
        m_weekSummaryWeek = m_turns.week();
        if (!m_watchingAI) m_showWeekSummary = true;

        // Mine income for player-controlled resource nodes
        for (const auto& r : m_resources)
            if (r.ownedBy == static_cast<uint32_t>(currentPlayerId())) m_playerResources.add(r.type, mineYield(r));

        // (Other human players' weekly income is applied in the
        //  lastPlayerEndedTurn block below via m_players[pi].resources.)

        // Each AI player's income: same rules as the player — towns pay
        // their weeklyIncome, owned mines pay mineYield, into THAT
        // player's own pool (previously every AI town/mine fed one shared
        // pool regardless of which bot actually owned it). This block
        // runs exactly once per game-week (hot-seat early-outs before
        // endTurn for all but the last player).
        {
            // Difficulty finally means something for the AI's *economy*, not
            // just the human's starting handicap. Higher difficulty grants the
            // AI a flat income multiplier (classic-strategy does the same) so
            // Hard fields bigger armies and builds faster — the AI actually
            // gets stronger, instead of the game only making YOU weaker.
            // Easy 90% / Normal 100% / Hard 130%. In watch mode every player is
            // an AI, so the multiplier applies evenly and stays fair.
            static const int kAiEconPct[3] = { 90, 100, 130 };
            const int econPct = kAiEconPct[std::clamp(m_newGameDifficulty, 0, 2)];
            int aiTowns = 0, aiMines = 0;
            for (const auto& t : m_towns)
                if (isAiOwner(t.ownerId)) {
                    for (int ri = 0; ri < RESOURCE_COUNT; ++ri) {
                        auto rt = static_cast<ResourceType>(ri);
                        int inc = t.weeklyIncome.get(rt);
                        if (inc) aiResources(t.ownerId).add(rt, inc * econPct / 100);
                    }
                    ++aiTowns;
                }
            for (const auto& r : m_resources)
                if (isAiOwner(r.ownedBy)) {
                    aiResources(r.ownedBy).add(r.type, mineYield(r) * econPct / 100);
                    ++aiMines;
                }
            int totalGold = 0, totalIron = 0;
            for (const auto& res : m_aiResources) {
                totalGold += res.get(ResourceType::Gold);
                totalIron += res.get(ResourceType::Iron);
            }
            gLog("AI economy: %d towns + %d mines across %zu players -> %dg %di combined\n",
                 aiTowns, aiMines, m_aiResources.size(), totalGold, totalIron);
            // ── Townless decay ──────────────────────────────────────────
            // An owner with no town can't sustain heroes forever. After a
            // grace period its heroes disband (starved of a home) and the
            // player is eliminated — this is what removes a player who lost
            // their last town but still has a wandering army.
            for (uint32_t o = 1; o <= 9; ++o) {
                int towns = 0;
                for (const auto& t : m_towns) if (t.ownerId == o) towns++;
                if (towns > 0) { m_ownerTownlessWeeks[o] = 0; continue; }
                // Does this owner still have any hero at all?
                bool hasHero = false;
                if (o == 1u) hasHero = !m_heroes.empty();
                else for (const auto& h : m_enemyHeroes)
                    if (h.ownerId == o && !h.eliminated) { hasHero = true; break; }
                if (!hasHero) continue;
                m_ownerTownlessWeeks[o]++;
                if (m_ownerTownlessWeeks[o] >= 6) {
                    // Starve out — disband this owner's heroes.
                    int disbanded = 0;
                    for (auto& h : m_enemyHeroes)
                        if (h.ownerId == o && !h.eliminated) {
                            h.eliminated = true; h.army.clear(); disbanded++;
                        }
                    if (o == 1u) m_heroes.clear();
                    gLog("P%u eliminated — no town for 6 weeks, %d hero(es) starved out (week %d)\n",
                         o, disbanded, m_turns.week());
                    m_ownerTownlessWeeks[o] = 0;
                }
            }
            // Per-player breakdown (every ~4 weeks) so we can see who is
            // active vs idle across the whole game.
            if (m_turns.week() % 4 == 0) {
                std::vector<uint32_t> owners;
                auto note=[&](uint32_t o){ if(o&&std::find(owners.begin(),owners.end(),o)==owners.end()) owners.push_back(o); };
                for (const auto& t : m_towns) note(t.ownerId);
                for (const auto& h : m_enemyHeroes) note(h.ownerId);
                std::sort(owners.begin(), owners.end());
                const auto& udl = m_registry.units();
                for (uint32_t o : owners) {
                    long long str=0; int heroes=0, towns=0, mines=0;
                    if (o == 1u) {
                        for (const auto& h : m_heroes) { str += heroStrength(h, udl); heroes++; }
                    } else {
                        for (const auto& h : m_enemyHeroes) if (h.ownerId==o){ str+=heroStrength(h,udl); heroes++; }
                    }
                    for (const auto& t : m_towns)     if (t.ownerId==o) towns++;
                    for (const auto& r : m_resources) if (r.ownedBy==o) mines++;
                    int gold = (o == 1u) ? m_playerResources.get(ResourceType::Gold)
                                         : aiResources(o).get(ResourceType::Gold);
                    gLog("  P%u %-8s str=%lld heroes=%d towns=%d mines=%d gold=%d\n",
                         o, aiPersonalityName(m_aiPersonality[std::min<uint32_t>(o,9)]),
                         str, heroes, towns, mines, gold);
                }
            }
        }

        // Garrison upkeep — 350 gold/week per garrisoned player hero
        {
            int garrisonCount = 0;
            for (const auto& h : m_heroes)
                if (h.isGarrisoned) ++garrisonCount;
            if (garrisonCount > 0) {
                int upkeep = garrisonCount * 350;
                m_playerResources.add(ResourceType::Gold, -upkeep);
                gLog("Garrison upkeep: -%dg (%d hero%s dug in)\n",
                     upkeep, garrisonCount, garrisonCount == 1 ? "" : "es");
            }
        }

        // Apply March bonus (10% move) for heroes who used March last week
        for (auto& h : m_heroes) {
            h.marchBonusActive = false;  // reset; will re-enable if cooldown was set last week
            if (h.marchCooldownWeek == m_turns.week()) {
                // Cooldown expires this week — grant the bonus move pool
                h.movePool = std::min(h.maxMove + h.maxMove / 10, h.movePool + h.maxMove / 10);
            }
        }

        gLog("New week %d - income applied\n", m_turns.week());
        if (!m_watchingAI && m_turns.week() >= 20) steam::unlockAchievement("ACH_WEEK_20");

        // AI weekly recruitment — REAL economy, same rules as the player:
        // each AI town recruits what the team pool affords from its own
        // dwellings into the town garrison; heroes pick garrisons up when
        // they visit. No conjured units, no difficulty multipliers.
        {
            const auto& unitDefs = m_registry.units();

            // NOTE: towns used to get silently reassigned here to "whoever
            // the strongest living AI hero is" every week, because a
            // capture-time bug stored a HERO id (not the player's ownerId)
            // on captured towns, orphaning them the moment that specific
            // hero died. That capture bug is fixed now (ownerId is always
            // a stable player-slot id, exactly like a human player's towns
            // when their hero dies) so a town no longer needs "inheriting"
            // — it just waits for its owner's emergency-recruit to field a
            // new hero, same as everyone else.

            // Each AI town recruits into its garrison at real cost — but
            // spends at most HALF the pool's gold on troops. Recruiting
            // first used to drain everything and starve construction
            // forever (Ironhold: 'cannot afford' weeks 6-50 while buying
            // 13 T2 every week).
            for (auto& t : m_towns) {
                // Watch-AI's watched side (ownerId==1) is a "human" slot for
                // hot-seat purposes but is fully AI-driven — without this it
                // never got the weekly garrison auto-fill that real AI towns
                // get, only true isAiOwner() towns did, so its economy grew
                // while its hero army starved. Mirrors the build loop below.
                bool watchPlayerTown = (t.ownerId == 1 && m_watchingAI);
                bool aiTown          = isAiOwner(t.ownerId);
                if (!watchPlayerTown && !aiTown) continue;
                Resources& pool = watchPlayerTown ? m_playerResources : aiResources(t.ownerId);
                Resources budget = pool;
                // Gold priority depends on whether the build tree is finished.
                // While it's still going up, CONSTRUCTION wins the gold — a town
                // that spends half its income on troops every week can't afford
                // its high tiers (measured: income-limited towns stalled at
                // ~10/24 buildings). So recruit with only a quarter until the
                // tree is complete, then pour gold into army. And once the
                // economy is clearly solved (rich pool), spend it all — a bot
                // sitting on a huge gold pile with a tiny army is the #1 failure.
                int poolGold = pool.get(ResourceType::Gold);
                int tfi = static_cast<int>(t.faction);
                bool treeDone = true;
                if (tfi >= 0 && tfi < 9)
                    for (int bid : kBuildOrder[tfi])
                        if (!t.hasBuilding(bid)) { treeDone = false; break; }
                if (poolGold < 50000)
                    budget.set(ResourceType::Gold, treeDone ? poolGold / 2 : poolGold / 4);
                Resources before = budget;
                // Cap garrison growth so towns stay defended but BEATABLE.
                // Unbounded weekly garrison recruiting made towns impregnable —
                // a full-economy game hit week 80 with 0 captures because no
                // hero could ever satisfy the garrison-strength gate. Once the
                // garrison is a solid (week-scaled) wall, stop pouring units in;
                // the units accumulate in the dwellings for a HERO to recruit
                // and take to war instead of turtling in the town.
                int64_t garStr = stacksStrength(t.garrison, unitDefs);
                // Garrison wall grows early, then PLATEAUS — an unbounded
                // linear cap (40k + 12k/week) reached ~1,000,000 by week 80,
                // far above any single hero's army, so late-game towns became
                // impregnable and games stalled at the week-80 backstop with
                // six players still alive. Plateau at a ceiling a dominant
                // army can crack (with fort multipliers a maxed Castle still
                // defends at ~2.5x this) so conquest keeps resolving.
                int64_t garCap = std::min<int64_t>(150000LL,
                                     40000LL + (int64_t)m_turns.week() * 4000LL);
                // Tech scouting #4: don't pour this week's gold into a town
                // that's about to fall to overwhelming force — hold the money
                // and let it flow to the towns that will still exist next week.
                bool writeOff = aiTownIsWriteOff(t, 0, unitDefs);
                if (writeOff)
                    gLog("[SCOUT] P%u writes off %s garrison this week — "
                         "overwhelming force inbound, gold redeployed\n",
                         t.ownerId, t.name.c_str());
                int got = (!writeOff && garStr < garCap)
                        ? aiPaidRecruit(t, t.garrison, budget, unitDefs) : 0;
                if (got > 0) {
                    for (int rt = 0; rt < RESOURCE_COUNT; ++rt) {
                        auto type = static_cast<ResourceType>(rt);
                        int spent = before.get(type) - budget.get(type);
                        if (spent > 0) pool.add(type, -spent);
                    }
                    gLog("AI %s recruited %d units into garrison (pool %dg)\n",
                         t.name.c_str(), got,
                         pool.get(ResourceType::Gold));
                }
            }
        }

        // Auto-save at start of each new week
        if (m_settingsAutoSave) saveGame();

        // ── AI town building: faction-specific priority order ─────────────────
        // Every side builds from its REAL pool: watch-mode player towns
        // from m_playerResources, each AI town from its OWN player's pool
        // via aiResources(town.ownerId). Human towns (any player in
        // hot-seat) are never auto-built.
        {
            const auto& allBuildings = m_registry.buildings();

            for (auto& town : m_towns) {
                bool watchPlayerTown = (town.ownerId == 1 && m_watchingAI);
                bool aiTown          = isAiOwner(town.ownerId);
                if (!watchPlayerTown && !aiTown) continue;

                int fIdx = static_cast<int>(town.faction);
                Resources& buildRes = watchPlayerTown ? m_playerResources
                                                      : aiResources(town.ownerId);

                // Build up to a human-competitive number of buildings per week
                // (a human builds 1/day → 7), stopping the instant a full pass
                // can't afford or unlock anything more. Was ONE build/week —
                // every AI town ran ~7x behind a human and never finished its
                // ~24-building tree (measured wk23: best AI town 22, watched 10).
                constexpr int kMaxBuildsPerWeek = 7;
                for (int buildPass = 0; buildPass < kMaxBuildsPerWeek; ++buildPass) {
                town.builtToday = 0;   // clear the 1/day gate for THIS pass
                bool built = false;

                // Market trading: find the first building we could LEGALLY
                // build (prereqs/week satisfied) but can't pay for, and
                // fill its FULL deficit — every missing resource type —
                // selling surplus at 4:1 (non-gold first, then gold). The
                // old logic chased prereq-blocked buildings and only ever
                // filled one resource type, so towns sat on six-figure
                // gold piles "unable to afford" a 2-essence dwelling.
                if (fIdx >= 0 && fIdx < 9) {
                    bool hasMarket = false;
                    for (const auto& t : m_towns) {
                        // Same OWNER, not just "any AI" — each AI player is
                        // its own economy now, so a market three players
                        // over doesn't help this town sell surplus.
                        if (t.ownerId != town.ownerId) continue;
                        if (t.hasBuilding(BID::MARKET)) { hasMarket = true; break; }
                    }
                    const BuildingDef* wantDef = nullptr;
                    if (hasMarket) {
                        for (int bid : kBuildOrder[fIdx]) {
                            if (!town.canBuild(bid, allBuildings, m_turns.week())) continue;
                            const BuildingDef* d2 = nullptr;
                            for (const auto& d : allBuildings) if (d.id == bid) { d2 = &d; break; }
                            if (!d2) continue;
                            if (buildRes.canAfford(d2->cost)) break;  // builder handles it
                            wantDef = d2;
                            break;
                        }
                        // Priority list done or unblocked: also trade for the
                        // common fallback buildings (Shipyard, Bastion, forts)
                        // — they're not in kBuildOrder, so a gold-rich town
                        // could sit 'cannot afford' on 6 iron forever.
                        if (!wantDef) {
                            for (const auto& d : allBuildings) {
                                if (d.faction != FactionId::None) continue;
                                if (d.category != BuildingCategory::Fort &&
                                    d.category != BuildingCategory::Support) continue;
                                if (!town.canBuild(d.id, allBuildings, m_turns.week())) continue;
                                if (buildRes.canAfford(d.cost)) break;
                                wantDef = &d;
                                break;
                            }
                        }
                    }
                    if (wantDef) {
                        constexpr int SELL_RATE = 4;
                        for (int rt = 0; rt < RESOURCE_COUNT; ++rt) {
                            auto need = static_cast<ResourceType>(rt);
                            int deficit = wantDef->cost.get(need) - buildRes.get(need);
                            if (deficit <= 0) continue;
                            for (int pass = 0; pass < 2 && deficit > 0; ++pass) {
                                for (int st = 0; st < RESOURCE_COUNT && deficit > 0; ++st) {
                                    auto sell = static_cast<ResourceType>(st);
                                    if (sell == need) continue;
                                    bool isGold = (sell == ResourceType::Gold);
                                    if ((pass == 0) == isGold) continue;  // pass 0: specials, pass 1: gold
                                    // never sell below what the building itself needs
                                    int surplus = buildRes.get(sell) - wantDef->cost.get(sell);
                                    int buy = std::min(deficit, surplus / SELL_RATE);
                                    if (buy > 0) {
                                        buildRes.add(sell, -(buy * SELL_RATE));
                                        buildRes.add(need, buy);
                                        deficit -= buy;
                                        gLog("%s market: %d res%d -> %d res%d for %s at %s\n",
                                             watchPlayerTown ? "Watch AI" : "AI",
                                             buy * SELL_RATE, st, buy, rt,
                                             wantDef->name.c_str(), town.name.c_str());
                                    }
                                }
                            }
                        }
                    }
                }

                // Coastal towns build a Shipyard as an EARLY priority (from
                // ~week 3, once the Hall is up) — it is NOT in any faction's
                // kBuildOrder, and the post-priority fallback further below
                // only fires on weeks when nothing else is buildable, so it
                // was starved for the entire early/mid game (verified: 0
                // shipyards + 0 boats built across a full AI-vs-AI game).
                // Without a dock the AI can never board a boat, so any
                // island- or water-separated rival is permanently
                // unreachable and can never be eliminated — the confirmed
                // root cause of the week-50 multi-player no-elimination
                // stalemate (heroes just oscillate on a local mine forever
                // "eyeing" an enemy capital they can't path to). A shipyard
                // is cheap and one-time; delaying a single dwelling on a
                // coastal town once is a trivial price for naval reach.
                if (!built && m_turns.week() >= 3
                    && !town.hasBuilding(BID::TOWN_SHIPYARD)) {
                    bool coastalEarly = false;
                    for (const auto& nb : HexGrid::neighbors(town.pos)) {
                        const HexTile* nt = m_map.getTile(nb);
                        if (nt && nt->terrain == Terrain::Water) { coastalEarly = true; break; }
                    }
                    if (coastalEarly
                        && town.build(BID::TOWN_SHIPYARD, allBuildings, buildRes, 1.0f, /*quiet*/true)) {
                        gLog("AI %s built Shipyard (coastal, early priority)\n", town.name.c_str());
                        built = true;
                    }
                }

                // Try faction priority list first
                if (fIdx >= 0 && fIdx < 9) {
                    for (int bid : kBuildOrder[fIdx]) {
                        if (town.build(bid, allBuildings, buildRes, 1.0f, /*quiet*/true)) {
                            gLog("AI %s built BID=%d (priority)\n", town.name.c_str(), bid);
                            built = true; break;
                        }
                    }
                }

                // Coastal towns get a Shipyard fairly promptly — it isn't
                // in any faction's kBuildOrder, so without this an
                // island-isolated AI only ever got one from the "last
                // resort" roll below, which dwelling/priority spam could
                // starve out indefinitely (a real "AI stuck on an island
                // forever" bug, not just a slow one).
                if (!built && !town.hasBuilding(BID::TOWN_SHIPYARD)) {
                    bool coastal = false;
                    for (const auto& nb : HexGrid::neighbors(town.pos)) {
                        const HexTile* nt = m_map.getTile(nb);
                        if (nt && nt->terrain == Terrain::Water) { coastal = true; break; }
                    }
                    if (coastal && town.build(BID::TOWN_SHIPYARD, allBuildings, buildRes, 1.0f, /*quiet*/true)) {
                        gLog("AI %s built Shipyard (coastal)\n", town.name.c_str());
                        built = true;
                    }
                }

                // Fallback: lowest unbought base dwelling tier
                for (int tier = 1; tier <= 6 && !built; ++tier) {
                    for (const auto& def : allBuildings) {
                        if (def.category != BuildingCategory::UnitDwelling) continue;
                        if (def.faction != town.faction) continue;
                        if (def.tier != tier) continue;
                        if (def.path != UpgradePath::None) continue;
                        if (town.build(def.id, allBuildings, buildRes, 1.0f, /*quiet*/true)) {
                            gLog("AI %s built %s\n", town.name.c_str(), def.name.c_str());
                            built = true; break;
                        }
                    }
                }
                // Last resort: any fort or support building
                if (!built) {
                    for (const auto& def : allBuildings) {
                        if (def.faction != town.faction && def.faction != FactionId::None) continue;
                        if (def.category != BuildingCategory::Fort &&
                            def.category != BuildingCategory::Support) continue;
                        if (town.build(def.id, allBuildings, buildRes, 1.0f, /*quiet*/true)) {
                            gLog("AI %s built %s\n", town.name.c_str(), def.name.c_str());
                            built = true;
                            break;
                        }
                    }
                }

                if (!built) break;   // nothing left to build/afford this week
                }   // end build-pass loop (up to kMaxBuildsPerWeek)
            }
        }

        // ── AI hero recruitment — one per week from owned tavern town ─────
        {
            // Hard fields more heroes than Easy/Normal (more map pressure).
            // The cap is behavioral: every hire costs real gold now.
            static const int kHeroCap[3] = { 5, 6, 7 };
            const int AI_HERO_CAP = kHeroCap[std::clamp(m_newGameDifficulty, 0, 2)];
            constexpr int AI_HIRE_COST = 2500;  // same as the human tavern
            {
                static const char* kAINames[] = {
                    "Drafted Sword","Hired Blade","Road Warden",
                    "Freelance Arm","Wandering Axe","Sellsword",
                    "Hired Shield","Iron Hand","Dusty Boot","Grim Pike"
                };
                for (auto& recruitTown : m_towns) {
                    if (recruitTown.ownerId <= static_cast<uint32_t>(m_numHumanPlayers)) continue;
                    // Cap and afford per OWNER now — each AI player fields
                    // its own roster from its own wallet, not one shared
                    // cap/pool across every bot on the map.
                    int ownerHeroCount = 0;
                    for (const auto& h : m_enemyHeroes)
                        if (h.ownerId == recruitTown.ownerId) ++ownerHeroCount;
                    if (ownerHeroCount >= AI_HERO_CAP) continue;
                    if (aiResources(recruitTown.ownerId).get(ResourceType::Gold) < AI_HIRE_COST) continue;
                    bool occupied = false;
                    for (const auto& e : m_enemyHeroes)
                        if (e.pos == recruitTown.pos) { occupied = true; break; }
                    if (occupied) continue;

                    uint32_t newId = 500u;
                    for (const auto& h : m_heroes)      newId = std::max(newId, h.id + 1u);
                    for (const auto& h : m_enemyHeroes) newId = std::max(newId, h.id + 1u);

                    uint32_t nameSeed = (m_turns.week() * 7919u) ^ static_cast<uint32_t>(recruitTown.pos.q * 317u + recruitTown.pos.r);
                    Hero newHero;
                    newHero.id      = newId;
                    newHero.ownerId = recruitTown.ownerId;
                    newHero.faction = recruitTown.faction;
                    newHero.name    = kAINames[nameSeed % 10];
                    newHero.pos     = recruitTown.pos;
                    newHero.movePool = newHero.maxMove;

                    // Tavern retinue included in the fee (same as human hires)
                    giveTavernRetinue(newHero, m_registry.buildings(), m_registry.units());
                    aiResources(recruitTown.ownerId).add(ResourceType::Gold, -AI_HIRE_COST);
                    // Consolidate: scoop the town garrison and buy more with the
                    // AI pool — a TRUE mirror of the watched-side hire below.
                    // Without this the enemy fielded fodder heroes while its
                    // garrison + gold rotted in the town (223 mines / 185k gold
                    // and still losing), so the enemy hire must match exactly.
                    takeGarrison(recruitTown, newHero, m_registry.units());
                    aiPaidRecruit(recruitTown, newHero.army, aiResources(recruitTown.ownerId), m_registry.units());
                    HexCoord spawnPos = recruitTown.pos;
                    for (auto& nb : HexGrid::neighbors(recruitTown.pos)) {
                        const HexTile* nt = m_map.getTile(nb);
                        if (nt && nt->terrain != Terrain::Water && nt->heroId == 0) {
                            spawnPos = nb; break;
                        }
                    }
                    newHero.pos = spawnPos;
                    if (HexTile* ht = m_map.getTile(spawnPos)) ht->heroId = newHero.id;
                    m_enemyHeroes.push_back(std::move(newHero));
                    gLog("AI recruited hero at %s (week %d)\n",
                         recruitTown.name.c_str(), m_turns.week());
                    // NOT a break: this used to cap the ENTIRE map to one
                    // new hero per week total, so whichever town happened
                    // to iterate first every week (same player, every
                    // time) hoarded all hero replenishment forever while
                    // every other AI player's civilization went silent
                    // the moment its one hero died — confirmed live via
                    // a 119-week Watch AI run where 4 of 5 AI players
                    // never recruited or built again after week ~3. Each
                    // owner already has its own per-owner cap/afford
                    // check above, so letting every eligible owner hire
                    // once per week (instead of only the map's first
                    // eligible town) is the correct per-player behaviour.
                }
            }
        }

        // ── Watch AI: watched-side hero recruitment — a MIRROR of the enemy ──
        // The watched side hires full combat heroes up to the same cap the
        // enemy uses, all driven by the same routine. (The old main +
        // courier/scout system was a crutch from when the AI fielded one
        // hero; both sides now field a real roster and fight for the map.)
        if (m_watchingAI) {
            constexpr int WATCH_HIRE_COST = 2500;
            static const int kHeroCap[3] = { 5, 6, 7 };
            const int WATCH_HERO_CAP = kHeroCap[std::clamp(m_newGameDifficulty, 0, 2)];
            const auto& unitDefs = m_registry.units();

            auto isCamped = [&](const Town& t, int freshStr) {
                for (const auto& eh : m_enemyHeroes)
                    if (HexGrid::distance(eh.pos, t.pos) <= 4
                        && heroStrength(eh, unitDefs) > freshStr * 2) return true;
                return false;
            };
            auto spawnNear = [&](const Town& t) {
                HexCoord spawnPos = t.pos;
                for (auto& nb : HexGrid::neighbors(t.pos)) {
                    const HexTile* nt = m_map.getTile(nb);
                    if (nt && nt->terrain != Terrain::Water && nt->heroId == 0) {
                        spawnPos = nb; break;
                    }
                }
                return spawnPos;
            };

            static const char* kWatchNames[] = {
                "Vanguard","Marshal","Warden","Crusader","Templar",
                "Champion","Paladin","Sentinel","Reaver","Zealot"
            };
            // Empty roster is urgent (else the watched side is inert); one
            // hire per week otherwise, just like the enemy.
            bool rosterEmpty = m_heroes.empty();
            if (static_cast<int>(m_heroes.size()) < WATCH_HERO_CAP
                && m_playerResources.get(ResourceType::Gold) >= WATCH_HIRE_COST) {
                for (auto& recruitTown : m_towns) {
                    if (recruitTown.ownerId != 1) continue;
                    bool occupied = false;
                    for (const auto& h : m_heroes)
                        if (h.pos == recruitTown.pos) { occupied = true; break; }
                    if (occupied && !rosterEmpty) continue;

                    uint32_t newId = 100u;
                    for (const auto& h : m_heroes) newId = std::max(newId, h.id + 1u);
                    uint32_t nameSeed = (m_turns.week() * 6271u) ^ newId;
                    Hero newHero;
                    newHero.id       = newId;
                    newHero.faction  = recruitTown.faction;
                    newHero.name     = kWatchNames[nameSeed % 10];
                    newHero.movePool = newHero.maxMove;

                    // Fresh level-1 hire, same as the enemy's — no XP
                    // catch-up, a true mirror. It levels through play.
                    giveTavernRetinue(newHero, m_registry.buildings(), unitDefs);

                    int fresh = heroStrength(newHero, unitDefs)
                              + stacksStrength(recruitTown.garrison, unitDefs);
                    if (isCamped(recruitTown, fresh)) continue;

                    m_playerResources.add(ResourceType::Gold, -WATCH_HIRE_COST);
                    takeGarrison(recruitTown, newHero, unitDefs);
                    aiPaidRecruit(recruitTown, newHero.army, m_playerResources, unitDefs);
                    newHero.pos = spawnNear(recruitTown);
                    if (HexTile* ht = m_map.getTile(newHero.pos)) ht->heroId = newHero.id;
                    gLog("Watch AI hired %s at %s (week %d)\n",
                         newHero.name.c_str(), recruitTown.name.c_str(), m_turns.week());
                    m_heroes.push_back(std::move(newHero));
                    if (m_activeHeroIdx >= static_cast<int>(m_heroes.size()))
                        m_activeHeroIdx = 0;
                    break;
                }
            }
        }

        // ── Weekly random event ────────────────────────────────────────────
        m_weeklyEventHeadline.clear();
        m_weeklyEventBody.clear();
        m_weekChoiceOptions.clear();
        // Use week number + a pseudo-hash for varied but deterministic events
        int evtRoll = ((m_turns.week() * 2654435761u) >> 8) % 28;
        switch (evtRoll) {
            case 0: { // no event
                break;
            }
            case 1: { // Merchant's Gift — bonus gold
                m_playerResources.add(ResourceType::Gold, 500);
                m_weeklyEventHeadline = "A Merchant's Gift";
                m_weeklyEventBody = "A wandering trader pays 500 Gold for safe passage through your lands.";
                break;
            }
            case 2: { // Wandering Wizard — learn a random unknown spell
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    for (int i = 0; i < SPELL_COUNT; ++i) {
                        int sid = ALL_SPELLS[i].id;
                        bool known = false;
                        for (int s : h.knownSpells) if (s == sid) { known = true; break; }
                        if (!known) {
                            h.knownSpells.push_back(sid);
                            m_weeklyEventHeadline = "Wandering Wizard";
                            m_weeklyEventBody = std::string("A sage teaches your hero: ")
                                              + ALL_SPELLS[i].name + "!";
                            break;
                        }
                    }
                }
                break;
            }
            case 3: { // Bandit Raid — lose gold
                int lost = std::min(200, m_playerResources.get(ResourceType::Gold));
                m_playerResources.add(ResourceType::Gold, -lost);
                m_weeklyEventHeadline = "Bandit Raid!";
                m_weeklyEventBody = "Raiders struck your supply wagons, stealing "
                                  + std::to_string(lost) + " Gold.";
                break;
            }
            case 4: { // Rich Harvest — bonus resources
                m_playerResources.add(ResourceType::Gold, 200);
                m_playerResources.add(ResourceType::Iron, 3);
                m_weeklyEventHeadline = "Rich Harvest";
                m_weeklyEventBody = "Abundant yields from your territories: +200 Gold, +3 Iron.";
                break;
            }
            case 5: { // Heroic Inspiration — XP boost
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    int xpGain = 150;
                    int oldLvl5 = h.level;
                    if (h.addXp(xpGain)) {
                        const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                        if (cls) {
                            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                            m_levelUpOffers = LevelUpSystem::generateOffers(
                                *cls, h.skills, h.level, allSkills, h.faction);
                        }
                        if (m_levelUpOffers.empty())
                            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                        m_pendingLevelUps = h.level - oldLvl5;
                        m_showLevelUpModal = true;
                        { ScriptContext lvCtx; lvCtx.heroId = h.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                        // Level-up particle burst at hero's screen position
                        {
                            float hwx, hwy;
                            m_hexRenderer.grid().hexToWorld(h.pos, hwx, hwy);
                            float hsx, hsy;
                            m_camera.worldToScreen(hwx, hwy, hsx, hsy);
                            m_particles.emit(hsx, hsy, ParticlePreset::LevelUp);
                        }
                    }
                    m_weeklyEventHeadline = "Battle Hardened";
                    m_weeklyEventBody = "Tales of your deeds spread: +"
                                       + std::to_string(xpGain) + " XP.";
                }
                break;
            }
            case 6: { // Arcane Font — bonus mana for the hero
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    h.maxMana = std::min(h.maxMana + 5, 99);
                    h.mana    = h.maxMana;
                    m_weeklyEventHeadline = "Arcane Font";
                    m_weeklyEventBody = "A ley-line resonance permanently expands your hero's mana pool by 5.";
                }
                break;
            }
            case 7: { // Ancient Armory — hero gains +1 Attack
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    h.attack++;
                    m_weeklyEventHeadline = "Ancient Armory";
                    m_weeklyEventBody = "You unearth a cache of fine weapons from an old war. Your hero gains +1 Attack.";
                }
                break;
            }
            case 8: { // Rally! — strongest army stack grows
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    int bestCount = 0; int bestIdx = -1;
                    for (int i = 0; i < (int)h.army.size(); ++i)
                        if (h.army[i].count > bestCount) { bestCount = h.army[i].count; bestIdx = i; }
                    if (bestIdx >= 0) {
                        h.army[bestIdx].count += 5;
                        m_weeklyEventHeadline = "Rally!";
                        m_weeklyEventBody = "Volunteers flock to your banner, reinforcing your ranks with 5 more fighters.";
                    }
                }
                break;
            }
            case 9: { // Magical Storm — enemy heroes lose mana
                for (auto& eh : m_enemyHeroes) eh.mana = std::max(0, eh.mana - 5);
                m_weeklyEventHeadline = "Magical Storm";
                m_weeklyEventBody = "A surge of wild magic disperses spell reserves. Enemy heroes lose 5 mana.";
                break;
            }
            case 10: { // Tribute from Vassals — multi-resource bonus
                m_playerResources.add(ResourceType::Gold,        300);
                m_playerResources.add(ResourceType::FaithStones,   2);
                m_playerResources.add(ResourceType::VerdantSap,    2);
                m_weeklyEventHeadline = "Tribute from Vassals";
                m_weeklyEventBody = "Subject villages send tribute: +300 Gold, +2 Faith Stones, +2 Verdant Sap.";
                break;
            }
            case 11: { // Plague — garrison defenders weakened in all human-owned towns
                int lostTotal = 0;
                for (auto& t : m_towns) {
                    if (t.ownerId == 0 || t.ownerId > static_cast<uint32_t>(m_numHumanPlayers)
                        || t.garrison.empty()) continue;
                    for (auto& s : t.garrison) {
                        int lost = std::max(0, s.count / 5);
                        s.count -= lost;
                        lostTotal += lost;
                    }
                }
                m_weeklyEventHeadline = "Plague Sweeps the Land!";
                m_weeklyEventBody = "A virulent sickness culls your town garrisons. "
                    + (lostTotal > 0 ? std::to_string(lostTotal) + " garrison troops perished."
                                     : "Your towns were untouched — no garrison losses.");
                break;
            }
            case 12: { // Fallen Knight — hero gains +1 Defense
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    h.defense++;
                    m_weeklyEventHeadline = "Fallen Knight's Legacy";
                    m_weeklyEventBody = "You bury a fallen champion and claim his mantle. Your hero gains +1 Defense.";
                }
                break;
            }
            case 13: { // Mercenary Camp — strongest stack grows by 8
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    int best = 0, bestIdx = -1;
                    for (int i = 0; i < (int)h.army.size(); ++i)
                        if (h.army[i].count > best) { best = h.army[i].count; bestIdx = i; }
                    if (bestIdx >= 0) {
                        h.army[bestIdx].count += 8;
                        m_weeklyEventHeadline = "Mercenary Camp";
                        m_weeklyEventBody = "Hired blades swell your ranks: +8 fighters join your strongest unit.";
                    }
                }
                break;
            }
            case 14: { // Scouting Report — enemy hero mana drained + player gets gold
                for (auto& eh : m_enemyHeroes) eh.mana = std::max(0, eh.mana - 8);
                m_playerResources.add(ResourceType::Gold, 150);
                m_weeklyEventHeadline = "Spy Network Pays Off";
                m_weeklyEventBody = "Your agents disrupt enemy supply lines: +150 Gold, enemy heroes lose 8 mana.";
                break;
            }
            case 15: { // Alchemy — rare resources
                m_playerResources.add(ResourceType::Mercury, 2);
                m_playerResources.add(ResourceType::BloodEssence, 1);
                m_weeklyEventHeadline = "Alchemist's Discovery";
                m_weeklyEventBody = "A rogue alchemist delivers rare reagents: +2 Mercury, +1 Blood Essence.";
                break;
            }
            case 16: { // Divine Favour — hero fully restores mana
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    h.mana = h.maxMana;
                    m_weeklyEventHeadline = "Divine Favour";
                    m_weeklyEventBody = "A radiant vision renews your hero's magical reserves. Mana fully restored.";
                }
                break;
            }
            case 17: { // Enemy Deserters — XP + small unit join
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    int xp = 100;
                    int oldLvl17 = h.level;
                    if (h.addXp(xp)) {
                        const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                        if (cls) {
                            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                            m_levelUpOffers = LevelUpSystem::generateOffers(
                                *cls, h.skills, h.level, allSkills, h.faction);
                        }
                        if (m_levelUpOffers.empty())
                            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                        m_pendingLevelUps = h.level - oldLvl17;
                        m_showLevelUpModal = true;
                        { ScriptContext lvCtx; lvCtx.heroId = h.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                    }
                    // Also add 3 to hero's weakest stack
                    int least = INT32_MAX, leastIdx = -1;
                    for (int i = 0; i < (int)h.army.size(); ++i)
                        if (h.army[i].count > 0 && h.army[i].count < least)
                            { least = h.army[i].count; leastIdx = i; }
                    if (leastIdx >= 0) h.army[leastIdx].count += 3;
                }
                m_weeklyEventHeadline = "Enemy Deserters";
                m_weeklyEventBody = "Enemy soldiers defect to your cause, bringing +100 XP and 3 recruits for your smallest unit.";
                break;
            }
            case 18: { // Tax Revolt — gold halved (one-time penalty)
                int lost = m_playerResources.get(ResourceType::Gold) / 2;
                m_playerResources.add(ResourceType::Gold, -lost);
                m_weeklyEventHeadline = "Tax Revolt!";
                m_weeklyEventBody = "Overtaxed peasants revolt and seize half your treasury. Lost: "
                    + std::to_string(lost) + " Gold.";
                break;
            }
            case 19: { // Titan's Favour — hero max HP +15
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    h.heroMaxHp += 15;
                    h.heroHp = std::min(h.heroHp + 15, h.heroMaxHp);
                    m_weeklyEventHeadline = "Titan's Favour";
                    m_weeklyEventBody = "A titan spirit blesses your hero's endurance. Max HP permanently increased by 15.";
                }
                break;
            }
            case 20: { // Merchant's Offer — choice: buy/pass
                m_weeklyEventHeadline = "Travelling Merchant";
                m_weeklyEventBody = "A well-stocked merchant arrives at your camp offering rare supplies. He wants gold for his wares.";
                m_weekChoiceOptions.clear();
                m_weekChoiceOptions.push_back({"Buy supplies (-800 Gold)", "+4 Iron, +3 faction resource", [this](){
                    if (m_playerResources.get(ResourceType::Gold) >= 800) {
                        m_playerResources.add(ResourceType::Gold, -800);
                        m_playerResources.add(ResourceType::Iron, 4);
                        if (!m_heroes.empty()) {
                            FactionId f = m_heroes[m_activeHeroIdx].faction;
                            ResourceType fRes = ResourceType::Gold;
                            switch (f) {
                                case FactionId::HolyOrder:
                                case FactionId::CrimsonWardens:  fRes = ResourceType::FaithStones;  break;
                                case FactionId::Thornkin:
                                case FactionId::Voidkin:         fRes = ResourceType::VerdantSap;   break;
                                case FactionId::EternalEmpire:   fRes = ResourceType::Mercury;      break;
                                case FactionId::Bloodsworn:
                                case FactionId::Amalgamate:      fRes = ResourceType::BloodEssence; break;
                                default: break;
                            }
                            if (fRes != ResourceType::Gold) m_playerResources.add(fRes, 3);
                        }
                    }
                }});
                m_weekChoiceOptions.push_back({"Sell surplus (+500 Gold)", "+500 Gold from your excess supplies", [this](){
                    m_playerResources.add(ResourceType::Gold, 500);
                }});
                m_weekChoiceOptions.push_back({"Send him away", "Nothing happens.", [](){}});
                break;
            }
            case 21: { // Mercenary Company — choice: hire/partial/decline
                m_weeklyEventHeadline = "Mercenary Company";
                m_weeklyEventBody = "A veteran mercenary company approaches, offering their swords for coin. They have seen many battles.";
                m_weekChoiceOptions.clear();
                m_weekChoiceOptions.push_back({"Hire them (-1200 Gold)", "+15 fighters join your largest stack", [this](){
                    if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 1200) {
                        m_playerResources.add(ResourceType::Gold, -1200);
                        Hero& h = m_heroes[m_activeHeroIdx];
                        int best = 0, bestIdx = -1;
                        for (int i = 0; i < (int)h.army.size(); ++i)
                            if (h.army[i].count > best) { best = h.army[i].count; bestIdx = i; }
                        if (bestIdx >= 0) h.army[bestIdx].count += 15;
                    }
                }});
                m_weekChoiceOptions.push_back({"Partial hire (-500 Gold)", "+6 fighters join your largest stack", [this](){
                    if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 500) {
                        m_playerResources.add(ResourceType::Gold, -500);
                        Hero& h = m_heroes[m_activeHeroIdx];
                        int best = 0, bestIdx = -1;
                        for (int i = 0; i < (int)h.army.size(); ++i)
                            if (h.army[i].count > best) { best = h.army[i].count; bestIdx = i; }
                        if (bestIdx >= 0) h.army[bestIdx].count += 6;
                    }
                }});
                m_weekChoiceOptions.push_back({"Decline", "The mercenaries move on.", [](){}});
                break;
            }
            case 22: { // Rogue Scholar — choice: buy spell/buy stat/ignore
                m_weeklyEventHeadline = "Rogue Scholar";
                m_weeklyEventBody = "An exiled mage-scholar approaches your camp. He offers his forbidden knowledge for coin.";
                m_weekChoiceOptions.clear();
                m_weekChoiceOptions.push_back({"Buy a spell (-1500 Gold)", "Learn a new spell you don't know yet", [this](){
                    if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 1500) {
                        m_playerResources.add(ResourceType::Gold, -1500);
                        Hero& h = m_heroes[m_activeHeroIdx];
                        for (int i = 0; i < SPELL_COUNT; ++i) {
                            int sid = ALL_SPELLS[i].id;
                            bool known = false;
                            for (int s : h.knownSpells) if (s == sid) { known = true; break; }
                            if (!known) { h.knownSpells.push_back(sid); break; }
                        }
                    }
                }});
                m_weekChoiceOptions.push_back({"Buy tactical insight (-800 Gold)", "+2 to hero Attack and Defense", [this](){
                    if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 800) {
                        m_playerResources.add(ResourceType::Gold, -800);
                        m_heroes[m_activeHeroIdx].attack  += 2;
                        m_heroes[m_activeHeroIdx].defense += 2;
                    }
                }});
                m_weekChoiceOptions.push_back({"Chase him off", "You don't trust outlaws.", [](){}});
                break;
            }
            case 23: { // Ancient Oracle — choice: pay for info/free lesser boon
                m_weeklyEventHeadline = "Ancient Oracle";
                m_weeklyEventBody = "An ancient seer emerges from the mist, offering visions of the near future -- for a price.";
                m_weekChoiceOptions.clear();
                m_weekChoiceOptions.push_back({"Pay the Oracle (-600 Gold)", "+250 XP and hero mana fully restored", [this](){
                    if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 600) {
                        m_playerResources.add(ResourceType::Gold, -600);
                        Hero& h = m_heroes[m_activeHeroIdx];
                        h.mana = h.maxMana;
                        int oldLvl = h.level;
                        if (h.addXp(250) && h.level > oldLvl) {
                            const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                            if (cls) {
                                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                m_levelUpOffers = LevelUpSystem::generateOffers(*cls, h.skills, h.level, allSkills, h.faction);
                            }
                            if (m_levelUpOffers.empty())
                                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                            m_pendingLevelUps = h.level - oldLvl;
                            m_showLevelUpModal = true;
                        }
                    }
                }});
                m_weekChoiceOptions.push_back({"Accept a free omen", "+100 XP, the seer warns of coming danger", [this](){
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        int oldLvl = h.level;
                        if (h.addXp(100) && h.level > oldLvl) {
                            const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                            if (cls) {
                                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                m_levelUpOffers = LevelUpSystem::generateOffers(*cls, h.skills, h.level, allSkills, h.faction);
                            }
                            if (m_levelUpOffers.empty())
                                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                            m_pendingLevelUps = h.level - oldLvl;
                            m_showLevelUpModal = true;
                        }
                    }
                }});
                m_weekChoiceOptions.push_back({"Walk away", "You have no time for riddles.", [](){}});
                break;
            }
            case 24: { // Wandering Herd — enemy hero loses units from their largest stack
                int lostTotal = 0;
                if (!m_enemyHeroes.empty()) {
                    Hero& enemy = m_enemyHeroes[0];
                    int best = 0, bestIdx = -1;
                    for (int i = 0; i < (int)enemy.army.size(); ++i)
                        if (enemy.army[i].count > best) { best = enemy.army[i].count; bestIdx = i; }
                    if (bestIdx >= 0) {
                        int lost = std::max(1, best / 6);
                        enemy.army[bestIdx].count -= lost;
                        lostTotal = lost;
                    }
                }
                m_weeklyEventHeadline = "Wandering Herd Stampede";
                m_weeklyEventBody = lostTotal > 0
                    ? "A wild beast stampede disrupts enemy camps, scattering " + std::to_string(lostTotal) + " soldiers."
                    : "A wild beast stampede sweeps across enemy territory.";
                break;
            }
            case 25: { // Ancient Discovery — hero gains +50 XP
                if (!m_heroes.empty()) {
                    Hero& h = m_heroes[m_activeHeroIdx];
                    int oldLvl = h.level;
                    if (h.addXp(50) && h.level > oldLvl) {
                        const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                        if (cls) {
                            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                            m_levelUpOffers = LevelUpSystem::generateOffers(*cls, h.skills, h.level, allSkills, h.faction);
                        }
                        if (m_levelUpOffers.empty())
                            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                        m_pendingLevelUps = h.level - oldLvl;
                        m_showLevelUpModal = true;
                    }
                }
                m_weeklyEventHeadline = "Ancient Discovery";
                m_weeklyEventBody = "Your scouts unearth inscriptions from a forgotten age. +50 XP for your hero.";
                break;
            }
            case 26: { // Trade Caravan — bonus resources
                m_playerResources.add(ResourceType::Gold, 350);
                m_playerResources.add(ResourceType::Iron, 2);
                m_playerResources.add(ResourceType::FaithStones, 1);
                m_weeklyEventHeadline = "Trade Caravan Passes";
                m_weeklyEventBody = "A laden merchant convoy pays toll to cross your lands: +350 Gold, +2 Iron, +1 Faith Stones.";
                break;
            }
            case 27: { // Supply Raid — choice: intercept or let pass
                m_weeklyEventHeadline = "Supply Convoy Spotted";
                m_weeklyEventBody = "Scouts report an enemy supply convoy moving through the region. You can intercept it.";
                m_weekChoiceOptions.clear();
                m_weekChoiceOptions.push_back({"Intercept (-200 Gold, hero -5 MP)", "+8 Iron, enemy hero loses 10 units", [this](){
                    if (m_playerResources.get(ResourceType::Gold) >= 200) {
                        m_playerResources.add(ResourceType::Gold, -200);
                        m_playerResources.add(ResourceType::Iron, 8);
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            h.movePool = std::max(0, h.movePool - 5);
                        }
                        if (!m_enemyHeroes.empty()) {
                            Hero& e = m_enemyHeroes[0];
                            for (auto& s : e.army) if (s.count > 0) { s.count = std::max(1, s.count - 10); break; }
                        }
                    }
                }});
                m_weekChoiceOptions.push_back({"Let it pass", "No action taken this week.", [](){}});
                break;
            }
        }

        ScriptContext ctx; ctx.heroId = 0;
        m_triggers.fire(TriggerType::WeekStart, ctx);
        if (m_turns.week() >= 10 && !m_hideout.isMilestoneComplete(Milestone::WEEK_10_REACHED))
            m_hideout.completeMilestone(Milestone::WEEK_10_REACHED);
        if (m_state == GameState::Campaign) {
            m_campaign.onWeekStart(m_turns.week(), m_lua);
            for (int rt = 0; rt < RESOURCE_COUNT; ++rt) {
                auto type = static_cast<ResourceType>(rt);
                m_campaign.onResourcesChecked(type, m_playerResources.get(type));
            }
        }
    }
    if (newWeek) {
        // Add weekly growth to unit dwellings
        for (auto& obj : m_worldObjects) {
            if (obj.type == WorldObjectType::UnitDwelling && !obj.collected) {
                int tier = obj.value;
                obj.available += 3 + tier;  // T1=4, T6=9 per week
                // Captured dwelling: +1 weekly growth of this tier in the
                // capturing player's own towns of the same faction. Was
                // "any AI town" (obj.linkedId used to get set from a hero
                // ID, which could never equal a real ownerId, so this
                // fell back to treating every AI player as one side) —
                // linkedId is a real ownerId now, so match it directly.
                if (obj.linkedId != 0) {
                    for (auto& t : m_towns) {
                        if (t.ownerId != obj.linkedId) continue;
                        if (static_cast<uint8_t>(t.faction) != obj.faction) continue;
                        for (auto& dw : t.dwellings)
                            if (dw.tier == tier) { dw.available += 1; break; }
                    }
                }
            }
            // Observatory resets (allow re-use each week)
            if (obj.type == WorldObjectType::Observatory)
                obj.collected = false;
            // HolyFountain / Oasis reset weekly
            if (obj.type == WorldObjectType::HolyFountain ||
                obj.type == WorldObjectType::Oasis)
                obj.collected = false;
            // Captured NeutralOutpost produces 4 T1 units per week
            if (obj.type == WorldObjectType::NeutralOutpost && obj.collected)
                obj.available += 4;
            // CursedGround: restore one charge per week (up to original max=5)
            if (obj.type == WorldObjectType::CursedGround && obj.questState < 5)
                obj.questState++;
        }

        // Auto-save at week start if enabled
        if (m_settingsAutoSave) saveGame();
    }
// Passive victory/defeat check: originally Watch-only ("needed when last
// enemy town captured without combat"), widened to plain single-player too —
// a normal single-player skirmish had NO automatic win/loss detection at all,
// only the opportunistic hotseat-elimination path when turns happened to
// advance past a dead player. This is also the hook for reporting
// skirmish-sourced Conquest quest progress: Conquest is the persistent
// out-of-game progression layer, and regular skirmish games are the actual
// play that should feed it (rather than requiring Conquest's own node map).
bool isPlainSingleplayer = (m_numHumanPlayers == 1 && !m_hotSeatMode && !m_watchingAI);
if ((m_watchingAI || isPlainSingleplayer) && !m_showVictory && !m_showDefeat) {
    bool noEnemyHeroes = m_enemyHeroes.empty();
    bool noEnemyTowns  = true;
    for (const auto& t : m_towns)
        if (t.ownerId > 1) { noEnemyTowns = false; break; }
    if (noEnemyHeroes && noEnemyTowns && !m_heroes.empty()) {
        m_showVictory = true;
        m_audio.playSound("victory");
    }
    if (!m_showVictory) {
        bool anyTown = false;
        for (const auto& t : m_towns) if (t.ownerId == 1) { anyTown = true; break; }
        bool anyArmy = !m_heroes.empty() && !m_heroes[m_activeHeroIdx].army.empty();
        if (!anyTown && !anyArmy) { m_showDefeat = true; m_finalDefeat = true; }
    }
    // Report skirmish quest progress exactly once per game, only for a real
    // (non-Watch) skirmish the human actually played.
    if (isPlainSingleplayer && (m_showVictory || m_showDefeat) && !m_skirmishQuestsReported) {
        m_skirmishQuestsReported = true;
        if (!m_conquest.active()) m_conquest.init(metaDbPath());
        m_conquest.reportEvent(QuestEvent::SkirmishPlayed);
        if (m_showVictory) {
            m_conquest.reportEvent(QuestEvent::SkirmishWonDifficulty, 1, m_newGameDifficulty);
            if (m_thisGameFactionWasRandom)
                m_conquest.reportEvent(QuestEvent::SkirmishWonRandomFaction);
        }
    }
}

// ── Hotseat: after full turn, regen non-P1 heroes and show "Player 1's Turn" ─
if (lastPlayerEndedTurn) {
    for (int pi = 1; pi < m_numHumanPlayers; ++pi) {
        auto& ps = m_players[pi];
        for (auto& h : ps.heroes) {
            h.movePool = h.maxMove;
            h.path.clear();
            h.pathStep = 0;
            int manaRegen = std::max(2, 2 + h.maxMana / 10);
            h.mana = std::min(h.maxMana, h.mana + manaRegen);
        }
        if (m_turns.day() == 1) {  // new week just started
            uint32_t pid = static_cast<uint32_t>(pi + 1);
            auto piIncome = m_turns.calculateWeeklyIncome(m_towns, pid);
            ps.resources.addAll(piIncome);
            for (const auto& r : m_resources)
                if (r.ownedBy == pid) ps.resources.add(r.type, mineYield(r));
            // Garrison upkeep
            int garrisonCount = 0;
            for (const auto& h : ps.heroes) if (h.isGarrisoned) ++garrisonCount;
            if (garrisonCount > 0)
                ps.resources.add(ResourceType::Gold, -(garrisonCount * 350));
            // Mirror weekly events (resource + silent hero-stat events only)
            {
                int evtRoll = ((m_turns.week() * 2654435761u) >> 8) % 28;
                auto piHero = [&]() -> Hero* {
                    if (ps.heroes.empty()) return nullptr;
                    int idx = std::min(ps.activeHeroIdx, (int)ps.heroes.size() - 1);
                    return &ps.heroes[idx];
                };
                switch (evtRoll) {
                    case 1:  ps.resources.add(ResourceType::Gold, 500); break;
                    case 2: { Hero* h = piHero();
                        if (h) for (int i = 0; i < SPELL_COUNT; ++i) {
                            int sid = ALL_SPELLS[i].id; bool known = false;
                            for (int s : h->knownSpells) if (s == sid) { known = true; break; }
                            if (!known) { h->knownSpells.push_back(sid); break; }
                        } break; }
                    case 3: { int lost = std::min(200, ps.resources.get(ResourceType::Gold));
                        ps.resources.add(ResourceType::Gold, -lost); break; }
                    case 4:  ps.resources.add(ResourceType::Gold, 200);
                             ps.resources.add(ResourceType::Iron, 3); break;
                    case 6: { Hero* h = piHero();
                        if (h) { h->maxMana = std::min(h->maxMana + 5, 99); h->mana = h->maxMana; } break; }
                    case 7: { Hero* h = piHero(); if (h) h->attack++; break; }
                    case 8: { Hero* h = piHero();
                        if (h) { int best = 0, bestIdx = -1;
                            for (int i = 0; i < (int)h->army.size(); ++i)
                                if (h->army[i].count > best) { best = h->army[i].count; bestIdx = i; }
                            if (bestIdx >= 0) h->army[bestIdx].count += 5; } break; }
                    case 10: ps.resources.add(ResourceType::Gold, 300);
                             ps.resources.add(ResourceType::FaithStones, 2);
                             ps.resources.add(ResourceType::VerdantSap, 2); break;
                    case 12: { Hero* h = piHero(); if (h) h->defense++; break; }
                    case 13: { Hero* h = piHero();
                        if (h) { int best = 0, bestIdx = -1;
                            for (int i = 0; i < (int)h->army.size(); ++i)
                                if (h->army[i].count > best) { best = h->army[i].count; bestIdx = i; }
                            if (bestIdx >= 0) h->army[bestIdx].count += 8; } break; }
                    case 14: ps.resources.add(ResourceType::Gold, 150); break;
                    case 15: ps.resources.add(ResourceType::Mercury, 2);
                             ps.resources.add(ResourceType::BloodEssence, 1); break;
                    case 16: { Hero* h = piHero(); if (h) h->mana = h->maxMana; break; }
                    case 18: { int lost = ps.resources.get(ResourceType::Gold) / 2;
                        ps.resources.add(ResourceType::Gold, -lost); break; }
                    case 19: { Hero* h = piHero();
                        if (h) { h->heroMaxHp += 15; h->heroHp = std::min(h->heroHp + 15, h->heroMaxHp); } break; }
                    default: break;
                }
            }
            // Store week summary to show at start of this player's next turn
            auto& notifs = m_playerNotifs[pi];
            notifs.weekIncome  = piIncome;
            for (const auto& r : m_resources)
                if (r.ownedBy == pid) notifs.weekIncome.add(r.type, mineYield(r));
            notifs.weekNum     = m_turns.week();
            notifs.weekSummary = true;
        }
    }
    m_showPlayerTurnBanner = true;
    m_playerTurnBannerT    = 2.5f;
}
}

void Game::renderPlayerTurnBanner()
{
    float alpha = std::min(1.0f, m_playerTurnBannerT);  // fade out in last second
    if (alpha <= 0.0f) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);

    // Full-width banner in the centre of the screen
    float bannerH = 90.0f;
    float bannerY = (sh - bannerH) * 0.5f;
    ImU32 bg = IM_COL32(10, 10, 30, static_cast<int>(200 * alpha));
    dl->AddRectFilled({0, bannerY}, {sw, bannerY + bannerH}, bg);
    dl->AddRect({0, bannerY}, {sw, bannerY + bannerH},
                IM_COL32(180, 160, 80, static_cast<int>(220 * alpha)), 0.0f, 0, 2.0f);

    char line1[48];
    std::snprintf(line1, sizeof(line1), "PLAYER %d", currentPlayerId());
    const char* line2 = "YOUR TURN";

    ImVec2 sz1 = ImGui::CalcTextSize(line1);
    ImVec2 sz2 = ImGui::CalcTextSize(line2);
    float cx = sw * 0.5f;
    float cy = bannerY + bannerH * 0.5f;

    dl->AddText({cx - sz1.x * 0.5f, cy - sz1.y - 2},
                IM_COL32(255, 220, 80, static_cast<int>(255 * alpha)), line1);
    dl->AddText({cx - sz2.x * 0.5f, cy + 2},
                IM_COL32(200, 200, 255, static_cast<int>(255 * alpha)), line2);
}

// ── World map render ──────────────────────────────────────────────────────────
void Game::renderWorldMapImGui()
{
    m_ui.beginFrame();
    {
        // m_heroes/m_playerResources always belong to the current player
        // (N-player handoff swaps them), so the HUD needs no special casing.
        m_worldHUD.draw(m_ui, m_playerResources, m_cachedWeeklyIncome,
                        m_turns, m_heroes, m_activeHeroIdx, m_towns);
    }
    m_ui.endFrame();
    m_ui.flushText(ImGui::GetBackgroundDrawList());
    renderWorldOverlay();

    // Non-game-state overlays (can coexist with most things)
    if (m_showWorldSpellPanel)    renderWorldSpellPanel();
    if (m_showKingdomPanel)       renderKingdomPanel();
    if (m_showTownPortalPopup)    renderTownPortalPopup();
    if (m_showFoundCityPopup)     renderFoundCityPopup();
    if (m_showHideoutScreen)      renderHideoutScreen();
    if (m_showArtifactPanel)      renderArtifactPanel();
    if (m_showHeroInspect)        renderHeroInspect();
    if (m_showUnitExchange)       renderUnitExchange();
    if (m_showDwellingPopup)      renderDwellingPopup();
    if (m_showStatShrinePopup)    renderStatShrinePopup();
    if (m_showQuestPopup)         renderQuestPopup();
    if (m_showTreasureChestPopup) renderTreasureChestPopup();
    if (m_showCryptPopup)         renderCryptPopup();
    if (m_showUtopiaPopup)        renderUtopiaPopup();
    if (m_showMineInfoPopup)      renderMineInfoPopup();
    if (m_showHeroSheetPopup)     renderHeroSheetPopup();
    if (m_showTreeKnowledgePopup) renderTreeOfKnowledgePopup();
    if (m_showShipyardPopup)      renderShipyardPopup();
    if (m_showMerchantPopup)      renderArtifactMerchantPopup();
    if (m_showArenaPopup)         renderArenaPopup();
    if (m_showEncounterPrompt)    renderEncounterPrompt();
    if (m_showTownLostPopup)      renderTownLostPopup();
    if (m_showWeekSummary)        renderWeekSummary();
    if (m_hotSeatHandoff)         renderHotSeatHandoff();
    if (m_showSiegeCampPrompt)    renderSiegeCampPrompt();
    if (m_showDefensePrepPopup)   renderDefensePrepPopup();
    renderSiegeIndicator();
    renderMarchButton();
    if (m_showPauseMenu)          renderPauseMenu();
    // Modal popups — only one at a time (ImGui popup stack conflict otherwise)
    // In campaign mode, victory/defeat are handled by the campaign HUD, not these modals.
    bool inCampaign = (m_state == GameState::Campaign);
    if (m_showCombatResult)                     renderCombatResultPopup();
    else if (m_showVictory && !inCampaign)      renderVictoryModal();
    else if (m_showDefeat  && !inCampaign)      renderDefeatModal();
    else if (m_showLevelUpModal)                renderLevelUpModal();

    // Watch AI overlay — minimal HUD showing current week + stop button
    if (m_watchingAI) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 8), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.80f);
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##watchai_hud", nullptr, wf)) {
            const auto& udefs = m_registry.units();
            ImGui::TextColored({1.f,0.82f,0.2f,1.f}, "WATCH AI vs AI");
            ImGui::SameLine(0, 16);
            // Show the day too — the watch overlay only had the week, so the
            // day counter the normal HUD shows "disappeared" the moment you
            // started spectating.
            ImGui::Text("Week %d, Day %d", m_turns.week(), m_turns.day());
            // One-click speed steps instead of a float slider. Dragging a
            // slider at 8x was effectively impossible: the AI turn eats the
            // frame, so you get a couple of frames a second and the drag never
            // registers. A button needs exactly one frame to hit.
            ImGui::SameLine(0, 16);
            ImGui::TextUnformatted("Speed");
            static const float kSpeeds[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
            static const char* kSpeedLbl[] = { "0.5x", "1x", "2x", "4x", "8x" };
            for (int i = 0; i < 5; ++i) {
                ImGui::SameLine(0, 4);
                bool active = (std::fabs(m_watchAISpeed - kSpeeds[i]) < 0.01f);
                if (active)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.85f, 1.0f));
                if (ImGui::Button(kSpeedLbl[i])) {
                    m_watchAISpeed = kSpeeds[i];
                    m_watchAITimer = 1.0f / m_watchAISpeed;
                }
                if (active) ImGui::PopStyleColor();
            }
            ImGui::SameLine(0, 12);
            if (ImGui::Button(m_watchAIPaused ? "Resume##waipause" : "Pause##waipause",
                              ImVec2(72, 0)))
                m_watchAIPaused = !m_watchAIPaused;
            ImGui::SameLine(0, 8);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop##waistop")) {
                // Pause first so no further turn is processed while the menu
                // tears down — stopping straight from 8x used to run another
                // full turn mid-transition and look like a hang.
                m_watchAIPaused = false;
                m_watchingAI    = false;
                m_fogDisabled   = false;
                m_watchAITimer  = 1.0f / std::max(0.25f, m_watchAISpeed);
                m_state = GameState::MainMenu;
            }
            ImGui::PopStyleColor();
            // Per-player strength/activity table (superseded the old
            // two-hero "player vs enemy" strength line).
            if (!m_heroes.empty() && !m_enemyHeroes.empty()) {
                ImGui::Separator();
                // Per-player summary — one line each so you can see who's active
                // vs idle. Rebuilt ONCE PER WEEK (cached) — computing it every
                // frame looped 8 players over all 4158 map resources and pegged
                // low-end CPUs.
                static const int pal[8][3] = {
                    {120,190,255},{255,100,100},{110,220,120},{235,200,70},
                    {200,120,255},{120,230,230},{255,150,70},{240,130,200}};
                auto rowColor = [&](uint32_t owner)->ImVec4{
                    int i=(int)((owner-1)%8);
                    return ImVec4(pal[i][0]/255.f,pal[i][1]/255.f,pal[i][2]/255.f,1.f);
                };
                if (m_watchSummaryWeek != m_turns.week()) {
                    m_watchSummaryWeek = m_turns.week();
                    m_watchSummary.clear();
                    std::vector<uint32_t> owners;
                    auto note=[&](uint32_t o){ if(o&&std::find(owners.begin(),owners.end(),o)==owners.end()) owners.push_back(o); };
                    note(1u);
                    for (const auto& h : m_enemyHeroes) note(h.ownerId);
                    for (const auto& t : m_towns) note(t.ownerId);
                    std::sort(owners.begin(), owners.end());
                    for (uint32_t o : owners) {
                        WatchPlayerRow row{o,0,0,0,0,0};
                        if (o == 1u) { for (const auto& h : m_heroes) { row.str+=heroStrength(h,udefs); row.heroes++; } }
                        else { for (const auto& h : m_enemyHeroes) if (h.ownerId==o){ row.str+=heroStrength(h,udefs); row.heroes++; } }
                        for (const auto& t : m_towns) if (t.ownerId==o) row.towns++;
                        for (const auto& r : m_resources) if (r.ownedBy==o) row.mines++;
                        row.gold = (o==1u)? m_playerResources.get(ResourceType::Gold)
                                          : aiResources(o).get(ResourceType::Gold);
                        m_watchSummary.push_back(row);
                    }
                }
                // Who's winning: the strongest army total. Tag that row so you
                // can see the leader at a glance instead of eyeballing numbers.
                uint32_t leaderOwner = 0; long long leaderStr = -1;
                for (const auto& row : m_watchSummary)
                    if (row.str > leaderStr) { leaderStr = row.str; leaderOwner = row.owner; }

                for (const auto& row : m_watchSummary) {
                    const char* pers = (row.owner==1u) ? "Watched"
                                     : aiPersonalityName(m_aiPersonality[std::min<uint32_t>(row.owner,9)]);
                    bool isLeader = (row.owner == leaderOwner);
                    ImGui::TextColored(rowColor(row.owner),
                        "%s P%u %-8s  Str:%lld  H:%d T:%d M:%d  Gold:%d%s",
                        isLeader ? ">" : " ",
                        row.owner, pers, row.str, row.heroes, row.towns, row.mines, row.gold,
                        isLeader ? "   <== STRONGEST" : "");
                }
            }
        }
        ImGui::End();
    }
}

void Game::renderWorldMap()
{
    // Ocean blue fills gaps at the circular map boundary (corners of screen have no hexes)
    glClearColor(0.04f, 0.12f, 0.30f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_hexRenderer.render(m_map, m_camera, m_hovered, m_selected, m_fogDisabled);

    float proj[16];
    m_camera.getMatrix(proj);
    m_batch.begin(proj);
    m_batch.end();

    for (auto& hero : m_heroes)
        drawHero(hero);
    for (auto& hero : m_enemyHeroes)
        drawHero(hero);

    beginImGuiFrame();
    renderWorldMapImGui();
    endImGuiFrame();
}

// ── State transition ──────────────────────────────────────────────────────────
void Game::enterWorldMap()
{
    m_state = GameState::WorldMap;
    gLog("Entered world map\n");
}

// ── Tile click ────────────────────────────────────────────────────────────────
void Game::onTileClicked(HexCoord h)
{
    const HexTile* tile = m_map.getTile(h);
    if (!tile) return;

    // (Hot-seat P2 uses the same path as everyone — m_heroes is swapped per player.)
    if (m_heroes.empty()) return;

    // Left-click on a player-owned town: open if hero is on/adjacent, else path there
    if (tile->townId != 0) {
        Hero& clickHero = m_heroes[m_activeHeroIdx];
        for (auto& t : m_towns) {
            if (t.id != tile->townId || t.ownerId != static_cast<uint32_t>(currentPlayerId())) continue;
            if (clickHero.pos == h || HexGrid::distance(clickHero.pos, h) <= 1) {
                enterTown(&t);
                return;
            }
            break; // far away — fall through to pathfinding
        }
    }

    Hero& hero = m_heroes[m_activeHeroIdx];
    if (!hero.canEnter(tile->terrain) || tile->blocked) return;
    if (m_moveT < 1.0f) return;

    auto costFn = [this, &hero](HexCoord c) -> int {
        const HexTile* t = m_map.getTile(c);
        if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
        int base = hero.moveCost(t->terrain);
        if (m_roadHexes.count(c)) base = std::max(1, base / 2);
        return base;
    };

    auto path = Pathfinder::find(m_map, hero.pos, h, costFn);
    if (path.empty()) return;

    hero.path     = path;
    hero.pathStep = 0;
    m_selected    = h;

    m_reachable = Pathfinder::reachable(m_map, hero.pos, costFn, hero.movePool);
}

// ── Hero movement ─────────────────────────────────────────────────────────────
void Game::updateHeroMovement(float dt)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];

    if (m_moveT < 1.0f) {
        m_moveT = std::min(1.0f, m_moveT + dt * MOVE_SPEED);
        if (m_moveT >= 1.0f)
            checkTileEvents();
        return;
    }

    if (!hero.path.empty() && hero.pathStep < static_cast<int>(hero.path.size())) {
        HexCoord next = hero.path[hero.pathStep];
        const HexTile* tile = m_map.getTile(next);
        int cost = tile ? hero.moveCost(tile->terrain) : 999;
        if (m_roadHexes.count(next)) cost = std::max(1, cost / 2);

        if (hero.movePool < cost) {
            hero.path.clear();
            hero.pathStep = 0;
            return;
        }

        float sx, sy, dx, dy;
        m_hexRenderer.grid().hexToWorld(hero.pos, sx, sy);
        m_hexRenderer.grid().hexToWorld(next, dx, dy);

        m_moveSrcX = sx; m_moveSrcY = sy;
        m_moveDstX = dx; m_moveDstY = dy;
        m_moveT    = 0.0f;

        // Update tile hero IDs for this player hero
        if (HexTile* oldT = m_map.getTile(hero.pos)) oldT->heroId = 0;
        hero.pos = next;
        hero.movePool -= cost;
        hero.pathStep++;
        if (HexTile* newT = m_map.getTile(hero.pos)) newT->heroId = hero.id;

        // Disembark when stepping from water onto land
        if (hero.onBoat && tile && tile->terrain != Terrain::Water)
            hero.onBoat = false;

        FogOfWar::updateVision(m_map, hero);

        if (hero.pathStep >= static_cast<int>(hero.path.size())) {
            hero.path.clear();
            hero.pathStep = 0;
            m_selected = {-999,-999};
        }
    }

    // (Hot-seat P2 movement animates through the normal m_heroes path above.)
}

// ── Tile events ───────────────────────────────────────────────────────────────
void Game::checkTileEvents()
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    const HexTile* tile = m_map.getTile(hero.pos);
    if (!tile) return;

    ScriptContext ctx;
    ctx.heroId     = static_cast<int>(hero.id);
    ctx.tileQ      = hero.pos.q;
    ctx.tileR      = hero.pos.r;
    ctx.playerSide = true;
    m_triggers.fireTileEnter(hero.pos, ctx);
    m_triggers.fire(TriggerType::EnterTile, ctx);

    if (m_state == GameState::Campaign)
        m_campaign.onTileReached(hero.pos);

    // ── Terrain traversal effects ────────────────────────────────────────────
    {
        // Count total army strength to scale danger
        int totalCount = 0;
        for (const auto& s : hero.army) totalCount += s.count;

        switch (tile->terrain) {
        case Terrain::Toxic: {
            // Poisonous vapors — kill one unit from the smallest non-empty stack
            int smallestCount = INT32_MAX, smallestIdx = -1;
            for (int i = 0; i < (int)hero.army.size(); ++i)
                if (hero.army[i].count > 0 && hero.army[i].count < smallestCount)
                    { smallestCount = hero.army[i].count; smallestIdx = i; }
            if (smallestIdx >= 0 && totalCount > 1) {
                hero.army[smallestIdx].count = std::max(0, hero.army[smallestIdx].count - 1);
                pushPickupEffect(hero.pos, "Toxic vapors — 1 unit lost!", IM_COL32(130, 200, 50, 255));
            }
            hero.mana = std::max(0, hero.mana - 1);
            break;
        }
        case Terrain::Volcanic: {
            // Lava heat — kill one unit from a random stack (not the last one)
            if (totalCount > 2 && !hero.army.empty()) {
                int idx = static_cast<int>((hero.pos.q * 31 + hero.pos.r * 17) % (int)hero.army.size());
                for (int i = 0; i < (int)hero.army.size(); ++i) {
                    int try_ = (idx + i) % (int)hero.army.size();
                    if (hero.army[try_].count > 0) {
                        hero.army[try_].count--;
                        pushPickupEffect(hero.pos, "Volcanic heat — 1 unit slain!", IM_COL32(220, 80, 30, 255));
                        break;
                    }
                }
            }
            break;
        }
        case Terrain::Corrupted:
        case Terrain::CorruptedForest:
            // Dark energy — mana drain
            if (hero.mana > 0) {
                hero.mana = std::max(0, hero.mana - 2);
                pushPickupEffect(hero.pos, "Corrupted — -2 mana", IM_COL32(160, 60, 200, 255));
            }
            break;
        case Terrain::Sacred:
            // Holy ground — restore 1 unit to weakest stack and heal hero HP
            if (!hero.army.empty()) {
                int leastCount = INT32_MAX, leastIdx = -1;
                for (int i = 0; i < (int)hero.army.size(); ++i)
                    if (hero.army[i].count > 0 && hero.army[i].count < leastCount)
                        { leastCount = hero.army[i].count; leastIdx = i; }
                if (leastIdx >= 0) hero.army[leastIdx].count++;
            }
            hero.heroHp = std::min(hero.heroMaxHp, hero.heroHp + 5);
            pushPickupEffect(hero.pos, "Sacred ground — healing", IM_COL32(200, 255, 180, 255));
            break;
        case Terrain::Industrial:
            // Machine district — passive gold income
            m_playerResources.add(ResourceType::Gold, 10);
            pushPickupEffect(hero.pos, "+10 Gold", IM_COL32(255, 215, 50, 255));
            break;
        default: break;
        }
    }

    // World objects (scrolls, chests, shrines, etc.)
    for (auto& obj : m_worldObjects) {
        if (obj.pos != hero.pos) continue;

        switch (obj.type) {
        case WorldObjectType::SpellScroll:
            if (!obj.collected) {
                obj.collected = true;
                bool already = false;
                for (int sid : hero.knownSpells) if (sid == obj.value) { already = true; break; }
                if (!already) {
                    hero.knownSpells.push_back(obj.value);
                    const SpellDef* sp = findSpell(obj.value);
                    char sBuf[64];
                    std::snprintf(sBuf, sizeof(sBuf), "Learned: %s!", sp ? sp->name : "Spell");
                    pushPickupEffect(obj.pos, sBuf, IM_COL32(180, 120, 255, 255));
                    m_audio.playSound("spell");
                    gLog("Hero learned spell %d from scroll\n", obj.value);
                }
            }
            break;
        case WorldObjectType::ArtifactChest:
            if (!obj.collected) {
                obj.collected = true;
                hero.artifactInventory.push_back(obj.value);
                pushPickupEffect(obj.pos, "Artifact found!", IM_COL32(255, 200, 80, 255));
                m_audio.playSound("pickup");
                gLog("Hero picked up artifact %d\n", obj.value);
            }
            break;
        case WorldObjectType::XPShrine:
            if (!obj.collected) {
                obj.collected = true;
                char xpBuf[32]; std::snprintf(xpBuf, sizeof(xpBuf), "+%d XP", obj.value);
                pushPickupEffect(obj.pos, xpBuf, IM_COL32(160, 255, 160, 255));
                m_audio.playSound("pickup");
                {
                    int oldLvlXP = hero.level;
                    if (hero.addXp(obj.value)) {
                        const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                        if (cls) {
                            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                            m_levelUpOffers = LevelUpSystem::generateOffers(
                                *cls, hero.skills, hero.level, allSkills, hero.faction);
                        }
                        if (m_levelUpOffers.empty())
                            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                        m_pendingLevelUps = hero.level - oldLvlXP;
                        m_showLevelUpModal = true;
                        m_audio.playSound("levelup");
                        { ScriptContext lvCtx; lvCtx.heroId = hero.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                    }
                }
                gLog("Hero gained %d XP from shrine\n", obj.value);
            }
            break;
        case WorldObjectType::ResourceCache:
            if (!obj.collected) {
                obj.collected = true;
                m_playerResources.add(obj.resourceType, obj.value);
                char resBuf[48]; std::snprintf(resBuf, sizeof(resBuf), "+%d %s",
                    obj.value, resourceName(obj.resourceType));
                pushPickupEffect(obj.pos, resBuf, IM_COL32(255, 215, 80, 255));
                m_audio.playSound("pickup");
                gLog("Hero found resource cache: %d %s\n", obj.value, resourceName(obj.resourceType));
            }
            break;
        case WorldObjectType::Observatory:
            if (!obj.collected) {
                obj.collected = true;  // will reset weekly
                // Reveal tiles in radius
                auto cells = HexGrid::range(hero.pos, obj.value);
                for (auto& c : cells) {
                    if (HexTile* t = m_map.getTile(c)) {
                        t->explored = true;
                        t->visible  = true;
                    }
                }
                pushPickupEffect(obj.pos, "Map revealed!", IM_COL32(220, 200, 120, 255));
                m_audio.playSound("pickup");
                gLog("Observatory: revealed %d tiles in radius %d\n",
                       static_cast<int>(cells.size()), obj.value);
            }
            break;
        case WorldObjectType::StatShrine:
            if (obj.questState > 0) {
                m_pendingObjId = obj.id;
                m_showStatShrinePopup = true;
            }
            break;
        case WorldObjectType::BanditCamp:
            if (!obj.collected) {
                int diff = obj.value;
                int weekScale = std::max(1, (m_turns.week() + 1) / 2); // doubles every 2 weeks
                Hero banditHero;
                banditHero.id      = 0;
                banditHero.name    = "Bandit Leader";
                banditHero.faction = FactionId::None;
                std::vector<CombatUnit> banditUnits;
                {
                    CombatUnit u;
                    u.id = 50; u.name = "Bandit"; u.count = 5 * diff * weekScale;
                    u.maxHp = u.hp = 5; u.attack = 2 + diff; u.defense = 1 + diff;
                    u.speed = 5; u.range = 0; u.shotsLeft = 0;
                    u.isPlayer = false;
                    banditUnits.push_back(u);
                    if (diff >= 2) {
                        CombatUnit u2;
                        u2.id = 51; u2.name = "Bandit Archer"; u2.count = 3 * diff * weekScale;
                        u2.maxHp = u2.hp = 4; u2.attack = 3; u2.defense = 1;
                        u2.speed = 4; u2.range = 4; u2.shotsLeft = u2.shots = 8;
                        u2.isPlayer = false;
                        banditUnits.push_back(u2);
                    }
                }
                // Build encounter description for prompt
                std::string desc = "Bandits x" + std::to_string(banditUnits[0].count);
                if (banditUnits.size() > 1)
                    desc += " + Archers x" + std::to_string(banditUnits[1].count);
                uint32_t objId = obj.id;
                m_encounterTitle        = "Bandit Camp (Difficulty " + std::to_string(diff) + ")";
                m_pendingEncounterHero  = banditHero;
                m_pendingEncounterUnits = banditUnits;
                m_encounterOnAccept = [this, objId]() {
                    for (auto& o : m_worldObjects) {
                        if (o.id != objId) continue;
                        o.collected = true;
                        m_lastBanditCampId     = objId;
                        m_lastCombatEnemyId    = 0;
                        m_pendingTownCaptureId = 0;
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                            enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                        }
                        break;
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) {
                        auto& h = m_heroes[m_activeHeroIdx];
                        h.path.clear(); h.pathStep = 0;
                    }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;
        case WorldObjectType::UnitDwelling:
            if (obj.available > 0) {
                m_pendingObjId = obj.id;
                m_showDwellingPopup = true;
            }
            break;
        case WorldObjectType::QuestGiver:
            if (obj.questState == 0) {
                m_pendingObjId = obj.id;
                m_showQuestPopup = true;
            } else if (obj.questState == 1) {
                // Check if QuestTarget was collected
                for (const auto& other : m_worldObjects) {
                    if (other.id == obj.linkedId && other.collected) {
                        // Quest complete — reward scales with hero level
                        const_cast<WorldObject&>(obj).questState = 2;
                        if (m_heroes.empty()) break;
                        Hero& qHero = m_heroes[m_activeHeroIdx];
                        int goldReward = 300 + qHero.level * 100;
                        // Bonus: rare resource or XP
                        int xpReward = 50 + qHero.level * 20;
                        m_playerResources.add(ResourceType::Gold, goldReward);
                        int oldLvlQ = qHero.level;
                        char qBuf[48];
                        std::snprintf(qBuf, sizeof(qBuf), "+%dg +%dXP Quest!", goldReward, xpReward);
                        pushPickupEffect(obj.pos, qBuf, IM_COL32(255, 215, 50, 255));
                        m_audio.playSound("levelup");
                        if (qHero.addXp(xpReward)) {
                            const HeroClassDef* cls = m_classRegistry.getClass(qHero.classId);
                            if (cls) {
                                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                m_levelUpOffers = LevelUpSystem::generateOffers(
                                    *cls, qHero.skills, qHero.level, allSkills, qHero.faction);
                            }
                            if (m_levelUpOffers.empty())
                                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                            m_pendingLevelUps = qHero.level - oldLvlQ;
                            m_showLevelUpModal = true;
                            { ScriptContext lvCtx; lvCtx.heroId = qHero.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                        }
                        gLog("Quest complete! Rewarded %d gold + %d XP\n", goldReward, xpReward);
                        break;
                    }
                }
            }
            break;
        case WorldObjectType::QuestTarget:
            if (!obj.collected) {
                obj.collected = true;
                pushPickupEffect(obj.pos, "Target reached!", IM_COL32(180, 255, 140, 255));
                for (auto& other : m_worldObjects) {
                    if (other.id == obj.linkedId) {
                        if (other.questState == 1)
                            gLog("Quest target reached! Return to quest giver.\n");
                        break;
                    }
                }
            }
            break;
        case WorldObjectType::ForestShrine:
            if (!obj.collected) {
                obj.collected = true;
                char buf[32]; std::snprintf(buf, sizeof(buf), "+%d XP", obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(120, 220, 120, 255));
                m_audio.playSound("pickup");
                {
                    int oldLvlFS = hero.level;
                    if (hero.addXp(obj.value)) {
                        const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                        if (cls) {
                            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                            m_levelUpOffers = LevelUpSystem::generateOffers(
                                *cls, hero.skills, hero.level, allSkills, hero.faction);
                        }
                        if (m_levelUpOffers.empty())
                            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                        m_pendingLevelUps = hero.level - oldLvlFS;
                        m_showLevelUpModal = true;
                        { ScriptContext lvCtx; lvCtx.heroId = hero.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                    }
                }
            }
            break;
        case WorldObjectType::HighlandRuin:
            if (!obj.collected) {
                obj.collected = true;
                auto cells = HexGrid::range(hero.pos, obj.value);
                for (auto& c : cells) {
                    if (HexTile* t = m_map.getTile(c)) { t->explored = true; t->visible = true; }
                }
                pushPickupEffect(obj.pos, "Revealed!", IM_COL32(200, 180, 120, 255));
            }
            break;
        case WorldObjectType::HolyFountain:
            if (!obj.collected) {
                obj.collected = true;
                hero.mana = hero.maxMana;
                pushPickupEffect(obj.pos, "Mana restored!", IM_COL32(100, 180, 255, 255));
                m_audio.playSound("spell");
            }
            break;
        case WorldObjectType::Oasis:
            if (!obj.collected) {
                obj.collected = true;
                hero.movePool = hero.maxMove;
                pushPickupEffect(obj.pos, "Movement!", IM_COL32(160, 220, 100, 255));
                m_audio.playSound("pickup");
            }
            break;
        case WorldObjectType::Campfire:
            if (!obj.collected) {
                obj.collected = true;
                m_playerResources.add(ResourceType::Gold, obj.value);
                char buf[32]; std::snprintf(buf, sizeof(buf), "+%d Gold", obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(255, 215, 0, 255));
                m_audio.playSound("pickup");
            }
            break;
        case WorldObjectType::LavaCrystal:
            if (!obj.collected) {
                obj.collected = true;
                m_playerResources.add(obj.resourceType, obj.value);
                char resBuf2[32]; std::snprintf(resBuf2, sizeof(resBuf2), "+%d %s",
                    obj.value, resourceName(obj.resourceType));
                pushPickupEffect(obj.pos, resBuf2, IM_COL32(200, 80, 80, 255));
                m_audio.playSound("pickup");
            }
            break;
        case WorldObjectType::SwampAltar:
            if (!obj.collected) {
                obj.collected = true;
                bool already = false;
                for (int sid : hero.knownSpells) if (sid == obj.value) { already = true; break; }
                if (!already) hero.knownSpells.push_back(obj.value);
                pushPickupEffect(obj.pos, "Spell learned!", IM_COL32(180, 100, 255, 255));
                m_audio.playSound("spell");
            }
            break;
        case WorldObjectType::TreasureChest:
            if (!obj.collected) {
                // Floor sub-minimum values (old saves, editor-placed chests)
                if (obj.value < 500)
                    obj.value = 500 + hero.level * 50;
                if (obj.questState < 300)
                    obj.questState = 300 + hero.level * 30;
                m_pendingChestId          = obj.id;
                m_showTreasureChestPopup  = true;
                // Don't mark collected yet — the popup choice does that
            }
            break;

        case WorldObjectType::Crypt:
            if (!obj.collected) {
                Hero cryptHero;
                cryptHero.id      = 0;
                cryptHero.name    = "Crypt Keeper";
                cryptHero.faction = static_cast<FactionId>(obj.faction % 9);
                int diff = std::max(1, obj.value);
                float wm = std::min(3.0f, 1.0f + (m_turns.week() - 1) * 0.15f);
                std::vector<CombatUnit> cryptUnits;
                {
                    static const char* kCryptNames[] = {
                        "Skeleton Warrior","Zombie","Cursed Knight","Wraithling","Bone Golem"
                    };
                    for (int si = 0; si < 3; ++si) {
                        CombatUnit cu;
                        cu.name    = kCryptNames[(obj.faction + si) % 5];
                        cu.count   = static_cast<int>(std::round((4 + si * 2) * diff * wm));
                        cu.maxHp   = cu.hp = 5 + si * 4;
                        cu.attack  = 2 + si + diff;
                        cu.defense = 1 + si + diff / 2;
                        cu.speed   = 5 - si;
                        if (si == 2) { cu.range = 3; cu.shots = cu.shotsLeft = 5; }
                        cu.isPlayer = false;
                        cryptUnits.push_back(cu);
                    }
                }
                uint32_t objId = obj.id;
                m_encounterTitle        = std::string("Crypt (") + cryptHero.name + ")";
                m_pendingEncounterHero  = cryptHero;
                m_pendingEncounterUnits = cryptUnits;
                m_encounterOnAccept = [this, objId]() {
                    m_pendingCryptId       = objId;
                    m_lastCombatEnemyId    = 0;
                    m_pendingTownCaptureId = 0;
                    m_lastBanditCampId     = 0;
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                        enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;

        case WorldObjectType::PandoraBox:
            if (!obj.collected) {
                Hero boxHero;
                boxHero.id      = 0;
                boxHero.name    = "Box Guardians";
                boxHero.faction = FactionId::Voidkin;
                // Scales harder than a Crypt and uncapped — this is late-game content
                float wm = 1.0f + (m_turns.week() - 1) * 0.18f;
                static const char* kBoxNames[] = {
                    "Chaos Spawn","Rift Horror","Warp Fiend","Abyss Watcher"
                };
                std::vector<CombatUnit> boxUnits;
                for (int si = 0; si < 4; ++si) {
                    CombatUnit cu;
                    cu.name    = kBoxNames[si];
                    cu.count   = static_cast<int>(std::round((6 + si * 3) * wm));
                    cu.maxHp   = cu.hp = 25 + si * 8;
                    cu.attack  = 10 + si * 3;
                    cu.defense = 8 + si * 2;
                    cu.speed   = 5 + si;
                    if (si == 1) { cu.range = 4; cu.shots = cu.shotsLeft = 4; }
                    cu.isPlayer = false;
                    boxUnits.push_back(cu);
                }
                uint32_t objId = obj.id;
                m_encounterTitle        = "Pandora's Box";
                m_pendingEncounterHero  = boxHero;
                m_pendingEncounterUnits = boxUnits;
                m_encounterOnAccept = [this, objId]() {
                    m_pendingPandoraId     = objId;
                    m_pendingCryptId       = 0;
                    m_pendingUtopiaId      = 0;
                    m_lastCombatEnemyId    = 0;
                    m_pendingTownCaptureId = 0;
                    m_lastBanditCampId     = 0;
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                        enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;

        case WorldObjectType::Utopia:
            if (!obj.collected) {
                Hero utopiaHero;
                utopiaHero.id      = 0;
                utopiaHero.name    = "Ancient Guardian";
                utopiaHero.faction = static_cast<FactionId>(obj.faction % 9);
                static const char* kUtoNames[] = {
                    "Titan","Dragon","Archon","Behemoth","Leviathan",
                    "Void Lord","Colossus","Flesh Titan","Eternal"
                };
                float wm = std::min(3.0f, 1.0f + (m_turns.week() - 1) * 0.12f);
                std::vector<CombatUnit> utoUnits;
                // obj.value == -1 marks a tutorial Utopia (weakened guards)
                bool tutorialUtopia = (obj.value == -1);
                if (tutorialUtopia) {
                    CombatUnit c1; c1.id=50; c1.name="Ruin Warden";   c1.count=6; c1.maxHp=c1.hp=18; c1.attack=6; c1.defense=4; c1.speed=4; c1.isPlayer=false; utoUnits.push_back(c1);
                    CombatUnit c2; c2.id=51; c2.name="Ruin Sentinel"; c2.count=4; c2.maxHp=c2.hp=28; c2.attack=9; c2.defense=6; c2.speed=3; c2.isPlayer=false; utoUnits.push_back(c2);
                } else {
                for (int si = 0; si < 4; ++si) {
                    CombatUnit cu;
                    cu.name    = kUtoNames[(obj.faction + si) % 9];
                    cu.count   = static_cast<int>(std::round((8 + si * 3) * wm));
                    cu.maxHp   = cu.hp = 40 + si * 10;
                    cu.attack  = 18 + si * 4;
                    cu.defense = 14 + si * 3;
                    cu.speed   = 6 + si;
                    if (si >= 2) { cu.flying = true; }
                    if (si == 3) { cu.range = 5; cu.shots = cu.shotsLeft = 4; }
                    cu.isPlayer = false;
                    utoUnits.push_back(cu);
                }
                }
                uint32_t objId = obj.id;
                m_encounterTitle        = "Utopia (Ancient Guardian)";
                m_pendingEncounterHero  = utopiaHero;
                m_pendingEncounterUnits = utoUnits;
                m_encounterOnAccept = [this, objId]() {
                    m_pendingUtopiaId      = objId;
                    m_lastCombatEnemyId    = 0;
                    m_pendingTownCaptureId = 0;
                    m_lastBanditCampId     = 0;
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                        enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;

        case WorldObjectType::Landmark:
            if (!obj.collected) {
                obj.collected = true;
                // Gain XP from visiting this historically significant site
                static const char* kLandmarkNames[] = {
                    "Ancient Monolith","Ruined Temple","Forgotten Citadel",
                    "Crumbling Observatory","Sunken Shrine"
                };
                int nameIdx = (obj.pos.q * 7 + obj.pos.r * 3) % 5;
                char buf[80];
                std::snprintf(buf, sizeof(buf), "%s: +%d XP",
                    kLandmarkNames[nameIdx], obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(220, 200, 120, 255));
                m_audio.playSound("pickup");
                int oldLvl = hero.level;
                if (hero.addXp(obj.value)) {
                    const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                    if (cls) {
                        std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                        m_levelUpOffers = LevelUpSystem::generateOffers(
                            *cls, hero.skills, hero.level, allSkills, hero.faction);
                    }
                    if (m_levelUpOffers.empty())
                        m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                    m_pendingLevelUps = hero.level - oldLvl;
                    m_showLevelUpModal = true;
                    m_audio.playSound("levelup");
                    ScriptContext lvCtx; lvCtx.heroId = hero.id;
                    m_triggers.fire(TriggerType::HeroLevel, lvCtx);
                }
            }
            break;

        case WorldObjectType::CursedGround:
            if (obj.questState > 0) {
                // Each crossing triggers a curse charge
                obj.questState--;
                int totalArmy = 0;
                for (const auto& s : hero.army) totalArmy += s.count;
                if (totalArmy > 1) {
                    // Kill units proportional to damage value
                    int dmg = obj.value;
                    for (auto& s : hero.army) {
                        if (s.count > 0 && dmg > 0) {
                            int kill = std::min(s.count - (totalArmy > s.count ? 0 : 1), dmg / 10);
                            kill = std::max(0, kill);
                            s.count -= kill;
                            dmg -= kill * 10;
                        }
                    }
                }
                char buf[48];
                std::snprintf(buf, sizeof(buf), "Cursed! -%d HP (charges: %d)",
                    obj.value, obj.questState);
                pushPickupEffect(obj.pos, buf, IM_COL32(160, 40, 200, 255));
                m_audio.playSound("hit");
            }
            break;

        case WorldObjectType::NeutralOutpost:
            if (!obj.collected) {
                Hero outpostHero;
                outpostHero.id      = 0;
                outpostHero.name    = "Outpost Guard";
                outpostHero.faction = static_cast<FactionId>(obj.faction % 9);
                float wm = std::min(2.5f, 1.0f + (m_turns.week() - 1) * 0.12f);
                std::vector<CombatUnit> outUnits;
                {
                    CombatUnit ou;
                    ou.name    = "Outpost Sentry";
                    ou.count   = static_cast<int>(std::round((6 + obj.value * 3) * wm));
                    ou.maxHp   = ou.hp = 5 + obj.value * 2;
                    ou.attack  = 2 + obj.value;
                    ou.defense = 1 + obj.value;
                    ou.speed   = 4;
                    ou.isPlayer = false; ou.factionHint = obj.faction;
                    outUnits.push_back(ou);
                    CombatUnit ou2;
                    ou2.name   = "Outpost Archer";
                    ou2.count  = static_cast<int>(std::round((3 + obj.value * 2) * wm));
                    ou2.maxHp  = ou2.hp = 4;
                    ou2.attack = 3; ou2.defense = 1;
                    ou2.speed  = 5; ou2.range = 4;
                    ou2.shots  = ou2.shotsLeft = 8;
                    ou2.isPlayer = false; ou2.factionHint = obj.faction;
                    outUnits.push_back(ou2);
                }
                uint32_t objId = obj.id;
                m_encounterTitle        = "Neutral Outpost";
                m_pendingEncounterHero  = outpostHero;
                m_pendingEncounterUnits = outUnits;
                m_encounterOnAccept = [this, objId]() {
                    m_pendingNeutralOutpostId = objId;
                    m_lastCombatEnemyId       = 0;
                    m_pendingTownCaptureId    = 0;
                    m_lastBanditCampId        = 0;
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                        enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            } else {
                // Already captured — check ownership
                bool ownsOutpost = (obj.linkedId == 0 ||
                                    obj.linkedId == static_cast<uint32_t>(currentPlayerId()));
                if (!ownsOutpost && m_numHumanPlayers >= 2) {
                    // Enemy player's outpost — offer recapture fight
                    Hero outpostHero;
                    outpostHero.id      = 0;
                    outpostHero.name    = "Outpost Guard";
                    outpostHero.faction = static_cast<FactionId>(obj.faction % 9);
                    float wm = std::min(2.5f, 1.0f + (m_turns.week() - 1) * 0.12f);
                    std::vector<CombatUnit> outUnits;
                    {
                        CombatUnit ou;
                        ou.name    = "Outpost Sentry";
                        ou.count   = static_cast<int>(std::round((4 + obj.value * 2) * wm));
                        ou.maxHp   = ou.hp = 5 + obj.value * 2;
                        ou.attack  = 2 + obj.value; ou.defense = 1 + obj.value;
                        ou.speed   = 4; ou.isPlayer = false; ou.factionHint = obj.faction;
                        outUnits.push_back(ou);
                    }
                    uint32_t objId = obj.id;
                    m_encounterTitle        = "Enemy Outpost";
                    m_pendingEncounterHero  = outpostHero;
                    m_pendingEncounterUnits = outUnits;
                    m_encounterOnAccept = [this, objId]() {
                        m_pendingNeutralOutpostId = objId;
                        m_lastCombatEnemyId       = 0;
                        m_pendingTownCaptureId    = 0;
                        m_lastBanditCampId        = 0;
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                            enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                        }
                    };
                    m_encounterOnDecline = [this]() {
                        if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                    };
                    m_showEncounterPrompt = true;
                    return;
                } else if (obj.available > 0 && ownsOutpost) {
                    // Produce T1 units weekly (handled via obj.available)
                    pushPickupEffect(obj.pos, "Outpost: recruit available!", IM_COL32(180, 255, 140, 255));
                    m_pendingObjId = obj.id;
                    m_showDwellingPopup = true;
                }
            }
            break;

        case WorldObjectType::WitchHut:
            {
                // questState holds the skillId this hut teaches; never permanently collected
                int skillId = obj.questState;
                const SkillDef* sd = findSkillDef(skillId);
                const char* sname = sd ? sd->name.c_str() : "Unknown";
                if (hero.skills.hasSkill(skillId)) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "You know: %s", sname);
                    pushPickupEffect(obj.pos, buf, IM_COL32(180, 120, 255, 255));
                } else if (!hero.skills.canLearn(skillId)) {
                    pushPickupEffect(obj.pos, "Skill slots full", IM_COL32(150, 150, 150, 255));
                } else {
                    hero.skills.learn(skillId);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "Learned: %s!", sname);
                    pushPickupEffect(obj.pos, buf, IM_COL32(200, 100, 255, 255));
                    m_audio.playSound("levelup");
                }
            }
            break;

        case WorldObjectType::Stables:
            if (!obj.collected) {
                obj.collected = true;
                hero.maxMove  += obj.value;
                hero.movePool = std::min(hero.movePool + obj.value, hero.maxMove);
                char buf[48];
                std::snprintf(buf, sizeof(buf), "+%d Max Move!", obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(200, 160, 80, 255));
                m_audio.playSound("pickup");
            }
            break;

        case WorldObjectType::TreeOfKnowledge:
            if (!obj.collected) {
                m_pendingTreeId          = obj.id;
                m_showTreeKnowledgePopup = true;
            }
            break;
        case WorldObjectType::Barrier:
            break;  // impassable — hero cannot enter this tile anyway

        case WorldObjectType::ChokeGuard:
        {
            if (!obj.collected) {
                Hero guardHero;
                guardHero.id      = 0;
                guardHero.name    = "Pass Guardian";
                guardHero.faction = FactionId::None;
                std::vector<CombatUnit> guardUnits;
                // obj.value==1 → stronger variant (centre-entry guards in Jebus 3.0)
                int stacks    = 5;
                int perStack  = (obj.value == 1) ? 30 : 20;
                int atk       = (obj.value == 1) ? 22 : 18;
                int def       = (obj.value == 1) ? 17 : 14;
                int hp        = (obj.value == 1) ? 280 : 200;
                for (int s = 0; s < stacks; ++s) {
                    CombatUnit u;
                    u.id = 60 + s;  u.name = "Ancient Guardian";
                    u.count = perStack; u.maxHp = u.hp = hp;
                    u.attack = atk;  u.defense = def;  u.speed = 5;
                    u.range = 0;     u.shotsLeft = 0;  u.isPlayer = false;
                    guardUnits.push_back(u);
                }
                uint32_t objId = obj.id;
                m_encounterTitle       = "Pass Guardian";
                m_pendingEncounterHero = guardHero;
                m_pendingEncounterUnits = guardUnits;
                m_encounterOnAccept = [this, objId]() {
                    for (auto& o : m_worldObjects) {
                        if (o.id != objId) continue;
                        o.collected          = true;
                        m_lastBanditCampId   = objId;
                        m_lastCombatEnemyId  = 0;
                        m_pendingTownCaptureId = 0;
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                            enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                        }
                        break;
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;
        }

        case WorldObjectType::Shipyard:
        {
            if (!obj.collected) {
                // Show build-boat popup (handled in ImGui popup section)
                m_pendingObjId      = obj.id;
                m_showShipyardPopup = true;
            }
            break;
        }

        case WorldObjectType::FishingHouse:
            // passive income — no interaction when stepped on
            break;

        // ── Naval objects: only reachable, and only meaningful, from a boat ──
        case WorldObjectType::Flotsam: {
            if (obj.collected || m_heroes.empty()) break;
            Hero& h = m_heroes[m_activeHeroIdx];
            obj.collected = true;
            int gold = 300 + (obj.value % 700);
            m_playerResources.add(ResourceType::Gold, gold);
            // Salvage also coughs up a little of one special resource.
            auto res = static_cast<ResourceType>(1 + (obj.value % (RESOURCE_COUNT - 1)));
            int amt  = 1 + (obj.value % 3);
            m_playerResources.add(res, amt);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Salvage: +%dg +%d res", gold, amt);
            pushPickupEffect(h.pos, buf, IM_COL32(255, 225, 120, 255));
            break;
        }

        case WorldObjectType::Shipwreck:
        case WorldObjectType::SeaMonsterLair: {
            if (obj.collected || m_heroes.empty()) break;
            Hero& h = m_heroes[m_activeHeroIdx];
            bool lair = (obj.type == WorldObjectType::SeaMonsterLair);
            // Guarded: the player fights for it. Reuse the generic guarded-site
            // combat the Crypt/Utopia sites use.
            m_pendingObjId = obj.id;
            m_showUtopiaPopup = false;
            obj.collected = true;   // resolved off the map either way
            int gold = lair ? 1200 + (obj.value % 900) : 800 + (obj.value % 700);
            int xp   = lair ? 600 + m_turns.week() * 40 : 350 + m_turns.week() * 25;
            m_playerResources.add(ResourceType::Gold, gold);
            int oldLevel = h.level;
            h.addXp(xp);
            if (h.level > oldLevel) m_showLevelUpModal = true;
            char buf[72];
            std::snprintf(buf, sizeof(buf), "%s cleared: +%dg +%d XP",
                          lair ? "Lair" : "Wreck", gold, xp);
            pushPickupEffect(h.pos, buf, IM_COL32(180, 240, 255, 255));
            break;
        }

        case WorldObjectType::Lighthouse: {
            if (m_heroes.empty()) break;
            Hero& h = m_heroes[m_activeHeroIdx];
            uint32_t me = static_cast<uint32_t>(currentPlayerId());
            if (obj.faction == static_cast<int>(me)) break;   // already ours
            obj.faction   = static_cast<int>(me);             // capture it
            obj.collected = true;
            refreshLighthouseBoosts();
            pushPickupEffect(h.pos, "Lighthouse captured — faster sailing!",
                             IM_COL32(255, 245, 180, 255));
            break;
        }

        case WorldObjectType::ArtifactMerchant:
            m_merchantSeed      = obj.value;
            m_showMerchantPopup = true;
            break;

        case WorldObjectType::Arena: {
            if (!m_heroes.empty()) {
                Hero& h = m_heroes[m_activeHeroIdx];
                if (obj.questState == static_cast<int>(h.id)) break;
                m_showArenaPopup = true;
                m_pendingObjId   = obj.id;
            }
            break;
        }

        case WorldObjectType::ExperienceWell:
            if (!obj.collected && !m_heroes.empty()) {
                obj.collected = true;
                int xp = 500 + m_turns.week() * 100;
                Hero& h = m_heroes[m_activeHeroIdx];
                char buf[32]; std::snprintf(buf, sizeof(buf), "+%d XP (Well)", xp);
                pushPickupEffect(h.pos, buf, IM_COL32(160, 255, 160, 255));
                int oldLevel = h.level;
                if (h.addXp(xp)) {
                    int levelsGained = h.level - oldLevel;
                    const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                    if (cls) {
                        std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                        m_levelUpOffers = LevelUpSystem::generateOffers(*cls, h.skills, h.level, allSkills, h.faction);
                    }
                    if (m_levelUpOffers.empty())
                        m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                    m_pendingLevelUps = levelsGained;
                    m_showLevelUpModal = true;
                }
                gLog("Experience Well: +%d XP\n", xp);
            }
            break;
        }
    }

    // Resource node — claim mine (guards if unbeaten)
    if (tile->resourceId != 0) {
        for (auto& r : m_resources) {
            if (r.id != tile->resourceId || r.ownedBy == static_cast<uint32_t>(currentPlayerId())) continue;
            if (!r.guardBeaten) {
                // Mine is guarded — show encounter prompt before committing
                Hero guardHero;
                guardHero.id      = 0;
                guardHero.name    = "Mine Guardian";
                guardHero.faction = FactionId::None;
                std::vector<CombatUnit> guardUnits = makeMineGuardUnits(r, m_turns.week());
                m_encounterTitle        = std::string("Mine Guardian (") + resourceName(r.type) + " Mine)";
                m_pendingEncounterHero  = guardHero;
                m_pendingEncounterUnits = guardUnits;
                uint32_t resId = r.id;
                m_encounterOnAccept = [this, resId]() {
                    m_pendingMineId        = resId;
                    m_lastCombatEnemyId    = 0;
                    m_pendingTownCaptureId = 0;
                    m_lastBanditCampId     = 0;
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                        enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            // Guards beaten (or already ours) — capture
            r.ownedBy = static_cast<uint32_t>(currentPlayerId());
            m_playerResources.add(r.type, r.amount);
            m_cachedWeeklyIncome.add(r.type, r.amount);
            char mineBuf[48];
            std::snprintf(mineBuf, sizeof(mineBuf), "+%d %s/week", r.amount, resourceName(r.type));
            pushPickupEffect(hero.pos, mineBuf, IM_COL32(255, 220, 80, 255));
            m_audio.playSound("buy");
            gLog("Claimed mine: +%d %s/week\n", r.amount, resourceName(r.type));
            break;
        }
    }

    // Town entry / capture
    if (tile->townId != 0) {
        for (auto& t : m_towns) {
            if (t.id != tile->townId) continue;
            if (t.ownerId != 1) {
                // If player is adjacent (not ON the town yet) and town has garrison → offer Siege or Attack
                bool alreadyCamping = false;
                for (const auto& h2 : m_heroes)
                    if (h2.isSiegeCamping && h2.siegeTargetTownId == t.id) { alreadyCamping = true; break; }

                if (!t.garrison.empty() && !alreadyCamping
                    && HexGrid::distance(hero.pos, t.pos) == 1) {
                    // Offer: attack now OR lay siege camp
                    m_siegePromptTownId     = t.id;
                    m_showSiegeCampPrompt   = true;
                    return;
                }

                // Fight the garrison if one exists (direct attack or hero already on town tile)
                if (!t.garrison.empty()) {
                    // Build garrison CombatUnits as the "enemy"
                    Hero garrisonHero; // dummy hero for the garrison
                    garrisonHero.id     = 0;
                    garrisonHero.name   = t.name + " Garrison";
                    garrisonHero.faction = t.faction;
                    garrisonHero.army   = t.garrison;
                    m_lastCombatEnemyId = 0; // no real enemy hero
                    m_pendingTownCaptureId = t.id;
                    auto pUnits = makeHeroUnits(hero, m_registry.units(), true);
                    auto gUnits = makeHeroUnits(garrisonHero, m_registry.units(), false);
                    enterCombat(hero, pUnits, garrisonHero, gUnits);
                    return;
                }
                // No garrison — capture immediately
                uint32_t prevOwner = t.ownerId;
                t.ownerId = 1;
                t.garrison.clear();
                gLog("Captured town: %s\n", t.name.c_str());
                steam::unlockAchievement("ACH_FIRST_TOWN");
                m_capturedTownName = t.name;
                m_showCapturePopup = true;
                m_hideout.completeMilestone(Milestone::FIRST_TOWN_CAPTURED);
                {
                    ScriptContext tCtx;
                    tCtx.townId = t.id;
                    m_triggers.fire(TriggerType::TownCaptured, tCtx);
                }
                m_campaign.onTownCaptured(t.id, prevOwner);
            }
            // Watch AI has no one to click through the town UI — opening it
            // here would hard-freeze the sim (m_state leaves WorldMap and
            // the auto-dismiss-modals loop above never runs again).
            if (!m_watchingAI) enterTown(&t);
            return;
        }
    }

    // Hero collision — player meets player → unit exchange; player meets enemy → combat
    // NOTE: tile->heroId is already overwritten with the player's own id at this point,
    // so we check by position rather than by heroId.
    {
        // Check own heroes (by position) → unit exchange between your own armies
        for (int i = 0; i < static_cast<int>(m_heroes.size()); ++i) {
            if (i == m_activeHeroIdx) continue;
            if (m_heroes[i].pos == hero.pos) {
                m_showUnitExchange    = true;
                m_exchangeHeroIdx     = i;
                m_exchangeSelSide     = -1;
                m_exchangeSelSlot     = -1;
                m_exchangeSplitMode   = false;
                m_exchangeSelArtifactSide = -1;
                m_exchangeSelArtifactIdx  = -1;
                return;
            }
        }
        // Another human player's hero (hot-seat) → combat, they're an opponent
        if (m_numHumanPlayers >= 2) {
            for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
                if (pi == m_currentPlayerIdx) continue;
                if (isAllied(static_cast<uint32_t>(pi + 1), static_cast<uint32_t>(currentPlayerId()))) continue;
                for (auto& oh : m_players[pi].heroes) {
                    if (oh.pos != hero.pos) continue;
                    if (HexTile* et = m_map.getTile(oh.pos)) et->heroId = 0;
                    m_pendingCryptId          = 0;
                    m_pendingUtopiaId         = 0;
                    m_pendingMineId           = 0;
                    m_pendingNeutralOutpostId = 0;
                    m_lastBanditCampId        = 0;
                    m_pendingTownCaptureId    = 0;
                    m_lastCombatEnemyId  = oh.id;
                    m_lastCombatHumanIdx = pi;
                    auto pUnits = makeHeroUnits(hero, m_registry.units(), true);
                    auto eUnits = makeHeroUnits(oh, m_registry.units(), false);
                    enterCombat(hero, pUnits, oh, eUnits);
                    return;
                }
            }
        }
        // Enemy AI hero (by position) — skip an ally, never a hostile target
        Hero* enemyPtr = nullptr;
        for (auto& e : m_enemyHeroes) {
            if (e.pos != hero.pos) continue;
            if (isAllied(e.ownerId, static_cast<uint32_t>(currentPlayerId()))) continue;
            enemyPtr = &e; break;
        }
        if (enemyPtr) {
            // Move enemy off this tile so the player can stand here after combat
            if (HexTile* et = m_map.getTile(enemyPtr->pos)) et->heroId = 0;
            // Clear encounter-specific pending IDs from any prior unresolved fight.
            m_pendingCryptId          = 0;
            m_pendingUtopiaId         = 0;
            m_pendingMineId           = 0;
            m_pendingNeutralOutpostId = 0;
            m_lastBanditCampId        = 0;
            m_pendingTownCaptureId    = 0;
            m_lastCombatEnemyId  = enemyPtr->id;
            m_lastCombatHumanIdx = -1;
            auto pUnits = makeHeroUnits(hero, m_registry.units(), true);
            auto eUnits = makeHeroUnits(*enemyPtr, m_registry.units(), false);
            enterCombat(hero, pUnits, *enemyPtr, eUnits);
        }
    }
}

void Game::drawHero(const Hero& hero)
{
    // Hero markers (circles + name labels) are drawn in renderWorldOverlay()
    // via ImGui's background DrawList, with animated position for the active hero.
    // This function is reserved for sprite-batch rendering once a tileset exists.
    (void)hero;
}

// ── World entity overlay (ImGui DrawList markers) ─────────────────────────────
void Game::renderWorldOverlay()
{
    if (!m_imguiReady) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Clip every world-entity marker below to the play area, so mine/resource
    // icons and hero markers never paint over the top bar, the bottom action
    // bar, or the right-side hero/town panels. The overlay renders on the same
    // background draw list AFTER the HUD, so without this they draw on top of
    // it (the "why are these on top of the HUD" bug). Popped before the minimap
    // block at the end, which manages its own clip.
    dl->PushClipRect({0.0f, 68.0f},
                     { static_cast<float>(m_width)  - 185.0f,
                       static_cast<float>(m_height) - 100.0f }, true);

    auto project = [&](HexCoord h, float& sx, float& sy) {
        float wx, wy;
        m_hexRenderer.grid().hexToWorld(h, wx, wy);
        m_camera.worldToScreen(wx, wy, sx, sy);
    };

    // Like project(), but eases the drawn world position toward the target tile
    // so AI / other-player heroes WALK between hexes instead of teleporting
    // (readability in Watch mode). Snaps on first sight or a big jump (boat
    // launch, town portal) so a hero never glides across the whole map.
    const float kHeroEaseHexW = m_hexRenderer.grid().hexSize();
    const float kHeroEaseDt   = ImGui::GetIO().DeltaTime;
    auto projectHeroSmooth = [&](uint32_t id, HexCoord h, float& sx, float& sy) {
        float tx, ty;
        m_hexRenderer.grid().hexToWorld(h, tx, ty);
        float rx = tx, ry = ty;
        auto it = m_heroRenderPos.find(id);
        if (it != m_heroRenderPos.end()) {
            float px = it->second.first, py = it->second.second;
            float dx = tx - px, dy = ty - py;
            float snap = kHeroEaseHexW * 3.5f;
            if (dx*dx + dy*dy <= snap*snap) {           // near enough → ease in
                float a = std::min(1.0f, kHeroEaseDt * 6.0f);
                rx = px + dx * a; ry = py + dy * a;
            }
        }
        m_heroRenderPos[id] = {rx, ry};
        m_camera.worldToScreen(rx, ry, sx, sy);
    };

    // On-screen radius of one hex tile — several per-tile overlays below (roads,
    // movement range) used to hardcode this as 20px, hand-tuned for the old
    // fixed hexSize=40 at zoom=1 (40*0.5=20). Now that hexSize varies by map
    // density, those must scale with it instead or they bleed across tiles.
    const float hexPxR = m_hexRenderer.grid().hexSize() * m_camera.zoom() * 0.5f;

    // Returns true if a label at (lx, ly) of approx width lw is in a safe area,
    // i.e. not overlapping the top bar, bottom bar, or right-side panels.
    const float HUD_TOP    = 68.0f;
    const float HUD_BOTTOM = static_cast<float>(m_height) - 100.0f;
    const float HUD_RIGHT  = static_cast<float>(m_width)  - 185.0f;
    auto labelOK = [&](float lx, float ly, float lw = 0.0f) -> bool {
        if (ly < HUD_TOP)    return false;
        if (ly > HUD_BOTTOM) return false;
        if (lx + lw > HUD_RIGHT) return false;
        return true;
    };

    // Helper: draw one icon from the atlas centered at (sx,sy) with half-size hs
    const bool hasIcons = m_iconTex.ok();
    ImTextureID iconTex = hasIcons
        ? (ImTextureID)(uintptr_t)m_iconTex.id()
        : (ImTextureID)(uintptr_t)0;

    auto addIcon = [&](int idx, float sx, float sy, float hs) {
        if (!hasIcons) return;
        float col = static_cast<float>(idx % 8);
        float row = static_cast<float>(idx / 8);
        ImVec2 uv0 = { col / 8.0f,          row / 6.0f };
        ImVec2 uv1 = { (col + 1.0f) / 8.0f, (row + 1.0f) / 6.0f };
        dl->AddImage(iconTex, {sx - hs, sy - hs}, {sx + hs, sy + hs}, uv0, uv1);
    };

    // Icon atlas indices
    enum : int {
        ICO_HERO_PLAYER  = 0, ICO_HERO_ENEMY   = 1,
        ICO_TOWN_PLAYER  = 2, ICO_TOWN_ENEMY   = 3, ICO_TOWN_NEUTRAL = 4,
        ICO_SCROLL       = 5, ICO_ARTIFACT     = 6, ICO_XP           = 7,
        ICO_CACHE        = 8, ICO_RES_GOLD     = 9, ICO_RES_IRON     = 10,
        ICO_RES_FAITH    = 11,ICO_RES_BLOOD    = 12,ICO_RES_SAP      = 13,
        ICO_RES_MERCURY  = 14,
        ICO_OBSERVATORY  = 16, ICO_STAT_SHRINE = 17, ICO_BANDIT_CAMP = 18,
        ICO_DWELLING     = 19, ICO_QUEST_GIVER = 20, ICO_QUEST_TARGET = 21,
        ICO_FOREST_SHRINE = 22, ICO_HIGHLAND_RUIN = 23,
        ICO_HOLY_FOUNTAIN = 24, ICO_OASIS         = 25,
        ICO_CAMPFIRE      = 26, ICO_LAVA_CRYSTAL  = 27, ICO_SWAMP_ALTAR = 28,
        ICO_CRYPT        = 29,
        ICO_UTOPIA       = 30,
        ICO_TREASURE     = 31,
        ICO_ARENA        = 32,
        ICO_MERCHANT     = 33,
        ICO_CHOKE_GUARD  = 34,
        ICO_SHIPYARD     = 35,
        ICO_FISH_HOUSE   = 36,
        ICO_XP_WELL      = 37,
    };

    // ── Per-owner colors ──────────────────────────────────────────────────────
    // Every player (ownerId) gets a distinct hue so an 8-player map isn't a sea
    // of identical red dots. ownerId 0 = neutral. The watched/current player is
    // always the first (blue) slot for consistency.
    auto ownerColor = [&](uint32_t owner, int alpha) -> ImU32 {
        static const int pal[8][3] = {
            {120, 190, 255},  // 1 blue
            {255, 100, 100},  // 2 red
            {110, 220, 120},  // 3 green
            {235, 200,  70},  // 4 gold
            {200, 120, 255},  // 5 purple
            {120, 230, 230},  // 6 cyan
            {255, 150,  70},  // 7 orange
            {240, 130, 200},  // 8 pink
        };
        if (owner == 0) return IM_COL32(255, 210, 60, alpha);   // neutral — gold
        int i = (int)((owner - 1) % 8);
        return IM_COL32(pal[i][0], pal[i][1], pal[i][2], alpha);
    };

    // ── Road network ──────────────────────────────────────────────────────────
    // Draw dirt-road paths connecting towns — render before towns/icons so they
    // appear underneath map objects
    if (!m_roadHexes.empty()) {
        for (const auto& rc : m_roadHexes) {
            const HexTile* rt = m_map.getTile(rc);
            if (!rt || !rt->explored) continue;
            float sx, sy;
            project(rc, sx, sy);
            if (sy < HUD_TOP + 22.0f || sy > HUD_BOTTOM) continue;
            // Base dirt circle
            dl->AddCircleFilled({sx, sy}, hexPxR * 0.9f, IM_COL32(160, 130, 85, 140));
            // Draw line segments to each explored road neighbor for continuity
            for (const auto& nb : HexGrid::neighbors(rc)) {
                if (m_roadHexes.count(nb)) {
                    const HexTile* nt = m_map.getTile(nb);
                    if (!nt || !nt->explored) continue;
                    float nx, ny;
                    project(nb, nx, ny);
                    float mx = (sx + nx) * 0.5f, my = (sy + ny) * 0.5f;
                    if (my < HUD_TOP + 22.0f || my > HUD_BOTTOM) continue;
                    dl->AddLine({sx, sy}, {mx, my}, IM_COL32(160, 130, 85, 120), 10.0f);
                }
            }
        }
    }

    // ── Movement range highlight ───────────────────────────────────────────────
    // Draw a soft green overlay on every hex the active hero can reach this turn
    if (!m_heroes.empty() && !m_reachable.empty()) {
        for (const auto& rc : m_reachable) {
            float sx, sy;
            project(rc, sx, sy);
            if (sy < HUD_TOP || sy > HUD_BOTTOM) continue;
            dl->AddCircleFilled({sx, sy}, hexPxR, IM_COL32(80, 220, 100, 35));
            dl->AddCircle({sx, sy}, hexPxR, IM_COL32(80, 220, 100, 110), 0, 1.2f);
        }
    }

    // ── Towns ─────────────────────────────────────────────────────────────────
    for (const auto& town : m_towns) {
        const HexTile* ttile = m_map.getTile(town.pos);
        if (!m_fogDisabled && ttile && !ttile->explored) continue;

        float sx, sy;
        project(town.pos, sx, sy);

        bool isPlayer = (town.ownerId == static_cast<uint32_t>(currentPlayerId()));
        uint32_t otherHuman2 = (m_numHumanPlayers >= 2)
            ? static_cast<uint32_t>((m_currentPlayerIdx == 0) ? 2 : 1) : 0u;
        bool isOtherHuman = (!isPlayer && m_numHumanPlayers >= 2 && town.ownerId == otherHuman2);
        bool isEnemy      = (town.ownerId != 0 && !isPlayer && !isOtherHuman);
        int  fid      = std::clamp(static_cast<int>(town.faction), 0, NUM_FACTIONS - 1);

        ImU32 ringCol = isPlayer     ? IM_COL32(120, 180, 255, 255)
                      : isEnemy      ? ownerColor(town.ownerId, 255)
                      : isOtherHuman ? ownerColor(town.ownerId, 255)
                                     : IM_COL32(210, 165,  50, 255);
        ImU32 flagCol = isPlayer     ? IM_COL32( 80, 140, 255, 230)
                      : isEnemy      ? ownerColor(town.ownerId, 230)
                      : isOtherHuman ? ownerColor(town.ownerId, 230)
                                     : IM_COL32(190, 145,  30, 230);

        unsigned int townTexId = townStageTexId(fid, townFortStage(town));
        ImTextureID townArt = townTexId
            ? (ImTextureID)(uintptr_t)townTexId : nullptr;

        // Scale icon with zoom but cap at half a tile so it never overflows the hex
        const float tileR = m_hexRenderer.grid().hexSize() * m_camera.zoom() * 0.5f;
        const float CS    = std::min(townArt ? 46.0f : 44.0f, tileR * 0.88f);
        const float glow  = std::min(12.0f, CS * 0.25f);

        // Outer glow
        dl->AddRectFilled({sx - CS - glow, sy - CS - glow},
                          {sx + CS + glow, sy + CS + glow},
                          (ringCol & 0x00FFFFFFu) | 0x28000000u, 10.0f);

        if (townArt) {
            // ── Faction art image ──────────────────────────────────────────
            dl->AddImageRounded(townArt, {sx - CS, sy - CS}, {sx + CS, sy + CS},
                                {0,0}, {1,1}, IM_COL32(255,255,255,230), 6.0f);
        } else {
            // ── Procedural silhouette fallback ─────────────────────────────
            ImU32 bgCol   = isPlayer     ? IM_COL32(12, 22, 55, 240)
                          : isOtherHuman ? IM_COL32(10, 45, 20, 240)
                          : isEnemy      ? IM_COL32(55, 12, 12, 240)
                                        : IM_COL32(45, 35, 10, 240);
            ImU32 wallCol = isPlayer     ? IM_COL32(70, 110, 210, 255)
                          : isOtherHuman ? IM_COL32(60, 180,  90, 255)
                          : isEnemy      ? IM_COL32(210,  65,  65, 255)
                                        : IM_COL32(185, 150,  45, 255);
            dl->AddRectFilled({sx-CS, sy-CS}, {sx+CS, sy+CS}, bgCol, 5.0f);
            const float TW=13.f, TH=CS*0.85f, KW=CS*0.38f, KH=CS;
            dl->AddRectFilled({sx-CS+5,     sy-TH*.55f},{sx-CS+5+TW, sy+TH*.45f}, wallCol,2.f);
            dl->AddRectFilled({sx+CS-5-TW,  sy-TH*.55f},{sx+CS-5,    sy+TH*.45f}, wallCol,2.f);
            dl->AddRectFilled({sx-KW, sy-KH*.5f},{sx+KW, sy+KH*.5f}, wallCol, 2.f);
            for (float bx=sx-KW+2; bx<sx+KW-4; bx+=11.5f)
                dl->AddRectFilled({bx,sy-KH*.5f-7},{bx+5.5f,sy-KH*.5f}, wallCol);
            dl->AddRectFilled({sx-4.5f,sy+KH*.5f-14},{sx+4.5f,sy+KH*.5f},IM_COL32(8,8,8,220));
        }

        // ── Ownership border + flag pole ──────────────────────────────────
        dl->AddRect({sx - CS, sy - CS}, {sx + CS, sy + CS}, ringCol, 6.0f, 0, 2.5f);

        // Flag pole (top-center)
        float poleX = sx + CS - 10.f, poleY1 = sy - CS - 18.f, poleY2 = sy - CS + 2.f;
        dl->AddLine({poleX, poleY1}, {poleX, poleY2}, IM_COL32(180,160,100,220), 2.0f);
        dl->AddTriangleFilled({poleX, poleY1}, {poleX + 16.f, poleY1 + 6.f},
                              {poleX, poleY1 + 12.f}, flagCol);

        // ── Town name ─────────────────────────────────────────────────────
        float nameW = town.name.size() * 5.0f;
        float nameX = sx - nameW, nameY = sy + CS + 5.0f;
        if (labelOK(nameX, nameY, nameW * 2.0f)) {
            dl->AddText(ImGui::GetFont(), 14.f, {nameX+1, nameY+1}, IM_COL32(0,0,0,200), town.name.c_str());
            dl->AddText(ImGui::GetFont(), 14.f, {nameX,   nameY},   IM_COL32(210,230,255,255), town.name.c_str());
        }

        // ── Build-progress bar — visible at a glance whether the town is
        // still mostly empty or fully built out, without opening it.
        {
            int maxB = static_cast<int>(m_registry.getBuildingsForFaction(town.faction).size());
            if (maxB > 0) {
                float frac = std::min(1.0f, static_cast<float>(town.builtBuildings.size()) / maxB);
                float barW = CS * 1.6f, barH = 4.0f;
                float barX = sx - barW * 0.5f, barY = sy + CS + (labelOK(nameX, nameY, nameW*2.0f) ? 20.0f : 5.0f);
                dl->AddRectFilled({barX, barY}, {barX + barW, barY + barH}, IM_COL32(0, 0, 0, 160));
                dl->AddRectFilled({barX, barY}, {barX + barW * frac, barY + barH}, IM_COL32(230, 190, 70, 230));
                dl->AddRect({barX, barY}, {barX + barW, barY + barH}, IM_COL32(0, 0, 0, 200));
            }
        }
    }

    // ── World objects ──────────────────────────────────────────────────────────
    for (int oi = 0; oi < static_cast<int>(m_worldObjects.size()); ++oi) {
        const auto& obj = m_worldObjects[oi];
        if (obj.type == WorldObjectType::Barrier) continue;  // drawn as blocked terrain
        // FishingHouse is always visible once built
        bool isFishHouse = (obj.type == WorldObjectType::FishingHouse);
        bool isShipyard  = (obj.type == WorldObjectType::Shipyard);
        // NeutralOutpost, WitchHut, Shipyard, FishingHouse stay visible after collection
        if (obj.collected && obj.type != WorldObjectType::NeutralOutpost
            && obj.type != WorldObjectType::WitchHut
            && !isFishHouse && !isShipyard) continue;
        if (obj.type == WorldObjectType::NeutralOutpost && obj.collected && obj.available <= 0
            && (obj.linkedId == 0 || obj.linkedId == static_cast<uint32_t>(currentPlayerId()))) continue;
        const HexTile* otile = m_map.getTile(obj.pos);
        if (!m_fogDisabled && (!otile || !otile->explored)) continue;
        float sx, sy;
        project(obj.pos, sx, sy);
        int ico;
        switch (obj.type) {
        case WorldObjectType::SpellScroll:   ico = ICO_SCROLL;          break;
        case WorldObjectType::ArtifactChest: ico = ICO_ARTIFACT;        break;
        case WorldObjectType::XPShrine:      ico = ICO_XP;              break;
        case WorldObjectType::ResourceCache: ico = ICO_CACHE;           break;
        case WorldObjectType::Observatory:   ico = ICO_OBSERVATORY;     break;
        case WorldObjectType::StatShrine:    ico = ICO_STAT_SHRINE;     break;
        case WorldObjectType::BanditCamp:    ico = ICO_BANDIT_CAMP;     break;
        case WorldObjectType::UnitDwelling:  ico = ICO_DWELLING;        break;
        case WorldObjectType::QuestGiver:    ico = ICO_QUEST_GIVER;     break;
        case WorldObjectType::QuestTarget:   ico = ICO_QUEST_TARGET;    break;
        case WorldObjectType::ForestShrine:  ico = ICO_FOREST_SHRINE;   break;
        case WorldObjectType::HighlandRuin:  ico = ICO_HIGHLAND_RUIN;   break;
        case WorldObjectType::HolyFountain:  ico = ICO_HOLY_FOUNTAIN;   break;
        case WorldObjectType::Oasis:         ico = ICO_OASIS;           break;
        case WorldObjectType::Campfire:      ico = ICO_CAMPFIRE;        break;
        case WorldObjectType::LavaCrystal:   ico = ICO_LAVA_CRYSTAL;    break;
        case WorldObjectType::SwampAltar:    ico = ICO_SWAMP_ALTAR;     break;
        case WorldObjectType::TreasureChest: ico = ICO_TREASURE;         break;
        case WorldObjectType::Crypt:         ico = ICO_CRYPT;        break;
        case WorldObjectType::Utopia:        ico = ICO_UTOPIA;       break;
        case WorldObjectType::Landmark:       ico = 38;              break; // row4 col6
        case WorldObjectType::CursedGround:  ico = 39;               break; // row4 col7
        case WorldObjectType::NeutralOutpost: ico = 40;              break; // row5 col0
        case WorldObjectType::WitchHut:       ico = 41;              break; // row5 col1
        case WorldObjectType::Stables:        ico = 42;              break; // row5 col2
        case WorldObjectType::TreeOfKnowledge:ico = 43;              break; // row5 col3
        // NOTE: atlas slots 32-37 hold the RESOURCE-BAR icons (gold/iron/faith/
        // blood/sap/mercury). These six objects previously reused those slots,
        // which overwrote the resource icons — they now fall back to the default
        // map marker (slot 15) so the resource bar renders correctly.
        case WorldObjectType::PandoraBox:       ico = 44;              break; // row5 col4
        default:                                ico = 15;          break;
        }
        // Idle glow pulse
        float pulse = 0.5f + 0.5f * sinf(m_mapTime * 2.0f + oi * 1.1f);
        float gR    = 10.0f + pulse * 3.0f;
        ImU32 glowC = IM_COL32(255, 240, 180, static_cast<int>(pulse * 90 + 40));

        if (obj.type == WorldObjectType::Crypt) {
            // Dark stone arch icon: charcoal square with purple glow
            glowC = IM_COL32(160, 80, 255, static_cast<int>(pulse * 100 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(25, 15, 35, 220));
            dl->AddRectFilled({sx-8,sy-10},{sx+8,sy+6}, IM_COL32(80,60,110,240), 2.0f);
            dl->AddRect({sx-8,sy-10},{sx+8,sy+6}, IM_COL32(160,80,255,200), 2.0f);
            // Skull-like dots
            dl->AddCircleFilled({sx-3.5f,sy-5.5f}, 2.0f, IM_COL32(200,180,240,220));
            dl->AddCircleFilled({sx+3.5f,sy-5.5f}, 2.0f, IM_COL32(200,180,240,220));
        } else if (obj.type == WorldObjectType::Utopia) {
            // Golden star/diamond icon
            glowC = IM_COL32(255, 215, 0, static_cast<int>(pulse * 140 + 60));
            dl->AddCircleFilled({sx, sy}, 16.0f, IM_COL32(35, 25, 5, 220));
            // Diamond shape
            ImVec2 diam[4] = {{sx, sy-13.0f},{sx+9.0f,sy},{sx, sy+13.0f},{sx-9.0f,sy}};
            dl->AddConvexPolyFilled(diam, 4, IM_COL32(200, 160, 20, 240));
            dl->AddPolyline(diam, 4, IM_COL32(255, 230, 80, 220), ImDrawFlags_Closed, 1.5f);
            // Inner gem
            ImVec2 gem[4] = {{sx, sy-7.0f},{sx+4.5f,sy},{sx, sy+7.0f},{sx-4.5f,sy}};
            dl->AddConvexPolyFilled(gem, 4, IM_COL32(255, 245, 160, 220));
        } else if (obj.type == WorldObjectType::TreasureChest) {
            // Chest icon: brown rectangle with gold latch
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(15, 10, 5, 200));
            dl->AddRectFilled({sx-9,sy-7},{sx+9,sy+7}, IM_COL32(120,80,30,240), 2.0f);
            dl->AddRectFilled({sx-9,sy-7},{sx+9,sy-2}, IM_COL32(90,55,20,240), 2.0f);
            dl->AddRect({sx-9,sy-7},{sx+9,sy+7}, IM_COL32(200,170,50,200), 2.0f);
            dl->AddRectFilled({sx-3,sy-4},{sx+3,sy+1}, IM_COL32(220,190,50,255), 2.0f);
        } else if (obj.type == WorldObjectType::Landmark) {
            // Ancient pillar — stone column silhouette, golden glow
            glowC = IM_COL32(220, 200, 120, static_cast<int>(pulse * 100 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 18, 10, 210));
            dl->AddRectFilled({sx-4,sy-11},{sx+4,sy+7}, IM_COL32(160,140,90,240), 2.0f);
            dl->AddRectFilled({sx-6,sy-11},{sx+6,sy-9}, IM_COL32(180,160,100,240), 2.0f);
            dl->AddRectFilled({sx-6,sy+5},{sx+6,sy+8}, IM_COL32(180,160,100,240), 2.0f);
            dl->AddRect({sx-6,sy-11},{sx+6,sy+8}, IM_COL32(220,200,120,180), 1.0f);
        } else if (obj.type == WorldObjectType::CursedGround) {
            // Skull icon — dark purple aura
            glowC = IM_COL32(130, 30, 180, static_cast<int>(pulse * 120 + 50));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 5, 25, 230));
            dl->AddCircleFilled({sx, sy-2.0f}, 8.0f, IM_COL32(180, 120, 200, 220));
            dl->AddRectFilled({sx-5,sy+4},{sx+5,sy+8}, IM_COL32(180,120,200,200), 1.0f);
            // Eye sockets
            dl->AddCircleFilled({sx-2.5f,sy-3.5f}, 1.8f, IM_COL32(20,5,25,255));
            dl->AddCircleFilled({sx+2.5f,sy-3.5f}, 1.8f, IM_COL32(20,5,25,255));
            // Charge counter
            if (obj.questState > 0) {
                char chBuf[12]; std::snprintf(chBuf, sizeof(chBuf), "%d", obj.questState);
                dl->AddText(ImGui::GetFont(), 9.0f, {sx+6,sy-12}, IM_COL32(220,180,255,255), chBuf);
            }
        } else if (obj.type == WorldObjectType::NeutralOutpost) {
            // Flag + tower icon
            glowC = obj.collected
                ? IM_COL32(120, 200, 255, static_cast<int>(pulse * 100 + 40))
                : IM_COL32(200, 200, 100, static_cast<int>(pulse * 80 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(15, 15, 10, 210));
            dl->AddRectFilled({sx-8,sy-4},{sx+8,sy+8}, IM_COL32(100,90,70,240), 2.0f);
            dl->AddLine({sx-3,sy-10},{sx-3,sy-4}, IM_COL32(160,140,100,255), 1.5f);
            ImU32 flagC = obj.collected ? IM_COL32(80,160,255,255) : IM_COL32(200,180,60,255);
            dl->AddTriangleFilled({sx-3,sy-10},{sx+5,sy-7},{sx-3,sy-4}, flagC);
            if (obj.collected && obj.available > 0) {
                char aBuf[12]; std::snprintf(aBuf, sizeof(aBuf), "%d", obj.available);
                dl->AddText(ImGui::GetFont(), 9.0f, {sx+6,sy-12}, IM_COL32(120,220,255,255), aBuf);
            }
        } else if (obj.type == WorldObjectType::WitchHut) {
            // Purple cauldron-hut: dark circle with a cauldron shape
            glowC = IM_COL32(180, 80, 255, static_cast<int>(pulse * 110 + 45));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 10, 30, 220));
            // Hut triangle roof
            ImVec2 roof[3] = {{sx-9,sy-2},{sx+9,sy-2},{sx,sy-12}};
            dl->AddTriangleFilled(roof[0], roof[1], roof[2], IM_COL32(100, 50, 150, 240));
            // Cauldron bowl
            dl->AddRectFilled({sx-6,sy-2},{sx+6,sy+7}, IM_COL32(60,30,100,240), 2.0f);
            dl->AddRect({sx-6,sy-2},{sx+6,sy+7}, IM_COL32(200,120,255,200), 1.5f);
            // Steam dots
            dl->AddCircleFilled({sx-2,sy-4}, 1.5f, IM_COL32(220,180,255,150));
            dl->AddCircleFilled({sx+3,sy-5}, 1.5f, IM_COL32(220,180,255,150));
        } else if (obj.type == WorldObjectType::Stables) {
            // Brown horseshoe / barn: dark circle with barn silhouette
            glowC = IM_COL32(200, 150, 60, static_cast<int>(pulse * 100 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 12, 5, 215));
            // Barn body
            dl->AddRectFilled({sx-7,sy-2},{sx+7,sy+7}, IM_COL32(140,90,40,240), 2.0f);
            // Roof peak
            ImVec2 broof[3] = {{sx-9,sy-2},{sx+9,sy-2},{sx,sy-10}};
            dl->AddTriangleFilled(broof[0], broof[1], broof[2], IM_COL32(110,65,25,255));
            dl->AddTriangle(broof[0], broof[1], broof[2], IM_COL32(200,150,60,200), 1.5f);
            dl->AddRect({sx-7,sy-2},{sx+7,sy+7}, IM_COL32(200,150,60,200), 1.5f);
            // Door
            dl->AddRectFilled({sx-2,sy+2},{sx+2,sy+7}, IM_COL32(60,35,10,255), 1.0f);
        } else if (obj.type == WorldObjectType::TreeOfKnowledge) {
            // Ancient oak: deep green with glowing leaves
            glowC = IM_COL32(60, 200, 80, static_cast<int>(pulse * 120 + 50));
            dl->AddCircleFilled({sx, sy}, 15.0f, IM_COL32(5, 18, 5, 220));
            // Trunk
            dl->AddRectFilled({sx-2.5f,sy+2},{sx+2.5f,sy+9}, IM_COL32(100,65,25,240));
            // Canopy (three overlapping circles)
            dl->AddCircleFilled({sx,     sy-5},  8.0f, IM_COL32(30,120,40,240));
            dl->AddCircleFilled({sx-5.5f,sy-1},  6.5f, IM_COL32(25,100,35,240));
            dl->AddCircleFilled({sx+5.5f,sy-1},  6.5f, IM_COL32(25,100,35,240));
            // Highlight
            dl->AddCircleFilled({sx-2.5f,sy-7},  3.5f, IM_COL32(80,200,90,180));
        } else {
            addIcon(ico, sx, sy, 22.0f);
        }
        dl->AddCircle({sx, sy}, gR + 8.0f, glowC, 0, 1.5f);

        // WitchHut: show skill name label so player knows what it teaches
        if (obj.type == WorldObjectType::WitchHut) {
            const SkillDef* sd = findSkillDef(obj.questState);
            if (sd) {
                float lw = sd->name.size() * 5.5f;
                float labelY = sy + gR + 10.0f;
                if (labelOK(sx - lw - 2, labelY, lw * 2.0f + 4.0f)) {
                    dl->AddRectFilled({sx-lw-2,labelY},{sx+lw+2,labelY+12}, IM_COL32(20,5,35,180), 3.0f);
                    dl->AddText({sx-lw,labelY+1}, IM_COL32(200,130,255,255), sd->name.c_str());
                }
            }
        }
        // TreeOfKnowledge: show "2000g or XP" hint
        if (obj.type == WorldObjectType::TreeOfKnowledge) {
            const char* hint = "2000g / free XP";
            float lw = strlen(hint) * 5.5f;
            float labelY = sy + gR + 10.0f;
            if (labelOK(sx - lw - 2, labelY, lw * 2.0f + 4.0f)) {
                dl->AddRectFilled({sx-lw-2,labelY},{sx+lw+2,labelY+12}, IM_COL32(5,20,5,180), 3.0f);
                dl->AddText({sx-lw,labelY+1}, IM_COL32(100,220,110,255), hint);
            }
        }
    }

    // ── Resource nodes (mines) ────────────────────────────────────────────────
    for (const auto& r : m_resources) {
        const HexTile* rtile = m_map.getTile(r.pos);
        if (!m_fogDisabled && (!rtile || !rtile->explored)) continue;
        float sx, sy;
        project(r.pos, sx, sy);
        // Full screen-bounds cull: skip anything off-screen (any edge) or inside
        // a HUD zone. On XL maps this is the difference between drawing ~40
        // visible mines vs all 4158 every frame.
        if (sx < -40.0f || sx > static_cast<float>(m_width) + 40.0f
            || sy < 68.0f || sy > static_cast<float>(m_height) - 52.0f
            || sx > static_cast<float>(m_width) - 185.0f) continue;
        int ico;
        switch (r.type) {
        case ResourceType::Gold:         ico = ICO_RES_GOLD;    break;
        case ResourceType::Iron:         ico = ICO_RES_IRON;    break;
        case ResourceType::FaithStones:  ico = ICO_RES_FAITH;   break;
        case ResourceType::BloodEssence: ico = ICO_RES_BLOOD;   break;
        case ResourceType::VerdantSap:   ico = ICO_RES_SAP;     break;
        case ResourceType::Mercury:      ico = ICO_RES_MERCURY; break;
        default:                         ico = 15;               break;
        }
        // Scale mine icon with zoom, capped to fit inside one tile
        const float mineR = std::min(28.0f, m_hexRenderer.grid().hexSize() * m_camera.zoom() * 0.46f);
        const float mineGlow = mineR + 2.0f;
        // Glow backdrop so mine is visible against any terrain
        ImU32 bgGlow = IM_COL32(0, 0, 0, 150);
        dl->AddCircleFilled({sx, sy}, mineGlow, bgGlow);
        addIcon(ico, sx, sy, mineR);
        // Ownership ring — per-owner color (blue=you, unique hue per rival)
        ImU32 ring;
        if (r.ownedBy == static_cast<uint32_t>(currentPlayerId()))
            ring = ownerColor(1, 255);               // you — always blue
        else if (r.ownedBy != 0)
            ring = ownerColor(r.ownedBy, 255);       // rival — unique hue
        else
            ring = IM_COL32(255, 210, 60, 200);      // neutral — gold
        dl->AddCircle({sx, sy}, mineGlow, ring, 0, 2.0f);
        // Resource name + weekly amount shown below the icon (if not inside a HUD panel)
        const char* resName = resourceName(r.type);
        char label[32];
        std::snprintf(label, sizeof(label), "%s +%d", resName, r.amount);
        float lw = strlen(label) * 5.5f;
        float labelY = sy + mineGlow + 2.0f;
        if (labelOK(sx - lw - 2, labelY, lw * 2.0f + 4.0f)) {
            dl->AddRectFilled({sx - lw - 2, labelY}, {sx + lw + 2, labelY + 12},
                              IM_COL32(0, 0, 0, 170), 3.0f);
            dl->AddText({sx - lw, labelY + 1},
                        r.ownedBy == static_cast<uint32_t>(currentPlayerId()) ? IM_COL32(140, 210, 255, 255)
                                                                               : IM_COL32(255, 230, 120, 255),
                        label);
        }
    }

    // BloodScent: any player hero with this specialty reveals Bloodsworn enemies
    bool playerHasBloodScent = false;
    for (const auto& ph : m_heroes) {
        if (ph.bloodScentSpecialty) { playerHasBloodScent = true; break; }
    }

    // ── Enemy heroes (only if tile is visible, or revealed by BloodScent; GhostWalk heroes are hidden) ─────
    for (const auto& hero : m_enemyHeroes) {
        // GhostWalk: Voidkin Shadow Stalker is invisible on the world map
        if (hero.ghostWalkSpecialty) continue;
        const HexTile* etile = m_map.getTile(hero.pos);
        bool revealedByBloodScent = playerHasBloodScent && hero.faction == FactionId::Bloodsworn;
        if (!m_fogDisabled && (!etile || (!etile->visible && !revealedByBloodScent))) continue;
        float sx, sy;
        projectHeroSmooth(hero.id, hero.pos, sx, sy);

        int fac = std::min(static_cast<int>(hero.faction), NUM_FACTIONS - 1);
        auto ait = m_heroMapAnimators.find(hero.id);
        bool heroArt = m_heroTex[fac].ok();
        const Texture& hbTex = heroArt ? m_heroTex[fac] : m_unitTex[fac][0];
        if (ait != m_heroMapAnimators.end() && hbTex.ok()) {
            float u0, v0, u1, v1;
            ait->second.getUV(u0, v0, u1, v1);
            ImTextureID tex = (ImTextureID)(uintptr_t)hbTex.id();
            if (heroArt) dl->AddImage(tex, {sx - 20, sy - 22}, {sx + 20, sy + 12}, {u0,v0}, {u1,v1});
            else         dl->AddImage(tex, {sx - 16, sy - 20}, {sx + 16, sy + 12}, {u0,v0}, {u1,v1});
        } else {
            addIcon(ICO_HERO_ENEMY, sx, sy, 13.0f);
        }
        dl->AddCircle({sx, sy}, 14.0f, ownerColor(hero.ownerId, 210), 0, 2.0f);
        {
            float lx = sx - (float)hero.name.size() * 3.0f;
            if (labelOK(lx, sy + 15, hero.name.size() * 6.0f))
                dl->AddText({lx, sy + 15}, ownerColor(hero.ownerId, 230), hero.name.c_str());
        }
        if (hero.isGarrisoned && labelOK(sx - 10.0f, sy - 30.0f))
            dl->AddText({sx - 10.0f, sy - 30.0f}, ownerColor(hero.ownerId, 255), "[G]");
    }

    // ── Other human player heroes (hotseat N-player) ──────────────────────────
    for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
        if (pi == m_currentPlayerIdx) continue;
        char pLabel[16]; std::snprintf(pLabel, sizeof(pLabel), "P%d", pi + 1);
        for (const auto& hero : m_players[pi].heroes) {
            float sx, sy;
            projectHeroSmooth(hero.id, hero.pos, sx, sy);
            if (sy < HUD_TOP || sy > HUD_BOTTOM) continue;
            int fac = std::min(static_cast<int>(hero.faction), NUM_FACTIONS - 1);
            auto ait = m_heroMapAnimators.find(hero.id);
            bool heroArt = m_heroTex[fac].ok();
            const Texture& hbTex = heroArt ? m_heroTex[fac] : m_unitTex[fac][0];
            if (ait != m_heroMapAnimators.end() && hbTex.ok()) {
                float u0, v0, u1, v1;
                ait->second.getUV(u0, v0, u1, v1);
                ImTextureID tex = (ImTextureID)(uintptr_t)hbTex.id();
                if (heroArt) dl->AddImage(tex, {sx - 20, sy - 22}, {sx + 20, sy + 12}, {u0,v0}, {u1,v1});
                else         dl->AddImage(tex, {sx - 16, sy - 20}, {sx + 16, sy + 12}, {u0,v0}, {u1,v1});
            } else {
                addIcon(ICO_HERO_PLAYER, sx, sy, 13.0f);
            }
            dl->AddCircle({sx, sy}, 14.0f, IM_COL32(120, 160, 255, 200), 0, 2.0f);
            {
                float lx = sx - (float)hero.name.size() * 3.0f;
                if (labelOK(lx, sy + 15, hero.name.size() * 6.0f)) {
                    dl->AddText({lx, sy + 15}, IM_COL32(140, 180, 255, 220), hero.name.c_str());
                    dl->AddText({sx - 8.0f, sy - 28.0f}, IM_COL32(140, 180, 255, 200), pLabel);
                }
            }
        }
    }

    // ── Player heroes ─────────────────────────────────────────────────────────
    for (int i = 0; i < static_cast<int>(m_heroes.size()); ++i) {
        const auto& hero = m_heroes[i];
        float sx, sy;
        if (i == m_activeHeroIdx && m_moveT < 1.0f) {
            // Active hero mid-path (normal play): follow the path tween exactly.
            float wx = m_moveSrcX + (m_moveDstX - m_moveSrcX) * m_moveT;
            float wy = m_moveSrcY + (m_moveDstY - m_moveSrcY) * m_moveT;
            m_camera.worldToScreen(wx, wy, sx, sy);
            m_heroRenderPos[hero.id] = {wx, wy};   // keep ease state in sync — no jump on handoff
        } else {
            // Idle, or AI-driven (Watch mode): ease toward the tile instead of snapping.
            projectHeroSmooth(hero.id, hero.pos, sx, sy);
        }

        bool  active = (i == m_activeHeroIdx);
        ImU32 ring   = active ? IM_COL32(255, 255, 160, 255) : IM_COL32(200, 200, 80, 200);

        int fac = std::min(static_cast<int>(hero.faction), NUM_FACTIONS - 1);
        auto ait = m_heroMapAnimators.find(hero.id);
        bool heroArt = m_heroTex[fac].ok();
        const Texture& hbTex = heroArt ? m_heroTex[fac] : m_unitTex[fac][0];
        if (ait != m_heroMapAnimators.end() && hbTex.ok()) {
            float u0, v0, u1, v1;
            ait->second.getUV(u0, v0, u1, v1);
            ImTextureID tex = (ImTextureID)(uintptr_t)hbTex.id();
            if (heroArt) dl->AddImage(tex, {sx - 20, sy - 22}, {sx + 20, sy + 12}, {u0,v0}, {u1,v1});
            else         dl->AddImage(tex, {sx - 16, sy - 20}, {sx + 16, sy + 12}, {u0,v0}, {u1,v1});
        } else {
            addIcon(ICO_HERO_PLAYER, sx, sy, 13.0f);
        }
        dl->AddCircle({sx, sy}, 14.0f, ring, 0, active ? 2.0f : 1.2f);
        {
            float lx = sx - (float)hero.name.size() * 3.0f;
            if (labelOK(lx, sy + 15, hero.name.size() * 6.0f))
                dl->AddText({lx, sy + 15},
                    active ? IM_COL32(255, 230, 100, 220) : IM_COL32(200, 200, 100, 160),
                    hero.name.c_str());
        }
        if (hero.isGarrisoned && labelOK(sx - 10.0f, sy - 30.0f))
            dl->AddText({sx - 10.0f, sy - 30.0f}, IM_COL32(255, 200, 60, 255), "[G]");
    }

    // ── Particles ────────────────────────────────────────────────────────────
    m_particles.render(dl);

    // ── Pickup effects (floating text) ────────────────────────────────────────
    for (const auto& e : m_pickupEffects) {
        float alpha = std::min(1.0f, e.t);
        if (alpha <= 0.0f) continue;
        float rise = (2.0f - e.t) * 30.0f;
        float sx, sy;
        m_camera.worldToScreen(e.wx, e.wy, sx, sy);
        sy -= rise;
        int   a   = static_cast<int>(alpha * 255);
        ImU32 col = (e.col & 0x00FFFFFFu) | (static_cast<ImU32>(a) << 24);
        dl->AddText({sx - static_cast<float>(e.text.size()) * 3.5f, sy}, col, e.text.c_str());
    }

    // ── Planned path visualization ────────────────────────────────────────────
    if (!m_heroes.empty()) {
        const Hero& activeHero = m_heroes[m_activeHeroIdx];
        if (!activeHero.path.empty() && activeHero.pathStep < static_cast<int>(activeHero.path.size())) {
            // Draw remaining path steps as small dots
            for (int pi = activeHero.pathStep; pi < static_cast<int>(activeHero.path.size()); ++pi) {
                float sx, sy;
                project(activeHero.path[pi], sx, sy);
                float alpha = 1.0f - static_cast<float>(pi - activeHero.pathStep)
                                     / static_cast<float>(activeHero.path.size() - activeHero.pathStep + 1);
                ImU32 dotCol = IM_COL32(255, 230, 80, static_cast<int>(alpha * 180));
                dl->AddCircleFilled({sx, sy}, 3.5f, dotCol);
                // Connector line to previous dot
                if (pi > activeHero.pathStep) {
                    float px, py;
                    project(activeHero.path[pi - 1], px, py);
                    dl->AddLine({px, py}, {sx, sy}, IM_COL32(255, 230, 80, static_cast<int>(alpha * 100)), 1.5f);
                } else {
                    // First dot: connect from hero position
                    float hx, hy;
                    m_hexRenderer.grid().hexToWorld(activeHero.pos, hx, hy);
                    float hsx, hsy;
                    m_camera.worldToScreen(hx, hy, hsx, hsy);
                    dl->AddLine({hsx, hsy}, {sx, sy}, IM_COL32(255, 230, 80, 80), 1.5f);
                }
            }
        }
    }

    // ── Hover tooltip: days to reach or Fight ─────────────────────────────────
    static auto worldObjectLabel = [](WorldObjectType t) -> const char* {
        switch (t) {
        case WorldObjectType::SpellScroll:      return "Spell Scroll";
        case WorldObjectType::ArtifactChest:    return "Artifact Chest";
        case WorldObjectType::XPShrine:         return "XP Shrine";
        case WorldObjectType::ResourceCache:    return "Resource Cache";
        case WorldObjectType::Observatory:      return "Observatory";
        case WorldObjectType::StatShrine:       return "Stat Shrine";
        case WorldObjectType::BanditCamp:       return "Bandit Camp";
        case WorldObjectType::UnitDwelling:     return "Unit Dwelling";
        case WorldObjectType::QuestGiver:       return "Quest Giver";
        case WorldObjectType::QuestTarget:      return "Quest Target";
        case WorldObjectType::ForestShrine:     return "Forest Shrine";
        case WorldObjectType::HighlandRuin:     return "Highland Ruin";
        case WorldObjectType::HolyFountain:     return "Holy Fountain";
        case WorldObjectType::Oasis:            return "Oasis";
        case WorldObjectType::Campfire:         return "Campfire";
        case WorldObjectType::LavaCrystal:      return "Lava Crystal";
        case WorldObjectType::SwampAltar:       return "Swamp Altar";
        case WorldObjectType::TreasureChest:    return "Treasure Chest";
        case WorldObjectType::Crypt:            return "Crypt";
        case WorldObjectType::Utopia:           return "Utopia";
        case WorldObjectType::Landmark:         return "Landmark";
        case WorldObjectType::CursedGround:     return "Cursed Ground";
        case WorldObjectType::NeutralOutpost:   return "Neutral Outpost";
        case WorldObjectType::WitchHut:         return "Witch Hut";
        case WorldObjectType::Stables:          return "Stables";
        case WorldObjectType::TreeOfKnowledge:  return "Tree of Knowledge";
        case WorldObjectType::ChokeGuard:       return "Chokepoint Guard";
        case WorldObjectType::Shipyard:         return "Shipyard";
        case WorldObjectType::Flotsam:         return "Floating Salvage";
        case WorldObjectType::Shipwreck:       return "Shipwreck";
        case WorldObjectType::SeaMonsterLair:  return "Sea Monster Lair";
        case WorldObjectType::Lighthouse:      return "Lighthouse";
        case WorldObjectType::FishingHouse:     return "Fishing House";
        case WorldObjectType::ArtifactMerchant: return "Traveling Merchant";
        case WorldObjectType::Arena:            return "Arena";
        case WorldObjectType::ExperienceWell:   return "Experience Well";
        case WorldObjectType::PandoraBox:       return "Pandora's Box";
        default:                                return nullptr;
        }
    };

    if (m_map.inBounds(m_hovered) && !m_heroes.empty()) {
        const HexTile* ht = m_map.getTile(m_hovered);
        const Hero& activeHero = m_heroes[m_activeHeroIdx];
        if (ht && ht->explored && m_hovered != activeHero.pos) {
            // Check if hovering over an enemy hero
            const Hero* enemyHovered = nullptr;
            if (ht->heroId != 0 && ht->visible) {
                for (const auto& e : m_enemyHeroes)
                    if (e.id == ht->heroId) { enemyHovered = &e; break; }
            }

            bool isFight = (enemyHovered != nullptr);
            if (!isFight && ht->townId != 0)
                for (const auto& t : m_towns)
                    if (t.id == ht->townId && t.ownerId > static_cast<uint32_t>(m_numHumanPlayers)) { isFight = true; break; }

            ImGui::BeginTooltip();
            if (enemyHovered) {
                // Show enemy hero details
                ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", enemyHovered->name.c_str());
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Fight!");
                ImGui::Separator();
                ImGui::Text("Lvl %d  ATK:%d  DEF:%d", enemyHovered->level,
                            enemyHovered->attack, enemyHovered->defense);
                if (!enemyHovered->army.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Army:");
                    const auto& unitDefs = m_registry.units();
                    for (const auto& stack : enemyHovered->army) {
                        if (stack.count <= 0) continue;
                        const char* uname = "?";
                        for (const auto& ud : unitDefs)
                            if (ud.id == stack.defId) { uname = ud.name.c_str(); break; }
                        ImGui::Text("  %-22s x%d", uname, stack.count);
                    }
                }
            } else if (isFight) {
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Fight!");
            } else {
                // Object label at hovered tile (shown above movement cost)
                for (const auto& obj : m_worldObjects) {
                    if (obj.pos != m_hovered || obj.collected) continue;
                    if (const char* lbl = worldObjectLabel(obj.type))
                        ImGui::TextColored({1.f, 0.9f, 0.4f, 1.f}, "%s", lbl);
                    break;
                }
                // Movement cost tooltip (roads halve terrain cost)
                auto costFn = [this, &activeHero](HexCoord c) -> int {
                    const HexTile* t = m_map.getTile(c);
                    if (!t || !activeHero.canEnter(t->terrain) || t->blocked) return 999;
                    int base = activeHero.moveCost(t->terrain);
                    if (m_roadHexes.count(c)) base = std::max(1, base / 2);
                    return base;
                };
                auto path = Pathfinder::find(m_map, activeHero.pos, m_hovered, costFn);
                if (!path.empty()) {
                    int totalCost = 0;
                    for (auto& c : path) {
                        const HexTile* t = m_map.getTile(c);
                        if (t) totalCost += activeHero.moveCost(t->terrain);
                    }
                    int spent     = std::min(totalCost, activeHero.movePool);
                    int remaining = totalCost - spent;
                    int days      = (remaining > 0)
                                    ? (remaining + activeHero.maxMove - 1) / activeHero.maxMove
                                    : 0;
                    if (days == 0) ImGui::Text("Reachable today  (%d MP)", totalCost);
                    else          ImGui::Text("%d day%s  (%d MP)", days, days == 1 ? "" : "s", totalCost);
                } else {
                    const HexTile* bt = m_map.getTile(m_hovered);
                    if (bt && bt->blocked)
                        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Barrier");
                    else if (bt && !activeHero.canEnter(bt->terrain))
                        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Impassable terrain");
                    else
                        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Unreachable");
                }
                // Also show terrain type
                static const char* kTerrainNames[] = {
                    "Plains","Forest","Highland","Corrupted","Toxic","Sacred",
                    "Industrial","Rocky","Swamp","Water","Volcanic","Barren",
                    "Wasteland","Corrupted Forest","Flesh Zone","Mountain"
                };
                int tidx = static_cast<int>(ht->terrain);
                if (tidx >= 0 && tidx < 16)
                    ImGui::TextDisabled("%s", kTerrainNames[tidx]);
            }
            ImGui::EndTooltip();
        }
    }

    dl->PopClipRect();   // end play-area clip; the minimap manages its own

    // ── Minimap ────────────────────────────────────────────────────────────────
    if (m_showMinimap && m_map.radius() > 0) {
        constexpr float MINI_W = 150.0f, MINI_H = 150.0f;
        constexpr float PAD    = 10.0f;
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float mm_left = PAD;
        const float mm_top  = disp.y - MINI_H - PAD;
        const float mm_cx   = mm_left + MINI_W * 0.5f;
        const float mm_cy   = mm_top  + MINI_H * 0.5f;
        const float R       = static_cast<float>(m_map.radius());
        const float scaleX  = MINI_W * 0.5f / R;
        const float scaleY  = MINI_H * 0.5f / R;

        // Terrain color lookup (dim if explored-only, bright if currently visible)
        auto terrainColor = [](Terrain t, bool vis) -> ImU32 {
            uint8_t a = vis ? 255 : 110;
            switch (t) {
            case Terrain::Water:          return IM_COL32( 35,  65, 145, a);
            case Terrain::Plains:         return IM_COL32(100, 165,  72, a);
            case Terrain::Forest:         return IM_COL32( 38,  95,  44, a);
            case Terrain::Highland:       return IM_COL32(115, 125,  75, a);
            case Terrain::Rocky:          return IM_COL32(135, 125, 105, a);
            case Terrain::Swamp:          return IM_COL32( 85, 105,  55, a);
            case Terrain::Sacred:         return IM_COL32(215, 195, 145, a);
            case Terrain::Industrial:     return IM_COL32( 75,  75,  75, a);
            case Terrain::Corrupted:      return IM_COL32( 75,  35,  95, a);
            case Terrain::Toxic:          return IM_COL32(135, 155,  25, a);
            case Terrain::Volcanic:       return IM_COL32(125,  35,  15, a);
            case Terrain::Barren:         return IM_COL32(175, 150,  95, a);
            case Terrain::Wasteland:      return IM_COL32(125,  95,  55, a);
            case Terrain::CorruptedForest:return IM_COL32( 45,  65,  65, a);
            case Terrain::FleshZone:      return IM_COL32(155,  75,  75, a);
            case Terrain::Mountain:       return IM_COL32(110, 100,  90, a);
            default:                      return IM_COL32( 95,  95,  95, a);
            }
        };

        // Dark backdrop
        dl->AddRectFilled({mm_left-2, mm_top-2},
                          {mm_left+MINI_W+2, mm_top+MINI_H+2},
                          IM_COL32(0, 0, 0, 190));

        // Clip all minimap drawing to its bounds
        dl->PushClipRect({mm_left, mm_top}, {mm_left+MINI_W, mm_top+MINI_H}, true);

        // Draw explored terrain tiles as 2×2 dots (fog-disabled = everything, bright)
        for (const HexCoord& c : m_map.coords()) {
            const HexTile* t = m_map.getTile(c);
            if (!t || (!m_fogDisabled && !t->explored)) continue;
            float mx = mm_cx + static_cast<float>(c.q) * scaleX;
            float my = mm_cy + (static_cast<float>(c.r) + static_cast<float>(c.q) * 0.5f) * scaleY;
            dl->AddRectFilled({mx-1.f, my-1.f}, {mx+1.f, my+1.f},
                              terrainColor(t->terrain, m_fogDisabled || t->visible));
        }

        // Towns: 4×4 colored square with white outline (per-owner hue via
        // ownerColor — superseded the old two-player "other human" special case)
        for (const auto& town : m_towns) {
            const HexTile* tt = m_map.getTile(town.pos);
            if (!tt || (!m_fogDisabled && !tt->explored)) continue;
            float mx = mm_cx + static_cast<float>(town.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(town.pos.r) + static_cast<float>(town.pos.q) * 0.5f) * scaleY;
            ImU32 col;
            if (town.ownerId == static_cast<uint32_t>(currentPlayerId()))
                col = ownerColor(1, 255);             // current player — blue
            else if (town.ownerId != 0)
                col = ownerColor(town.ownerId, 255);  // rival — unique hue
            else
                col = IM_COL32(210, 165, 45, 255);    // neutral — gold
            dl->AddRectFilled({mx-2.f, my-2.f}, {mx+2.f, my+2.f}, col);
            dl->AddRect({mx-2.f, my-2.f}, {mx+2.f, my+2.f}, IM_COL32(255, 255, 255, 220));
        }

        // Resource mines
        for (const auto& r : m_resources) {
            const HexTile* rt = m_map.getTile(r.pos);
            if (!rt || (!m_fogDisabled && !rt->explored)) continue;
            float mx = mm_cx + static_cast<float>(r.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(r.pos.r) + static_cast<float>(r.pos.q) * 0.5f) * scaleY;
            ImU32 col;
            if (r.ownedBy == static_cast<uint32_t>(currentPlayerId()))
                col = ownerColor(1, 200);
            else if (r.ownedBy != 0)
                col = ownerColor(r.ownedBy, 200);    // rival mine — unique hue
            else
                col = IM_COL32(200, 180, 80, 150);   // unclaimed — gold
            dl->AddCircleFilled({mx, my}, 1.5f, col);
        }

        // Enemy heroes (only when tile is visible, unless fog is off)
        for (const auto& hero : m_enemyHeroes) {
            const HexTile* ht2 = m_map.getTile(hero.pos);
            if (!ht2 || (!m_fogDisabled && !ht2->visible)) continue;
            float mx = mm_cx + static_cast<float>(hero.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(hero.pos.r) + static_cast<float>(hero.pos.q) * 0.5f) * scaleY;
            dl->AddCircleFilled({mx, my}, 2.5f, ownerColor(hero.ownerId, 255));
        }

        // Other human player heroes: blue circle
        for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
            if (pi == m_currentPlayerIdx) continue;
            for (const auto& oh : m_players[pi].heroes) {
                float mx = mm_cx + static_cast<float>(oh.pos.q) * scaleX;
                float my = mm_cy + (static_cast<float>(oh.pos.r) + static_cast<float>(oh.pos.q) * 0.5f) * scaleY;
                dl->AddCircleFilled({mx, my}, 2.5f, IM_COL32(100, 140, 255, 240));
                dl->AddCircle({mx, my}, 3.0f, IM_COL32(160, 200, 255, 220));
            }
        }

        // Player heroes: bright cyan circle
        for (const auto& ph : m_heroes) {
            float mx = mm_cx + static_cast<float>(ph.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(ph.pos.r) + static_cast<float>(ph.pos.q) * 0.5f) * scaleY;
            dl->AddCircleFilled({mx, my}, 3.0f, IM_COL32(70, 200, 255, 255));
            dl->AddCircle({mx, my}, 3.5f, IM_COL32(255, 240, 80, 220));
        }

        // Camera viewport rectangle
        {
            constexpr float SQRT3 = 1.7320508f;
            const float hs   = m_hexRenderer.grid().hexSize();
            const float cx_w = m_camera.x();
            const float cy_w = m_camera.y();
            const float z    = m_camera.zoom();
            const float sw   = static_cast<float>(m_width);
            const float sh   = static_cast<float>(m_height);
            // half-extents of visible region in hex-axial units
            const float hx_q  = (sw * 0.5f / z) / (hs * 1.5f);
            const float hy_rq = (sh * 0.5f / z) / (hs * SQRT3);
            // camera center in axial units
            const float cx_q  = cx_w / (hs * 1.5f);
            const float cy_rq = cy_w / (hs * SQRT3);
            float vl = mm_cx + (cx_q - hx_q) * scaleX;
            float vr = mm_cx + (cx_q + hx_q) * scaleX;
            float vt = mm_cy + (cy_rq - hy_rq) * scaleY;
            float vb = mm_cy + (cy_rq + hy_rq) * scaleY;
            dl->AddRect({vl, vt}, {vr, vb}, IM_COL32(255, 255, 255, 210), 0.f, 0, 1.5f);
        }

        dl->PopClipRect();

        // Minimap border
        dl->AddRect({mm_left-2, mm_top-2},
                    {mm_left+MINI_W+2, mm_top+MINI_H+2},
                    IM_COL32(175, 155, 115, 230), 2.0f, 0, 1.5f);
        // "MAP [M]" label above
        dl->AddText({mm_left+2, mm_top-14}, IM_COL32(195, 175, 135, 220), "MAP [M]");
    }
}

// ── Level-up modal ────────────────────────────────────────────────────────────
void Game::renderLevelUpModal()
{
    if (!m_showLevelUpModal) return;
    ImGui::OpenPopup("Level Up!");

    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Level Up!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Hero& hero = m_heroes[m_activeHeroIdx];

        ImGui::Text("Congratulations! %s reached Level %d!", hero.name.c_str(), hero.level);
        ImGui::Separator();
        ImGui::Text("Choose a skill:");
        ImGui::Spacing();

        for (int i = 0; i < static_cast<int>(m_levelUpOffers.size()); ++i) {
            const auto& offer = m_levelUpOffers[i];
            const SkillDef* offerSd = findSkillDef(offer.skillId);
            ImGui::PushID(i);
            if (ImGui::Button(offer.label.c_str(), ImVec2(-1, 36))) {
                // Compute tier before applying offer (for delta calculation)
                int prevTier = 0;
                if (const SkillInstance* existing = hero.skills.getSkill(offer.skillId))
                    prevTier = static_cast<int>(existing->tier);

                LevelUpSystem::applyOffer(offer, hero.skills);

                // Apply immediate passive bonuses for Movement/Vision/Magic skills
                {
                    const SkillDef* sd = findSkillDef(offer.skillId);
                    if (sd) {
                        // v = incremental gain from this skill event
                        // values[] indexed as Basic=0, Advanced=1, Master=2
                        int v;
                        if (offer.isUpgrade) {
                            // prevTier is the index before upgrade (0=Basic, 1=Advanced)
                            v = sd->values[prevTier + 1] - sd->values[prevTier];
                        } else {
                            v = sd->values[0]; // fresh skill at Basic tier
                        }
                        if (sd->effectType == SkillEffectType::MovementBonus) {
                            hero.maxMove += v; hero.movePool = std::min(hero.movePool + v, hero.maxMove);
                        } else if (sd->effectType == SkillEffectType::VisionBonus) {
                            hero.visionRange += v;
                            FogOfWar::updateVision(m_map, hero);
                        } else if (sd->effectType == SkillEffectType::MagicSchoolBonus) {
                            if      (sd->statName == "lightPower")  hero.lightPower  += v;
                            else if (sd->statName == "bloodPower")  hero.bloodPower  += v;
                            else if (sd->statName == "deathPower")  hero.deathPower  += v;
                            else if (sd->statName == "naturePower") hero.naturePower += v;
                            else if (sd->statName == "forgePower")  hero.forgePower  += v;
                            else if (sd->statName == "fleshPower")  hero.fleshPower  += v;
                        }
                    }
                }

                // Apply per-class stat growth
                const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                if (cls) {
                    if (cls->scalesAttack) hero.attack  += 1;
                    else                   hero.defense += 1;
                    // Use the level actually being gained (not the final level) so
                    // even-level bonuses are applied correctly when skipping levels.
                    int gainedLevel = hero.level - m_pendingLevelUps + 1;
                    if (gainedLevel % 2 == 0) {
                        if (cls->scalesLightPower)  hero.lightPower  += 1;
                        if (cls->scalesBloodPower)  hero.bloodPower  += 1;
                        if (cls->scalesDeathPower)  hero.deathPower  += 1;
                        if (cls->scalesNaturePower) hero.naturePower += 1;
                        if (cls->scalesForgePower)  hero.forgePower  += 1;
                        if (cls->scalesFleshPower)  hero.fleshPower  += 1;
                    }
                    hero.maxMana += 1;
                    hero.mana = hero.maxMana;
                    // HP grows 10 per level
                    hero.heroMaxHp += 10;
                    hero.heroHp = hero.heroMaxHp;
                }
                m_levelUpOffers.clear();
                // If more level-ups are queued, generate the next set of offers
                if (m_pendingLevelUps > 1) {
                    m_pendingLevelUps--;
                    const HeroClassDef* ncls = m_classRegistry.getClass(hero.classId);
                    if (ncls) {
                        std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                        m_levelUpOffers = LevelUpSystem::generateOffers(
                            *ncls, hero.skills, hero.level, allSkills, hero.faction);
                    }
                    if (m_levelUpOffers.empty())
                        m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                    // Keep m_showLevelUpModal true so the next modal opens immediately
                } else {
                    m_pendingLevelUps = 0;
                    m_showLevelUpModal = false;
                    // Grant Found City: level 5 in campaign (tutorial), level 10 elsewhere
                    int foundCityLevel = (m_state == GameState::Campaign) ? 5 : 10;
                    if (hero.level >= foundCityLevel) {
                        bool hasFC = false;
                        for (int sid : hero.knownSpells)
                            if (sid == SPL::FOUND_CITY) { hasFC = true; break; }
                        if (!hasFC) {
                            hero.knownSpells.push_back(SPL::FOUND_CITY);
                            pushPickupEffect(hero.pos, "Learned: Found City!", IM_COL32(255, 215, 50, 255));
                        }
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            // Show description below button
            if (offerSd) {
                ImGui::SameLine(0, 4);
                ImGui::TextDisabled("  %s", offerSd->description.c_str());
            }
            if (ImGui::IsItemHovered() && offerSd)
                ImGui::SetTooltip("%s", offerSd->description.c_str());
            ImGui::PopID();
            ImGui::Spacing();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("XP: %d / %d", hero.xp, hero.xpToNext);
        ImGui::EndPopup();
    }
}

// ── Hideout screen ────────────────────────────────────────────────────────────
void Game::renderHideoutScreen()
{
    m_hideoutScreen.draw(m_hideout, m_showHideoutScreen);
}

// ── Artifact equip panel [F7] ─────────────────────────────────────────────────
void Game::renderArtifactPanel()
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Artifacts  [F7]", &m_showArtifactPanel)) { ImGui::End(); return; }

    static const char* slotNames[] = {
        "Helm","Armor","Weapon","Shield","Ring","Boots","Cloak","Misc"
    };

    // Helper: format non-zero bonus fields into a short string
    auto bonusStr = [](const ArtifactBonus& b) -> std::string {
        std::string s;
        auto app = [&](const char* label, int v) {
            if (v == 0) return;
            if (!s.empty()) s += "  ";
            if (v > 0) s += '+';
            s += std::to_string(v);
            s += ' ';
            s += label;
        };
        app("ATK",    b.attack);
        app("DEF",    b.defense);
        app("SPD",    b.moveBonus);
        app("HP",     b.hpBonus);
        app("Mana",   b.manaBonus);
        app("Light",  b.lightPower);
        app("Blood",  b.bloodPower);
        app("Death",  b.deathPower);
        app("Nature", b.naturePower);
        app("Forge",  b.forgePower);
        app("Flesh",  b.fleshPower);
        app("Vision", b.visionBonus);
        return s.empty() ? "no bonus" : s;
    };

    // Total equipped bonus summary
    ArtifactBonus total = m_artifactRegistry.totalBonus(hero.artifacts);
    std::string totalStr = bonusStr(total);
    if (!totalStr.empty() && totalStr != "no bonus")
        ImGui::TextColored({0.8f,0.8f,0.3f,1.0f}, "Total: %s", totalStr.c_str());
    ImGui::Separator();

    ImGui::Text("Equipped:");
    ImGui::Separator();
    for (int i = 0; i < HeroArtifacts::SLOT_COUNT; ++i) {
        int aid = hero.artifacts.equippedIds[i];
        const ArtifactDef* def = aid ? m_artifactRegistry.getDef(aid) : nullptr;
        ImGui::PushID(i);
        if (def) {
            std::string bs = bonusStr(def->bonus);
            ImGui::Text("%-8s : %-20s  %s", slotNames[i], def->name.c_str(), bs.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unequip")) {
                hero.artifactInventory.push_back(aid);
                hero.artifacts.unequip(static_cast<ArtifactSlot>(i));
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", def->description.c_str());
        } else {
            ImGui::TextDisabled("%-8s : —", slotNames[i]);
        }
        ImGui::PopID();
    }

    if (!hero.artifactInventory.empty()) {
        ImGui::Spacing();
        ImGui::Text("Inventory:");
        ImGui::Separator();
        for (int j = 0; j < static_cast<int>(hero.artifactInventory.size()); ++j) {
            int aid = hero.artifactInventory[j];
            const ArtifactDef* def = m_artifactRegistry.getDef(aid);
            if (!def) continue;
            ImGui::PushID(j + 1000);
            std::string bs = bonusStr(def->bonus);
            ImGui::Text("%-20s  %s", def->name.c_str(), bs.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", def->description.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Equip")) {
                auto slot    = def->slot;
                int  slotIdx = static_cast<int>(slot);
                int  old     = hero.artifacts.equippedIds[slotIdx];
                if (old) hero.artifactInventory.push_back(old);
                hero.artifacts.equip(aid, slot);
                hero.artifactInventory.erase(hero.artifactInventory.begin() + j);
                steam::unlockAchievement("ACH_FIRST_ARTIFACT");
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

// ── Hero inspect panel [F8] ───────────────────────────────────────────────────
void Game::renderHeroInspect()
{
    if (m_heroes.empty()) return;
    const Hero& hero = m_heroes[m_activeHeroIdx];

    ImGui::SetNextWindowSize(ImVec2(340, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Hero  [F8]", &m_showHeroInspect)) { ImGui::End(); return; }

    ImGui::Text("%s", hero.name.c_str());
    const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
    if (cls) {
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), "%s", cls->name.c_str());
        if (!cls->specialtyDesc.empty()) {
            ImGui::TextDisabled("Specialty: %s", cls->specialtyDesc.c_str());
        }
    }
    ImGui::TextDisabled("Level %d  —  XP %d / %d", hero.level, hero.xp, hero.xpToNext);
    {
        float xpFrac = hero.xpToNext > 0 ? static_cast<float>(hero.xp) / hero.xpToNext : 1.0f;
        char xpLabel[32]; std::snprintf(xpLabel, sizeof(xpLabel), "XP %.0f%%", xpFrac * 100.0f);
        ImGui::ProgressBar(xpFrac, ImVec2(-1, 10), xpLabel);
    }
    ImGui::TextDisabled("Battles won: %d", hero.battlesWon);
    if (hero.isGarrisoned)
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "[Garrisoned — +2 DEF in combat]");
    ImGui::Separator();

    ImGui::Text("ATK %d   DEF %d   Vision %d", hero.attack, hero.defense, hero.visionRange);
    ImGui::Text("Mana %d / %d   Move %d / %d",
                hero.mana, hero.maxMana, hero.movePool, hero.maxMove);
    ImGui::Text("HP   %d / %d", hero.heroHp, hero.heroMaxHp);

    // Specialty progression stats
    if (cls) {
        // Veteran / Predator use specialtyAtk as their counter
        bool showGenericAtk = (cls->specialty == SpecialtyType::Veteran ||
                               cls->specialty == SpecialtyType::Predator);
        if (showGenericAtk && hero.specialtyAtk > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                               "Specialty bonus: +%d ATK", hero.specialtyAtk);
        if (cls->specialty == SpecialtyType::Phylactery && hero.phylacteryUsed)
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Phylactery consumed");
        if (cls->specialty == SpecialtyType::Elixir && hero.elixirUsed)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Elixir used this battle");
        if (cls->specialty == SpecialtyType::Recycler && hero.recyclerBonus > 0)
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 0.4f, 1.0f),
                               "Recycler: +%d ATK to all units (%d/5)", hero.recyclerBonus, hero.recyclerBonus);
        if (cls->specialty == SpecialtyType::LivingRune && hero.livingRuneBonus > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                               "Living Rune: +%d ATK/DEF to hero (%d/5)", hero.livingRuneBonus, hero.livingRuneBonus);
        if (cls->specialty == SpecialtyType::BloodScent)
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                               "Blood Scent: Bloodsworn heroes always revealed on map");
    }
    ImGui::Spacing();

    {
        bool anyPower = hero.lightPower || hero.bloodPower || hero.deathPower ||
                        hero.naturePower || hero.forgePower || hero.fleshPower;
        ImGui::Text("Casting Power:");
        if (anyPower) {
            if (hero.lightPower)  ImGui::Text("  Light  +%d", hero.lightPower);
            if (hero.bloodPower)  ImGui::Text("  Blood  +%d", hero.bloodPower);
            if (hero.deathPower)  ImGui::Text("  Death  +%d", hero.deathPower);
            if (hero.naturePower) ImGui::Text("  Nature +%d", hero.naturePower);
            if (hero.forgePower)  ImGui::Text("  Forge  +%d", hero.forgePower);
            if (hero.fleshPower)  ImGui::Text("  Flesh  +%d", hero.fleshPower);
        } else {
            ImGui::TextDisabled("  (no school specialisation)");
        }
    }

    if (!hero.skills.slots.empty()) {
        ImGui::Spacing();
        ImGui::Text("Skills:");
        for (auto& s : hero.skills.slots) {
            if (s.defId == 0) continue;
            const SkillDef* sd = findSkillDef(s.defId);
            if (!sd) continue;
            const char* tierStr[] = {"Basic","Advanced","Master"};
            int t = static_cast<int>(s.tier);
            ImGui::Text("  %s (%s)", sd->name.c_str(), (t >= 0 && t <= 2) ? tierStr[t] : "Basic");
        }

        // Archetype label
        static const int kMight[] = { SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::ARCHERY,
            SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::LOGISTICS,
            SkillID::SCOUTING, SkillID::FIRST_AID, SkillID::LUCK };
        static const int kMagic[] = { SkillID::LIGHT_MAGIC, SkillID::BLOOD_MAGIC,
            SkillID::DEATH_MAGIC, SkillID::NATURE_MAGIC, SkillID::FORGE_MAGIC, SkillID::FLESH_MAGIC };
        int mightN = 0, magicN = 0;
        for (int sid : kMight) if (hero.skills.hasSkill(sid)) ++mightN;
        for (int sid : kMagic) if (hero.skills.hasSkill(sid)) ++magicN;

        const char* archetypeLabel   = nullptr;
        ImVec4      archetypeColour  = {1,1,1,1};
        if      (mightN >= 5 && magicN == 0)         { archetypeLabel = "★ PURE MIGHT";  archetypeColour = {1.0f,0.75f,0.2f,1}; }
        else if (magicN >= 4 && mightN <= 1)         { archetypeLabel = "★ PURE MAGIC";  archetypeColour = {0.6f,0.5f,1.0f,1}; }
        else if (mightN >= 3 && magicN >= 2)         { archetypeLabel = "★ WARLORD";     archetypeColour = {0.8f,0.3f,0.3f,1}; }
        else if (mightN >= 4)                        { archetypeLabel = "Might Build";    archetypeColour = {1.0f,0.85f,0.5f,1}; }
        else if (mightN >= 2)                        { archetypeLabel = "Might Synergy";  archetypeColour = {0.9f,0.8f,0.5f,1}; }
        else if (magicN >= 3)                        { archetypeLabel = "Magic Synergy";  archetypeColour = {0.7f,0.6f,1.0f,1}; }
        else if (magicN >= 2)                        { archetypeLabel = "Dual Magic";     archetypeColour = {0.7f,0.6f,1.0f,1}; }

        if (archetypeLabel) {
            ImGui::Spacing();
            ImGui::TextColored(archetypeColour, "%s", archetypeLabel);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (mightN >= 5 && magicN == 0)
                    ImGui::TextUnformatted("+1 Speed and +10% HP to all units in combat");
                else if (magicN >= 4 && mightN <= 1)
                    ImGui::TextUnformatted("+3 to all casting stats, spells cost -1 mana");
                else if (mightN >= 3 && magicN >= 2)
                    ImGui::TextUnformatted("+1 Morale and +1 Luck to all units in combat");
                else if (mightN >= 4)
                    ImGui::TextUnformatted("+2 ATK/DEF to all units in combat");
                else if (mightN >= 2)
                    ImGui::TextUnformatted("+1 ATK/DEF to all units in combat");
                else if (magicN >= 3)
                    ImGui::TextUnformatted("+2 to all casting stats");
                else if (magicN >= 2)
                    ImGui::TextUnformatted("+1 to all casting stats");
                ImGui::EndTooltip();
            }
        }
    }

    {
        ImGui::Spacing();
        ImGui::Text("Spellbook:");
        ImGui::Separator();
        if (hero.knownSpells.empty()) {
            ImGui::TextDisabled("  -- no spells known --");
        } else {
            for (int sid : hero.knownSpells) {
                const SpellDef* sp = findSpell(sid);
                if (sp) {
                    ImGui::Text("  %s  (%d mana)", sp->name, sp->manaCost);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", sp->desc);
                }
            }
        }
    }

    // Artifacts equipped
    {
        ImGui::Spacing();
        ImGui::Text("Artifacts:");
        ImGui::Separator();
        bool anyEquipped = false;
        static const char* kSlotLabel[] = { "Helm","Armor","Wpn","Shld","Ring","Boots","Cloak","Misc" };
        for (int s = 0; s < static_cast<int>(ArtifactSlot::COUNT); ++s) {
            int artId = hero.artifacts.equippedIds[s];
            if (artId == 0) continue;
            const ArtifactDef* art = m_artifactRegistry.getDef(artId);
            if (!art) continue;
            ImGui::Text("  [%-5s] %s", kSlotLabel[s], art->name.c_str());
            if (ImGui::IsItemHovered() && !art->description.empty())
                ImGui::SetTooltip("%s", art->description.c_str());
            anyEquipped = true;
        }
        if (!anyEquipped)
            ImGui::TextDisabled("  — none equipped —");
        if (!hero.artifactInventory.empty()) {
            ImGui::TextDisabled("  Inventory: %zu artifact(s) unequipped",
                                hero.artifactInventory.size());
        }
    }

    // ── Army portrait row ─────────────────────────────────────────────────────
    {
        ImGui::Spacing();
        ImGui::Text("Army:");
        ImGui::Separator();

        const auto& unitDefs = m_registry.units();
        const float SW = 44.0f, SH = 64.0f, GAP = 3.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int totalUnits = 0, totalHp = 0;
        int slotIdx = 0;

        for (const auto& stack : hero.army) {
            if (slotIdx > 0) ImGui::SameLine(0, GAP);

            const UnitDef* ud = nullptr;
            for (const auto& u : unitDefs) if (u.id == stack.defId) { ud = &u; break; }

            ImTextureID tex = nullptr;
            if (ud) {
                int fid = std::clamp(static_cast<int>(ud->faction), 0, NUM_FACTIONS - 1);
                int tid = std::clamp(ud->tier - 1, 0, NUM_UNIT_TIERS - 1);
                if (m_unitTex[fid][tid].ok())
                    tex = (ImTextureID)(uintptr_t)m_unitTex[fid][tid].id();
            }

            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool hasUnit = (stack.count > 0);

            // Slot bg
            dl->AddRectFilled(pos, {pos.x + SW, pos.y + SH}, IM_COL32(18, 20, 32, 230), 4.0f);
            dl->AddRect(pos, {pos.x + SW, pos.y + SH},
                        hasUnit ? IM_COL32(80, 95, 130, 200) : IM_COL32(35, 40, 58, 140),
                        4.0f, 0, 1.5f);

            if (hasUnit) {
                float sprH2 = SH - 18.0f;
                if (tex) {
                    dl->AddImage(tex, {pos.x + 1, pos.y + 1}, {pos.x + SW - 1, pos.y + 1 + sprH2},
                                 {0.0f, 0.0f}, {0.125f, 1.0f});
                } else if (ud) {
                    char tl[4]; std::snprintf(tl, sizeof(tl), "T%d", ud->tier);
                    dl->AddText({pos.x + SW * 0.5f - 8, pos.y + sprH2 * 0.5f - 7},
                                IM_COL32(130, 140, 165, 200), tl);
                }
                char cnt[12]; std::snprintf(cnt, sizeof(cnt), "x%d", stack.count);
                ImVec2 csz = ImGui::CalcTextSize(cnt);
                float cx = pos.x + (SW - csz.x) * 0.5f, cy = pos.y + SH - 15.0f;
                dl->AddText({cx + 1, cy + 1}, IM_COL32(0, 0, 0, 200), cnt);
                dl->AddText({cx, cy},          IM_COL32(220, 225, 255, 255), cnt);
                totalUnits += stack.count;
                if (ud) totalHp += ud->hp * stack.count;
            }

            char bid[24]; std::snprintf(bid, sizeof(bid), "##hi_army_%d", slotIdx);
            ImGui::InvisibleButton(bid, {SW, SH});

            if (ImGui::IsItemHovered() && hasUnit && ud) {
                ImGui::BeginTooltip();
                // Name + path label
                if (ud->path == UpgradePath::PathA)
                    ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f), "%s  x%d  [Path A]", ud->name.c_str(), stack.count);
                else if (ud->path == UpgradePath::PathB)
                    ImGui::TextColored(ImVec4(0.8f,0.5f,1.0f,1.0f), "%s  x%d  [Path B]", ud->name.c_str(), stack.count);
                else
                    ImGui::Text("%s  x%d", ud->name.c_str(), stack.count);
                ImGui::Separator();
                ImGui::TextDisabled("ATK %d  DEF %d  HP %d  SPD %d",
                                    ud->attack, ud->defense, ud->hp, ud->speed);
                ImGui::TextDisabled("Dmg %d-%d    Total HP: %d",
                                    ud->damage_min, ud->damage_max, ud->hp * stack.count);
                if (ud->range > 0) ImGui::TextDisabled("Ranged  %d shots  range %d", ud->shots, ud->range);
                // Traits
                if (ud->flying)           ImGui::TextColored(ImVec4(0.7f,0.8f,1.0f,1.0f),  "[Flying]");
                if (ud->vampiric)         ImGui::TextColored(ImVec4(0.9f,0.3f,0.3f,1.0f),  "[Vampiric]");
                if (ud->regenerates)      ImGui::TextColored(ImVec4(0.4f,0.9f,0.5f,1.0f),  "[Regenerates]");
                if (ud->moraleImmune)     ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f),  "[Morale Immune]");
                if (ud->hasSecondLife)    ImGui::TextColored(ImVec4(0.7f,0.5f,1.0f,1.0f),
                                              ud->secondLifeFullHeal ? "[Second Life — full HP]" : "[Second Life]");
                if (ud->rapidEvolution)   ImGui::TextColored(ImVec4(0.4f,0.9f,0.7f,1.0f),  "[Rapid Evolution]");
                if (ud->adaptationDouble) ImGui::TextColored(ImVec4(0.4f,0.9f,0.7f,1.0f),  "[Double Adaptation]");
                // Tags
                if (hasTag(ud->tags, UnitTag::Undead))     ImGui::TextDisabled("Type: Undead");
                else if (hasTag(ud->tags, UnitTag::Holy))  ImGui::TextDisabled("Type: Holy");
                else if (hasTag(ud->tags, UnitTag::Void))  ImGui::TextDisabled("Type: Void");
                ImGui::EndTooltip();
            }
            ++slotIdx;
        }
        if (totalUnits > 0) {
            ImGui::Spacing();
            ImGui::TextDisabled("  Total: %d units  |  %d HP", totalUnits, totalHp);
        } else if (hero.army.empty()) {
            ImGui::TextDisabled("  No units");
        }
    }
    ImGui::End();
}

// ── Combat result popup ───────────────────────────────────────────────────────
void Game::renderCombatResultPopup()
{
    const char* title = m_combatResultWon ? "Battle Won!" : "Battle Lost";
    ImGui::OpenPopup(title);
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);

    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    // ── Header ────────────────────────────────────────────────────────────────
    if (m_combatResultWon)
        ImGui::TextColored({0.3f, 1.0f, 0.4f, 1.0f}, "VICTORY!");
    else
        ImGui::TextColored({1.0f, 0.25f, 0.25f, 1.0f}, "DEFEAT");
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Day %d  Week %d", m_turns.day(), m_turns.week());
    ImGui::Separator();

    // ── XP / Gold row ─────────────────────────────────────────────────────────
    if (m_combatResultXp > 0 || m_combatResultGold > 0) {
        if (m_combatResultXp > 0)
            ImGui::TextColored({0.55f, 0.95f, 0.55f, 1.0f}, " XP  +%d", m_combatResultXp);
        if (m_combatResultXp > 0 && m_combatResultGold > 0) ImGui::SameLine(0, 20);
        if (m_combatResultGold > 0)
            ImGui::TextColored({1.0f, 0.82f, 0.12f, 1.0f}, " Gold  +%d", m_combatResultGold);
        ImGui::Spacing();
        ImGui::Separator();
    }

    // ── Unit card helper: draws a row of unit cards ───────────────────────────
    constexpr float CW = 56.0f, CH = 78.0f, CGAP = 4.0f;
    auto drawUnitRow = [&](const std::vector<BattleUnitRecord>& units,
                           ImU32 borderCol, const char* emptyMsg)
    {
        if (units.empty()) {
            ImGui::TextDisabled("  %s", emptyMsg);
            return;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int idx = 0;
        for (const auto& u : units) {
            if (idx > 0) ImGui::SameLine(0, CGAP);
            ImVec2 p = ImGui::GetCursorScreenPos();

            // Card background + border
            dl->AddRectFilled(p, {p.x + CW, p.y + CH}, IM_COL32(18, 20, 32, 235), 4.0f);
            dl->AddRect(p, {p.x + CW, p.y + CH}, borderCol, 4.0f, 0, 1.8f);

            // Sprite (first frame: uv 0..0.125, 0..1)
            float sprY2 = p.y + CH - 20.0f;
            bool drewSprite = false;
            if (u.faction >= 0 && u.faction < NUM_FACTIONS) {
                int ti = std::clamp(u.tier - 1, 0, NUM_UNIT_TIERS - 1);
                if (m_unitTex[u.faction][ti].ok()) {
                    ImTextureID tid = (ImTextureID)(uintptr_t)m_unitTex[u.faction][ti].id();
                    dl->AddImage(tid, {p.x + 2, p.y + 2}, {p.x + CW - 2, sprY2},
                                 {0.0f, 0.0f}, {0.125f, 1.0f});
                    drewSprite = true;
                }
            }
            if (!drewSprite) {
                // Fallback: colored card with abbreviated unit name
                // Hash name for a unique-per-unit-type color
                uint32_t nh = 0;
                for (unsigned char c : u.name) nh = nh * 31u + c;
                ImU32 bgShade = IM_COL32(
                    35 + (int)(nh & 0x45u),
                    35 + (int)((nh >> 6) & 0x35u),
                    55 + (int)((nh >> 12) & 0x55u), 200);
                dl->AddRectFilled({p.x + 2, p.y + 2}, {p.x + CW - 2, sprY2}, bgShade, 3.0f);
                // First word of name (or first 5 chars)
                std::string abbr = u.name;
                {
                    size_t sp = abbr.find(' ');
                    if (sp != std::string::npos && sp <= 7) abbr = abbr.substr(0, sp);
                    else if (abbr.size() > 6) abbr = abbr.substr(0, 6);
                }
                ImVec2 nsz = ImGui::CalcTextSize(abbr.c_str());
                float nx = p.x + (CW - nsz.x) * 0.5f;
                float ny = p.y + (sprY2 - p.y - nsz.y) * 0.5f + 2.0f;
                dl->AddText({nx + 1, ny + 1}, IM_COL32(0, 0, 0, 180), abbr.c_str());
                dl->AddText({nx, ny},         IM_COL32(215, 220, 255, 255), abbr.c_str());
            }

            // Count badge at bottom
            char cnt[12]; std::snprintf(cnt, sizeof(cnt), "x%d", u.count);
            ImVec2 csz = ImGui::CalcTextSize(cnt);
            float cx = p.x + (CW - csz.x) * 0.5f;
            float cy = p.y + CH - 16.0f;
            dl->AddRectFilled({p.x + 2, cy - 2}, {p.x + CW - 2, p.y + CH - 2},
                              IM_COL32(0, 0, 0, 160), 2.0f);
            dl->AddText({cx + 1, cy + 1}, IM_COL32(0, 0, 0, 200), cnt);
            dl->AddText({cx, cy},         IM_COL32(230, 230, 255, 255), cnt);

            // Invisible button for hover/tooltip
            char bid[24]; std::snprintf(bid, sizeof(bid), "##cr_%d", idx);
            ImGui::InvisibleButton(bid, {CW, CH});
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", u.name.c_str());
                ImGui::TextDisabled("x%d  killed/lost", u.count);
                ImGui::EndTooltip();
            }
            ++idx;
        }
    };

    // ── Enemies defeated ──────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::TextColored({0.85f, 0.55f, 0.2f, 1.0f}, "ENEMIES SLAIN");
    ImGui::Spacing();
    drawUnitRow(m_combatEnemiesDefeated, IM_COL32(180, 60, 40, 220), "none");

    // ── Player losses ─────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored({0.6f, 0.65f, 0.85f, 1.0f}, "YOUR LOSSES");
    ImGui::Spacing();
    drawUnitRow(m_combatUnitsLost, IM_COL32(50, 80, 200, 220), "No losses!");

    // ── Continue button ───────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Continue", ImVec2(-1, 32))) {
        m_showCombatResult = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── Victory modal ─────────────────────────────────────────────────────────────
void Game::recordFinalScore(bool won)
{
    if (m_scoreRecorded) return;
    m_scoreRecorded = true;

    int days = (m_turns.week() - 1) * 7 + m_turns.day();
    int townsHeld = 0;
    for (const auto& t : m_towns)
        if (t.ownerId == static_cast<uint32_t>(currentPlayerId())) ++townsHeld;
    int maxLevel = 0;
    FactionId fac = FactionId::HolyOrder;
    std::string who = "Commander";
    for (const auto& h : m_heroes) {
        if (h.level > maxLevel) { maxLevel = h.level; fac = h.faction; who = h.name; }
    }

    m_finalScore = computeGameScore(won, days, m_newGameDifficulty, townsHeld, maxLevel);
    m_finalScore.faction = static_cast<int>(fac);

    // Persist to the shared meta DB (self-creating highscores table).
    ScoreDB db;
    if (db.open(metaDbPath())) {
        HighScore hs;
        hs.name       = who;
        hs.faction    = static_cast<int>(fac);
        hs.score      = m_finalScore.score;
        hs.days       = m_finalScore.days;
        hs.difficulty = m_finalScore.difficulty;
        hs.won        = won;
        hs.rank       = m_finalScore.rank;
        hs.when       = static_cast<long long>(std::time(nullptr));
        db.addScore(hs, &m_scoreIsBest);
        db.close();
    }
    gLog("[SCORE] %s — %d pts (%s), %d days, %s\n",
         won ? "VICTORY" : "DEFEAT", m_finalScore.score, m_finalScore.rank.c_str(),
         m_finalScore.days, m_scoreIsBest ? "NEW BEST" : "recorded");
}

// Shared renderer for the score block inside the victory/defeat modals.
static void drawScoreBlock(const GameScore& s, bool isBest)
{
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Score: %d", s.score);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.8f, 0.85f, 1.0f, 1.0f), "  [%s]", s.rank.c_str());
    if (isBest) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "  NEW BEST!");
    }
    ImGui::Spacing();
    for (const auto& b : s.breakdown)
        ImGui::TextDisabled("   %-30s %+d", b.first.c_str(), b.second);
}

void Game::renderVictoryModal()
{
    recordFinalScore(true);
    ImGui::OpenPopup("Victory!");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Victory!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* msg = m_victoryMessage.empty()
                        ? "All enemies have been defeated!"
                        : m_victoryMessage.c_str();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f), "%s", msg);
        ImGui::Spacing();
        ImGui::TextDisabled("Day %d  Week %d  |  Gold: %d",
                            m_turns.day(), m_turns.week(),
                            m_playerResources.get(ResourceType::Gold));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        {
            // Rank badge: a faction unit whose tier rises with the score.
            int rfac = std::clamp(m_finalScore.faction, 0, NUM_FACTIONS - 1);
            int rti  = std::clamp(scoreRankTier(m_finalScore.score), 1, NUM_UNIT_TIERS) - 1;
            if (m_unitTex[rfac][rti].ok()) {
                ImGui::Image((ImTextureID)(uintptr_t)m_unitTex[rfac][rti].id(),
                             ImVec2(72, 72), ImVec2(0, 0), ImVec2(0.125f, 1.0f));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            drawScoreBlock(m_finalScore, m_scoreIsBest);
            ImGui::EndGroup();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bw = ImGui::GetWindowWidth() - 32.0f;
        if (ImGui::Button("Continue Exploring", ImVec2(bw * 0.55f, 36))) {
            m_showVictory = false;
            m_audio.playMusic("worldmap_music");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Main Menu", ImVec2(-1, 36))) {
            m_showVictory = false;
            m_state = GameState::MainMenu;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Defeat modal ──────────────────────────────────────────────────────────────
void Game::renderDefeatModal()
{
    if (m_finalDefeat) recordFinalScore(false);
    ImGui::OpenPopup("Defeat");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Defeat", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_finalDefeat) {
            ImGui::TextColored(ImVec4(1.0f, 0.15f, 0.15f, 1.0f), "Total defeat!");
            ImGui::Spacing();
            ImGui::TextWrapped("You have no heroes with armies and no towns. There is no way to continue.");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Your army was defeated!");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Day %d  Week %d", m_turns.day(), m_turns.week());
        if (m_finalDefeat) {
            ImGui::Spacing();
            {
                int rfac = std::clamp(m_finalScore.faction, 0, NUM_FACTIONS - 1);
                int rti  = std::clamp(scoreRankTier(m_finalScore.score), 1, NUM_UNIT_TIERS) - 1;
                if (m_unitTex[rfac][rti].ok()) {
                    ImGui::Image((ImTextureID)(uintptr_t)m_unitTex[rfac][rti].id(),
                                 ImVec2(72, 72), ImVec2(0, 0), ImVec2(0.125f, 1.0f));
                    ImGui::SameLine();
                }
                ImGui::BeginGroup();
                drawScoreBlock(m_finalScore, false);
                ImGui::EndGroup();
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bw = ImGui::GetWindowWidth() - 32.0f;
        if (!m_finalDefeat) {
            if (ImGui::Button("Continue (retreat)", ImVec2(bw * 0.55f, 36))) {
                m_showDefeat  = false;
                m_finalDefeat = false;
                m_audio.playMusic("worldmap_music");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Load Last Save", ImVec2(m_finalDefeat ? bw * 0.6f : -1, 36))) {
            m_showDefeat  = false;
            m_finalDefeat = false;
            if (m_activeSaveId) loadGame(m_activeSaveId);
            m_audio.playMusic("worldmap_music");
            ImGui::CloseCurrentPopup();
        }
        if (m_finalDefeat) {
            ImGui::SameLine();
            if (ImGui::Button("Main Menu", ImVec2(-1, 36))) {
                m_showDefeat  = false;
                m_finalDefeat = false;
                m_state = GameState::MainMenu;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

// ── Unit exchange overlay (two player heroes on same tile) ────────────────────
void Game::renderUnitExchange()
{
    if (!m_showUnitExchange) return;
    if (m_heroes.empty()) { m_showUnitExchange = false; return; }

    if (m_exchangeHeroIdx < 0 || m_exchangeHeroIdx >= static_cast<int>(m_heroes.size())
        || m_exchangeHeroIdx == m_activeHeroIdx) {
        m_showUnitExchange = false;
        return;
    }

    Hero& heroA = m_heroes[m_activeHeroIdx];
    Hero& heroB = m_heroes[m_exchangeHeroIdx];

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({7 * (58.0f + 4.0f) + 24.0f, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##exchange", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.2f, 1.0f}, "Hero Exchange");
    ImGui::SameLine(ImGui::GetWindowWidth() - 60);
    if (ImGui::SmallButton("Close")) {
        m_showUnitExchange = false;
        m_exchangeSelSide = -1; m_exchangeSelSlot = -1; m_exchangeSplitMode = false;
        m_exchangeSelArtifactSide = -1; m_exchangeSelArtifactIdx = -1;
    }
    ImGui::Separator();

    // Same slot-grid UI as the town garrison screen: click to select, click a
    // target to move/merge/swap, ctrl+click to split half off.
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "%s", heroA.name.c_str());
    drawUnitSlotRow(0, heroA.army, m_exchangeSelSide, m_exchangeSelSlot, m_exchangeSplitMode);

    ImGui::Separator();

    ImGui::TextColored({0.65f, 0.75f, 0.95f, 1.0f}, "%s", heroB.name.c_str());
    drawUnitSlotRow(1, heroB.army, m_exchangeSelSide, m_exchangeSelSlot, m_exchangeSplitMode);

    // A hero can never be left with an empty army — block a full move (not a
    // split, which always leaves half behind) that would empty the source.
    if (m_slotTransferTargetSide >= 0 && m_exchangeSelSide >= 0) {
        auto& srcArmy = (m_exchangeSelSide == 0) ? heroA.army : heroB.army;
        auto& dstArmy = (m_slotTransferTargetSide == 0) ? heroA.army : heroB.army;
        bool wouldEmpty = !m_exchangeSplitMode && srcArmy.size() == 1
                        && m_exchangeSelSlot >= 0 && m_exchangeSelSlot < (int)srcArmy.size();
        if (wouldEmpty) {
            m_slotTransferTargetSide = -1; m_slotTransferTargetSlot = -1;
            m_exchangeSelSide = -1; m_exchangeSelSlot = -1; m_exchangeSplitMode = false;
        } else {
            resolveSlotTransfer(srcArmy, dstArmy, m_exchangeSelSlot, m_exchangeSplitMode);
            m_exchangeSelSide = -1;
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Click unit to select  |  Click target to move/merge/swap  |  Ctrl+click to split");

    // ── Artifacts ─────────────────────────────────────────────────────────────
    if (!heroA.artifactInventory.empty() || !heroB.artifactInventory.empty()) {
        ImGui::Separator();
        ImGui::TextColored({0.85f, 0.75f, 1.0f, 1.0f}, "Artifacts");

        auto drawArtList = [&](const char* label, Hero& owner, Hero& other, int side) {
            ImGui::BeginGroup();
            ImGui::TextDisabled("%s (%d)", label, (int)owner.artifactInventory.size());
            for (int j = 0; j < (int)owner.artifactInventory.size(); ++j) {
                int aid = owner.artifactInventory[j];
                const ArtifactDef* def = m_artifactRegistry.getDef(aid);
                ImGui::PushID(side * 1000 + j);
                ImGui::TextUnformatted(def ? def->name.c_str() : "Artifact");
                if (def && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", def->description.c_str());
                ImGui::SameLine();
                if (other.artifactInventory.size() + 1 <= 64 && ImGui::SmallButton(side == 0 ? ">>" : "<<")) {
                    other.artifactInventory.push_back(aid);
                    owner.artifactInventory.erase(owner.artifactInventory.begin() + j);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndGroup();
        };

        drawArtList(heroA.name.c_str(), heroA, heroB, 0);
        ImGui::SameLine(0, 24);
        drawArtList(heroB.name.c_str(), heroB, heroA, 1);
    }

    ImGui::End();
}

// ── Dwelling recruit popup ────────────────────────────────────────────────────
void Game::renderDwellingPopup()
{
    if (!m_showDwellingPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingObjId) { obj = &o; break; }
    if (!obj) { m_showDwellingPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##dwelling", nullptr, wf)) { ImGui::End(); return; }

    int tier = obj->value;
    int costPerUnit = tier * 50;
    FactionId fac = static_cast<FactionId>(obj->faction);

    if (ImGui::IsWindowAppearing())
        gLog("[DBG dwelling popup] objId=%u type=%d value=%d faction=%d available=%d collected=%d pos=(%d,%d)\n",
             obj->id, (int)obj->type, obj->value, (int)obj->faction, obj->available, (int)obj->collected,
             obj->pos.q, obj->pos.r);

    // Find unit name for display
    const char* unitName = "Units";
    for (const auto& ud : m_registry.units())
        if (ud.faction == fac && ud.tier == tier && ud.path == UpgradePath::None) {
            unitName = ud.name.c_str(); break;
        }

    ImGui::TextColored({0.9f, 0.7f, 0.2f, 1.0f}, "Unit Dwelling — Tier %d  [%s]", tier, unitName);
    ImGui::Separator();
    ImGui::Text("Available: %d units   |   Cost: %d gold each", obj->available, costPerUnit);
    ImGui::Text("Gold on hand: %d", m_playerResources.get(ResourceType::Gold));
    ImGui::Spacing();

    if (m_heroes.empty()) { m_showDwellingPopup = false; ImGui::End(); return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    int maxAfford = (costPerUnit > 0) ? m_playerResources.get(ResourceType::Gold) / costPerUnit : obj->available;
    int canBuy    = std::min(maxAfford, obj->available);

    // Custom quantity slider
    static int s_dwellingQty = 0;
    if (ImGui::IsWindowAppearing()) s_dwellingQty = canBuy;
    s_dwellingQty = std::clamp(s_dwellingQty, 0, canBuy);

    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderInt("Quantity", &s_dwellingQty, 0, canBuy);
    ImGui::Text("Total cost: %d gold", s_dwellingQty * costPerUnit);
    ImGui::Spacing();

    auto doBuy = [&](int qty) {
        if (qty <= 0) return;
        int total = qty * costPerUnit;
        m_playerResources.add(ResourceType::Gold, -total);
        for (const auto& ud : m_registry.units()) {
            if (ud.faction == fac && ud.tier == tier && ud.path == UpgradePath::None) {
                bool merged = false;
                for (auto& s : hero.army)
                    if (s.defId == ud.id) { s.count += qty; merged = true; break; }
                if (!merged && hero.army.size() < 7)
                    hero.army.push_back({ud.id, qty});
                break;
            }
        }
        obj->available -= qty;
        // Recruiting claims the dwelling: +1 weekly growth of this tier in
        // your towns of the same faction from now on.
        obj->linkedId = static_cast<uint32_t>(currentPlayerId());
        char pickBuf[40];
        std::snprintf(pickBuf, sizeof(pickBuf), "+%d %s", qty, unitName);
        pushPickupEffect(obj->pos, pickBuf, IM_COL32(120, 220, 120, 255));
        m_audio.playSound("pickup");
    };

    if (s_dwellingQty > 0) {
        if (ImGui::Button("Buy", {100, 28})) { doBuy(s_dwellingQty); m_showDwellingPopup = false; }
        ImGui::SameLine();
        if (ImGui::Button("Buy All", {100, 28})) { doBuy(canBuy); m_showDwellingPopup = false; }
        ImGui::SameLine();
    }
    if (ImGui::Button("Close", {80, 28}))
        m_showDwellingPopup = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_showDwellingPopup = false;

    ImGui::End();
}

// ── Stat shrine popup ─────────────────────────────────────────────────────────
void Game::renderStatShrinePopup()
{
    if (!m_showStatShrinePopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingObjId) { obj = &o; break; }
    if (!obj) { m_showStatShrinePopup = false; return; }

    // Stat shrine options — faction-aware bonus rotation (6 options)
    static const char* kStatNames[] = { "Attack", "Defense", "Move", "Mana", "Vision", "Hero HP" };
    const char* statName = kStatNames[obj->value % 6];

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##statshrine", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.5f, 0.1f, 1.0f}, "Stat Shrine");
    ImGui::Separator();
    static const char* kStatAmounts[] = { "+1", "+1", "+2", "+5", "+1", "+10" };
    const char* amt = kStatAmounts[obj->value % 6];
    ImGui::Text("Spend 1000 gold for %s %s?", amt, statName);
    ImGui::Text("Gold: %d   Uses remaining: %d", m_playerResources.get(ResourceType::Gold), obj->questState);
    ImGui::Spacing();

    bool canAfford = m_playerResources.get(ResourceType::Gold) >= 1000;

    if (!canAfford) ImGui::BeginDisabled();
    if (ImGui::Button("Yes", {80, 28})) {
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            m_playerResources.add(ResourceType::Gold, -1000);
            switch (obj->value % 6) {
            case 0: hero.attack  += 1; break;
            case 1: hero.defense += 1; break;
            case 2: hero.maxMove += 2; hero.movePool = std::min(hero.movePool + 2, hero.maxMove); break;
            case 3: hero.maxMana += 5; hero.mana = std::min(hero.mana + 5, hero.maxMana); break;
            case 4: hero.visionRange += 1; FogOfWar::updateVision(m_map, hero); break;
            case 5: hero.heroMaxHp += 10; hero.heroHp = std::min(hero.heroHp + 10, hero.heroMaxHp); break;
            }
            obj->questState--;
            gLog("StatShrine: %s %s, uses left: %d\n", amt, statName, obj->questState);
        }
        m_showStatShrinePopup = false;
    }
    if (!canAfford) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("No", {80, 28}))
        m_showStatShrinePopup = false;

    ImGui::End();
}

// ── Treasure chest popup ─────────────────────────────────────────────────────
void Game::renderTreasureChestPopup()
{
    if (!m_showTreasureChestPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingChestId) { obj = &o; break; }
    if (!obj || obj->collected) { m_showTreasureChestPopup = false; return; }

    static const char* kStatNames[] = { "Attack", "Defense", "Movement" };
    int statType = obj->faction % 3;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##tchest", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "Treasure Chest");
    ImGui::Separator();
    ImGui::TextWrapped("You found a weathered chest. Choose your reward:");
    ImGui::Spacing();

    float bw = ImGui::GetWindowWidth() - 32.0f;

    // Option A: Gold
    char goldLbl[64];
    std::snprintf(goldLbl, sizeof(goldLbl), "Gold  (+%d gold coins)", obj->value);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.42f, 0.05f, 1.0f));
    if (ImGui::Button(goldLbl, ImVec2(bw, 34))) {
        m_playerResources.add(ResourceType::Gold, obj->value);
        char buf[32]; std::snprintf(buf, sizeof(buf), "+%d Gold", obj->value);
        pushPickupEffect(obj->pos, buf, IM_COL32(255, 215, 0, 255));
        m_audio.playSound("buy");
        obj->collected = true;
        m_showTreasureChestPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option B: Experience
    char xpLbl[64];
    std::snprintf(xpLbl, sizeof(xpLbl), "Experience  (+%d XP)", obj->questState);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.15f, 1.0f));
    if (ImGui::Button(xpLbl, ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            int oldLevel = hero.level;
            bool leveled = hero.addXp(obj->questState);
            if (leveled) {
                const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                if (cls) {
                    std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                    m_levelUpOffers = LevelUpSystem::generateOffers(*cls, hero.skills, hero.level, allSkills, hero.faction);
                }
                if (m_levelUpOffers.empty())
                    m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                m_pendingLevelUps = hero.level - oldLevel;
                m_showLevelUpModal = true;
            }
            char buf[32]; std::snprintf(buf, sizeof(buf), "+%d XP", obj->questState);
            pushPickupEffect(obj->pos, buf, IM_COL32(120, 220, 120, 255));
        }
        m_audio.playSound("pickup");
        obj->collected = true;
        m_showTreasureChestPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option C: Stat boost
    char statLbl[64];
    std::snprintf(statLbl, sizeof(statLbl), "Stat Boost  (+1 %s, permanent)", kStatNames[statType]);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
    if (ImGui::Button(statLbl, ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            if (statType == 0)      hero.attack++;
            else if (statType == 1) hero.defense++;
            else                  { hero.maxMove += 2; hero.movePool = std::min(hero.movePool + 2, hero.maxMove); }
            char buf[48]; std::snprintf(buf, sizeof(buf), "+1 %s!", kStatNames[statType]);
            pushPickupEffect(obj->pos, buf, IM_COL32(100, 160, 255, 255));
        }
        m_audio.playSound("pickup");
        obj->collected = true;
        m_showTreasureChestPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::Separator();
    if (ImGui::Button("Leave it", ImVec2(bw, 26)))
        m_showTreasureChestPopup = false;

    ImGui::End();
}

// ── Pickup effect helper ──────────────────────────────────────────────────────
void Game::pushPickupEffect(HexCoord pos, const char* text, ImU32 col)
{
    float wx, wy;
    m_hexRenderer.grid().hexToWorld(pos, wx, wy);
    m_pickupEffects.push_back({wx, wy, 2.0f, text, col});

    // Emit matching particles at screen position
    float sx, sy;
    m_camera.worldToScreen(wx, wy, sx, sy);
    m_particles.emit(sx, sy, ParticlePreset::Pickup, 8);
}

// ── Quest popup ───────────────────────────────────────────────────────────────
void Game::renderQuestPopup()
{
    if (!m_showQuestPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingObjId) { obj = &o; break; }
    if (!obj) { m_showQuestPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##quest", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.1f, 1.0f}, "Quest Available!");
    ImGui::Separator();

    // Varied quest descriptions keyed by id
    static const char* kQuestDesc[] = {
        "A hooded traveller speaks in hushed tones: \"There is a relic hidden beyond these hills — I dare not retrieve it myself. Bring it back and I will reward you handsomely.\"",
        "An elder points toward the horizon: \"Something stirs in that forsaken place. Venture forth and confirm our fears. I shall compensate your bravery well.\"",
        "A wounded scout gasps: \"My companions fell near that cursed site. Find what drove them off and return with proof — gold awaits you.\"",
        "A merchant clutches his pack nervously: \"I lost my ledger at a strange landmark east of here. Retrieve it and five-hundred gold pieces are yours.\"",
        "A hermit emerges from shadows: \"The spirits show me a sign at a place I cannot name. You, traveller — find it and return to me. Your effort will not go unrewarded.\"",
        "A garrison captain frowns: \"We've had reports of unusual activity at a location I've marked. Scout it and report back; there's coin in it for you.\"",
        "A scholar waves a parchment: \"Ancient writings speak of an artifact at these coordinates. Recover it for study and I'll pay you fairly.\"",
        "A cloaked figure steps forward: \"Call it fate that brings you here. Travel to the marked spot, survive what you find, and claim your gold.\"",
    };
    int descIdx = static_cast<int>(obj->id) % (int)(sizeof(kQuestDesc) / sizeof(kQuestDesc[0]));
    ImGui::TextWrapped("%s\n\nReward: 500 gold.", kQuestDesc[descIdx]);
    ImGui::Spacing();

    if (ImGui::Button("Accept", {100, 28})) {
        obj->questState = 1;
        m_showQuestPopup = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Decline", {100, 28}))
        m_showQuestPopup = false;

    ImGui::End();
}

void Game::renderCryptPopup()
{
    if (!m_showCryptPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingCryptId) { obj = &o; break; }
    if (!obj) { m_pendingCryptId = 0; m_showCryptPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##crypt", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.7f, 0.4f, 1.0f, 1.0f}, "Crypt Cleared!");
    ImGui::Separator();
    int goldReward = 200 + obj->value * 150;
    int spellId    = obj->questState; // set when combat wins
    char msg[128];
    std::snprintf(msg, sizeof(msg),
        "The crypt's defenders have fallen. You claim the spoils:\n+%d Gold", goldReward);
    ImGui::TextWrapped("%s", msg);
    if (spellId > 0) {
        const SpellDef* sp = findSpell(spellId);
        if (sp) {
            ImGui::TextColored({0.4f,0.8f,1.0f,1.0f}, "Spell: %s", sp->name);
        }
    }
    ImGui::Spacing();
    float bw = ImGui::GetWindowWidth() - 32.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.2f, 0.55f, 1.0f));
    if (ImGui::Button("Claim Reward", ImVec2(bw, 34))) {
        m_playerResources.add(ResourceType::Gold, goldReward);
        char buf[32]; std::snprintf(buf, sizeof(buf), "+%d Gold (Crypt)", goldReward);
        pushPickupEffect(obj->pos, buf, IM_COL32(200, 140, 255, 255));
        if (spellId > 0 && !m_heroes.empty()) {
            Hero& h = m_heroes[m_activeHeroIdx];
            bool known = false;
            for (int k : h.knownSpells) if (k == spellId) { known = true; break; }
            if (!known) h.knownSpells.push_back(spellId);
        }
        obj->collected = true;
        m_pendingCryptId = 0;
        m_showCryptPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

void Game::renderUtopiaPopup()
{
    if (!m_showUtopiaPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingUtopiaId) { obj = &o; break; }
    if (!obj || obj->collected) { m_pendingUtopiaId = 0; m_showUtopiaPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##utopia", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.1f, 1.0f}, "Utopia Conquered!");
    ImGui::Separator();
    ImGui::TextWrapped("Ancient guardians have been vanquished. Choose your reward:");
    ImGui::Spacing();

    float bw = ImGui::GetWindowWidth() - 32.0f;

    // Option A: Rare artifact (give a known artifact id if any left)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.35f, 0.05f, 1.0f));
    if (ImGui::Button("Artifact  (Rare equip from the vault)", ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& h = m_heroes[m_activeHeroIdx];
            // Give a random artifact based on reward seed
            int artId = 1 + static_cast<int>(obj->value % 8);
            if (!m_artifactRegistry.getDef(artId)) artId = 1;
            h.artifactInventory.push_back(artId);
            char buf[48]; std::snprintf(buf, sizeof(buf), "Artifact acquired! (Utopia)");
            pushPickupEffect(obj->pos, buf, IM_COL32(255, 200, 50, 255));
        }
        obj->collected = true; m_pendingUtopiaId = 0; m_showUtopiaPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option B: Gold + faction primary resource
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.35f, 0.04f, 1.0f));
    if (ImGui::Button("2000 Gold  +  10x Faction Resource", ImVec2(bw, 34))) {
        m_playerResources.add(ResourceType::Gold, 2000);
        // Give faction-appropriate primary resource
        static const ResourceType kFacRes[] = {
            ResourceType::FaithStones, ResourceType::BloodEssence,
            ResourceType::VerdantSap,  ResourceType::Mercury,
            ResourceType::BloodEssence,ResourceType::Mercury,
            ResourceType::Iron,        ResourceType::BloodEssence,
            ResourceType::FaithStones,
        };
        int fac = obj->faction % 9;
        m_playerResources.add(kFacRes[fac], 10);
        char buf[56];
        std::snprintf(buf, sizeof(buf), "+2000 Gold +10 Resource (Utopia)");
        pushPickupEffect(obj->pos, buf, IM_COL32(255, 215, 0, 255));
        obj->collected = true; m_pendingUtopiaId = 0; m_showUtopiaPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option C: Stack of T5 creatures from the faction
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.38f, 0.15f, 1.0f));
    if (ImGui::Button("Army  (12x T5 units join your force)", ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& h = m_heroes[m_activeHeroIdx];
            // Find a T5 unit for this faction from registry
            int t5DefId = 0;
            for (const auto& ud : m_registry.units()) {
                if (ud.tier == 5 && static_cast<int>(ud.faction) == (obj->faction % 9)) {
                    t5DefId = ud.id; break;
                }
            }
            if (t5DefId == 0) {
                // Fallback: pick any T5 unit
                for (const auto& ud : m_registry.units())
                    if (ud.tier == 5) { t5DefId = ud.id; break; }
            }
            if (t5DefId > 0) {
                bool merged = false;
                for (auto& s : h.army)
                    if (s.defId == t5DefId) { s.count += 12; merged = true; break; }
                if (!merged && h.army.size() < 7)
                    h.army.push_back({t5DefId, 12});
                char buf[56]; std::snprintf(buf, sizeof(buf), "+12 T5 Units (Utopia)");
                pushPickupEffect(obj->pos, buf, IM_COL32(100, 220, 100, 255));
            }
        }
        obj->collected = true; m_pendingUtopiaId = 0; m_showUtopiaPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

// ── Mine info popup (right-click on mine) ────────────────────────────────────
void Game::renderMineInfoPopup()
{
    if (!m_showMineInfoPopup) return;
    const ResourceNode* r = nullptr;
    for (const auto& rn : m_resources)
        if (rn.id == m_mineInfoId) { r = &rn; break; }
    if (!r) { m_showMineInfoPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##mine_info", nullptr, wf)) { ImGui::End(); return; }

    bool ownedByOtherHuman = (m_numHumanPlayers >= 2 &&
                               r->ownedBy != 0 &&
                               r->ownedBy != static_cast<uint32_t>(currentPlayerId()) &&
                               r->ownedBy <= static_cast<uint32_t>(m_numHumanPlayers));
    const char* status = r->ownedBy == static_cast<uint32_t>(currentPlayerId()) ? " [Owned]"
                       : ownedByOtherHuman                                       ? " [Enemy]"
                       : r->guardBeaten  ? " [Captured by AI]"
                       : " [Guarded]";
    ImGui::TextColored({0.9f, 0.75f, 0.2f, 1.0f}, "%s Mine%s  +%d/wk",
                       resourceName(r->type), status, r->amount);
    ImGui::Separator();

    if (r->guardBeaten || r->ownedBy == static_cast<uint32_t>(currentPlayerId()) || ownedByOtherHuman) {
        ImGui::TextDisabled(ownedByOtherHuman ? "Held by opposing player — capture to claim income."
                                              : "No defenders — mine is unguarded.");
    } else {
        ImGui::TextColored({0.95f, 0.45f, 0.45f, 1.0f}, "Defenders:");
        auto guards = makeMineGuardUnits(*r);
        int guardPower = 0;
        for (const auto& g : guards) {
            ImGui::Text("  %-20s x%-4d  ATK %d  DEF %d  HP %d  SPD %d",
                        g.name.c_str(), g.count, g.attack, g.defense, g.maxHp, g.speed);
            guardPower += g.count * g.maxHp * (g.attack + g.defense / 2);
        }

        int playerPower = 0;
        if (!m_heroes.empty()) {
            const Hero& h = m_heroes[m_activeHeroIdx];
            for (const auto& stack : h.army) {
                const UnitDef* ud = m_registry.getUnitDef(stack.defId);
                if (ud) playerPower += stack.count * ud->hp * (ud->attack + ud->defense / 2);
            }
        }

        ImGui::Separator();
        if (playerPower > 0) {
            float ratio = static_cast<float>(guardPower) / static_cast<float>(playerPower);
            const char* rating;
            ImVec4 col;
            if      (ratio < 0.30f) { rating = "Trivial";      col = {0.5f, 1.0f, 0.5f, 1.0f}; }
            else if (ratio < 0.60f) { rating = "Weak";         col = {0.7f, 1.0f, 0.5f, 1.0f}; }
            else if (ratio < 0.90f) { rating = "Moderate";     col = {1.0f, 0.9f, 0.4f, 1.0f}; }
            else if (ratio < 1.10f) { rating = "Even Match";   col = {1.0f, 0.7f, 0.2f, 1.0f}; }
            else if (ratio < 1.50f) { rating = "Strong";       col = {1.0f, 0.4f, 0.3f, 1.0f}; }
            else                    { rating = "Overwhelming"; col = {0.9f, 0.1f, 0.1f, 1.0f}; }
            ImGui::Text("Threat vs your army: ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(col, " %s", rating);
        } else {
            ImGui::TextDisabled("(no hero selected to compare)");
        }
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 80.0f) * 0.5f);
    if (ImGui::Button("Close", {80, 0}))
        m_showMineInfoPopup = false;
    ImGui::End();
}

// ── Hero quick-sheet popup (right-click on a hero) ────────────────────────────
// Right-clicking any hero on the world map opens this card. Your own heroes
// show the full sheet — stats (with artifact totals folded in), army and the
// equipped-artifact list. Enemy heroes reveal only the coarse scouting view:
// name, faction, level and army composition, never their artifacts.
void Game::renderHeroSheetPopup()
{
    if (!m_showHeroSheetPopup) return;

    static const char* kFactionNames[] = {
        "Holy Order", "Crimson Wardens", "Thornkin", "Eternal Empire",
        "Bloodsworn", "Voidkin", "Iron Assembly", "Amalgamate", "Convergence"
    };

    // Locate the hero by id. Ownership: found in m_heroes => the active player's.
    const Hero* hero = nullptr;
    bool owned = false;
    for (const auto& h : m_heroes)
        if (h.id == m_heroSheetId) { hero = &h; owned = true; break; }
    if (!hero) {
        for (const auto& h : m_enemyHeroes)
            if (h.id == m_heroSheetId) { hero = &h; break; }
    }
    if (!hero) {
        for (int pi = 0; pi < m_numHumanPlayers && !hero; ++pi) {
            if (pi == m_currentPlayerIdx) continue;
            for (const auto& h : m_players[pi].heroes)
                if (h.id == m_heroSheetId) { hero = &h; break; }
        }
    }
    if (!hero) { m_showHeroSheetPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##hero_sheet", nullptr, wf)) { ImGui::End(); return; }

    int fi = static_cast<int>(hero->faction);
    const char* fname = (fi >= 0 && fi < 9) ? kFactionNames[fi] : "Unaligned";
    const HeroClassDef* cls = m_classRegistry.getClass(hero->classId);

    // Header
    if (owned)
        ImGui::TextColored({0.95f, 0.85f, 0.35f, 1.0f}, "%s", hero->name.c_str());
    else
        ImGui::TextColored({0.95f, 0.45f, 0.45f, 1.0f}, "%s  [Enemy]", hero->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(" Lv %d", hero->level);
    ImGui::TextDisabled("%s  \xe2\x80\xa2  %s", fname, cls ? cls->name.c_str() : "Adventurer");
    ImGui::Separator();

    // Primary stats — fold equipped-artifact bonuses into the shown totals.
    ArtifactBonus ab = m_artifactRegistry.totalBonus(hero->artifacts);
    auto statLine = [](const char* label, int base, int bonus) {
        if (bonus != 0)
            ImGui::Text("%-8s %d  (%+d)", label, base + bonus, bonus);
        else
            ImGui::Text("%-8s %d", label, base);
    };
    statLine("Attack",  hero->attack,  ab.attack);
    statLine("Defense", hero->defense, ab.defense);
    if (owned) {
        ImGui::Text("Move     %d / %d", hero->movePool, hero->maxMove);
        ImGui::Text("Mana     %d / %d", hero->mana, hero->maxMana);
        ImGui::TextDisabled("XP %d / %d   \xe2\x80\xa2   Battles won %d",
                            hero->xp, hero->xpToNext, hero->battlesWon);
    }
    ImGui::Separator();

    // Army composition (shown for owned and enemy heroes alike — coarse scouting).
    ImGui::TextColored({0.75f, 0.8f, 0.9f, 1.0f}, "Army:");
    bool anyStack = false;
    for (const auto& stack : hero->army) {
        if (stack.count <= 0) continue;
        anyStack = true;
        const UnitDef* ud = m_registry.getUnitDef(stack.defId);
        ImGui::BulletText("%-20s x%d", ud ? ud->name.c_str() : "Unknown", stack.count);
    }
    if (!anyStack) ImGui::TextDisabled("  (no troops)");

    // Equipped artifacts — own heroes only.
    if (owned) {
        static const char* slotNames[] = {
            "Helm","Armor","Weapon","Shield","Ring","Boots","Cloak","Misc"
        };
        ImGui::Separator();
        ImGui::TextColored({0.8f, 0.7f, 0.4f, 1.0f}, "Artifacts:");
        bool anyArt = false;
        for (int i = 0; i < HeroArtifacts::SLOT_COUNT; ++i) {
            int aid = hero->artifacts.equippedIds[i];
            if (!aid) continue;
            const ArtifactDef* def = m_artifactRegistry.getDef(aid);
            if (!def) continue;
            anyArt = true;
            ImGui::BulletText("%-8s %s", slotNames[i], def->name.c_str());
            if (ImGui::IsItemHovered() && !def->description.empty())
                ImGui::SetTooltip("%s", def->description.c_str());
        }
        if (!anyArt) ImGui::TextDisabled("  (none equipped — press F7 to manage)");
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 80.0f) * 0.5f);
    if (ImGui::Button("Close", {80, 0}))
        m_showHeroSheetPopup = false;
    ImGui::End();
}

// ── Tree of Knowledge popup ───────────────────────────────────────────────────
void Game::renderTreeOfKnowledgePopup()
{
    if (!m_showTreeKnowledgePopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingTreeId) { obj = &o; break; }
    if (!obj || obj->collected) { m_showTreeKnowledgePopup = false; return; }
    if (m_heroes.empty()) { m_showTreeKnowledgePopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    int goldCost  = 2000;
    int freeXp    = 200 * hero.level;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##treknowledge", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.3f, 0.9f, 0.4f, 1.0f}, "Tree of Knowledge");
    ImGui::Separator();
    ImGui::TextWrapped("An ancient tree pulses with accumulated wisdom. It offers two paths:");
    ImGui::Spacing();

    ImGui::BulletText("Pay %d Gold -> gain one full level", goldCost);
    ImGui::BulletText("Accept freely -> gain %d XP", freeXp);
    ImGui::Spacing();
    ImGui::Text("Hero: %s  (Level %d)  Gold: %d",
        hero.name.c_str(), hero.level, m_playerResources.get(ResourceType::Gold));
    ImGui::Spacing();

    bool canAfford = m_playerResources.get(ResourceType::Gold) >= goldCost;

    if (!canAfford) ImGui::BeginDisabled();
    if (ImGui::Button("Pay Gold (+1 Level)", {160, 28})) {
        m_playerResources.add(ResourceType::Gold, -goldCost);
        obj->collected = true;
        m_showTreeKnowledgePopup = false;
        // Force-level: give enough XP to trigger level-up
        int xpNeeded = hero.xpToNext - hero.xp;
        int oldLvl = hero.level;
        hero.addXp(xpNeeded);
        pushPickupEffect(obj->pos, "Level Up!", IM_COL32(80, 220, 100, 255));
        const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
        if (cls) {
            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
            m_levelUpOffers = LevelUpSystem::generateOffers(
                *cls, hero.skills, hero.level, allSkills, hero.faction);
        }
        if (m_levelUpOffers.empty())
            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
        m_pendingLevelUps = hero.level - oldLvl;
        m_showLevelUpModal = true;
        m_audio.playSound("levelup");
        ScriptContext lvCtx; lvCtx.heroId = hero.id;
        m_triggers.fire(TriggerType::HeroLevel, lvCtx);
    }
    if (!canAfford) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Take XP", {80, 28})) {
        obj->collected = true;
        m_showTreeKnowledgePopup = false;
        int oldLvl = hero.level;
        char buf[32]; std::snprintf(buf, sizeof(buf), "+%d XP", freeXp);
        pushPickupEffect(obj->pos, buf, IM_COL32(100, 220, 130, 255));
        m_audio.playSound("pickup");
        if (hero.addXp(freeXp)) {
            const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
            if (cls) {
                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                m_levelUpOffers = LevelUpSystem::generateOffers(
                    *cls, hero.skills, hero.level, allSkills, hero.faction);
            }
            if (m_levelUpOffers.empty())
                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
            m_pendingLevelUps = hero.level - oldLvl;
            m_showLevelUpModal = true;
            m_audio.playSound("levelup");
            ScriptContext lvCtx; lvCtx.heroId = hero.id;
            m_triggers.fire(TriggerType::HeroLevel, lvCtx);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Pass", {60, 28}))
        m_showTreeKnowledgePopup = false;

    ImGui::End();
}

// ── Pre-combat encounter prompt ───────────────────────────────────────────────
void Game::renderEncounterPrompt()
{
    if (!m_showEncounterPrompt) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({420, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##encounter_prompt", nullptr, wf)) { ImGui::End(); return; }

    // Title
    ImGui::TextColored({1.0f, 0.55f, 0.15f, 1.0f}, "%s", m_encounterTitle.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    // Enemy unit list
    ImGui::TextColored({0.95f, 0.45f, 0.45f, 1.0f}, "Defenders:");
    int guardPower = 0;
    for (const auto& u : m_pendingEncounterUnits) {
        ImGui::Text("  %-22s x%-4d  ATK %d  DEF %d  HP %d",
                    u.name.c_str(), u.count, u.attack, u.defense, u.maxHp);
        guardPower += u.count * u.maxHp * (u.attack + u.defense / 2);
    }

    // Threat rating vs player army
    ImGui::Spacing();
    ImGui::Separator();
    int playerPower = 0;
    if (!m_heroes.empty()) {
        const Hero& h = m_heroes[m_activeHeroIdx];
        for (const auto& stack : h.army) {
            const UnitDef* ud = m_registry.getUnitDef(stack.defId);
            if (ud) playerPower += stack.count * ud->hp * (ud->attack + ud->defense / 2);
        }
    }
    if (playerPower > 0 && guardPower > 0) {
        float ratio = static_cast<float>(guardPower) / static_cast<float>(playerPower);
        const char* rating;
        ImVec4 col;
        if      (ratio < 0.30f) { rating = "Trivial";      col = {0.5f, 1.0f, 0.5f, 1.0f}; }
        else if (ratio < 0.60f) { rating = "Weak";         col = {0.7f, 1.0f, 0.5f, 1.0f}; }
        else if (ratio < 0.90f) { rating = "Moderate";     col = {1.0f, 0.9f, 0.4f, 1.0f}; }
        else if (ratio < 1.10f) { rating = "Even Match";   col = {1.0f, 0.7f, 0.2f, 1.0f}; }
        else if (ratio < 1.50f) { rating = "Strong";       col = {1.0f, 0.4f, 0.3f, 1.0f}; }
        else                    { rating = "Overwhelming"; col = {0.9f, 0.1f, 0.1f, 1.0f}; }
        ImGui::Text("Threat: ");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(col, " %s", rating);
    } else {
        ImGui::TextDisabled("(no army to compare)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Fight button (green)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.55f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.70f, 0.20f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.40f, 0.10f, 1.0f});
    if (ImGui::Button("Fight!", {140, 32})) {
        m_showEncounterPrompt = false;
        if (m_encounterOnAccept) m_encounterOnAccept();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 20);

    // Retreat button (dark red)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.50f, 0.12f, 0.12f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.65f, 0.18f, 0.18f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.35f, 0.08f, 0.08f, 1.0f});
    if (ImGui::Button("Retreat", {140, 32})) {
        m_showEncounterPrompt = false;
        if (m_encounterOnDecline) m_encounterOnDecline();
    }
    ImGui::PopStyleColor(3);

    ImGui::End();
}

// ── Kingdom Overview panel ────────────────────────────────────────────────────
void Game::renderKingdomPanel()
{
    if (!m_showKingdomPanel) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Once, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({560, 480}, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.97f);

    if (!ImGui::Begin("Kingdom Overview", &m_showKingdomPanel,
        ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }

    // ── Visual helpers: pull thumbnails straight from the loaded art. Each
    //    guards on .ok(); a missing sheet falls back to reserved blank space so
    //    layout never shifts and no garbage-texture quad is drawn.
    auto factionPortrait = [&](FactionId f) -> ImTextureID {
        int fi = std::clamp(static_cast<int>(f), 0, NUM_FACTIONS - 1);
        return m_portraitTex[fi].ok()
             ? (ImTextureID)(uintptr_t)m_portraitTex[fi].id() : nullptr;
    };
    auto factionCrest = [&](FactionId f) -> ImTextureID {
        int fi = std::clamp(static_cast<int>(f), 0, NUM_FACTIONS - 1);
        return m_townTex[fi].ok()
             ? (ImTextureID)(uintptr_t)m_townTex[fi].id() : nullptr;
    };
    // Unit sprite sheets are horizontal strips; the first frame is the left 1/8.
    auto drawUnitIcon = [&](int defId, int count, float sz) {
        const UnitDef* ud = m_registry.getUnitDef(defId);
        ImTextureID t = nullptr;
        if (ud) {
            int fid = std::clamp(static_cast<int>(ud->faction), 0, NUM_FACTIONS - 1);
            int tid = std::clamp(ud->tier - 1, 0, NUM_UNIT_TIERS - 1);
            if (m_unitTex[fid][tid].ok())
                t = (ImTextureID)(uintptr_t)m_unitTex[fid][tid].id();
        }
        if (t) ImGui::Image(t, {sz, sz}, {0.0f, 0.0f}, {0.125f, 1.0f});
        else   ImGui::Dummy({sz, sz});
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s x%d", ud ? ud->name.c_str() : "Unit", count);
        ImGui::SameLine(0, 2);
        ImGui::Text("x%d", count);
    };
    // Resource icons live in the shared 8x6 HUD atlas at ICO_RES_GOLD (9) + type.
    ImTextureID resAtlas = m_iconTex.ok() ? (ImTextureID)(uintptr_t)m_iconTex.id() : nullptr;
    auto drawResIcon = [&](int ri, float sz) {
        if (resAtlas) {
            int idx    = 9 + ri;
            float col  = static_cast<float>(idx % 8);
            float row  = static_cast<float>(idx / 8);
            ImGui::Image(resAtlas, {sz, sz},
                         {col / 8.0f, row / 6.0f}, {(col + 1.0f) / 8.0f, (row + 1.0f) / 6.0f});
        } else {
            ImGui::Dummy({sz, sz});
        }
    };

    static const char* kFactionNames[] = {
        "Holy Order", "Crimson Wardens", "Thornkin", "Eternal Empire",
        "Bloodsworn", "Voidkin", "Iron Assembly", "Amalgamate", "Convergence"
    };

    // ── HEROES ────────────────────────────────────────────────────────────────
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "HEROES");
    ImGui::Separator();

    for (const auto& h : m_heroes) {
        ImGui::PushID(static_cast<int>(h.id));
        ImTextureID port = factionPortrait(h.faction);
        if (port) { ImGui::Image(port, {30, 30}); ImGui::SameLine(0, 6); }
        char hdr[120];
        std::snprintf(hdr, sizeof(hdr), "%s  (Level %d  ATK %d  DEF %d  MP %d/%d)",
                      h.name.c_str(), h.level, h.attack, h.defense, h.mana, h.maxMana);
        bool open = ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            // Move bar
            float mv = h.maxMove > 0 ? static_cast<float>(h.movePool) / h.maxMove : 0.0f;
            ImGui::ProgressBar(mv, ImVec2(-1, 8), "");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Movement: %d / %d", h.movePool, h.maxMove);

            // Army — one sprite thumbnail per stack
            ImGui::TextColored({0.7f, 1.0f, 0.7f, 1.0f}, "Army:");
            bool first = true;
            for (const auto& s : h.army) {
                if (s.count <= 0) continue;
                ImGui::SameLine(0, 8);
                drawUnitIcon(s.defId, s.count, 22.0f);
                first = false;
            }
            if (first) { ImGui::SameLine(); ImGui::TextDisabled("(none)"); }

            // Known world-map spells
            bool hasWS = false;
            for (int sid : h.knownSpells) {
                const SpellDef* sp = findSpell(sid);
                if (sp && sp->target == SpellTarget::WorldMap) {
                    if (!hasWS) { ImGui::TextColored({0.7f, 0.7f, 1.0f, 1.0f}, "Spells:"); ImGui::SameLine(); hasWS = true; }
                    ImGui::TextColored({0.8f, 0.8f, 1.0f, 1.0f}, "[%s]", sp->name);
                    ImGui::SameLine();
                }
            }
            if (hasWS) ImGui::NewLine();

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();

    // ── RIVAL HEROES (strongest first) ─────────────────────────────────────────
    //    The panel used to show only your own heroes, so in Watch mode you could
    //    never see the leading AI's actual army — only aggregate Str in the HUD.
    //    List every rival hero ranked by strength, top one flagged, each with its
    //    unit stacks so you can see WHAT the strongest player is fielding.
    {
        const auto& udefs = m_registry.units();
        std::vector<const Hero*> rivals;
        for (const auto& h : m_enemyHeroes)
            if (!h.eliminated) rivals.push_back(&h);
        std::sort(rivals.begin(), rivals.end(), [&](const Hero* a, const Hero* b){
            return heroStrength(*a, udefs) > heroStrength(*b, udefs);
        });
        ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "RIVAL HEROES");
        ImGui::Separator();
        if (rivals.empty()) {
            ImGui::TextDisabled("No rival heroes on the map.");
        } else {
            int shown = 0;
            for (const Hero* rp : rivals) {
                if (shown >= 8) break;               // keep the panel bounded
                const Hero& h = *rp;
                ImGui::PushID(9000 + shown);
                ImTextureID port = factionPortrait(h.faction);
                if (port) { ImGui::Image(port, {26, 26}); ImGui::SameLine(0, 6); }
                bool isLeader = (shown == 0);
                char hdr[160];
                std::snprintf(hdr, sizeof(hdr),
                    "%sP%u  %s  (Lv %d, Str %lld)%s",
                    isLeader ? ">" : " ", h.ownerId, h.name.c_str(), h.level,
                    (long long)heroStrength(h, udefs),
                    isLeader ? "   <== STRONGEST" : "");
                bool open = ImGui::TreeNodeEx(hdr,
                    isLeader ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                if (open) {
                    ImGui::TextColored({0.9f, 0.75f, 0.75f, 1.0f}, "Army:");
                    bool any = false;
                    for (const auto& s : h.army) {
                        if (s.count <= 0) continue;
                        ImGui::SameLine(0, 8);
                        drawUnitIcon(s.defId, s.count, 22.0f);
                        any = true;
                    }
                    if (!any) { ImGui::SameLine(); ImGui::TextDisabled("(none)"); }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                ++shown;
            }
        }
    }

    ImGui::Spacing();

    // ── TOWNS ─────────────────────────────────────────────────────────────────
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "TOWNS");
    ImGui::Separator();

    bool anyTown = false;
    int  townPid = 0;
    for (const auto& t : m_towns) {
        if (t.ownerId != static_cast<uint32_t>(currentPlayerId())) continue;
        anyTown = true;
        ImGui::PushID(4000 + townPid++);
        int fi = static_cast<int>(t.faction);
        ImTextureID crest = factionCrest(t.faction);
        if (crest) { ImGui::Image(crest, {28, 28}); ImGui::SameLine(0, 6); }
        char thdr[120];
        std::snprintf(thdr, sizeof(thdr), "%s  [%s]  %d buildings",
                      t.name.c_str(),
                      (fi >= 0 && fi < 9) ? kFactionNames[fi] : "?",
                      static_cast<int>(t.builtBuildings.size()));
        if (ImGui::TreeNodeEx(thdr, ImGuiTreeNodeFlags_Leaf)) {
            // Garrison — sprite thumbnails
            if (!t.garrison.empty()) {
                ImGui::TextColored({0.9f, 0.8f, 0.5f, 1.0f}, "Garrison:");
                for (const auto& s : t.garrison) {
                    if (s.count <= 0) continue;
                    ImGui::SameLine(0, 8);
                    drawUnitIcon(s.defId, s.count, 22.0f);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!anyTown) ImGui::TextDisabled("No towns owned.");

    ImGui::Spacing();

    // ── MINES & INCOME ────────────────────────────────────────────────────────
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "MINES & INCOME");
    ImGui::Separator();

    // Aggregate mines by resource type
    int mineCount[RESOURCE_COUNT] = {};
    int mineIncome[RESOURCE_COUNT] = {};
    for (const auto& r : m_resources) {
        if (r.ownedBy != static_cast<uint32_t>(currentPlayerId())) continue;
        int ri = static_cast<int>(r.type);
        mineCount[ri]++;
        mineIncome[ri] += r.amount;
    }

    static constexpr ImVec4 kResCol[] = {
        {1.0f, 0.82f, 0.2f, 1.0f},  // Gold
        {0.65f,0.72f,0.80f, 1.0f},  // Iron
        {0.91f,0.89f,1.00f, 1.0f},  // FaithStones
        {0.85f,0.25f,0.25f, 1.0f},  // BloodEssence
        {0.30f,0.75f,0.35f, 1.0f},  // VerdantSap
        {0.20f,0.72f,0.65f, 1.0f},  // Mercury
    };

    ImGui::Columns(4, "mines_cols", true);
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Resource");     ImGui::NextColumn();
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Mines owned");  ImGui::NextColumn();
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Income / wk");  ImGui::NextColumn();
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Current");      ImGui::NextColumn();
    ImGui::Separator();
    for (int i = 0; i < RESOURCE_COUNT; ++i) {
        auto rt = static_cast<ResourceType>(i);
        ImVec4 col = (i < 6) ? kResCol[i] : ImVec4(1,1,1,1);
        drawResIcon(i, 16.0f); ImGui::SameLine(0, 5);
        ImGui::TextColored(col, "%s", resourceName(rt));     ImGui::NextColumn();
        ImGui::Text("%d", mineCount[i]);                      ImGui::NextColumn();
        if (mineIncome[i] > 0)
            ImGui::TextColored({0.4f,1.f,0.4f,1.f}, "+%d", mineIncome[i]);
        else
            ImGui::TextDisabled("0");
        ImGui::NextColumn();
        ImGui::Text("%d", m_playerResources.get(rt));         ImGui::NextColumn();
    }
    ImGui::Columns(1);

    ImGui::Spacing();
    float bw = ImGui::GetWindowWidth() - 32.0f;
    if (ImGui::Button("Close", ImVec2(bw, 28)))
        m_showKingdomPanel = false;

    ImGui::End();
}

// ── Mini-map ──────────────────────────────────────────────────────────────────
// Minimap rendering is handled inside renderWorldOverlay(), toggled with M key.
void Game::renderMinimap() {}

// ── World-map spell panel ─────────────────────────────────────────────────────
void Game::renderWorldSpellPanel()
{
    if (m_heroes.empty()) { m_showWorldSpellPanel = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({8.0f, io.DisplaySize.y - 340.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({220, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    if (!ImGui::Begin("World Spells", &m_showWorldSpellPanel,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }

    ImGui::Text("Mana: %d / %d", hero.mana, hero.maxMana);
    ImGui::Separator();

    bool any = false;
    for (int sid : hero.knownSpells) {
        const SpellDef* spl = findSpell(sid);
        if (!spl || spl->target != SpellTarget::WorldMap) continue;
        any = true;
        bool canAfford = hero.mana >= spl->manaCost;
        if (!canAfford) ImGui::BeginDisabled();
        char label[128];
        std::snprintf(label, sizeof(label), "%dmana  %s", spl->manaCost, spl->name);
        if (ImGui::Button(label, ImVec2(-1, 0)))
            castWorldSpell(sid);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", spl->desc);
        if (!canAfford) ImGui::EndDisabled();
    }
    if (!any) ImGui::TextDisabled("No world-map spells known.");
    ImGui::End();
}

// ── Land connectivity flood fill ──────────────────────────────────────────────
// One BFS over every land tile, labelling connected components. Lets the AI
// reject an unreachable land target in O(1) instead of burning a full 400-hex
// A* discovering the same thing (~76 ms a call, the dominant turn cost).
void Game::rebuildLandComponents()
{
    auto flood = [this](std::unordered_map<HexCoord, int, HexCoordHash>& comp,
                        auto&& passable) {
        comp.clear();
        int id = 0;
        std::vector<HexCoord> stack;
        for (const HexCoord& start : m_map.coords()) {
            if (!passable(start))              continue;
            if (comp.find(start) != comp.end()) continue;
            ++id;
            stack.clear();
            stack.push_back(start);
            comp[start] = id;
            while (!stack.empty()) {
                HexCoord cur = stack.back();
                stack.pop_back();
                for (const HexCoord& nb : HexGrid::neighbors(cur)) {
                    if (!passable(nb)) continue;
                    if (comp.find(nb) != comp.end()) continue;
                    comp[nb] = id;
                    stack.push_back(nb);
                }
            }
        }
    };
    flood(m_landComp, [this](HexCoord c) {
        const HexTile* t = m_map.getTile(c);
        return t && !t->blocked
            && t->terrain != Terrain::Water && t->terrain != Terrain::Mountain;
    });
    // Amphibious map for heroes already afloat: Water is traversable and any
    // land tile is a disembark point, so only Mountain/Barrier split components.
    flood(m_seaComp, [this](HexCoord c) {
        const HexTile* t = m_map.getTile(c);
        return t && !t->blocked && t->terrain != Terrain::Mountain;
    });
    m_landCompTurn = m_turns.week() * 100 + m_turns.day();
}

// ── Lighthouse ownership → sea-speed bonus ────────────────────────────────────
void Game::refreshLighthouseBoosts()
{
    auto ownerHasBeacon = [&](uint32_t owner) -> bool {
        if (owner == 0) return false;
        for (const auto& o : m_worldObjects)
            if (o.type == WorldObjectType::Lighthouse
                && o.faction == static_cast<int>(owner)) return true;
        return false;
    };
    bool playerHas = ownerHasBeacon(static_cast<uint32_t>(currentPlayerId()));
    for (auto& h : m_heroes)      h.lighthouseBoost = playerHas;
    for (auto& h : m_enemyHeroes) h.lighthouseBoost = ownerHasBeacon(h.ownerId);
}

// ── castWorldSpell ────────────────────────────────────────────────────────────
void Game::castWorldSpell(int spellId)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    const SpellDef* spl = findSpell(spellId);
    if (!spl || hero.mana < spl->manaCost) return;

    if (spellId == SPL::VISIONS) {
        hero.mana -= spl->manaCost;
        m_audio.playSound("spell");
        // Temporarily expand vision to reveal radius around hero
        int saved = hero.visionRange;
        hero.visionRange = spl->power;
        FogOfWar::updateVision(m_map, hero);
        hero.visionRange = saved;
        // Count revealed entities for feedback
        int found = 0;
        for (const auto& eh : m_enemyHeroes)
            if (HexGrid::distance(hero.pos, eh.pos) <= spl->power) ++found;
        for (const auto& obj : m_worldObjects)
            if (!obj.collected && HexGrid::distance(hero.pos, obj.pos) <= spl->power) ++found;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Visions: %d things revealed", found);
        pushPickupEffect(hero.pos, buf, IM_COL32(160, 160, 255, 255));
        m_showWorldSpellPanel = false;
    }
    else if (spellId == SPL::TOWN_PORTAL) {
        if (hero.movePool < hero.maxMove) {
            pushPickupEffect(hero.pos,
                "Town Portal needs full movement — hero already moved!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        hero.mana -= spl->manaCost;
        m_audio.playSound("spell");
        m_showWorldSpellPanel = false;
        m_showTownPortalPopup = true;
    }
    else if (spellId == SPL::FOUND_CITY) {
        if (hero.level < 10) {
            pushPickupEffect(hero.pos, "Found City requires hero level 10!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        // Must stand on a cleared Utopia
        WorldObject* utopia = nullptr;
        for (auto& obj : m_worldObjects)
            if (obj.type == WorldObjectType::Utopia && obj.pos == hero.pos && obj.collected)
                { utopia = &obj; break; }
        if (!utopia) {
            pushPickupEffect(hero.pos, "Found City: must stand on a cleared Utopia!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        // Check cost: 10 000 gold + 10 each other resource
        Resources cost;
        cost.set(ResourceType::Gold,         10000);
        cost.set(ResourceType::Iron,            10);
        cost.set(ResourceType::FaithStones,     10);
        cost.set(ResourceType::BloodEssence,    10);
        cost.set(ResourceType::VerdantSap,      10);
        cost.set(ResourceType::Mercury,         10);
        if (!m_playerResources.canAfford(cost)) {
            pushPickupEffect(hero.pos, "Found City: insufficient resources!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        m_foundCityUtopiaId = utopia->id;
        hero.mana -= spl->manaCost;
        m_audio.playSound("spell");
        m_showWorldSpellPanel = false;
        m_showFoundCityPopup  = true;
    }
}

// ── Town Portal popup ─────────────────────────────────────────────────────────
void Game::renderTownPortalPopup()
{
    if (!m_showTownPortalPopup) return;
    if (m_heroes.empty()) { m_showTownPortalPopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({320, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##townportal", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "Town Portal");
    ImGui::Separator();
    ImGui::TextWrapped("Choose a destination (all movement spent on arrival):");
    ImGui::Spacing();

    // Collect player towns sorted by distance
    std::vector<std::pair<int,Town*>> options;
    for (auto& t : m_towns)
        if (t.ownerId == static_cast<uint32_t>(currentPlayerId()))
            options.push_back({HexGrid::distance(hero.pos, t.pos), &t});
    std::sort(options.begin(), options.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    if (options.empty()) {
        ImGui::TextDisabled("No friendly towns.");
    }
    for (auto& [dist, t] : options) {
        char label[80];
        std::snprintf(label, sizeof(label), "%s  (%d tiles away)", t->name.c_str(), dist);
        if (ImGui::Button(label, ImVec2(-1, 30))) {
            // Remove hero from old tile
            if (HexTile* ot = m_map.getTile(hero.pos)) ot->heroId = 0;
            hero.pos = t->pos;
            hero.movePool = 0;
            if (HexTile* nt = m_map.getTile(hero.pos)) nt->heroId = hero.id;
            FogOfWar::updateVision(m_map, hero);
            // Snap camera to destination
            float wx, wy;
            m_hexRenderer.grid().hexToWorld(hero.pos, wx, wy);
            m_camera.setPosition(wx, wy);
            pushPickupEffect(hero.pos, "Town Portal!", IM_COL32(100, 180, 255, 255));
            m_showTownPortalPopup = false;
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(-1, 28))) {
        // Refund mana — spell was already deducted before opening popup
        const SpellDef* spl = findSpell(SPL::TOWN_PORTAL);
        if (spl) hero.mana = std::min(hero.maxMana, hero.mana + spl->manaCost);
        m_showTownPortalPopup = false;
    }
    ImGui::End();
}

// ── Found City popup ──────────────────────────────────────────────────────────
void Game::renderFoundCityPopup()
{
    if (!m_showFoundCityPopup) return;
    if (m_heroes.empty()) { m_showFoundCityPopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    // Find the Utopia object by id
    WorldObject* utopia = nullptr;
    for (auto& obj : m_worldObjects)
        if (obj.id == m_foundCityUtopiaId) { utopia = &obj; break; }
    if (!utopia) { m_showFoundCityPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##foundcity", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.3f, 1.0f}, "Found City");
    ImGui::Separator();
    ImGui::TextWrapped("Choose the faction for your new settlement.");
    ImGui::TextWrapped("Cost: 10000 Gold + 10 Iron + 10 Faith Stones + 10 Blood Essence + 10 Verdant Sap + 10 Mercury");
    ImGui::Spacing();

    static const char* kFacNames[] = {
        "Holy Order","Bloodsworn","Thornkin","Eternal Empire",
        "Crimson Wardens","Voidkin","Iron Assembly","Amalgamate","Convergence"
    };
    float bw = ImGui::GetWindowWidth() - 32.0f;
    HexCoord cityPos = utopia->pos;

    for (int i = 0; i < 9; ++i) {
        if (ImGui::Button(kFacNames[i], ImVec2(bw, 30))) {
            Resources cost;
            cost.set(ResourceType::Gold,         10000);
            cost.set(ResourceType::Iron,            10);
            cost.set(ResourceType::FaithStones,     10);
            cost.set(ResourceType::BloodEssence,    10);
            cost.set(ResourceType::VerdantSap,      10);
            cost.set(ResourceType::Mercury,         10);
            m_playerResources.spend(cost);

            // Build unique town id
            uint32_t newId = 1;
            for (const auto& t : m_towns) newId = std::max(newId, t.id + 1);

            Town newTown;
            newTown.id      = newId;
            newTown.name    = std::string(kFacNames[i]) + " Settlement";
            newTown.faction = static_cast<FactionId>(i);
            newTown.pos     = cityPos;
            newTown.ownerId = static_cast<uint32_t>(currentPlayerId());
            if (HexTile* ht = m_map.getTile(newTown.pos)) ht->townId = newTown.id;
            m_towns.push_back(newTown);

            // Remove the Utopia world object
            m_worldObjects.erase(
                std::remove_if(m_worldObjects.begin(), m_worldObjects.end(),
                    [this](const WorldObject& o){ return o.id == m_foundCityUtopiaId; }),
                m_worldObjects.end());

            char buf[64];
            std::snprintf(buf, sizeof(buf), "Founded: %s Settlement!", kFacNames[i]);
            pushPickupEffect(hero.pos, buf, IM_COL32(255, 215, 50, 255));
            m_showFoundCityPopup = false;
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(bw, 28))) {
        // Refund mana
        const SpellDef* spl = findSpell(SPL::FOUND_CITY);
        if (spl) hero.mana = std::min(hero.maxMana, hero.mana + spl->manaCost);
        m_showFoundCityPopup = false;
    }
    ImGui::End();
}

// ── Shipyard popup (build a boat) ─────────────────────────────────────────────
void Game::renderShipyardPopup()
{
    if (!m_showShipyardPopup) return;
    if (m_heroes.empty()) { m_showShipyardPopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    int goldCost = 2000 + hero.boatCount * 1000;
    int ironCost = 10;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##shipyard", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.3f, 0.6f, 1.0f, 1.0f}, "Shipyard");
    ImGui::Separator();
    ImGui::TextWrapped("Choose a hull. Each sails the sea differently.");
    ImGui::Spacing();
    if (hero.boatCount > 0)
        ImGui::TextDisabled("(Boats built: %d — each costs 1000g more)", hero.boatCount);

    int gold = m_playerResources.get(ResourceType::Gold);
    int iron = m_playerResources.get(ResourceType::Iron);
    ImGui::Text("Your resources: %d Gold, %d Iron", gold, iron);
    ImGui::Spacing();

    struct HullOpt { BoatType type; const char* name; const char* desc; };
    static const HullOpt kHulls[3] = {
        { BoatType::Travel,  "Travel Ship",  "Fastest crossing (1 move/sea hex)." },
        { BoatType::Fishing, "Fishing Boat", "Cheap but slow (3 move/sea hex); earns 100g each day at sea." },
        { BoatType::War,     "War Galley",   "Rams and sinks any lesser boat (2 move/sea hex)." },
    };

    BoatType chosen   = BoatType::Travel;
    bool     didBuild = false;
    float    bw       = ImGui::GetWindowWidth() - 32.0f;

    for (const auto& h : kHulls) {
        int cost = BOAT_BASE_COST[static_cast<int>(h.type)] + hero.boatCount * 1000;
        bool afford = (gold >= cost && iron >= ironCost);
        if (!afford) ImGui::BeginDisabled();
        char label[96];
        std::snprintf(label, sizeof(label), "%s — %dg + %di", h.name, cost, ironCost);
        if (ImGui::Button(label, {bw, 30})) { chosen = h.type; didBuild = true; }
        if (!afford) ImGui::EndDisabled();
        ImGui::TextDisabled("   %s", h.desc);
    }
    ImGui::Spacing();

    goldCost = BOAT_BASE_COST[static_cast<int>(chosen)] + hero.boatCount * 1000;
    bool canBuild = didBuild;

    if (canBuild) {
        m_playerResources.add(ResourceType::Gold, -goldCost);
        m_playerResources.add(ResourceType::Iron, -ironCost);
        hero.onBoat    = true;
        hero.boatType  = chosen;
        hero.boatCount += 1;
        m_showShipyardPopup = false;
        pushPickupEffect(hero.pos, "Set Sail!", IM_COL32(80, 160, 255, 255));
        m_audio.playSound("pickup");
        // Recalculate reachable tiles with boat movement
        auto costFn = [this, &hero](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
            int base = hero.moveCost(t->terrain);
            if (m_roadHexes.count(c)) base = std::max(1, base / 2);
            return base;
        };
        m_reachable = Pathfinder::reachable(m_map, hero.pos, costFn, hero.movePool);
    }
    if (!canBuild) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", {70, 28}))
        m_showShipyardPopup = false;

    ImGui::Spacing();
    if (hero.onBoat) {
        ImGui::Separator();
        ImGui::TextColored({0.4f, 0.9f, 0.5f, 1.0f}, "You are already on a boat.");
        ImGui::TextWrapped("Disembark onto land first before building another.");
    }

    ImGui::End();
}

// ── Siege camp prompt ─────────────────────────────────────────────────────────
void Game::renderSiegeCampPrompt()
{
    if (!m_showSiegeCampPrompt) return;

    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == m_siegePromptTownId) { town = &t; break; }
    if (!town) { m_showSiegeCampPrompt = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##siegeprompt", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "Besiege %s?", town->name.c_str());
    ImGui::Separator(); ImGui::Spacing();

    // Garrison summary
    ImGui::TextColored({0.9f, 0.4f, 0.4f, 1.0f}, "Garrison:");
    for (const auto& s : town->garrison) {
        const UnitDef* ud = m_registry.getUnitDef(s.defId);
        if (ud) ImGui::Text("  %-22s x%d", ud->name.c_str(), s.count);
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextWrapped("Attack now or lay siege and wait for allies. "
                       "While sieged, defenders cannot leave the town, "
                       "but they may Fortify (+4 DEF, stronger walls, +3 tower dmg).");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    Hero& hero = m_heroes[m_activeHeroIdx];
    float bw = (ImGui::GetWindowWidth() - 40.0f) / 3.0f;

    // Attack now
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.55f, 0.15f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.20f, 0.20f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.40f, 0.10f, 0.10f, 1.0f});
    if (ImGui::Button("Attack Now!", {bw, 34})) {
        m_showSiegeCampPrompt = false;
        Hero garrisonHero;
        garrisonHero.id      = 0;
        garrisonHero.name    = town->name + " Garrison";
        garrisonHero.faction = town->faction;
        garrisonHero.army    = town->garrison;
        m_lastCombatEnemyId  = 0;
        m_pendingTownCaptureId = town->id;
        auto pUnits = makeHeroUnits(hero, m_registry.units(), true);
        auto gUnits = makeHeroUnits(garrisonHero, m_registry.units(), false);
        enterCombat(hero, pUnits, garrisonHero, gUnits);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Storm the town immediately. No waiting for allies.\nYou face the full garrison alone.");
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 6);

    // Lay siege
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.35f, 0.60f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.45f, 0.75f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.25f, 0.45f, 1.0f});
    if (ImGui::Button("Lay Siege", {bw, 34})) {
        m_showSiegeCampPrompt = false;
        hero.isSiegeCamping    = true;
        hero.siegeTargetTownId = town->id;
        town->underSiege       = true;
        // Spend 25% of remaining movement
        int cost = std::max(1, hero.movePool / 4);
        hero.movePool = std::max(0, hero.movePool - cost);
        pushPickupEffect(hero.pos, "Siege Camp!", IM_COL32(100, 160, 255, 255));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Park your army outside the town.\n"
                          "Defenders can't leave. Allied heroes can join before\n"
                          "the siege assault fires automatically at end of turn.\n"
                          "Costs 25%% of remaining movement.");
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 6);

    // Cancel
    if (ImGui::Button("Retreat", {bw, 34}))
        m_showSiegeCampPrompt = false;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Back away. Do nothing this action.");

    ImGui::End();
}

// ── Siege indicator overlay ───────────────────────────────────────────────────
void Game::renderSiegeIndicator()
{
    // Draw a pulsing ring around any besieged town visible on screen
    auto* dl = ImGui::GetBackgroundDrawList();
    for (const auto& t : m_towns) {
        if (!t.underSiege) continue;
        float wx, wy;
        m_hexRenderer.grid().hexToWorld(t.pos, wx, wy);
        float sx, sy;
        m_camera.worldToScreen(wx, wy, sx, sy);
        // Pulsing alpha
        float pulse = 0.55f + 0.35f * sinf(m_mapTime * 3.0f);
        ImU32 col = IM_COL32(255, 200, 60, static_cast<int>(pulse * 255));
        dl->AddCircle({sx, sy}, 28.0f, col, 24, 3.0f);
        dl->AddText({sx - 20.0f, sy - 32.0f}, IM_COL32(255,200,60,230), "SIEGE");
    }
    // Highlight camped heroes with a blue tent icon
    for (const auto& h : m_heroes) {
        if (!h.isSiegeCamping) continue;
        float wx, wy;
        m_hexRenderer.grid().hexToWorld(h.pos, wx, wy);
        float sx, sy;
        m_camera.worldToScreen(wx, wy, sx, sy);
        dl->AddCircleFilled({sx, sy - 24.0f}, 6.0f, IM_COL32(100, 180, 255, 200));
        dl->AddText({sx - 12.0f, sy - 38.0f}, IM_COL32(100,180,255,230), "Camp");
    }
}

// ── Siege combat trigger ──────────────────────────────────────────────────────
void Game::triggerSiegeCombat(uint32_t townId)
{
    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == townId) { town = &t; break; }
    if (!town || town->garrison.empty()) return;

    // Collect all camping heroes for this town (combine their armies)
    std::vector<Hero*> campers;
    for (auto& h : m_heroes)
        if (h.isSiegeCamping && h.siegeTargetTownId == townId) campers.push_back(&h);
    if (campers.empty()) return;

    // Lift siege camp flags
    for (auto* h : campers) {
        h->isSiegeCamping    = false;
        h->siegeTargetTownId = 0;
    }

    // Lead attacker = first camper; merge other campers' armies into them
    Hero& lead = *campers[0];
    for (size_t i = 1; i < campers.size(); ++i) {
        for (auto& s : campers[i]->army) {
            bool merged = false;
            for (auto& ls : lead.army)
                if (ls.defId == s.defId) { ls.count += s.count; merged = true; break; }
            if (!merged && lead.army.size() < 7) lead.army.push_back(s);
        }
        campers[i]->army.clear();
    }

    // Build garrison defender hero
    Hero garrisonHero;
    garrisonHero.id      = 0;
    garrisonHero.name    = town->name + " Garrison";
    garrisonHero.faction = town->faction;
    garrisonHero.army    = town->garrison;

    // Apply fortify bonuses to garrison hero stats
    if (town->siegeFortified || town->fortifyDefBonus > 0) {
        garrisonHero.defense += town->fortifyDefBonus;
    }

    m_lastCombatEnemyId    = 0;
    m_pendingTownCaptureId = town->id;

    auto pUnits = makeHeroUnits(lead, m_registry.units(), true);
    auto gUnits = makeHeroUnits(garrisonHero, m_registry.units(), false);

    // Boost garrison units' defense from fortify
    if (town->fortifyDefBonus > 0) {
        for (auto& u : gUnits) u.defense += town->fortifyDefBonus;
    }

    // Reset fortify state after combat is entered
    town->underSiege       = false;
    town->siegeFortified   = false;
    town->fortifyDefBonus  = 0;
    town->fortifyWallBonus = 0;
    town->fortifyTowerBonus= 0;

    enterCombat(lead, pUnits, garrisonHero, gUnits);
}

// ── Town defense: AI assaults a human town → real playable siege battle ──────
void Game::startTownDefenseBattle(int prepChoice)
{
    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == m_pendingTownDefenseId) { town = &t; break; }
    Hero* attacker = nullptr;
    for (auto& eh : m_enemyHeroes) if (eh.id == m_defenseAttackerId) { attacker = &eh; break; }
    if (!town || !attacker || town->garrison.empty()) {
        m_pendingTownDefenseId = 0;
        m_defenseAttackerId    = 0;
        return;
    }
    m_siegePrepChoice = prepChoice;

    Hero defHero;
    defHero.id      = 0;
    defHero.name    = town->name + " Garrison";
    defHero.faction = town->faction;
    defHero.army    = town->garrison;

    m_lastCombatEnemyId = attacker->id;
    auto dUnits = makeHeroUnits(defHero, m_registry.units(), true);
    auto aUnits = makeHeroUnits(*attacker, m_registry.units(), false);
    if (m_watchingAI) {
        m_fromBattleSim  = true;
        m_simAutoPlay    = true;
        m_simAutoPlayTimer = 0.f;
    }
    enterCombat(defHero, dUnits, *attacker, aUnits);
}

void Game::renderDefensePrepPopup()
{
    if (!m_showDefensePrepPopup) return;
    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == m_pendingTownDefenseId) { town = &t; break; }
    if (!town) { m_showDefensePrepPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({430, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##defenseprep", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.6f, 0.2f, 1.0f}, "%s is under siege!", town->name.c_str());
    ImGui::TextWrapped("The Bastion crews stand ready. Choose your defensive preparation:");
    ImGui::Spacing();

    auto pick = [&](int choice) {
        m_showDefensePrepPopup = false;
        startTownDefenseBattle(choice);
    };
    if (ImGui::Button("Spikes — fast attackers take 20% losses", {410, 30})) pick(0);
    if (ImGui::Button("Nets — enemy flyers are grounded",         {410, 30})) pick(1);
    if (ImGui::Button("Shield Wall — 30% less ranged damage",     {410, 30})) pick(2);
    if (ImGui::Button("Plating — every hit deals 12 less damage", {410, 30})) pick(3);
    ImGui::Spacing();
    if (ImGui::Button("No preparation", {410, 26})) pick(-1);

    ImGui::End();
}

// ── March button — sits in the HUD bottom bar, left of End Turn ──────────────
void Game::renderMarchButton()
{
    // Only show when player controls active hero and game is in world map
    if (m_heroes.empty() || m_activeHeroIdx < 0 ||
        m_activeHeroIdx >= static_cast<int>(m_heroes.size())) return;

    Hero& hero = m_heroes[m_activeHeroIdx];
    if (hero.isSiegeCamping) return;

    bool nearEnemyTown = false;
    for (const auto& t : m_towns) {
        if (t.ownerId != 0 && t.ownerId != 1 &&
            HexGrid::distance(hero.pos, t.pos) <= 1) {
            nearEnemyTown = true; break;
        }
    }
    if (nearEnemyTown) return;

    int curWeek = m_turns.week();
    bool onCooldown = (hero.marchCooldownWeek > curWeek);

    // Mirror WorldMapHUD layout constants to sit in row 2 left of End Turn
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x, sh = io.DisplaySize.y;
    static constexpr float BOT_H_M = 100.0f, BTN_W_M = 150.0f, BTN_H_M = 40.0f, BTN_GAP_M = 6.0f;
    float col2X = sw - BTN_W_M - 8.0f;
    float col1X = col2X - BTN_W_M - BTN_GAP_M;
    float col0X = col1X - BTN_W_M - BTN_GAP_M;
    float row2Y = sh - BOT_H_M + 8.0f + BTN_H_M + BTN_GAP_M;

    ImGui::SetNextWindowPos(ImVec2(col0X, row2Y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(BTN_W_M, BTN_H_M));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##march_btn", nullptr, wf)) {
        if (onCooldown) ImGui::BeginDisabled();
        if (ImGui::Button("March!", ImVec2(-1, -1))) {
            int cost = std::max(1, hero.movePool / 4);
            hero.movePool -= cost;
            if (hero.movePool < 0) hero.movePool = 0;
            hero.marchCooldownWeek = curWeek + 1;
            hero.marchBonusActive  = true;
        }
        if (onCooldown) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (onCooldown) {
                ImGui::SetTooltip("March Order\nOn cooldown until Week %d", hero.marchCooldownWeek);
            } else {
                int cost = std::max(1, hero.movePool / 4);
                ImGui::SetTooltip(
                    "March Order\n\n"
                    "Push your troops to move faster.\n"
                    "Costs 25%% of current movement (%d MP).\n"
                    "Grants +10%% movement next week.\n"
                    "Cooldown: 1 week.", cost);
            }
        }
    }
    ImGui::End();
}

// ── Arena combat entry ────────────────────────────────────────────────────────
void Game::startArenaCombat()
{
    m_showArenaPopup = false;
    if (m_heroes.empty()) return;
    Hero& h = m_heroes[m_activeHeroIdx];
    // Mark this hero as having visited this arena
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingObjId) { o.questState = static_cast<int>(h.id); break; }

    Hero arenaHero;
    // Arena champion uses a deterministic neutral faction (not the player's own)
    int fIdx = (static_cast<int>(h.faction) + 1) % 9;
    arenaHero.faction = static_cast<FactionId>(fIdx);
    arenaHero.name    = "Arena Champion";
    int week = m_turns.week();
    auto units = makeFactionUnits(arenaHero.faction, false, arenaHero.level);
    for (auto& u : units) u.count = std::max(1, u.count * week / 2);

    m_lastCombatEnemyId    = 0;
    m_pendingTownCaptureId = 0;
    auto pUnits = makeHeroUnits(h, m_registry.units(), true);
    enterCombat(h, pUnits, arenaHero, units);
}
