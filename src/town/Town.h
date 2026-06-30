#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../world/HexMap.h"
#include "../hero/Hero.h"
#include "../data/Resources.h"
#include "BuildingDef.h"
#include "UnitDef.h"

// ── Per-dwelling state ─────────────────────────────────────────────────────────
struct DwellingState
{
    int         buildingId  = 0;
    int         tier        = 0;
    UpgradePath path        = UpgradePath::None;
    int         available   = 0;   // units available to recruit this week
    int         accumulated = 0;   // carried over from previous weeks (uncapped)
};

// ── Town instance ──────────────────────────────────────────────────────────────
class Town
{
public:
    uint32_t    id       = 0;
    std::string name;
    FactionId   faction  = FactionId::None;
    HexCoord    pos      = {0, 0};
    uint32_t    ownerId  = 0;   // hero/player id, 0 = neutral

    // Built buildings (set of building IDs)
    std::vector<int> builtBuildings;

    // Dwelling states per tier
    std::vector<DwellingState> dwellings; // indexed by tier-1

    // Weekly resource income (sum of all economy buildings)
    Resources weeklyIncome;

    // Fort HP (for siege) — 0 = no fort built
    int fortHP    = 0;
    int fortMaxHP = 0;

    // One building per day limit
    int builtToday = 0;

    // Garrison — units defending the town (up to 7 slots)
    std::vector<UnitStack> garrison;

    // ── Siege state ───────────────────────────────────────────────────────────
    bool underSiege        = false;  // an enemy siege camp is adjacent
    bool siegeFortified    = false;  // defender used Fortify this siege turn (one-shot)
    int  fortifyDefBonus   = 0;      // +DEF to all garrison units this siege
    int  fortifyWallBonus  = 0;      // extra wall HP rounds for siege combat
    int  fortifyTowerBonus = 0;      // extra tower damage per shot in siege combat

    // ── Queries ────────────────────────────────────────────────────────────────
    bool hasBuilding(int buildingId) const;
    // currentWeek: pass 0 to skip week check. weekDiscount reduces minWeek requirement.
    bool canBuild(int buildingId, const std::vector<BuildingDef>& defs,
                  int currentWeek = 0, int weekDiscount = 0) const;

    // Returns total weekly growth for a tier (base + support bonuses)
    int weeklyGrowth(int tier) const;

    // ── Actions ───────────────────────────────────────────────────────────────
    // Build a building — returns false if prerequisites not met or already built.
    // costMult: multiplier applied to build cost (e.g. 0.8 for 20% discount)
    bool build(int buildingId, const std::vector<BuildingDef>& defs, Resources& playerRes,
               float costMult = 1.0f);

    // Called every week — add growth to available pools
    void onWeekStart(const std::vector<BuildingDef>& defs);

    // Recruit units from a dwelling — returns actual count recruited
    // costMult: multiplier applied to total cost (e.g. 0.8 for 20% discount)
    int recruit(int tier, int count, Resources& playerRes,
                const std::vector<UnitDef>& unitDefs, float costMult = 1.0f);
};
