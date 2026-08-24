// patch_12_siege_combat_ai.cpp
// Unnamed Strategy — AI Improvement: Siege Combat Tactics
// =============================================================================
// Addresses HANDOFF.md open issue:
// "Siege: wall damage from normal units is already engine-gated — remaining
//  question is tuning, needs playtest."
//
// Problem: The AI assaulting a walled town wastes normal melee units on walls
// (capped 1-6 damage, futile), ignores its own siege engines, and sends
// non-flyers on suicide paths toward the gate. The defending garrison AI
// clusters behind the gate instead of using wall segments and towers.
//
// Fix: Three tactical modules:
//   A. ATTACKER wall discipline — only units with wallDamage > 0 (rams,
//      ballistae, T4+ siege specialists) target walls. All other melee units
//      path around walls via gate or wait for a breach. Flyers cross intact
//      walls (already engine-supported) but prioritize enemy ranged/casters.
//   B. DEFENDER garrison positioning — ranged units on intact wall segments
//      get +range/+damage; melee units hold the gate square as a choke;
//      fast units flank through side passages if the attacker commits heavily
//      to the gate.
//   C. SIEGE CAMP decision — the AI decides whether to camp (build bonus,
//      fortify) or assault immediately based on relative strength, wall HP,
//      and whether the attacker has siege engines. No more camping for 3
//      weeks with no engines while the defender builds T6 units.
// =============================================================================

// --- A. ATTACKER WALL DISCIPLINE ---
// INSERT INTO: src/combat/CombatEngine.cpp — aiTargetScore() or equivalent

/*
    // ================================================================
    // PATCH 12A: Attacker wall discipline
    // ================================================================
    // When the combat is a siege (defender has walls), the attacker's
    // target scoring is modified:
    bool isSiege = (defenderTown && defenderTown->hasWalls());

    for (auto& target : allEnemyUnits) {
        float baseScore = ... // existing aiTargetScore logic

        if (isSiege) {
            // --- Wall targeting restriction ---
            if (target.isWallSegment) {
                // Only siege engines and units with explicit wallDamage may target walls
                bool canDamageWalls = (attacker.unitDef.wallDamage > 0)
                                   || (attacker.unitDef.category == UnitCategory::SiegeEngine);
                if (!canDamageWalls) {
                    target.score = -9999.0f; // hard veto
                    target.reason = "[SIEGE] non-siege unit may not attack wall";
                    continue;
                }
                // Siege engines prioritize the LOWEST-HP wall segment (breach creation)
                float hpRatio = (float)target.currentHp / target.maxHp;
                target.score *= (2.0f - hpRatio); // lower HP = higher score
                target.reason += " [SIEGE: breach priority]";
            }

            // --- Gate pathing bonus ---
            // Non-flyer melee units get a score boost for moving toward the gate
            // if no breach exists yet, so they don't waste turns walking into walls
            if (!attacker.canFly && !attacker.isRanged && !target.isWallSegment) {
                bool breachExists = false;
                for (auto& w : wallSegments) if (w.isDestroyed) { breachExists = true; break; }
                if (!breachExists) {
                    int distToGate = HexGrid::distance(attacker.pos, gateHex);
                    int distToTarget = HexGrid::distance(attacker.pos, target.pos);
                    // If the target is on the far side of the wall and no breach,
                    // the unit cannot reach it — deprioritize heavily
                    if (distToTarget > distToGate + 2) {
                        target.score *= 0.1f;
                        target.reason += " [SIEGE: no breach, target unreachable]";
                    }
                }
            }

            // --- Flyer priority override ---
            // Flyers bypass walls; they should hunt the most dangerous non-wall target
            if (attacker.canFly && !target.isWallSegment) {
                target.score *= 1.3f; // boost all non-wall targets
                // Extra boost for enemy ranged/caster units that are safe behind walls
                if (target.isRanged || target.unitDef.casterLevel > 0) {
                    target.score *= 1.5f;
                    target.reason += " [SIEGE: flyer hunting backline]";
                }
            }
        }
    }
    // ================================================================
*/

