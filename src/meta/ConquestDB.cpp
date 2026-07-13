#include "ConquestDB.h"
#include <sqlite3.h>
#include <cstdio>

ConquestDB::~ConquestDB() { close(); }

bool ConquestDB::open(const std::string& dbPath)
{
    if (m_db) return true;
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::fprintf(stderr, "[ConquestDB] open failed %s: %s\n",
                     dbPath.c_str(), sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    return createSchema();
}

void ConquestDB::close()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

bool ConquestDB::execSQL(const char* sql) const
{
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "[ConquestDB] SQL error: %s\n", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}

int ConquestDB::queryInt(const char* sql, int defaultVal) const
{
    if (!m_db) return defaultVal;
    sqlite3_stmt* st = nullptr;
    int out = defaultVal;
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW)
            out = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return out;
}

bool ConquestDB::createSchema()
{
    // All tables (incl. later-phase ones) created up front — cheap, avoids
    // migrations mid-development. Only Phase-1 accessors exist so far.
    return execSQL(R"SQL(
        CREATE TABLE IF NOT EXISTS conquest_hero (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            name TEXT, faction INTEGER, classId INTEGER,
            level INTEGER, xp INTEGER, attack INTEGER, defense INTEGER,
            skillsBlob TEXT DEFAULT '', spellsBlob TEXT DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS conquest_currencies (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            gold INTEGER DEFAULT 0, gems INTEGER DEFAULT 0
        );
        INSERT OR IGNORE INTO conquest_currencies (id, gold, gems) VALUES (1, 0, 0);
        CREATE TABLE IF NOT EXISTS conquest_map (
            week INTEGER PRIMARY KEY, nodeState TEXT
        );
        CREATE TABLE IF NOT EXISTS conquest_state (
            key TEXT PRIMARY KEY, value INTEGER
        );
        CREATE TABLE IF NOT EXISTS conquest_collection (
            defId INTEGER PRIMARY KEY, count INTEGER DEFAULT 0,
            pathChoice INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS conquest_keys (
            faction INTEGER PRIMARY KEY, count INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS conquest_quests (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type INTEGER, param INTEGER, progress INTEGER DEFAULT 0,
            target INTEGER, expiry INTEGER, claimed INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS conquest_arena (
            week INTEGER PRIMARY KEY, points INTEGER DEFAULT 0,
            entriesToday INTEGER DEFAULT 0, lastEntryDay INTEGER DEFAULT 0
        );
    )SQL");
}

// ── Hero ─────────────────────────────────────────────────────────────────────

ConquestHero ConquestDB::loadHero() const
{
    ConquestHero h;
    if (!m_db) return h;
    sqlite3_stmt* st = nullptr;
    const char* sql = "SELECT name, faction, classId, level, xp, attack, defense,"
                      " skillsBlob, spellsBlob FROM conquest_hero WHERE id = 1;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW)
    {
        h.exists  = true;
        h.name    = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        h.faction = static_cast<FactionId>(sqlite3_column_int(st, 1));
        h.classId = sqlite3_column_int(st, 2);
        h.level   = sqlite3_column_int(st, 3);
        h.xp      = sqlite3_column_int(st, 4);
        h.attack  = sqlite3_column_int(st, 5);
        h.defense = sqlite3_column_int(st, 6);
        if (auto* s = sqlite3_column_text(st, 7)) h.skillsBlob = reinterpret_cast<const char*>(s);
        if (auto* s = sqlite3_column_text(st, 8)) h.spellsBlob = reinterpret_cast<const char*>(s);
    }
    sqlite3_finalize(st);
    return h;
}

void ConquestDB::saveHero(const ConquestHero& h)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO conquest_hero (id,name,faction,classId,level,xp,attack,defense,skillsBlob,spellsBlob)"
        " VALUES (1,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET name=excluded.name, faction=excluded.faction,"
        " classId=excluded.classId, level=excluded.level, xp=excluded.xp,"
        " attack=excluded.attack, defense=excluded.defense,"
        " skillsBlob=excluded.skillsBlob, spellsBlob=excluded.spellsBlob;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, h.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st, 2, static_cast<int>(h.faction));
        sqlite3_bind_int (st, 3, h.classId);
        sqlite3_bind_int (st, 4, h.level);
        sqlite3_bind_int (st, 5, h.xp);
        sqlite3_bind_int (st, 6, h.attack);
        sqlite3_bind_int (st, 7, h.defense);
        sqlite3_bind_text(st, 8, h.skillsBlob.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 9, h.spellsBlob.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

void ConquestDB::resetHero()
{
    execSQL("DELETE FROM conquest_hero WHERE id = 1;");
}

// ── Currencies ───────────────────────────────────────────────────────────────

int ConquestDB::gold() const { return queryInt("SELECT gold FROM conquest_currencies WHERE id=1;"); }
int ConquestDB::gems() const { return queryInt("SELECT gems FROM conquest_currencies WHERE id=1;"); }

void ConquestDB::addGold(int amount)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "UPDATE conquest_currencies SET gold = MAX(0, gold + ?) WHERE id = 1;",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, amount);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

void ConquestDB::addGems(int amount)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "UPDATE conquest_currencies SET gems = MAX(0, gems + ?) WHERE id = 1;",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, amount);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

// ── Map node state ───────────────────────────────────────────────────────────

std::string ConquestDB::mapNodeState(int week) const
{
    if (!m_db) return "";
    sqlite3_stmt* st = nullptr;
    std::string out;
    if (sqlite3_prepare_v2(m_db,
        "SELECT nodeState FROM conquest_map WHERE week = ?;", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, week);
        if (sqlite3_step(st) == SQLITE_ROW)
            if (auto* s = sqlite3_column_text(st, 0))
                out = reinterpret_cast<const char*>(s);
    }
    sqlite3_finalize(st);
    return out;
}

void ConquestDB::saveMapNodeState(int week, const std::string& state)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO conquest_map (week, nodeState) VALUES (?, ?)"
        " ON CONFLICT(week) DO UPDATE SET nodeState = excluded.nodeState;",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int (st, 1, week);
        sqlite3_bind_text(st, 2, state.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

// ── Streak ───────────────────────────────────────────────────────────────────

int ConquestDB::winStreak() const
{
    return queryInt("SELECT value FROM conquest_state WHERE key = 'winStreak';");
}

void ConquestDB::setWinStreak(int streak)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO conquest_state (key, value) VALUES ('winStreak', ?)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, streak);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

// ── Generic persisted ints ───────────────────────────────────────────────────

int ConquestDB::stateInt(const std::string& key, int defaultVal) const
{
    if (!m_db) return defaultVal;
    sqlite3_stmt* st = nullptr;
    int out = defaultVal;
    if (sqlite3_prepare_v2(m_db,
        "SELECT value FROM conquest_state WHERE key = ?;", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) out = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return out;
}

void ConquestDB::setStateInt(const std::string& key, int value)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO conquest_state (key, value) VALUES (?, ?)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st, 2, value);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

// ── Collection pool ──────────────────────────────────────────────────────────

std::vector<std::pair<int,int>> ConquestDB::collectionAll() const
{
    std::vector<std::pair<int,int>> out;
    if (!m_db) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT defId, count FROM conquest_collection WHERE count > 0 ORDER BY defId;",
        -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW)
            out.emplace_back(sqlite3_column_int(st, 0), sqlite3_column_int(st, 1));
    }
    sqlite3_finalize(st);
    return out;
}

int ConquestDB::collectionCount(int defId) const
{
    if (!m_db) return 0;
    sqlite3_stmt* st = nullptr;
    int out = 0;
    if (sqlite3_prepare_v2(m_db,
        "SELECT count FROM conquest_collection WHERE defId = ?;", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, defId);
        if (sqlite3_step(st) == SQLITE_ROW) out = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return out;
}

void ConquestDB::collectionAdd(int defId, int delta)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO conquest_collection (defId, count) VALUES (?, MAX(0, ?))"
        " ON CONFLICT(defId) DO UPDATE SET count = MAX(0, count + ?);",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, defId);
        sqlite3_bind_int(st, 2, delta);
        sqlite3_bind_int(st, 3, delta);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

// ── Team ─────────────────────────────────────────────────────────────────────

std::vector<std::pair<int,int>> ConquestDB::teamGet() const
{
    std::vector<std::pair<int,int>> out;
    if (!m_db) return out;
    // Table created lazily here so older DBs pick it up without a migration.
    const_cast<ConquestDB*>(this)->execSQL(
        "CREATE TABLE IF NOT EXISTS conquest_team ("
        " slot INTEGER PRIMARY KEY, defId INTEGER, count INTEGER);");
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT defId, count FROM conquest_team WHERE count > 0 ORDER BY slot;",
        -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW)
            out.emplace_back(sqlite3_column_int(st, 0), sqlite3_column_int(st, 1));
    }
    sqlite3_finalize(st);
    return out;
}

