#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../hero/FactionId.h"

struct sqlite3;

struct TurnSnapshot
{
    int      week         = 0;
    int      p1Gold       = 0;
    int      p1OtherRes   = 0;   // sum of non-gold resources
    int      p2Gold       = 0;
    int      p2OtherRes   = 0;
    int      p1Strength   = 0;
    int      p2Strength   = 0;
};

class SimDB
{
public:
    SimDB() = default;
    ~SimDB();

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // Insert a completed match record. Returns new match id (>0) or 0 on error.
    int64_t insertMatch(FactionId f1, FactionId f2, int winner, int endWeek, uint32_t seed);

    // Insert per-turn snapshot rows for a match.
    void insertSnapshots(int64_t matchId, const std::vector<TurnSnapshot>& snaps);

    // Compute win rates from stored matches and print a faction balance report.
    std::string buildBalanceReport() const;

private:
    bool createSchema();
    bool execSQL(const char* sql) const;

    sqlite3* m_db = nullptr;
};
