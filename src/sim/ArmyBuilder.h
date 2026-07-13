#pragma once
#include <vector>
#include "../combat/CombatUnit.h"
#include "../hero/Hero.h"
#include "../town/BuildingRegistry.h"

// Builds a representative army and hero for a faction after N campaign weeks.
// Build order: T1+T2 week 1-2, T3 week 3, T4 week 5, T5 week 7, T6 week 9.
// Stack count = accumulated weekly growth from unlock week to present.
// Stack sizes are capped to prevent O(n) damage roll loops from dominating.
//
// Unit stats/tags/cost come from BuildingRegistry (the single source of truth —
// same data the real game's towns/recruitment use). This class only adds the
// simulation-specific "how many weeks has this been unlocked / growing" math;
// it does not keep its own copy of unit stats.
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

    // Builds a single CombatUnit stack from a UnitDef (shared by buildArmy and
    // Conquest-mode team assembly).
    static CombatUnit makeCombatUnit(const UnitDef& d, int count, int slot);

    // Hero level from weeks: 2 fights/week × XP ramp → approx level
    static int heroLevelFromWeeks(int weeks);

private:
    // Simplified build-order assumption used only for the "what would this
    // faction's army look like at week N" estimate (real in-game unlock
    // depends on prerequisites/resources, not a fixed week).
    static int unlockWeekForTier(int tier);

    // Shared registry instance — self-initializing, used by both the in-game
    // Battle Simulator and the headless balance sim (fullgame_main), neither
    // of which necessarily has a live Game/town instance to query.
    static const BuildingRegistry& registry();
};