void ConquestDB::teamSet(const std::vector<std::pair<int,int>>& team)
{
    if (!m_db) return;
    execSQL("CREATE TABLE IF NOT EXISTS conquest_team ("
            " slot INTEGER PRIMARY KEY, defId INTEGER, count INTEGER);");
    execSQL("DELETE FROM conquest_team;");
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO conquest_team (slot, defId, count) VALUES (?, ?, ?);",
        -1, &st, nullptr) == SQLITE_OK) {
        int slot = 0;
        for (const auto& [defId, count] : team) {
            if (count <= 0 || slot >= 6) continue;
            sqlite3_reset(st);
            sqlite3_bind_int(st, 1, slot++);
            sqlite3_bind_int(st, 2, defId);
            sqlite3_bind_int(st, 3, count);
            sqlite3_step(st);
        }
    }
    sqlite3_finalize(st);
}

// ── Keys ─────────────────────────────────────────────────────────────────────

int ConquestDB::keyCount(int faction) const
{
    if (!m_db) return 0;
    sqlite3_stmt* st = nullptr;
    int out = 0;
    if (sqlite3_prepare_v2(m_db,
        "SELECT count FROM conquest_keys WHERE faction = ?;", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, faction);
        if (sqlite3_step(st) == SQLITE_ROW) out = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return out;
}

void ConquestDB::addKeys(int faction, int delta)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO conquest_keys (faction, count) VALUES (?, MAX(0, ?))"
        " ON CONFLICT(faction) DO UPDATE SET count = MAX(0, count + ?);",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, faction);
        sqlite3_bind_int(st, 2, delta);
        sqlite3_bind_int(st, 3, delta);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

