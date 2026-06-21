#include "../core/DevLog.h"
#include "HideoutDB.h"
#include <sqlite3.h>
#include <cstdio>
#include <cstring>

// ── Lifecycle ─────────────────────────────────────────────────────────────────
HideoutDB::~HideoutDB()
{
    close();
}

bool HideoutDB::open(const std::string& dbPath)
{
    if (m_db) close();

    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "HideoutDB open '%s': %s\n",
                dbPath.c_str(), sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // WAL mode for better concurrent reads and crash safety
    execSQL("PRAGMA journal_mode=WAL;");
    execSQL("PRAGMA foreign_keys=ON;");

    return createSchema();
}

void HideoutDB::close()
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

// ── Schema ────────────────────────────────────────────────────────────────────
bool HideoutDB::createSchema()
{
    static const char* kSchema = R"(
        CREATE TABLE IF NOT EXISTS hideout_progress (
            key   TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS hideout_upgrades (
            branch   TEXT    NOT NULL,
            tier     INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (branch)
        );

        CREATE TABLE IF NOT EXISTS hideout_milestones (
            name         TEXT PRIMARY KEY NOT NULL,
            completed    INTEGER NOT NULL DEFAULT 0,
            completed_at TEXT
        );
    )";
    return execSQL(kSchema);
}

bool HideoutDB::execSQL(const char* sql) const
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "HideoutDB SQL error: %s\n", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// ── KV store helpers ──────────────────────────────────────────────────────────
void HideoutDB::setString(const std::string& key, const std::string& value)
{
    if (!m_db) return;
    const char* sql =
        "INSERT INTO hideout_progress(key,value) VALUES(?1,?2) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string HideoutDB::getString(const std::string& key,
                                  const std::string& defaultVal) const
{
    if (!m_db) return defaultVal;
    const char* sql = "SELECT value FROM hideout_progress WHERE key=?1;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::string result = defaultVal;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (v) result = v;
    }
    sqlite3_finalize(stmt);
    return result;
}

void HideoutDB::setInt(const std::string& key, int value)
{
    setString(key, std::to_string(value));
}

int HideoutDB::getInt(const std::string& key, int defaultVal) const
{
    std::string s = getString(key, "");
    if (s.empty()) return defaultVal;
    try { return std::stoi(s); } catch (...) { return defaultVal; }
}

// ── XP ────────────────────────────────────────────────────────────────────────
int HideoutDB::getXP() const
{
    return getInt("xp", 0);
}

void HideoutDB::addXP(int amount)
{
    setInt("xp", getXP() + amount);
}

// ── Upgrades ──────────────────────────────────────────────────────────────────
int HideoutDB::getUpgradeLevel(const std::string& branch) const
{
    if (!m_db) return 0;
    const char* sql = "SELECT tier FROM hideout_upgrades WHERE branch=?1;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, branch.c_str(), -1, SQLITE_TRANSIENT);
    int tier = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) tier = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return tier;
}

bool HideoutDB::canUnlockNextTier(const std::string& branch, int xpCost) const
{
    return getXP() >= xpCost;
}

bool HideoutDB::unlockNextTier(const std::string& branch, int xpCost)
{
    if (!m_db) return false;
    if (!canUnlockNextTier(branch, xpCost)) return false;

    int newTier = getUpgradeLevel(branch) + 1;

    const char* sql =
        "INSERT INTO hideout_upgrades(branch, tier) VALUES(?1, ?2) "
        "ON CONFLICT(branch) DO UPDATE SET tier=excluded.tier;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, branch.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, newTier);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Deduct XP
    setInt("xp", getXP() - xpCost);

    // Check Convergence unlock condition
    if (isConvergenceUnlocked() && !isMilestoneComplete(Milestone::CONVERGENCE_UNLOCK))
        completeMilestone(Milestone::CONVERGENCE_UNLOCK);

    gLog("HideoutDB: unlocked %s tier %d\n", branch.c_str(), newTier);
    return true;
}

// ── Milestones ────────────────────────────────────────────────────────────────
bool HideoutDB::isMilestoneComplete(const std::string& name) const
{
    if (!m_db) return false;
    const char* sql = "SELECT completed FROM hideout_milestones WHERE name=?1;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool done = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) done = sqlite3_column_int(stmt, 0) != 0;
    sqlite3_finalize(stmt);
    return done;
}

void HideoutDB::completeMilestone(const std::string& name)
{
    if (!m_db) return;
    const char* sql =
        "INSERT INTO hideout_milestones(name, completed, completed_at) "
        "VALUES(?1, 1, datetime('now')) "
        "ON CONFLICT(name) DO UPDATE SET completed=1, completed_at=datetime('now');";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    gLog("HideoutDB: milestone '%s' completed\n", name.c_str());
}

// ── Convergence unlock ────────────────────────────────────────────────────────
bool HideoutDB::isConvergenceUnlocked() const
{
    return getUpgradeLevel(HideoutBranch::CASTLE)   >= 2 &&
           getUpgradeLevel(HideoutBranch::BARRACKS) >= 1 &&
           getUpgradeLevel(HideoutBranch::VAULT)    >= 1;
}
