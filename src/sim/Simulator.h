#pragma once
#include <functional>
#include "SimTypes.h"
#include "../combat/CombatEngine.h"

// Monte Carlo batch simulator.
// Runs thousands of headless CombatEngine fights and produces win-rate statistics.
// All stdout suppressed during runs via CombatEngine::setSilent(true).
class Simulator
{
public:
    using ProgressCallback = std::function<void(int done, int total)>;

    // Run the simulation described by config. Progress callback is optional.
    static SimResult run(const SimConfig& cfg,
                         ProgressCallback onProgress = nullptr);

private:
    // Run a single faction matchup — returns the filled FactionMatchup.
    static FactionMatchup runMatchup(FactionId f1, FactionId f2,
                                     int weeks, int numBattles,
                                     uint32_t baseSeed,
                                     AIDifficulty ai1, AIDifficulty ai2);

    // Build human-readable balance report from all matchups.
    static std::string buildReport(const SimResult& result);

    static constexpr float IMBALANCE_THRESHOLD = 0.15f;
    static constexpr int   MAX_ROUNDS          = 500;
};