// ── Quests (Phase 3) ─────────────────────────────────────────────────────────

std::vector<ConquestDB::QuestRow> ConquestDB::questsAll() const
{
    std::vector<QuestRow> out;
    if (!m_db) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT id, type, param, progress, target, expiry, claimed FROM conquest_quests"
        " ORDER BY type, id;", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            QuestRow q;
            q.id       = sqlite3_column_int(st, 0);
            // conquest_quests.type encodes (weekly<<8 | event)
            int packed = sqlite3_column_int(st, 1);
            q.weekly   = (packed >> 8) & 1;
            q.event    = packed & 0xFF;
            q.param    = sqlite3_column_int(st, 2);
            q.progress = sqlite3_column_int(st, 3);
            q.target   = sqlite3_column_int(st, 4);
            q.expiry   = sqlite3_column_int64(st, 5);
            q.claimed  = sqlite3_column_int(st, 6) != 0;
            out.push_back(q);
        }
    }
    sqlite3_finalize(st);
    return out;
}

void ConquestDB::questClear(bool weekly)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    // Delete rows whose packed weekly-bit matches
    if (sqlite3_prepare_v2(m_db,
        "DELETE FROM conquest_quests WHERE ((type >> 8) & 1) = ?;", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, weekly ? 1 : 0);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

int ConquestDB::questInsert(bool weekly, int event, int param, int target, long long expiry)
{
    if (!m_db) return -1;
    sqlite3_stmt* st = nullptr;
    int packed = ((weekly ? 1 : 0) << 8) | (event & 0xFF);
    int id = -1;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO conquest_quests (type, param, progress, target, expiry, claimed)"
        " VALUES (?, ?, 0, ?, ?, 0);", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int  (st, 1, packed);
        sqlite3_bind_int  (st, 2, param);
        sqlite3_bind_int  (st, 3, target);
        sqlite3_bind_int64(st, 4, expiry);
        if (sqlite3_step(st) == SQLITE_DONE)
            id = (int)sqlite3_last_insert_rowid(m_db);
    }
    sqlite3_finalize(st);
    return id;
}

void ConquestDB::questSetProgress(int id, int progress)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "UPDATE conquest_quests SET progress = ? WHERE id = ?;", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, progress);
        sqlite3_bind_int(st, 2, id);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

void ConquestDB::questSetClaimed(int id, bool claimed)
{
    if (!m_db) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "UPDATE conquest_quests SET claimed = ? WHERE id = ?;", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, claimed ? 1 : 0);
        sqlite3_bind_int(st, 2, id);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}

// ── Path upgrades (Phase 4) ──────────────────────────────────────────────────
// Stored in conquest_state under key "path_F_T" so no schema change needed.

int ConquestDB::pathChoice(int faction, int tier) const
{
    char key[24];
    std::snprintf(key, sizeof(key), "path_%d_%d", faction, tier);
    return const_cast<ConquestDB*>(this)->stateInt(key, 0);
}

void ConquestDB::setPathChoice(int faction, int tier, int choice)
{
    char key[24];
    std::snprintf(key, sizeof(key), "path_%d_%d", faction, tier);
    setStateInt(key, choice);
}
