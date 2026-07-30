#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "ConquestDB.h"
#include "ConquestQuests.h"
#include "../hero/FactionId.h"

// ── ConquestMode ─────────────────────────────────────────────────────────────
// Phase 1: weekly seeded node-graph map ("almost linear" chain with side
// branches), persistent hero, XP/gold rewards. The map is a node graph screen
// (Slay-the-Spire style), not a hex world map — battles jump into the normal
// combat engine and return here.
//
// Node state chars (persisted): 'L' locked, 'A' available, 'C' cleared.

enum class ConquestNodeType : uint8_t
{
    Battle,      // normal fight, scaled to depth
    Elite,       // harder fight, better rewards
    Treasure,    // gold cache (chests proper arrive in Phase 2)
    Boss,        // end-of-map fight, big rewards + key (key grant lands Phase 4)
};

struct ConquestNode
{
    ConquestNodeType type   = ConquestNodeType::Battle;
    int   depth             = 0;      // 0..N along the chain; drives difficulty
    bool  sideBranch        = false;  // optional detour node
    float x = 0.f, y = 0.f;           // layout position (0-1 normalized)
    std::vector<int> next;            // indices of reachable nodes
    char  state             = 'L';    // L/A/C
};

class ConquestMode
{
public:
    // Open DB + build (or restore) this ISO-week's map.
    bool init(const std::string& dbPath);
    void shutdown();

    bool active() const { return m_active; }

    // ── Hero ─────────────────────────────────────────────────────────────────
    ConquestHero&       hero()       { return m_hero; }
    const ConquestHero& hero() const { return m_hero; }
    bool  hasHero() const  { return m_hero.exists; }
    void  createHero(const std::string& name, FactionId f, int classId);
    void  saveHero()       { m_db.saveHero(m_hero); }

    // XP → level curve: next level at 100 × level² cumulative.
    static int xpForLevel(int level);
    int  currentLevel() const;
    // Grants XP (applies streak multiplier), returns levels gained.
    int  grantVictoryRewards(int nodeIndex);
    void onDefeat();       // resets streak

    // ── Map ──────────────────────────────────────────────────────────────────
    int  week() const { return m_week; }
    // Gold collected from Dwellings passive income on THIS session's init()
    // call — read once by the UI to show a "+N gold while away" toast, then
    // it's just informational (not re-collected on re-read).
    int  lastDwellingGoldCollected() const { return m_lastDwellingGoldCollected; }
    const std::vector<ConquestNode>& nodes() const { return m_nodes; }
    // Marks node cleared, unlocks its successors, persists state.
    void clearNode(int index);
    bool isNodeAvailable(int index) const;
    // Difficulty scaling for a node → "weeks" parameter for ArmyBuilder.
    int  enemyWeeksForNode(int index) const;
    FactionId enemyFactionForNode(int index) const;

    // ── Currencies ───────────────────────────────────────────────────────────
    int  gold() const { return m_db.gold(); }
    int  gems() const { return m_db.gems(); }

    // ── Chests (Phase 2) ─────────────────────────────────────────────────────
    // Inventory of unopened chests, persisted per type.
    enum class ChestType : int { Wooden = 0, Iron = 1, Golden = 2, Grand = 3 };
    struct ChestDrop { int defId; int count; std::string name; int tier; FactionId faction; };
    struct ChestResult
    {
        std::vector<ChestDrop> units;
        int keysFaction = -1;   // faction that got keys (-1 = none)
        int keysGained  = 0;
        int gemsGained  = 0;
    };
    int  chestCount(ChestType t) const;
    void grantChest(ChestType t, int n = 1);
    // Opens one chest (if owned); rolls contents from the registry and applies
    // them to the collection/keys/gems. Returns what dropped.
    ChestResult openChest(ChestType t, const class BuildingRegistry& reg);

    // ── Collection & team (Phase 2) ──────────────────────────────────────────
    std::vector<std::pair<int,int>> collection() const { return m_db.collectionAll(); }
    int  ownedCount(int defId) const { return m_db.collectionCount(defId); }
    void addUnits(int defId, int n)  { m_db.collectionAdd(defId, n); }
    std::vector<std::pair<int,int>> team() const { return m_db.teamGet(); }
    void setTeam(const std::vector<std::pair<int,int>>& t) { m_db.teamSet(t); }

    // ── Quests (Phase 3) ──────────────────────────────────────────────────────
    // Regenerates daily/weekly sets whose window has elapsed, then returns all.
    void refreshQuests();
    std::vector<class Quest> quests() const;
    // Gameplay hook: bump matching quests. `count` is how much to add.
    void reportEvent(QuestEvent e, int count = 1, int matchParam = -1);
    // Claim a completed, unclaimed quest → applies reward. Returns reward desc.
    std::string claimQuest(int questId, const class BuildingRegistry& reg);

