// patch_07_naval_sticky_dock.cpp
// Unnamed Strategy — AI Improvement: Sticky Dock Walk for Naval Invasion
// =============================================================================
// Addresses AI_ROADMAP.md (2026-07-25) naval investigation:
// "The remaining fix is to make a committed dock walk STICKY — latch it like
//  marchGoal already latches an overseas target after boarding."
//
// Problem: On water-separated maps (Ring seed 999), heroes walk toward docks
// (40->27, 71->59, 61->39 hexes) then abandon the trip at week 7 because
// wantBoatForBestTarget goes false when local targets outbid the overseas town.
// Result: zero boats, games never resolve (5 of 6 players alive at week 80).
//
// Fix: Once a dock is chosen for an overseas target, latch the dock hex and
// target ID in the hero's AI state. On subsequent turns, continue pathing to
// that dock even if the overseas target's score drops. Only release when:
//   - Hero successfully boards
//   - Hero reaches dock but aiTryBoat fails
//   - Dock is proven unreachable by Pathfinder
//   - Target town no longer exists or was captured
//   - Dock structure was destroyed/removed
// =============================================================================

// --- INSERT INTO: src/hero/Hero.h (Hero struct, near marchCooldownWeek) ---
/*
    // -----------------------------------------------------------------
    // Naval invasion: sticky dock walk (patch_07)
    // -----------------------------------------------------------------
    // When committed to an overseas target, latch the dock so we don't
    // turn back just because local targets outbid next turn.
    HexCoord stickyDockHex;
    bool     hasStickyDockGoal = false;
    int      stickyDockTargetId = -1;   // town ID being pursued
    // -----------------------------------------------------------------
*/

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: top of the per-hero turn function (aiTakeHeroTurn or equivalent),
// BEFORE target scoring / wantBoatForBestTarget is evaluated.

/*
    // ================================================================
    // PATCH 07: Sticky dock walk — continue a committed naval march
    // ================================================================
    if (hero.hasStickyDockGoal) {
        // --- Validation ---
        Town* targetTown = getTown(hero.stickyDockTargetId);
        bool targetStillValid = (targetTown != nullptr && 
                                 targetTown->owner != hero.ownerId &&
                                 !targetTown->isAbandoned);
        bool dockStillValid = (mapGetObject(hero.stickyDockHex) == ObjShipyard ||
                               mapGetObject(hero.stickyDockHex) == ObjTownDock); // adapt to your object enum

        if (!targetStillValid || !dockStillValid) {
            gLog.add("[NAVAL] %s sticky dock released (target=%d dock=%d)",
                     hero.name.c_str(), (int)targetStillValid, (int)dockStillValid);
            hero.hasStickyDockGoal = false;
        } else {
            // --- Continue pathing to the dock ---
            // Use existing pathfinder; same params as normal hero pathing.
            Pathfinder::Result path = m_pathfinder.find(
                hero.hex,
                hero.stickyDockHex,
                hero.movementPoints,
                PathFlags::HeroWalk
            );

            if (path.found) {
                hero.setPath(path); // assign and follow

                if (hero.hex == hero.stickyDockHex) {
                    // --- Reached dock: attempt boarding ---
                    if (aiTryBoat(hero, *targetTown)) {
                        gLog.add("[NAVAL] %s boarded at latched dock (%d,%d) -> %s",
                                 hero.name.c_str(),
                                 hero.stickyDockHex.x, hero.stickyDockHex.y,
                                 targetTown->name.c_str());
                    } else {
                        gLog.add("[NAVAL] %s reached latched dock (%d,%d) but boarding failed",
                                 hero.name.c_str(),
                                 hero.stickyDockHex.x, hero.stickyDockHex.y);
                    }
                    hero.hasStickyDockGoal = false;
                } else {
                    gLog.add("[NAVAL] %s walking to sticky dock (%d,%d), dist=%.1f mp=%d",
                             hero.name.c_str(),
                             hero.stickyDockHex.x, hero.stickyDockHex.y,
                             path.totalCost, hero.movementPoints);
                }

                // Skip the rest of this turn's target re-evaluation;
                // the dock walk is the priority.
                return;
            } else {
                gLog.add("[NAVAL] %s sticky dock (%d,%d) unreachable (path.found=false), releasing",
                         hero.name.c_str(),
                         hero.stickyDockHex.x, hero.stickyDockHex.y);
                hero.hasStickyDockGoal = false;
            }
        }
    }
    // ================================================================
*/

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: inside the overseas-target / boat-evaluation block,
// AFTER candidateDocks have been filtered by land-connectivity and
// sorted nearest-first (existing 2026-07-25 logic), but BEFORE the
// hero actually starts walking toward the chosen dock.

/*
    // ================================================================
    // PATCH 07: Latch the dock goal once we commit to an overseas invasion
    // ================================================================
    if (wantBoatForBestTarget && !hero.hasStickyDockGoal) {
        // candidateDocks is already filtered (same landmass) and sorted.
        for (const HexCoord& dock : candidateDocks) {
            // Double-check connectivity (O(1) fast-reject)
            if (m_pathfinder.sameLandmass(hero.hex, dock)) {
                hero.stickyDockHex      = dock;
                hero.stickyDockTargetId = bestOverseasTarget.id;
                hero.hasStickyDockGoal  = true;

                gLog.add("[NAVAL] %s latched sticky dock (%d,%d) for target %s (score=%.1f)",
                         hero.name.c_str(),
                         dock.x, dock.y,
                         bestOverseasTarget.name.c_str(),
                         bestOverseasTarget.score);
                break; // latch first reachable dock only
            }
        }
    }
    // ================================================================
*/

// --- OPTIONAL: also clear sticky goal on successful boat purchase ---
// Location: inside aiTryBoat() on the success path, or wherever the hero
// transitions from land to boat / sea movement.
/*
    hero.hasStickyDockGoal = false; // already on boat; no longer need dock latch
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6:3:0 --seed=999
// Expected: [NAVAL] "latched sticky dock" followed by distance-decreasing
//           "walking to sticky dock" logs, then "boarded at latched dock".
//           Previously, logs stopped at week 7 with zero boats.
// =============================================================================
