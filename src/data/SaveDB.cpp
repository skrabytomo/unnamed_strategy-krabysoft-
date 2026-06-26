#include "SaveDB.h"
#include <sqlite3.h>
#include <ctime>
#include <cstdio>

SaveDB::~SaveDB() { close(); }

bool SaveDB::open(const std::string& path)
{
    if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
        fprintf(stderr, "SaveDB: cannot open %s: %s\n", path.c_str(), sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    execSQL("PRAGMA journal_mode=WAL;");
    execSQL("PRAGMA synchronous=NORMAL;");
    return createSchema();
}

void SaveDB::close()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

bool SaveDB::execSQL(const char* sql) const
{
    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SaveDB SQL error: %s\n", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool SaveDB::createSchema()
{
    return execSQL(R"sql(
        CREATE TABLE IF NOT EXISTS saves (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            name         TEXT    NOT NULL,
            save_json    TEXT    NOT NULL,
            is_campaign  INTEGER NOT NULL DEFAULT 0,
            mission_idx  INTEGER NOT NULL DEFAULT 0,
            hero_name    TEXT    NOT NULL DEFAULT '',
            faction_name TEXT    NOT NULL DEFAULT '',
            day          INTEGER NOT NULL DEFAULT 1,
            week         INTEGER NOT NULL DEFAULT 1,
            created_at   INTEGER NOT NULL DEFAULT 0,
            updated_at   INTEGER NOT NULL DEFAULT 0
        );
    )sql");
}

int64_t SaveDB::upsert(int64_t id,
                       const std::string& name,
                       const std::string& saveJson,
                       bool isCampaign,
                       int missionIdx,
                       const std::string& heroName,
                       const std::string& factionName,
                       int day, int week)
{
    int64_t now = (int64_t)std::time(nullptr);

    if (id == 0) {
        // INSERT
        const char* sql =
            "INSERT INTO saves (name,save_json,is_campaign,mission_idx,hero_name,faction_name,day,week,created_at,updated_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_text  (stmt, 1,  name.c_str(),       -1, SQLITE_TRANSIENT);
        sqlite3_bind_text  (stmt, 2,  saveJson.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_int   (stmt, 3,  isCampaign ? 1 : 0);
        sqlite3_bind_int   (stmt, 4,  missionIdx);
        sqlite3_bind_text  (stmt, 5,  heroName.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_text  (stmt, 6,  factionName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int   (stmt, 7,  day);
        sqlite3_bind_int   (stmt, 8,  week);
        sqlite3_bind_int64 (stmt, 9,  now);
        sqlite3_bind_int64 (stmt, 10, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return sqlite3_last_insert_rowid(m_db);
    } else {
        // UPDATE
        const char* sql =
            "UPDATE saves SET name=?,save_json=?,is_campaign=?,mission_idx=?,hero_name=?,faction_name=?,day=?,week=?,updated_at=? "
            "WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_text  (stmt, 1,  name.c_str(),       -1, SQLITE_TRANSIENT);
        sqlite3_bind_text  (stmt, 2,  saveJson.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_int   (stmt, 3,  isCampaign ? 1 : 0);
        sqlite3_bind_int   (stmt, 4,  missionIdx);
        sqlite3_bind_text  (stmt, 5,  heroName.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_text  (stmt, 6,  factionName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int   (stmt, 7,  day);
        sqlite3_bind_int   (stmt, 8,  week);
        sqlite3_bind_int64 (stmt, 9,  now);
        sqlite3_bind_int64 (stmt, 10, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return id;
    }
}

bool SaveDB::load(int64_t id, std::string& jsonOut) const
{
    const char* sql = "SELECT save_json FROM saves WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = (const char*)sqlite3_column_text(stmt, 0);
        if (txt) { jsonOut = txt; ok = true; }
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool SaveDB::del(int64_t id)
{
    const char* sql = "DELETE FROM saves WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

bool SaveDB::rename(int64_t id, const std::string& newName)
{
    const char* sql = "UPDATE saves SET name=? WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text (stmt, 1, newName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

static std::vector<SaveEntry> query(sqlite3* db, const char* sql)
{
    std::vector<SaveEntry> result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SaveEntry e;
        e.id          = sqlite3_column_int64(stmt, 0);
        auto str = [&](int col) -> std::string {
            const char* t = (const char*)sqlite3_column_text(stmt, col);
            return t ? t : "";
        };
        e.name        = str(1);
        e.heroName    = str(2);
        e.factionName = str(3);
        e.day         = sqlite3_column_int(stmt, 4);
        e.week        = sqlite3_column_int(stmt, 5);
        e.isCampaign  = sqlite3_column_int(stmt, 6) != 0;
        e.missionIdx  = sqlite3_column_int(stmt, 7);
        e.updatedAt   = sqlite3_column_int64(stmt, 8);
        result.push_back(e);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<SaveEntry> SaveDB::list(bool campaignOnly) const
{
    const char* sql = campaignOnly
        ? "SELECT id,name,hero_name,faction_name,day,week,is_campaign,mission_idx,updated_at FROM saves WHERE is_campaign=1 ORDER BY updated_at DESC;"
        : "SELECT id,name,hero_name,faction_name,day,week,is_campaign,mission_idx,updated_at FROM saves WHERE is_campaign=0 ORDER BY updated_at DESC;";
    return query(m_db, sql);
}

std::vector<SaveEntry> SaveDB::listAll() const
{
    return query(m_db,
        "SELECT id,name,hero_name,faction_name,day,week,is_campaign,mission_idx,updated_at FROM saves ORDER BY updated_at DESC;");
}