    // ── Gem spending (Phase 3) ────────────────────────────────────────────────
    // Buy a chest with gems. Returns false if too few gems.
    bool buyChestWithGems(ChestType t);
    static int chestGemPrice(ChestType t);
    // Gold -> Gems exchange rate and helper. Gold's ONLY other sink is the
    // cheap recruit shop (40/90/180g) — meanwhile gems are the currency that
    // actually buys Golden (150) and Grand (400) chests, and gem income from
    // quests alone (~70-120/week) makes those a multi-week grind even though
    // gold from winning (100-400+/battle) piles up with nothing to spend it on.
    // 25 gold -> 1 gem lets banked victory gold convert into real chest-buying
    // power instead of sitting idle.
    static constexpr int GOLD_PER_GEM = 25;
    bool exchangeGoldForGems(int gemsWanted);
    // Hero respec: refund one attribute point cost, etc. (kept minimal for now)
    bool spendGems(int amount);

    // ── Keys & path upgrades (Phase 4) ────────────────────────────────────────
    int  keys(int faction) const { return m_db.keyCount(faction); }
    int  pathChoice(int faction, int tier) const { return m_db.pathChoice(faction, tier); }
    static int keyCostForTier(int tier);       // T1=1 … T5=8
    static int respecGemCost() { return 100; }
    // Spend keys to lock in Path A(1)/B(2) for faction+tier. False if too few
    // keys or already chosen (use respec to change).
    bool chooseUnitPath(int faction, int tier, int choice);
    // Change an existing choice for gems.
    bool respecUnitPath(int faction, int tier, int newChoice);

    // ── Per-unit leveling ("level up the troops you like best") ──────────────
    // XP scales with USAGE: deploying N units of a type into a Conquest battle
    // grants that unit type N xp (win or lose — using it is what counts).
    // Capped at MAX_UNIT_LEVEL; +ATTACK/HP_PCT_PER_LEVEL% combat stats per
    // level, applied at deploy time in ArmyBuilder.
    static constexpr int MAX_UNIT_LEVEL        = 20;
    static constexpr int UNIT_STAT_PCT_PER_LVL = 3;   // +3%/level, max +60% at 20
    int  unitLevel(int defId) const { return m_db.unitLevel(defId); }
    int  unitXp(int defId)    const { return m_db.unitXp(defId); }
    static int unitXpForLevel(int level);   // cumulative XP needed to REACH `level`
    // Called once per deployed stack at battle start (in Game_Conquest.cpp).
    void grantUnitUsageXp(int defId, int count);

    // ── Infinite Conquest Level (separate from hero level & unit level) ───────
    // A never-capped meta-progression track fed by BOTH battles (same taps as
    // quest reportEvent) and quest claims combined. Every level-up grants 1 key
    // to a random faction — a guaranteed, ever-growing key income so "unlock
    // every unit upgrade" is a real long-term goal instead of pure chest RNG.
    // Higher Conquest Level also scales chest drop sizes (see openChest()).
    int  conquestLevel() const { return m_db.stateInt("conquest_level", 0); }
    int  conquestXp()    const { return m_db.stateInt("conquest_xp", 0); }
    static int conquestXpForNextLevel(int level);   // XP needed to go level->level+1
    // Adds XP to the Conquest Level track; handles (possibly multiple) level-ups
    // and grants keys. Call from both battle-outcome and quest-claim code.
    void grantConquestXp(int amount);

    // ── Town (2026-07) — the persistent hideout town from the original design
    // doc that never got built. Three gold-upgradable tracks, each capped at
    // MAX_TOWN_LEVEL. Levels stored via the generic conquest_state key-value
    // store (no schema migration needed).
    enum class TownTrack : uint8_t { Dwellings, Walls, MageGuild };
    static constexpr int MAX_TOWN_LEVEL = 10;
    int  townLevel(TownTrack t) const;
    int  townUpgradeCost(TownTrack t) const;   // cost to go from current->current+1
    bool upgradeTown(TownTrack t);             // spends gold, +1 level

    // Dwellings: passive gold (collected on Conquest entry, capped at 7 days'
    // worth so leaving the game open/idle doesn't let it accrue forever) +
    // a free weekly chest (tier scales with level, big jumps at milestones)
    // + extra weekly quest slots at high levels.
    static constexpr int DWELLING_GOLD_PER_LEVEL_PER_DAY = 40;
    // Called once per Conquest session entry; returns gold actually collected
    // (0 if nothing pending) so the UI can show a "+N gold while away" toast.
    int  collectDwellingGold();
    // Which chest tier the weekly free chest grants at the current Dwellings
    // level (milestone jumps at 3/6/9/10, not a smooth ramp).
    ChestType dwellingWeeklyChestTier() const;
    bool claimDwellingWeeklyChest(const class BuildingRegistry& reg);
    bool dwellingWeeklyChestAvailable() const;
    // Extra weekly quest slots: level>=5 -> 4 slots, level>=10 -> 5 slots
    // (base is 3). Read by refreshQuests().
    int  weeklyQuestSlotCount() const;

