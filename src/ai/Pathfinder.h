#pragma once
#include <vector>
#include <functional>
#include "../world/HexMap.h"

// Returns list of hexes from start (exclusive) to goal (inclusive)
// Empty if no path found or start == goal
// costFn: given a HexCoord, return movement cost to enter it (or 999 if impassable)

class Pathfinder
{
public:
    using CostFn = std::function<int(HexCoord)>;

    // Find path on map from start to goal using costFn for terrain costs
    // maxCost: stop search if path cost exceeds this (movement pool limit)
    //          pass INT_MAX to find path regardless of cost
    // maxNodes: hard cap on tiles EXPANDED before giving up (returns {}).
    //   `maxCost` bounds the search by path COST, which on a large map with
    //   cheap road tiles still sweeps a colossal area before admitting a goal
    //   is unreachable. Measured on an 8-player XL map: a *failing* 400-cost
    //   search cost ~305 ms, and a late-game turn ran 15 of them — 4.5 SECONDS
    //   in one turn, which is the Watch-AI freeze.
    //   A* is best-first, so a REACHABLE goal is found after relatively few
    //   expansions; it is the failures that explore everything. Capping node
    //   count therefore bites almost exclusively on doomed searches.
    //   Trade-off: an extremely long but genuinely walkable route may now come
    //   back empty. That is safe here — the AI re-attempts on a cadence and
    //   caches committed marches — but pass a larger cap if an exact answer
    //   matters more than latency.
    static std::vector<HexCoord> find(
        const HexMap& map,
        HexCoord      start,
        HexCoord      goal,
        CostFn        costFn,
        int           maxCost  = 999,
        int           maxNodes = 20000
    );

    // Returns all reachable hexes within movementPoints budget
    // Used for movement range highlight
    static std::vector<HexCoord> reachable(
        const HexMap& map,
        HexCoord      start,
        CostFn        costFn,
        int           movementPoints
    );
};
