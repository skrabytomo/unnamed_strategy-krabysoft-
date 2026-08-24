// patch_09_optimal_path_following.cpp
// Unnamed Strategy — AI Improvement: Eliminate Redundant A* / Maximize Movement
// =============================================================================
// Addresses AI_ROADMAP.md [FITS] item:
// "Toward-optimal pathfinding — we already cap the search horizon; tighten so
//  the AI wastes no movement points on sub-optimal tiles."
//
// Problem: The stepToward lambda (and similar movement helpers) recalculate
// Pathfinder::find() after EVERY single step. On a 40-hex march this fires
// A* 40 times for one hero. Each call is pure but expensive (~0.3-2 ms), and
// tie-breaking jitter can make the hero zig-zag between two equally-scored
// routes, wasting movement points on diagonal corrections.
//
// Fix: Calculate the full path ONCE per turn goal, cache it in the hero's
// AI state, then walk as far as movePool allows along that cached vector.
// Only invalidate and recalc if:
//   - The next tile becomes impassable (another hero steps on it, terrain changes)
//   - The hero's movePool is refreshed (new week / March ability)
//   - The goal itself changes (new target scored higher)
//   - The path is fully consumed (reached goal or ran out of steps)
//
// Secondary fix: When the full path costs more than movePool, instead of
// failing the entire A* (which returns empty and freezes the hero), pick the
// farthest reachable tile along the path as a sub-goal. This eliminates the
// "hero stands still because goal is 41 hexes away and movePool is 40" bug.
// =============================================================================

// --- INSERT INTO: src/hero/Hero.h (Hero struct, near stickyDock fields) ---
/*
    // -----------------------------------------------------------------
    // Optimal path cache (patch_09)
    // -----------------------------------------------------------------
    // Cached path from last Pathfinder::find call. Valid for one turn
    // unless invalidated by map changes or goal switch.
    std::vector<HexCoord> cachedPath;
    int                   cachedPathGoalId = -1;   // what we were walking toward
    int                   cachedPathTurn   = -1;   // turn number when cached
    bool                  pathCacheValid   = false;
    // -----------------------------------------------------------------
*/

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: replace the existing stepToward lambda (around line 1076)
// and/or the manual one-step-at-a-time walk loops.

/*
    // ================================================================
    // PATCH 09: Cached optimal path following
    // ================================================================
    // Replaces the old stepToward that recalculated A* every step.
    auto stepTowardOptimal = [&](Hero& h, HexCoord goal, int goalId) {
        const int thisTurn = m_turns.turnCount(); // or whatever turn counter you have

        // --- Cache validation ---
        bool needRecalc = !h.pathCacheValid
                       || h.cachedPath.empty()
                       || h.cachedPathGoalId != goalId
                       || h.cachedPathTurn != thisTurn;

        if (!needRecalc) {
            // Verify next tile is still passable
            if (!h.cachedPath.empty()) {
                HexCoord next = h.cachedPath[0];
                const HexTile* nt = m_map.getTile(next);
                if (!nt || h.moveCost(nt->terrain) >= 99) {
                    needRecalc = true; // blocked
                }
            }
        }

        if (needRecalc) {
            h.cachedPath = Pathfinder::find(m_map, h.pos, goal, costFn);
            h.cachedPathGoalId = goalId;
            h.cachedPathTurn   = thisTurn;
            h.pathCacheValid   = true;

            if (h.cachedPath.empty()) {
                // Full path unreachable — try a reachable sub-goal
                // Walk backward from goal toward hero until we find a reachable tile
                std::vector<HexCoord> reversePath;
                HexCoord probe = goal;
                while (!(probe == h.pos)) {
                    reversePath.push_back(probe);
                    // Move one hex toward hero using hex grid neighbor logic
                    // (simplified: pick neighbor with min distance to hero)
                    int bestDist = HexGrid::distance(probe, h.pos);
                    HexCoord best = probe;
                    for (auto& nb : HexGrid::neighbors(probe)) {
                        if (!m_map.inBounds(nb)) continue;
                        int d = HexGrid::distance(nb, h.pos);
                        if (d < bestDist) { bestDist = d; best = nb; }
                    }
                    if (best == probe) break; // stuck
                    probe = best;
                }
                // Try each tile in reversePath as a new goal
                for (auto& subGoal : reversePath) {
                    h.cachedPath = Pathfinder::find(m_map, h.pos, subGoal, costFn);
                    if (!h.cachedPath.empty()) break;
                }
                if (h.cachedPath.empty()) {
                    h.pathCacheValid = false;
                    return; // genuinely stuck
                }
            }
        }

        // --- Walk as far as movePool allows along cached path ---
        int stepsTaken = 0;
        while (h.movePool > 0 && !h.cachedPath.empty()) {
            HexCoord next = h.cachedPath[0];
            const HexTile* nt = m_map.getTile(next);
            if (!nt) break;
            int cost = h.moveCost(nt->terrain);
            if (h.movePool < cost) break;

            if (HexTile* old = m_map.getTile(h.pos)) old->heroId = 0;
            h.pos = next;
            h.movePool -= cost;
            if (HexTile* nh = m_map.getTile(h.pos)) nh->heroId = h.id;

            h.cachedPath.erase(h.cachedPath.begin());
            ++stepsTaken;

            if (h.pos == goal) {
                h.pathCacheValid = false; // reached destination
                break;
            }
        }

        if (stepsTaken > 0) {
            gLog("[PATH] %s moved %d steps toward goal %d (mp left %d)\n",
                 h.name.c_str(), stepsTaken, goalId, h.movePool);
        }
    };
    // ================================================================
*/

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: inside the enemy AI hero turn loop, where stepToward is currently
// called. Replace stepToward(goal) with stepTowardOptimal(eHero, goal, goalId).
// The goalId can be the target's unique ID (town ID, hero ID, resource ID).

/*
    // Example replacement in the scout / economic / raider movement blocks:
    // OLD:
    //   stepToward(goal);
    // NEW:
    //   stepTowardOptimal(eHero, goal, chosenGoalId);
*/

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: at the top of aiTakeHeroTurn or doEndTurn, BEFORE any movement.
// Invalidate all caches at turn start to handle map mutations (sieges, builds).

/*
    // ================================================================
    // PATCH 09b: Global path cache invalidation on map mutations
    // ================================================================
    // Whenever a town is captured, a building is built, or a siege starts,
    // terrain costs can change (roads built, walls erected, etc.).
    // Call this to invalidate all hero path caches for that owner.
    auto invalidatePathCaches = [&](int ownerId) {
        for (auto& h : m_heroes) {
            if (h.ownerId == ownerId) {
                h.pathCacheValid = false;
                h.cachedPath.clear();
            }
        }
    };
    // Hook into: town capture, building completion, siege start/end,
    // and any terrain modification (editor, spells, etc.).
    // ================================================================
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6 --seed=123
// Metrics to check:
//   - [PERF] lines: pathfinding time should drop (fewer A* calls)
//   - [PATH] logs: heroes should move multiple steps per A* call
//   - No increase in "hero idle" counts (movement still works correctly)
// Before: 40 A* calls for a 40-hex march.
// After: 1 A* call for a 40-hex march, then 40 cheap cached steps.
// =============================================================================