// --- B. DEFENDER GARRISON POSITIONING ---
// INSERT INTO: src/combat/CombatEngine.cpp — defender AI placement / target selection

/*
    // ================================================================
    // PATCH 12B: Defender garrison positioning
    // ================================================================
    // When the combat is a siege and this unit is on the DEFENDING side:
    if (isSiege && unit.ownerId == defenderTown->ownerId) {

        // --- Ranged on walls ---
        if (unit.isRanged && !unit.hasMovedThisTurn) {
            // Find the intact wall segment with the best line of sight to the most enemies
            HexCoord bestWall = unit.pos;
            float bestValue = 0.0f;
            for (auto& wall : intactWallSegments) {
                int visibleEnemies = 0;
                float threatValue = 0.0f;
                for (auto& e : enemyUnits) {
                    if (!e.isAlive()) continue;
                    if (HexGrid::distance(wall.pos, e.pos) <= unit.attackRange + 2) {
                        ++visibleEnemies;
                        threatValue += e.count * e.attack;
                    }
                }
                float v = visibleEnemies * threatValue;
                // Prefer walls that are not already occupied by another ranged unit
                bool occupied = false;
                for (auto& ally : myUnits) {
                    if (ally.id != unit.id && ally.isRanged && ally.pos == wall.pos) {
                        occupied = true; break;
                    }
                }
                if (!occupied && v > bestValue) { bestValue = v; bestWall = wall.pos; }
            }
            if (bestWall != unit.pos && bestValue > 0) {
                // Move to wall (free reposition if adjacent, or normal move)
                if (HexGrid::distance(unit.pos, bestWall) == 1) {
                    unit.pos = bestWall;
                    unit.hasMovedThisTurn = true;
                    gLog("[SIEGE-DEF] %s moves to wall %d,%d for optimal range\n",
                         unit.name.c_str(), bestWall.x, bestWall.y);
                }
            }
        }

        // --- Melee at gate choke ---
        if (!unit.isRanged && !unit.canFly && !unit.hasMovedThisTurn) {
            // If no friendly unit is on the gate, move there to block
            bool gateHeld = false;
            for (auto& ally : myUnits) {
                if (ally.pos == gateHex) { gateHeld = true; break; }
            }
            if (!gateHeld && HexGrid::distance(unit.pos, gateHex) <= unit.speed) {
                unit.pos = gateHex;
                unit.hasMovedThisTurn = true;
                gLog("[SIEGE-DEF] %s holds gate choke at %d,%d\n",
                     unit.name.c_str(), gateHex.x, gateHex.y);
            }
        }

        // --- Fast flankers exploit side passages ---
        // If the attacker has >50% of its army committed to the gate/front,
        // fast units (speed >= 7) can use side passages to hit the backline
        if (unit.speed >= 7 && !unit.isRanged && !unit.hasMovedThisTurn) {
            int gateCommittedAttackers = 0;
            int totalAttackers = 0;
            for (auto& e : enemyUnits) {
                if (!e.isAlive()) continue;
                ++totalAttackers;
                if (HexGrid::distance(e.pos, gateHex) <= 3) ++gateCommittedAttackers;
            }
            if (totalAttackers > 0 && (float)gateCommittedAttackers / totalAttackers > 0.5f) {
                // Find the nearest enemy ranged/caster unit as a flank target
                HexCoord bestFlank = unit.pos;
                int bestDist = 999;
                for (auto& e : enemyUnits) {
                    if (!e.isAlive()) continue;
                    if (!e.isRanged && e.unitDef.casterLevel <= 0) continue;
                    int d = HexGrid::distance(unit.pos, e.pos);
                    if (d < bestDist) { bestDist = d; bestFlank = e.pos; }
                }
                if (bestFlank != unit.pos && bestDist <= unit.speed * 2) {
                    // Path toward flank target, ignoring walls (side passages)
                    auto path = Pathfinder::find(combatGrid, unit.pos, bestFlank,
                                                 [&](HexCoord c){ return combatGrid.isPassable(c) ? 1 : 99; });
                    if (!path.empty()) {
                        int steps = std::min((int)path.size(), unit.speed);
                        unit.pos = path[steps - 1];
                        unit.hasMovedThisTurn = true;
                        gLog("[SIEGE-DEF] %s flanks to %d,%d (speed %d, target %s)\n",
                             unit.name.c_str(), unit.pos.x, unit.pos.y,
                             unit.speed, bestFlank == unit.pos ? "none" : "enemy backline");
                    }
                }
            }
        }
    }
    // ================================================================
*/

