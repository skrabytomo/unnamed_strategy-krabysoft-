#include "SimDB.h"
#include <sqlite3.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <array>

static const char* factionName(FactionId f)
{
    switch (f) {
    case FactionId::HolyOrder:      return "HolyOrder";
    case FactionId::CrimsonWardens: return "CrimsonWardens";
    case FactionId::Thornkin:       return "Thornkin";
    case FactionId::EternalEmpire:  return "EternalEmpire";
    case FactionId::Bloodsworn:     return "Bloodsworn";
    case FactionId::Voidkin:        return "Voidkin";
    case FactionId::IronAssembly:   return "IronAssembly";
    case FactionId::Amalgamate:     return "Amalgamate";
    case FactionId::Convergence:    return "Convergence";
    default:                        return "Unknown";
    }
}

SimDB::~SimDB() { close(); }

bool SimDB::open(const std::string& path)
{
    if (m_db) close();
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SimDB open '%s': %s\n", path.c_str(), sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    execSQL("PRAGMA journal_mode=WAL;");
    execSQL("PRAGMA foreign_keys=ON;");
    return createSchema();
}

void SimDB::close()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

bool SimDB::createSchema()
{
    static const char* kSchema = R"(
        CREATE TABLE IF NOT EXISTS matches (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            f1              INTEGER NOT NULL,
            f2              INTEGER NOT NULL,
            winner          INTEGER NOT NULL,
            end_week        INTEGER NOT NULL,
            seed            INTEGER NOT NULL,
            combat_decided  INTEGER NOT NULL DEFAULT 1,
            run_ts          INTEGER NOT NULL DEFAULT (strftime('%s','now'))
        );
        CREATE TABLE IF NOT EXISTS turn_snapshots (
            match_id    INTEGER NOT NULL REFERENCES matches(id),
            week        INTEGER NOT NULL,
            p1_gold     INTEGER NOT NULL DEFAULT 0,
            p1_other    INTEGER NOT NULL DEFAULT 0,
            p2_gold     INTEGER NOT NULL DEFAULT 0,
            p2_other    INTEGER NOT NULL DEFAULT 0,
            p1_strength INTEGER NOT NULL DEFAULT 0,
            p2_strength INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_snap_match ON turn_snapshots(match_id);
    )";
    return execSQL(kSchema);
}

bool SimDB::execSQL(const char* sql) const
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SimDB SQL error: %s\n", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

int64_t SimDB::insertMatch(FactionId f1, FactionId f2, int winner, int endWeek, uint32_t seed, int combatDecided)
{
    if (!m_db) return 0;
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "INSERT INTO matches(f1,f2,winner,end_week,seed,combat_decided) VALUES(%d,%d,%d,%d,%u,%d);",
        static_cast<int>(f1), static_cast<int>(f2), winner, endWeek, seed, combatDecided);
    if (!execSQL(sql)) return 0;
    return sqlite3_last_insert_rowid(m_db);
}

void SimDB::insertSnapshots(int64_t matchId, const std::vector<TurnSnapshot>& snaps)
{
    if (!m_db || snaps.empty()) return;
    execSQL("BEGIN;");
    for (const auto& s : snaps) {
        char sql[512];
        std::snprintf(sql, sizeof(sql),
            "INSERT INTO turn_snapshots(match_id,week,p1_gold,p1_other,p2_gold,p2_other,"
            "p1_strength,p2_strength) VALUES(%lld,%d,%d,%d,%d,%d,%d,%d);",
            (long long)matchId, s.week,
            s.p1Gold, s.p1OtherRes, s.p2Gold, s.p2OtherRes,
            s.p1Strength, s.p2Strength);
        execSQL(sql);
    }
    execSQL("COMMIT;");
}

std::string SimDB::buildBalanceReport() const
{
    if (!m_db) return "";

    // 9x9 win count matrix: wins[f1][f2] = games where f1 won vs f2
    std::array<std::array<int,9>,9> wins{};
    std::array<std::array<int,9>,9> total{};
    for (auto& r : wins)  r.fill(0);
    for (auto& r : total) r.fill(0);

    sqlite3_stmt* stmt = nullptr;
    const char* q = "SELECT f1,f2,winner FROM matches;";
    if (sqlite3_prepare_v2(m_db, q, -1, &stmt, nullptr) != SQLITE_OK) return "";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int f1 = sqlite3_column_int(stmt, 0);
        int f2 = sqlite3_column_int(stmt, 1);
        int w  = sqlite3_column_int(stmt, 2);
        if (f1 < 0 || f1 >= 9 || f2 < 0 || f2 >= 9) continue;
        total[f1][f2]++;
        total[f2][f1]++;
        if (w == 1) { wins[f1][f2]++; }
        else if (w == 2) { wins[f2][f1]++; }
    }
    sqlite3_finalize(stmt);

    // Overall win rate per faction (sum vs all opponents)
    std::array<int,9> factionWins{};
    std::array<int,9> factionGames{};
    factionWins.fill(0);  factionGames.fill(0);
    for (int i = 0; i < 9; ++i)
        for (int j = 0; j < 9; ++j)
            if (i != j) { factionWins[i] += wins[i][j]; factionGames[i] += total[i][j]; }

    std::ostringstream ss;
    ss << "=== AI vs AI Balance Report ===\n\n";
    ss << "Overall win rates (all matchups combined):\n";
    for (int i = 0; i < 9; ++i) {
        int g = factionGames[i];
        float wr = g > 0 ? 100.f * factionWins[i] / g : 0.f;
        ss << std::left << std::setw(18) << factionName(static_cast<FactionId>(i))
           << std::right << std::setw(5) << std::fixed << std::setprecision(1) << wr << "%"
           << "  (" << factionWins[i] << "/" << g << ")";
        if (wr > 65.f) ss << "  *** OVERPOWERED";
        else if (wr < 35.f) ss << "  *** UNDERPOWERED";
        ss << "\n";
    }

    ss << "\n--- Head-to-head win rates (row faction vs col faction) ---\n";
    ss << std::setw(18) << " ";
    for (int j = 0; j < 9; ++j) {
        std::string name = factionName(static_cast<FactionId>(j));
        if (name.size() > 6) name = name.substr(0,6);
        ss << std::setw(7) << name;
    }
    ss << "\n";
    for (int i = 0; i < 9; ++i) {
        ss << std::left << std::setw(18) << factionName(static_cast<FactionId>(i));
        for (int j = 0; j < 9; ++j) {
            if (i == j) { ss << std::setw(7) << "  --  "; continue; }
            int t = total[i][j] / 2; // halved since total counts both directions
            if (t == 0) { ss << std::setw(7) << "  N/A "; continue; }
            float wr = 100.f * wins[i][j] / t;
            ss << std::right << std::setw(5) << std::fixed << std::setprecision(1) << wr << "%";
            ss << " ";
        }
        ss << "\n";
    }
    return ss.str();
}
