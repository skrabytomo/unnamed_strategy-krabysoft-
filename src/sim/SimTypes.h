#pragma once
#include <string>
#include <array>
#include "../hero/Hero.h"
#include "../combat/CombatEngine.h"   // AIDifficulty

struct SimConfig
{
    FactionId    faction1      = FactionId::HolyOrder;
    FactionId    faction2      = FactionId::CrimsonWardens;
    bool         allVsAll      = false;
    int          weeks         = 4;
    int          numBattles    = 1000;
    uint32_t     seed          = 42;
    AIDifficulty side1AI       = AIDifficulty::Standard;
    AIDifficulty side2AI       = AIDifficulty::Standard;
};

struct FactionMatchup
{
    FactionId f1            = FactionId::None;
    FactionId f2            = FactionId::None;
    int       battles       = 0;
    float     winRate1      = 0.0f;   // [0.0-1.0] faction1 win rate
    float     avgRounds     = 0.0f;
    float     avgF1Survival = 0.0f;   // surviving HP fraction on f1 wins
    float     avgF2Survival = 0.0f;
    float     avgF1LossRate = 0.0f;   // avg fraction of f1 army killed per battle
    float     avgF2LossRate = 0.0f;   // avg fraction of f2 army killed per battle
    int       armyCostF1    = 0;      // total gold cost of f1 army
    int       armyCostF2    = 0;
    bool      imbalanced    = false;  // |winRate1 - 0.5| > 0.15
};

struct SimResult
{
    int weeksSimulated   = 0;
    int battlesPerMatchup = 0;
    // 9x9 grid — matchups[i][j] = faction i vs faction j
    std::array<std::array<FactionMatchup, 9>, 9> matchups{};
    std::string balanceReport;
    bool        done = false;
};
