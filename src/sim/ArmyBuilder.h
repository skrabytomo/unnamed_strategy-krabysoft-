#pragma once
#include <vector>
#include "../combat/CombatUnit.h"
#include "../hero/Hero.h"
#include "SimUnitData.h"

// Builds a representative army and hero for a faction after N campaign weeks.
// Build order: T1+T2 week 1-2, T3 week 3, T4 week 5, T5 week 7, T6 week 9.
// Stack count = accumulated weekly growth from unlock week to present.
// Stack sizes are capped to prevent O(n) damage roll loops from dominating.
class ArmyBuilder
{
public:
    static constexpr int MAX_STACK = 80;   // cap units per slot for combat speed

    // Returns up to 6 CombatUnit stacks for the given faction + weeks
    static std::vector<CombatUnit> buildArmy(FactionId faction, int weeks);

    // Returns total gold cost of the army produced by buildArmy()
    static int armyGoldCost(FactionId faction, int weeks);

    // Returns a Hero with attack/defense scaled by hero level (derived from weeks)
    static Hero buildHero(FactionId faction, int weeks);

    // Hero level from weeks: 2 fights/week × XP ramp → approx level
    static int heroLevelFromWeeks(int weeks);

private:
    static const UnitSimData* getTierData(FactionId faction, int tier);
};
