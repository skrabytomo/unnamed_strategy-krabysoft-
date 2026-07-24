#include "GameScore.h"
#include <algorithm>
#include <cmath>

const char* scoreRank(int score)
{
    // Original fantasy rank ladder (deliberately NOT classic-strategy's hero names).
    if (score >= 1800) return "Immortal Sovereign";
    if (score >= 1300) return "Grand Marshal";
    if (score >=  900) return "Warlord";
    if (score >=  600) return "Conqueror";
    if (score >=  350) return "Knight-Captain";
    if (score >=  150) return "Squire";
    return "Wandering Sellsword";
}

int scoreRankTier(int score)
{
    if (score >= 1800) return 6;
    if (score >= 1300) return 6;
    if (score >=  900) return 5;
    if (score >=  600) return 4;
    if (score >=  350) return 3;
    if (score >=  150) return 2;
    return 1;
}

GameScore computeGameScore(bool won, int days, int difficulty,
                           int townsHeld, int maxHeroLevel)
{
    GameScore gs;
    gs.won        = won;
    gs.days       = std::max(0, days);
    gs.difficulty = std::clamp(difficulty, 0, 2);

    if (won) {
        // Speed is king: a fast win scores near the cap, a slow grind decays to
        // a floor. ~5 pts/day, cap 1200, floor 50.
        int speed = std::max(50, 1200 - gs.days * 5);
        int towns = townsHeld    * 40;   // holding the map
        int hero  = maxHeroLevel * 15;   // developed a champion
        gs.breakdown.push_back({ "Speed (fewer days is better)", speed });
        gs.breakdown.push_back({ "Towns held",                   towns });
        gs.breakdown.push_back({ "Best hero level",              hero  });
    } else {
        // Consolation: reward how long you held out.
        int survival = gs.days / 2;
        gs.breakdown.push_back({ "Weeks survived", survival });
    }

    int subtotal = 0;
    for (auto& b : gs.breakdown) subtotal += b.second;

    // Difficulty coefficient, applied last (classic-strategy does this too).
    static const float kMult[3] = { 0.8f, 1.0f, 1.3f };
    float mult = kMult[gs.difficulty];
    gs.score = static_cast<int>(std::lround(subtotal * mult));
    gs.breakdown.push_back({ gs.difficulty == 0 ? "Difficulty x0.8 (Easy)"
                           : gs.difficulty == 2 ? "Difficulty x1.3 (Hard)"
                           :                      "Difficulty x1.0 (Normal)",
                             gs.score - subtotal });

    gs.rank = scoreRank(gs.score);
    return gs;
}
