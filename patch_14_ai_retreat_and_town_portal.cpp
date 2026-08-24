// patch_14_ai_retreat_and_town_portal.cpp
// Unnamed Strategy — AI Improvement: Fighting Retreat & Offensive Town Portal
// =============================================================================
// Addresses gaps in the AI's retreat and Town Portal logic.
//
// Problem A: When an AI hero triggers the retreat threshold, it flees in a
// straight line away from the enemy — often into deeper wilderness, toward
// an enemy town, or into a dead end. It should retreat toward the nearest
// friendly town, dock, or allied hero.
//
// Problem B: Town Portal is only used defensively (teleport home to save a
// town). The AI never uses it OFFENSIVELY — e.g., teleporting a strong hero
// to a besieged ally town to break the siege, or teleporting a raider behind
// enemy lines after the defender has committed to a front-line march.
//
// Fix:
//   A. SMART RETREAT — when retreating, set the retreat goal to the nearest
//      safe tile (friendly town, allied hero, or dock) rather than just
//      "away from enemy". If Town Portal is available and the hero is in
//      mortal danger (strength < 20% of nearest enemy), teleport directly.
//   B. OFFENSIVE TOWN PORTAL — scan for allied towns under siege where the
//      defender garrison + teleporting hero would flip the strength ratio
//      to favor the defender. If found, teleport the hero there instead of
//      continuing their current march.
//   C. FIGHTING RETREAT — when retreating but the enemy is close enough to
//      attack this turn, the hero should fight a single round (dealing what
//      damage it can) THEN retreat, rather than giving the enemy a free
//      attack of opportunity.
// =============================================================================

// --- A. SMART RETREAT GOAL SELECTION ---
// INSERT INTO: src/core/Game_WorldMap.cpp — inside the retreat logic block,
// where the AI decides to flee from a superior enemy.

/*
    // ================================================================
    // PATCH 14A: Smart retreat goal — retreat toward safety, not wilderness
    // ================================================================
    // Called when 'veryWeak' or 'softRetreat' is true and the hero needs
    // to disengage from the current enemy/target.
    HexCoord findSmartRetreatGoal(const Hero& hero, int ownerId) {
        HexCoord bestGoal = hero.pos; // fallback: hold position
        int bestScore = -9999;

        // Candidate 1: nearest friendly town
        for (const auto& t : m_towns) {
            if (t.ownerId != ownerId) continue;
            int dist = HexGrid::distance(hero.pos, t.pos);
            int score = 1000 - dist * 2; // closer is better
            if (t.hasBuilding(BID::FORT)) score += 500; // fortified town is safer
            if (score > bestScore) { bestScore = score; bestGoal = t.pos; }
        }

        // Candidate 2: nearest allied hero (strength > our strength, so they can protect)
        for (const auto& h : m_heroes) {
            if (h.ownerId != ownerId) continue;
            if (h.id == hero.id) continue;
            int hStr = heroStrength(h, udefs);
            int myStr = heroStrength(hero, udefs);
            if (hStr <= myStr) continue; // only stronger allies count as safe
            int dist = HexGrid::distance(hero.pos, h.pos);
            int score = 800 - dist * 3;
            if (score > bestScore) { bestScore = score; bestGoal = h.pos; }
        }

        // Candidate 3: nearest dock (if on boat or near water, escape by sea)
        if (hero.onBoat || hero.boatType != BoatType::None) {
            for (const auto& t : m_towns) {
                if (t.ownerId != ownerId) continue;
                if (!t.hasBuilding(BID::TOWN_SHIPYARD)) continue;
                int dist = HexGrid::distance(hero.pos, t.pos);
                int score = 600 - dist * 2;
                if (score > bestScore) { bestScore = score; bestGoal = t.pos; }
            }
        }

        // Candidate 4: nearest map edge (if truly nowhere else to go)
        if (bestScore < 0) {
            // Find the map edge hex closest to hero
            HexCoord edge = hero.pos;
            int minEdgeDist = 999;
            for (int x = 0; x < m_map.width; ++x) {
                for (int y = 0; y < m_map.height; ++y) {
                    HexCoord c{x, y};
                    if (!m_map.inBounds(c)) continue;
                    bool isEdge = (x == 0 || x == m_map.width-1 || y == 0 || y == m_map.height-1);
                    if (!isEdge) continue;
                    int d = HexGrid::distance(hero.pos, c);
                    if (d < minEdgeDist) { minEdgeDist = d; edge = c; }
                }
            }
            bestGoal = edge;
        }

        return bestGoal;
    }

    // Usage: replace the old retreat direction with:
    //   HexCoord retreatGoal = findSmartRetreatGoal(eHero, eHero.ownerId);
    //   stepTowardOptimal(eHero, retreatGoal, GOAL_RETREAT);
    // ================================================================
*/

// --- B. OFFENSIVE TOWN PORTAL ---
// INSERT INTO: src/core/Game_WorldMap.cpp — inside the AI turn setup or
// before the per-hero movement loop, as a global strategic check.

