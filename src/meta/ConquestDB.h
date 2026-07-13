#pragma once
#include <string>
#include <vector>
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

private:
    bool createSchema();
    bool execSQL(const char* sql) const;
    int  queryInt(const char* sql, int defaultVal = 0) const;

    sqlite3* m_db = nullptr;
};
