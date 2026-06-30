#include "../core/DevLog.h"
#include "Town.h"
#include "UnitDef.h"
#include <algorithm>
#include <stdio.h>

bool Town::hasBuilding(int buildingId) const
{
    return std::find(builtBuildings.begin(), builtBuildings.end(), buildingId)
           != builtBuildings.end();
}

bool Town::canBuild(int buildingId, const std::vector<BuildingDef>& defs,
                    int currentWeek, int weekDiscount) const
{
    if (hasBuilding(buildingId)) return false;

    for (auto& def : defs) {
        if (def.id != buildingId) continue;
        // Check faction match
        if (def.faction != FactionId::None && def.faction != faction) return false;
        // Check week requirement (0 = always available; skip check when currentWeek==0)
        if (currentWeek > 0 && def.minWeek > 0) {
            int effectiveMin = std::max(1, def.minWeek - weekDiscount);
            if (currentWeek < effectiveMin) return false;
        }
        // Check prerequisites
        for (int prereq : def.prerequisites)
            if (!hasBuilding(prereq)) return false;
        // Block opposite path upgrade for same tier (PathA and PathB are mutually exclusive)
        if (def.path != UpgradePath::None && def.tier > 0) {
            for (const auto& d : dwellings) {
                if (d.tier == def.tier && d.path != UpgradePath::None && d.path != def.path)
                    return false;
            }
        }
        return true;
    }
    return false;
}

int Town::weeklyGrowth(int tier) const
{
    // Base growth from dwelling + any support building bonuses
    // Support bonus: +2 per support building built (simplified for now)
    int base = 0;
    for (auto& d : dwellings)
        if (d.tier == tier) { base = 4 + tier; break; } // T1=5, T6=10 base
    return base;
}

bool Town::build(int buildingId, const std::vector<BuildingDef>& defs, Resources& playerRes,
                 float costMult)
{
    if (builtToday >= 1) return false;
    if (!canBuild(buildingId, defs)) return false;

    const BuildingDef* def = nullptr;
    for (auto& d : defs) if (d.id == buildingId) { def = &d; break; }
    if (!def) return false;

    Resources cost = def->cost;
    if (costMult != 1.0f) {
        for (int i = 0; i < RESOURCE_COUNT; ++i)
            cost.amounts[i] = static_cast<int>(cost.amounts[i] * costMult);
    }

    if (!playerRes.canAfford(cost)) {
        gLog("Town %s: cannot afford %s\n", name.c_str(), def->name.c_str());
        return false;
    }

    playerRes.spend(cost);
    builtBuildings.push_back(buildingId);
    builtToday = 1;

    // If dwelling, add dwelling state and grant first week's units immediately
    if (def->category == BuildingCategory::UnitDwelling && def->tier > 0) {
        bool found = false;
        for (auto& d : dwellings) {
            if (d.tier == def->tier) {
                d.buildingId  = buildingId;
                d.path        = def->path;
                found = true;
                break;
            }
        }
        if (!found) {
            DwellingState ds;
            ds.buildingId = buildingId;
            ds.tier       = def->tier;
            ds.path       = def->path;
            ds.available  = def->weeklyGrowth > 0 ? def->weeklyGrowth : (4 + def->tier);
            ds.accumulated = ds.available;
            dwellings.push_back(ds);
        }
    }

    // Add weekly income if economy building
    weeklyIncome.addAll(def->weeklyIncome);

    gLog("Town %s: built %s\n", name.c_str(), def->name.c_str());
    return true;
}

void Town::onWeekStart(const std::vector<BuildingDef>& defs)
{
    // Sum growth bonuses from all built non-dwelling buildings
    int globalBonus = 0;
    for (int bid : builtBuildings) {
        for (const auto& d : defs) {
            if (d.id == bid && d.growthBonus > 0) { globalBonus += d.growthBonus; break; }
        }
    }

    // Add weekly growth to each dwelling
    for (auto& dwelling : dwellings) {
        const BuildingDef* def = nullptr;
        for (auto& d : defs) if (d.id == dwelling.buildingId) { def = &d; break; }
        if (!def) continue;

        int growth = def->weeklyGrowth;
        if (dwelling.path == UpgradePath::PathA && def->growthA > 0)
            growth = def->growthA;
        else if (dwelling.path == UpgradePath::PathB && def->growthB > 0)
            growth = def->growthB;

        dwelling.available   += growth + globalBonus;
        dwelling.accumulated  = dwelling.available;
    }
}

int Town::recruit(int tier, int count, Resources& playerRes,
                  const std::vector<UnitDef>& unitDefs, float costMult)
{
    DwellingState* dwelling = nullptr;
    for (auto& d : dwellings)
        if (d.tier == tier) { dwelling = &d; break; }

    if (!dwelling || dwelling->available <= 0) return 0;

    int actual = std::min(count, dwelling->available);

    // Find unit def for this faction + tier + path
    const UnitDef* udef = nullptr;
    for (auto& u : unitDefs) {
        if (u.faction == faction && u.tier == tier && u.path == dwelling->path) {
            udef = &u; break;
        }
    }
    if (!udef) return 0;

    // Calculate total cost (costMult applies Efficient specialty discount etc.)
    auto scaledCost = [&](int n) -> Resources {
        Resources c;
        for (int i = 0; i < RESOURCE_COUNT; ++i)
            c.amounts[i] = static_cast<int>(udef->cost.amounts[i] * n * costMult);
        return c;
    };

    Resources totalCost = scaledCost(actual);
    if (!playerRes.canAfford(totalCost)) {
        // Recruit as many as we can afford
        actual = 0;
        for (int n = dwelling->available; n > 0; --n) {
            if (playerRes.canAfford(scaledCost(n))) { actual = n; break; }
        }
        if (actual == 0) return 0;
        totalCost = scaledCost(actual);
    }

    playerRes.spend(totalCost);
    dwelling->available -= actual;

    gLog("Town %s: recruited %d tier-%d units\n", name.c_str(), actual, tier);
    return actual;
}
