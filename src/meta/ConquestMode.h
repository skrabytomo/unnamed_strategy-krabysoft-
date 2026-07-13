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
    void reportEvent(QuestEvent e, int count = 1);
    // Claim a completed, unclaimed quest → applies reward. Returns reward desc.
    std::string claimQuest(int questId, const class BuildingRegistry& reg);

    // ── Gem spending (Phase 3) ────────────────────────────────────────────────
    // Buy a chest with gems. Returns false if too few gems.
    bool buyChestWithGems(ChestType t);
    static int chestGemPrice(ChestType t);
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
    // Resolve a base unit's defId to the player's chosen variant defId.
    // If no choice made (or lookup fails) returns the input defId unchanged.
    int  resolveVariant(int baseDefId, const class BuildingRegistry& reg) const;

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
};
