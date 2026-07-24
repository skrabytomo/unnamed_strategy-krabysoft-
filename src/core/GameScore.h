#pragma once
#include <string>
#include <vector>
#include <utility>

// classic-strategy-style end-of-game score: speed-dominated, difficulty-multiplied, with a
// dominance bonus and a rank title. Fast wins on high difficulty score highest;
// a loss still earns a small survival score so the board isn't all-or-nothing.
// (Rank titles are original — no trademarked hero names.)
struct GameScore {
    int  score = 0;
    int  days  = 0;      // in-game days elapsed
    bool won   = false;
    int  difficulty = 1; // 0=Easy 1=Normal 2=Hard
    std::string rank;    // title for this score bracket
    // Human-readable point breakdown for the results screen: (label, points).
    std::vector<std::pair<std::string, int>> breakdown;
};

// Inputs are all things the game already tracks at the moment of win/loss.
GameScore computeGameScore(bool won, int days, int difficulty,
                           int townsHeld, int maxHeroLevel);

// Rank title for a final score (exposed so the highscores list can show it too).
const char* scoreRank(int score);
