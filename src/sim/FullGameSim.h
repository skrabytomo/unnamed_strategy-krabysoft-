#pragma once
#include <cstdint>
#include <vector>
#include "../hero/FactionId.h"
#include "SimDB.h"

// Simulates a complete 2-player game (WorldGen → hero moves → town builds → combat)
// without any SDL/ImGui/OpenGL dependency. Both sides are AI-controlled.
class FullGameSim
{
public:
    struct Config
    {
        FactionId f1           = FactionId::HolyOrder;
        FactionId f2           = FactionId::CrimsonWardens;
        uint32_t  seed         = 42;
        int       maxWeeks     = 30;
        bool      recordSnapshots = false;
        bool      useMCTS         = false;  // slower but smarter hero decisions
        int       mctsSimulations = 20;
        int       mctsRolloutWeeks= 3;
    };

    struct Result
    {
        int       winner        = 0;   // 1 or 2; 0 = true draw (equal strength)
        int       endWeek       = 0;
        bool      combatDecided = false; // true = winner decided by actual combat
        FactionId winFaction    = FactionId::None;
        std::vector<TurnSnapshot> snapshots;
    };

    Result run(const Config& cfg);
};
