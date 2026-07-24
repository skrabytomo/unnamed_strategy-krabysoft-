#include "ScoreDB.h"
#include "../core/DevLog.h"
#include <sqlite3.h>

ScoreDB::~ScoreDB() { close(); }

void ScoreDB::close()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

bool ScoreDB::open(const std::string& dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) {
        gLog("[SCORE] cannot open %s: %s\n", dbPath.c_str(),
             m_db ? sqlite3_errmsg(m_db) : "?");
        close();
        return false;
    }
    const char* sql =
        "CREATE TABLE IF NOT EXISTS highscores ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT, faction INTEGER, score INTEGER, days INTEGER,"
        " difficulty INTEGER, won INTEGER, rank TEXT, ts INTEGER);";
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        gLog("[SCORE] create table failed: %s\n", err ? err : "?");
        sqlite3_free(err);
        close();
        return false;
    }
    return true;
}

bool ScoreDB::addScore(const HighScore& s, bool* outIsBest)
{
    if (!m_db) return false;

    if (outIsBest) {
        *outIsBest = false;
        sqlite3_stmt* q = nullptr;
        if (sqlite3_prepare_v2(m_db, "SELECT MAX(score) FROM highscores;", -1, &q, nullptr) == SQLITE_OK) {
            if (sqlite3_step(q) == SQLITE_ROW) {
                bool haveRows = sqlite3_column_type(q, 0) != SQLITE_NULL;
                int  best     = sqlite3_column_int(q, 0);
                *outIsBest = (!haveRows || s.score > best);
            }
            sqlite3_finalize(q);
        }
    }

    const char* sql =
        "INSERT INTO highscores(name,faction,score,days,difficulty,won,rank,ts)"
        " VALUES(?,?,?,?,?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, s.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, s.faction);
    sqlite3_bind_int (st, 3, s.score);
    sqlite3_bind_int (st, 4, s.days);
    sqlite3_bind_int (st, 5, s.difficulty);
    sqlite3_bind_int (st, 6, s.won ? 1 : 0);
    sqlite3_bind_text(st, 7, s.rank.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, s.when);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

std::vector<HighScore> ScoreDB::topScores(int limit)
{
    std::vector<HighScore> out;
    if (!m_db) return out;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT name,faction,score,days,difficulty,won,rank,ts FROM highscores"
        " ORDER BY score DESC, ts ASC LIMIT ?;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        HighScore h;
        const unsigned char* nm = sqlite3_column_text(st, 0);
        h.name       = nm ? reinterpret_cast<const char*>(nm) : "";
        h.faction    = sqlite3_column_int(st, 1);
        h.score      = sqlite3_column_int(st, 2);
        h.days       = sqlite3_column_int(st, 3);
        h.difficulty = sqlite3_column_int(st, 4);
        h.won        = sqlite3_column_int(st, 5) != 0;
        const unsigned char* rk = sqlite3_column_text(st, 6);
        h.rank       = rk ? reinterpret_cast<const char*>(rk) : "";
        h.when       = sqlite3_column_int64(st, 7);
        out.push_back(h);
    }
    sqlite3_finalize(st);
    return out;
}
