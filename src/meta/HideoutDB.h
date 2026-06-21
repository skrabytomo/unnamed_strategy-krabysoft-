#pragma once
#include <string>
#include <optional>

// Forward-declare sqlite3 so callers don't need to include sqlite3.h
struct sqlite3;

// Hideout branch names — used as primary keys in the DB
namespace HideoutBranch
{
    inline constexpr const char* CASTLE   = "castle";
    inline constexpr const char* BARRACKS = "barracks";
    inline constexpr const char* VAULT    = "vault";
    inline constexpr const char* SHRINE   = "shrine";
    inline constexpr const char* SANCTUM  = "sanctum";
}

// Milestone names — persistent per account
namespace Milestone
{
    // Hideout upgrades
    inline constexpr const char* CASTLE_T1          = "castle_tier1";
    inline constexpr const char* CASTLE_T2          = "castle_tier2";
    inline constexpr const char* CASTLE_T3          = "castle_tier3";
    inline constexpr const char* BARRACKS_T1        = "barracks_tier1";
    inline constexpr const char* BARRACKS_T2        = "barracks_tier2";
    inline constexpr const char* VAULT_T1           = "vault_tier1";
    inline constexpr const char* VAULT_T2           = "vault_tier2";
    inline constexpr const char* CONVERGENCE_UNLOCK = "convergence_unlocked";

    // Gameplay achievements
    inline constexpr const char* FIRST_BATTLE_WON  = "first_battle_won";
    inline constexpr const char* FIRST_TOWN_CAPTURED= "first_town_captured";
    inline constexpr const char* HERO_LEVEL_5       = "hero_level_5";
    inline constexpr const char* HERO_LEVEL_10      = "hero_level_10";
    inline constexpr const char* WEEK_10_REACHED    = "week_10_reached";
    inline constexpr const char* CAMPAIGN_WON       = "campaign_won";
}

// ── HideoutDB ─────────────────────────────────────────────────────────────────
// Persistent SQLite database for the hideout meta-layer.
// Stores XP, upgrade levels, and milestone completion across all playthroughs.
// Does NOT affect campaign balance — cosmetic/unlock layer only.
class HideoutDB
{
public:
    HideoutDB() = default;
    ~HideoutDB();

    // Open (creates if not exists). Returns false on error.
    bool open(const std::string& dbPath);
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // ── XP ────────────────────────────────────────────────────────────────────
    int  getXP() const;
    void addXP(int amount);

    // ── Upgrades ──────────────────────────────────────────────────────────────
    // tier = 1-based upgrade tier within a branch
    int  getUpgradeLevel(const std::string& branch) const;
    bool unlockNextTier(const std::string& branch, int xpCost);
    bool canUnlockNextTier(const std::string& branch, int xpCost) const;

    // ── Milestones ────────────────────────────────────────────────────────────
    bool isMilestoneComplete(const std::string& name) const;
    void completeMilestone(const std::string& name);

    // ── Convergence unlock check ──────────────────────────────────────────────
    // Returns true if Castle T2 + Barracks T1 + Vault T1 are all complete
    bool isConvergenceUnlocked() const;

    // ── Generic KV store ──────────────────────────────────────────────────────
    void        setString(const std::string& key, const std::string& value);
    std::string getString(const std::string& key,
                          const std::string& defaultVal = "") const;
    void        setInt(const std::string& key, int value);
    int         getInt(const std::string& key, int defaultVal = 0) const;

private:
    bool createSchema();
    bool execSQL(const char* sql) const;

    sqlite3* m_db = nullptr;
};
