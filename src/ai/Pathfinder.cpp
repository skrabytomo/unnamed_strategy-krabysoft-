#include "Pathfinder.h"
#include "../world/HexGrid.h"
#include <unordered_map>
#include <queue>
#include <climits>
#include <algorithm>

// ── A* ─────────────────────────────────────────────────────────────────────────
std::vector<HexCoord> Pathfinder::find(
    const HexMap& map,
    HexCoord      start,
    HexCoord      goal,
    CostFn        costFn,
    int           maxCost)
{
    if (start == goal) return {};
    if (!map.inBounds(goal)) return {};

    struct Node {
        int      f;
        HexCoord h;
        bool operator>(const Node& o) const { return f > o.f; }
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<HexCoord, HexCoord, HexCoordHash> cameFrom;
    std::unordered_map<HexCoord, int,      HexCoordHash> gScore;

    gScore[start] = 0;
    open.push({ HexGrid::distance(start, goal), start });

    while (!open.empty()) {
        auto [f, current] = open.top(); open.pop();

        if (current == goal) {
            // Reconstruct path
            std::vector<HexCoord> path;
            HexCoord c = goal;
            while (!(c == start)) {
                path.push_back(c);
                c = cameFrom[c];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        int gCur = gScore.count(current) ? gScore[current] : INT_MAX;

        for (auto& nb : HexGrid::neighbors(current)) {
            if (!map.inBounds(nb)) continue;

            const HexTile* tile = map.getTile(nb);
            if (!tile) continue;

            int cost = costFn(nb);
            if (cost >= 99) continue; // impassable

            int tentG = gCur + cost;
            if (tentG > maxCost) continue;

            int prevG = gScore.count(nb) ? gScore[nb] : INT_MAX;
            if (tentG < prevG) {
                cameFrom[nb] = current;
                gScore[nb]   = tentG;
                int h        = HexGrid::distance(nb, goal);
                open.push({ tentG + h, nb });
            }
        }
    }

    return {}; // no path
}

// ── Reachable (Dijkstra flood fill) ───────────────────────────────────────────
std::vector<HexCoord> Pathfinder::reachable(
    const HexMap& map,
    HexCoord      start,
    CostFn        costFn,
    int           movementPoints)
{
    struct Node {
        int      cost;
        HexCoord h;
        bool operator>(const Node& o) const { return cost > o.cost; }
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<HexCoord, int, HexCoordHash> visited;

    open.push({ 0, start });
    visited[start] = 0;

    while (!open.empty()) {
        auto [cost, current] = open.top(); open.pop();

        if (cost > visited[current]) continue; // stale entry

        for (auto& nb : HexGrid::neighbors(current)) {
            if (!map.inBounds(nb)) continue;

            int stepCost = costFn(nb);
            if (stepCost >= 99) continue;

            int newCost = cost + stepCost;
            if (newCost > movementPoints) continue;

            auto it = visited.find(nb);
            if (it == visited.end() || newCost < it->second) {
                visited[nb] = newCost;
                open.push({ newCost, nb });
            }
        }
    }

    std::vector<HexCoord> result;
    result.reserve(visited.size());
    for (auto& [h, c] : visited)
        if (!(h == start)) result.push_back(h);
    return result;
}
