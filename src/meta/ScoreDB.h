#pragma once
#include <string>
#include <vector>

struct sqlite3;

// One recorded end-of-game result.
struct HighScore {
    std::string name;        // player/hero name
    int         faction    = 0;
    int         score      = 0;
    int         days       = 0;
    int         difficulty = 1;
    bool        won        = false;
    std::string rank;        // title at record time
    long long   when       = 0; // unix seconds
};

// ── ScoreDB ───────────────────────────────────────────────────────────────────
// Highscore table in the shared meta sqlite file (same file as Hideout/Conquest,
// its own `highscores` table). Self-creates on open, like the others — nothing
// to ship, it builds itself in the per-user data dir on first run.
class ScoreDB {
public:
    ScoreDB() = default;
    ~ScoreDB();

    bool open(const std::string& dbPath);   // creates the table if missing
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // Insert a result. Returns true and sets outIsBest=true if it's the new #1.
    bool addScore(const HighScore& s, bool* outIsBest = nullptr);

    // Top N by score, descending.
    std::vector<HighScore> topScores(int limit = 10);

private:
    sqlite3* m_db = nullptr;
};
