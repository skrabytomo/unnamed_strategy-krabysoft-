#pragma once
#include <string>
#include <vector>
#include "../data/Resources.h"
#include "../hero/Hero.h"  // FactionId

// ── Building categories ────────────────────────────────────────────────────────
enum class BuildingCategory : uint8_t
{
    UnitDwelling,   // produces units each week
    Support,        // buffs stats / magic
    Economy,        // generates resources passively
    Special,        // unique per faction
    Fort,           // walls, gate, towers (siege defense)
    MageGuild,      // teaches spells to visiting hero
};

// ── Unit upgrade path selection ────────────────────────────────────────────────
enum class UpgradePath : uint8_t { None = 0, PathA, PathB };

// ── Static building definition (shared, not per-town) ─────────────────────────
struct BuildingDef
{
    int               id          = 0;
    std::string       name;
    std::string       description;
    BuildingCategory  category    = BuildingCategory::Economy;
    FactionId         faction     = FactionId::None; // None = any faction

    Resources         cost;           // one-time build cost
    Resources         weeklyIncome;   // passive income if economy building

    int               tier        = 0;  // unit tier this dwelling produces (0 = not a dwelling)
    UpgradePath       path        = UpgradePath::None; // which upgrade path this dwelling provides
    int               weeklyGrowth = 0; // base units added per week
    int               growthA     = 0;  // growth for path A upgrade
    int               growthB     = 0;  // growth for path B upgrade
    int               growthBonus = 0;  // bonus units/week added to ALL dwellings when this is built

    // Minimum game week before this building can be constructed (0 = always available)
    int              minWeek     = 0;

    // Prerequisites — building IDs that must be built first
    std::vector<int> prerequisites;

    // Path A / B upgrade building IDs (0 = none)
    int upgradeA = 0;
    int upgradeB = 0;
};
