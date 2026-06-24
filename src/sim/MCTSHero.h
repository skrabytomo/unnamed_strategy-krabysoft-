#pragma once
#include "../world/HexGrid.h"
#include "../hero/Hero.h"
#include "../town/Town.h"
#include "../data/ResourceNode.h"
#include "../world/WorldObject.h"
#include "../world/HexMap.h"
#include "../town/BuildingRegistry.h"
#include "../hero/HeroClass.h"
#include <vector>
#include <cstdint>

struct UnitDef;

// Lightweight snapshot of game state needed for MCTS rollouts.
struct GameSnapshot
{
    Hero                      hero;
    Hero                      opponent;
    std::vector<Town>         towns;
    std::vector<ResourceNode> resources;
    std::vector<WorldObject>  objects;
    // HexMap is large — we store only hero/town positions and blocked tiles implicitly
    // via the real map pointer (read-only during rollout).
};

// MCTS over hero-move decisions.
// Evaluates a list of candidate target positions by running shallow forward
// simulations and picking the one that maximises estimated win probability.
//
// Usage: call selectGoal() instead of just taking the highest-scored candidate.
class MCTSHero
{
public:
    struct Params
    {
        int  simulations  = 30;    // rollout sims per candidate
        int  rolloutWeeks = 4;     // weeks to simulate per rollout
        int  seed         = 0;
    };

    // Returns the best goal position from candidates[].
    // Falls back to candidates[0] if simulations==0 or candidates is empty.
    static HexCoord selectGoal(
        const std::vector<HexCoord>& candidates,
        const GameSnapshot&          snap,
        HexMap&                      map,
        const std::vector<UnitDef>&  udefs,
        const BuildingRegistry&      reg,
        const HeroClassRegistry&     classReg,
        const Params&                params);

private:
    // Run one short forward simulation starting from snap with hero moving toward goal.
    // Returns estimated win probability for the hero in snap (0.0 or 1.0 from rollout outcome).
    static float rollout(
        HexCoord             goal,
        GameSnapshot         snap,        // copy — modified during rollout
        HexMap&              map,
        const std::vector<UnitDef>& udefs,
        const BuildingRegistry& reg,
        const HeroClassRegistry& classReg,
        int                  rolloutWeeks,
        uint32_t             seed);
};
