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
    static std::vector<HexCoord> find(
        const HexMap& map,
        HexCoord      start,
        HexCoord      goal,
        CostFn        costFn,
        int           maxCost = 999
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