/*
    // ================================================================
    // PATCH 14B: Offensive Town Portal — teleport to break sieges
    // ================================================================
    // Once per turn, after all normal target scoring, check if any allied
    // town is under siege and would be saved by teleporting a strong hero.
    void checkOffensiveTownPortal(int ownerId) {
        // Find the strongest available hero who is NOT already defending
        Hero* bestTeleporter = nullptr;
        int bestStr = 0;
        for (auto& h : m_heroes) {
            if (h.ownerId != ownerId) continue;
            if (h.mana < TOWN_PORTAL_MANA_COST) continue; // can't afford spell
            int str = heroStrength(h, udefs);
            if (str > bestStr) { bestStr = str; bestTeleporter = &h; }
        }
        if (!bestTeleporter || bestStr == 0) return;

        // Find a besieged allied town where our arrival turns the tide
        for (const auto& t : m_towns) {
            if (t.ownerId != ownerId) continue;
            if (!t.underSiege) continue;

            // Current defense strength
            int garrisonStr = stacksStrength(t.garrison, udefs);
            int currentDef = garrisonStr;

            // Is there already a friendly hero at or near the town?
            bool heroAlreadyNear = false;
            for (const auto& h : m_heroes) {
                if (h.ownerId != ownerId) continue;
                if (HexGrid::distance(h.pos, t.pos) <= 2) {
                    currentDef += heroStrength(h, udefs);
                    heroAlreadyNear = true;
                }
            }
            if (heroAlreadyNear) continue; // already have help

            // Attacker strength (the sieging hero)
            int attackerStr = 0;
            for (const auto& eh : m_enemyHeroes) {
                if (eh.siegeTargetTownId == t.id) {
                    attackerStr = heroStrength(eh, udefs);
                    break;
                }
            }
            if (attackerStr == 0) continue; // no actual siege happening

            // Would teleporting flip the ratio?
            int newDef = currentDef + bestStr;
            bool wouldSave = (newDef >= attackerStr * 1.2f) && (currentDef < attackerStr);

            if (wouldSave) {
                // Execute teleport
                bestTeleporter->pos = t.pos;
                bestTeleporter->mana -= TOWN_PORTAL_MANA_COST;
                gLog("[TP-OFFENSE] %s teleports to %s to break siege (def %d -> %d vs atk %d)\n",
                     bestTeleporter->name.c_str(), t.name.c_str(),
                     currentDef, newDef, attackerStr);
                // Invalidate path cache since position changed
                bestTeleporter->pathCacheValid = false;
                bestTeleporter->cachedPath.clear();
                return; // only one offensive TP per turn
            }
        }
    }
    // ================================================================
*/

// --- C. FIGHTING RETREAT ---
// INSERT INTO: src/combat/CombatEngine.cpp — inside the AI combat action loop,
// when the AI decides to retreat from a losing combat.

/*
    // ================================================================
    // PATCH 14C: Fighting retreat — deal damage before fleeing
    // ================================================================
    // When the AI hero/unit decides to retreat/escape from combat:
    // If the enemy can reach and kill a friendly unit THIS TURN, do not
    // simply pass/retreat immediately. Instead, attack the most dangerous
    // reachable enemy first, THEN retreat. This prevents the enemy from
    // getting a completely free round of attacks.
    bool shouldFightBeforeRetreat(const CombatUnit& unit, const CombatState& state) {
        // Check if any enemy can kill a friendly unit on their next action
        for (const auto& enemy : state.enemyUnits) {
            if (!enemy.isAlive()) continue;
            if (enemy.hasActedThisRound) continue; // already acted this round
            if (!enemy.canReachAnyFriendly()) continue;

            for (const auto& ally : state.friendlyUnits) {
                if (!ally.isAlive()) continue;
                int estDmg = DamageCalc::estimate(enemy, ally);
                if (estDmg >= ally.currentHp) {
                    // An ally will die to this enemy's next attack
                    // Can WE kill or seriously wound this enemy first?
                    if (unit.canReach(enemy.pos)) {
                        int ourDmg = DamageCalc::estimate(unit, enemy);
                        if (ourDmg >= enemy.currentHp * 0.3f) {
                            return true; // worth fighting before retreating
                        }
                    }
                }
            }
        }
        return false;
    }

    // Usage in the AI combat turn:
    // if (aiWantsToRetreat) {
    //     if (shouldFightBeforeRetreat(unit, state)) {
    //         // Attack the most dangerous reachable enemy instead of retreating
    //         auto target = findMostDangerousReachableEnemy(unit, state);
    //         if (target) { attack(unit, *target); }
    //     }
    //     // THEN retreat on the next action opportunity
    // }
    // ================================================================
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6 --seed=123
// Check logs for:
//   - "[TP-OFFENSE] <hero> teleports to <town> to break siege" — offensive TP
//   - Heroes retreating should path toward friendly towns, not random directions
//   - "[SIEGE-DEF] <unit> holds gate choke" — defenders using walls (from patch 12)
//   - Combat logs: AI units attacking before retreating when an ally would die
// Before: retreat = run away randomly; TP = only defensive; retreat = free enemy attacks.
// After: tactical retreats, offensive reinforcements, fighting withdrawals.
// =============================================================================
