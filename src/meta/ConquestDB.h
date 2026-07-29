#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include "../hero/FactionId.h"

struct sqlite3;

// ── ConquestDB ────────────────────────────────────────────────────────────────
// Persistence for Conquest mode (see CONQUEST_MODE.md). Lives in the same
// sqlite file as HideoutDB but owns its own conquest_* tables.
//
// Phase 1 scope: persistent hero, currencies (gold/gems), weekly map node
// state, XP. Collection/quests/keys/arena tables are created up front so later
// phases don't need schema migrations, but only Phase-1 accessors exist yet.

struct ConquestHero
{
    bool        exists   = false;
    std::string name;
    FactionId   faction  = FactionId::HolyOrder;
    int         classId  = 0;
    int         level    = 1;
    int         xp       = 0;      // lifetime XP (never resets)
    int         attack   = 2;
    int         defense  = 2;
    std::string skillsBlob;        // serialized skill levels
    std::string spellsBlob;        // serialized known spell ids
};

class ConquestDB
{
public:
    ConquestDB() = default;
    ~ConquestDB();

    bool open(const std::string& dbPath);   // creates tables if missing
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // ── Hero (Phase 1) ────────────────────────────────────────────────────────
    ConquestHero loadHero() const;
    void         saveHero(const ConquestHero& h);
    void         resetHero();                // delete row (fresh start)

    // ── Currencies (Phase 1: gold; gems column exists for Phase 3) ───────────
    int  gold() const;
    int  gems() const;
    void addGold(int amount);                // negative allowed (spend)
    void addGems(int amount);

    // ── Weekly map node state (Phase 1) ───────────────────────────────────────
    // One row per ISO week; nodeState is a compact string of one char per node:
    // 'L' locked, 'A' available, 'C' cleared.
    std::string mapNodeState(int week) const;              // "" if none saved
    void        saveMapNodeState(int week, const std::string& state);

    // ── Streak (Phase 1: feeds XP multiplier) ─────────────────────────────────
    int  winStreak() const;
    void setWinStreak(int streak);

    // ── Generic persisted ints (chest counts, flags…) ─────────────────────────
    int  stateInt(const std::string& key, int defaultVal = 0) const;
    void setStateInt(const std::string& key, int value);

    // ── Collection pool (Phase 2) ─────────────────────────────────────────────
    // defId → owned count. pathChoice is reserved for Phase 4.
    std::vector<std::pair<int,int>> collectionAll() const;   // (defId, count), count>0
    int  collectionCount(int defId) const;
    void collectionAdd(int defId, int delta);                // clamped at 0

    // ── Per-unit-type leveling ("use your favorite Paladins") ─────────────────
    // Dumb storage only — the XP curve / level-up logic lives in ConquestMode.
    // XP scales with USAGE: every time a stack of this unit type is deployed
    // into a Conquest battle, it gains XP proportional to how many were sent.
    int  unitXp(int defId) const;
    int  unitLevel(int defId) const;
    void unitSetXpLevel(int defId, int xp, int level);   // single upsert

    // ── Team (Phase 2): up to 6 slots of (defId, count) ───────────────────────
    std::vector<std::pair<int,int>> teamGet() const;
    void teamSet(const std::vector<std::pair<int,int>>& team);

    // ── Faction keys (granted from Phase 2 chests, spent in Phase 4) ──────────
    int  keyCount(int faction) const;
    void addKeys(int faction, int delta);

    // ── Quests (Phase 3) ──────────────────────────────────────────────────────
    struct QuestRow {
        int id; bool weekly; int event; int param;
        int progress; int target; long long expiry; bool claimed;
    };
    std::vector<QuestRow> questsAll() const;
    void  questClear(bool weekly);                       // wipe daily or weekly set
    int   questInsert(bool weekly, int event, int param, int target, long long expiry);
    void  questSetProgress(int id, int progress);
    void  questSetClaimed(int id, bool claimed);

    // ── Path upgrades (Phase 4) ───────────────────────────────────────────────
    // Per faction (0-8) × tier (1-5): 0 = base/undecided, 1 = Path A, 2 = Path B.
    // Once chosen, all owned/future units of that faction+tier resolve to it.
    int  pathChoice(int faction, int tier) const;
    void setPathChoice(int faction, int tier, int choice);

    // ── Arena (Phase 5) ───────────────────────────────────────────────────────
    struct ArenaRow { int week; int points; int entriesToday; int lastEntryDay; };
    ArenaRow arenaGet(int week) const;                   // zero row if none
    void     arenaSet(const ArenaRow& r);

private:
    bool createSchema();
    bool execSQL(const char* sql) const;
    int  queryInt(const char* sql, int defaultVal = 0) const;

    sqlite3* m_db = nullptr;
};