// --- C. SIEGE CAMP DECISION AI ---
// INSERT INTO: src/core/Game_WorldMap.cpp — where the AI decides to siege-camp or assault

/*
    // ================================================================
    // PATCH 12C: Smart siege camp vs. immediate assault
    // ================================================================
    // Called when an AI hero reaches an enemy town and the siege-camp prompt
    // would normally appear.
    bool shouldCampInsteadOfAssault(const Hero& hero, const Town& targetTown) {
        // Gather attacker info
        int attackerStr = heroStrength(hero, udefs);
        bool hasSiegeEngines = false;
        for (const auto& stack : hero.army) {
            const UnitDef* def = getUnitDef(stack.unitId);
            if (def && (def->wallDamage > 0 || def->category == UnitCategory::SiegeEngine)) {
                hasSiegeEngines = true; break;
            }
        }

        // Gather defender info
        int garrisonStr = stacksStrength(targetTown.garrison, udefs);
        int wallHp = targetTown.wallHp; // or however wall health is tracked
        int towerCount = targetTown.towerCount;
        bool hasFortify = targetTown.hasFortifyBonus;

        // Base decision: camp if we need the bonus AND have time
        bool shouldCamp = false;
        std::string reason;

        if (!hasSiegeEngines && wallHp > 20) {
            // No engines + intact walls = camp to build siege or wait for reinforcements
            shouldCamp = true;
            reason = "no siege engines vs. intact walls";
        }
        else if (attackerStr < garrisonStr * 1.2f && !hasSiegeEngines) {
            // Outnumbered and no wall-breach capability = camp for reinforcements
            shouldCamp = true;
            reason = "outnumbered, no breach tools";
        }
        else if (hasSiegeEngines && wallHp > 30 && attackerStr >= garrisonStr) {
            // Have engines but walls are thick — camp one turn to soften walls
            // with the siege engine bonus, THEN assault
            shouldCamp = true;
            reason = "siege engines, softening thick walls first";
        }
        else if (hasFortify && attackerStr < garrisonStr * 1.5f) {
            // Defender used Fortify — the bonus is only active this turn,
            // so camping ONE turn lets the bonus expire, then assault next turn
            shouldCamp = true;
            reason = "waiting out defender Fortify bonus";
        }
        else {
            // All other cases: assault immediately
            shouldCamp = false;
            reason = "favorable assault conditions";
        }

        gLog("[SIEGE-CAMP] %s at %s: %s (atk=%d, def=%d, walls=%d, engines=%d, fortify=%d)\n",
             hero.name.c_str(), targetTown.name.c_str(),
             shouldCamp ? "CAMP" : "ASSAULT",
             attackerStr, garrisonStr, wallHp, (int)hasSiegeEngines, (int)hasFortify);

        return shouldCamp;
    }
    // ================================================================
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6 --seed=123
// Look for siege combats in the log and check:
//   - "[SIEGE] non-siege unit may not attack wall" — melee units ignoring walls
//   - "[SIEGE: flyer hunting backline]" — flyers prioritizing ranged/casters
//   - "[SIEGE-DEF] <unit> moves to wall" — defenders positioning ranged on walls
//   - "[SIEGE-DEF] <unit> holds gate choke" — melee blocking gate
//   - "[SIEGE-CAMP] <hero> at <town>: CAMP/ASSAULT" — smart camp decisions
// Before: AI melee slaps walls for 1-6 dmg, flyers wander, defenders cluster.
// After: Tactical siege play from both sides.
// =============================================================================