    // Walls: 1 perk point per level (10 max), spent on permanent perks split
    // into a Unit line (buffs your army) and a Player line (buffs you/economy).
    // Points are never refunded — a simple accumulator, not a respec system.
    enum class Perk : uint8_t {
        UnitAttack, UnitHp, UnitDefense,      // unit line
        PlayerGold, PlayerXp, PlayerLuck,     // player line
    };
    static constexpr int MAX_PERK_RANK = 5;
    int  perkPointsTotal() const { return townLevel(TownTrack::Walls); }
    int  perkPointsSpent() const;
    int  perkPointsAvailable() const { return perkPointsTotal() - perkPointsSpent(); }
    int  perkRank(Perk p) const;
    int  perkCostForRank(int rank) const;    // cost to buy the NEXT rank (1..5)
    bool buyPerk(Perk p);
    // Aggregate bonus percentages, read at combat/economy time.
    int  perkUnitAttackPct() const  { return perkRank(Perk::UnitAttack)  * 4; }  // +4%/rank
    int  perkUnitHpPct() const      { return perkRank(Perk::UnitHp)      * 4; }
    int  perkUnitDefensePct() const { return perkRank(Perk::UnitDefense) * 4; }
    int  perkPlayerGoldPct() const  { return perkRank(Perk::PlayerGold)  * 5; }  // +5%/rank
    int  perkPlayerXpPct() const    { return perkRank(Perk::PlayerXp)    * 5; }
    int  perkPlayerLuckPct() const  { return perkRank(Perk::PlayerLuck)  * 3; }  // +3%/rank chest-count luck

    // Mage Guild: flat spell-power bonus applied to the hero's casting stat in
    // Conquest combat (kept simple — doesn't touch the shared skill/spell pick
    // system used elsewhere). +8%/level, max +80% at level 10.
    int  mageGuildSpellPowerPct() const { return townLevel(TownTrack::MageGuild) * 8; }
    // Resolve a base unit's defId to the player's chosen variant defId.
    // If no choice made (or lookup fails) returns the input defId unchanged.
    int  resolveVariant(int baseDefId, const class BuildingRegistry& reg) const;

    // ── Arena (Phase 5) — NO CASUALTIES (exhibition, see CONQUEST_MODE.md) ─────
    static constexpr int ARENA_ENTRIES_PER_DAY = 3;
    static constexpr int ARENA_EXTRA_ENTRY_GEMS = 15;
    // Total power of the current battle team (with chosen path variants applied).
    int  teamPower(const class BuildingRegistry& reg) const;
    int  arenaPoints() const;
    int  arenaEntriesLeft() const;                 // free tries remaining today
    bool arenaCanEnter() const;                    // free entry OR affordable
    bool arenaConsumeEntry();                      // spend a free try or gems; false if none
    // Apply arena result: points delta, win streak → chest, quest event.
    // Returns points gained (can be negative on loss).
    int  arenaReportResult(bool won);
    // Ghost opponent power for the player's next arena fight (0.95-1.15× team).
    int  arenaOpponentPower(const class BuildingRegistry& reg) const;

    // ── Cheap recruits (rebuild after a wipe) ─────────────────────────────────
    // Buy basic units (T1-T3) with gold so a bottomed-out player can always get
    // back in the game within a round or two. Price scales with tier.
    static int recruitGoldPrice(int tier) {
        switch (tier) { case 1: return 40; case 2: return 90; case 3: return 180; }
        return 999999;   // T4+ not buyable here
    }
    // Buys `count` of a faction+tier basic unit into the collection. Returns
    // how many were actually afforded/bought.
    int buyRecruits(FactionId faction, int tier, int count, const class BuildingRegistry& reg);

    ConquestDB& db() { return m_db; }

private:
    void generateMap(uint32_t seed);
    void persistNodeState();
    void restoreNodeState(const std::string& s);
    static int isoWeekNumber();      // current real-world ISO week + year hash

    ConquestDB  m_db;
    ConquestHero m_hero;
    std::vector<ConquestNode> m_nodes;
    int   m_week   = 0;
    bool  m_active = false;
    int   m_arenaStreak = 0;   // consecutive arena wins (session, feeds chest reward)
    int   m_lastDwellingGoldCollected = 0;
};
