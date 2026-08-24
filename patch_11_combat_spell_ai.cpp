// patch_11_combat_spell_ai.cpp
// Unnamed Strategy — AI Improvement: Intelligent Spell Selection in Combat
// =============================================================================
// Addresses the basic "one spell per round" AI hero casting mentioned in
// HANDOFF.md. Currently the AI likely casts the first available spell or a
// hardcoded favorite. This patch adds situational scoring so the AI hero
// casts the RIGHT spell at the RIGHT time.
//
// Problem: AI wastes mana on overkill damage spells, buffs units that never
// attack, heals full-HP stacks, or ignores dangerous enemy abilities.
// Result: human player out-values the AI in every combat despite equal stats.
//
// Fix: A spell-scoring function that evaluates every known spell against the
// current combat board state and picks the highest-score legal cast. Covers
// four spell archetypes with faction-aware weighting:
//   1. DIRECT DAMAGE  — target the highest-value enemy stack, prefer clumps
//   2. BUFF           — cast on the AI's largest/most-forward stack before it acts
//   3. DEBUFF         — target the enemy stack that poses the greatest threat
//   4. HEAL / RESURRECT — only when a friendly stack has lost >25% HP
//
// Also adds mana-budget logic: never spend >60% of remaining mana on a single
// spell unless it's a kill shot or the AI is dominant. Saves mana for late-game
// T3/T4 spells when the board is thinner and each cast is more decisive.
// =============================================================================

// --- INSERT INTO: src/combat/CombatEngine.cpp (or CombatAI.cpp if split) ---
// Location: inside the AI hero spell-cast block, before the actual cast.

/*
    // ================================================================
    // PATCH 11: Intelligent spell selection
    // ================================================================
    struct SpellScore {
        int spellId;
        int targetUnitId; // 0 = no target (mass spell), -1 = self-cast (buff)
        float score;
        std::string reason;
    };
    std::vector<SpellScore> spellScores;

    const int myMana = hero.mana;
    const bool iAmDominant = (myTotalStr > enemyTotalStr * 1.5f);
    const bool iAmLosing   = (myTotalStr * 1.5f < enemyTotalStr);

    for (const Spell& spell : hero.knownSpells) {
        if (spell.manaCost > myMana) continue;
        if (spell.castPerBattle > 0 && spell.timesCast >= spell.castPerBattle) continue;

        float baseScore = 0.0f;
        int bestTarget = 0;
        std::string reason;

        switch (spell.archetype) {

        case SpellArchetype::DirectDamage: {
            // Score each enemy stack: (damage dealt / enemy HP) * enemy threat
            float best = 0.0f;
            for (const CombatUnit& enemy : enemyUnits) {
                if (!enemy.isAlive()) continue;
                int rawDmg = spell.calcDamage(hero, enemy);
                int actualDmg = std::min(rawDmg, enemy.currentHp);
                float killRatio = (float)actualDmg / enemy.maxHp;
                float threat = enemy.attack * enemy.count * 0.1f;
                float clumpBonus = 1.0f;
                // Bonus if the spell is AOE and enemies are adjacent
                if (spell.isAreaEffect) {
                    int adjacentEnemies = countAdjacentEnemies(enemy.pos);
                    clumpBonus = 1.0f + adjacentEnemies * 0.4f;
                }
                float s = killRatio * threat * clumpBonus;
                // Overkill penalty: don't waste mana on a spell that does 5x the HP left
                if (rawDmg > actualDmg * 3) s *= 0.3f;
                if (s > best) { best = s; bestTarget = enemy.id; }
            }
            baseScore = best;
            reason = "direct damage";
            break;
        }

        case SpellArchetype::Buff: {
            // Cast on the friendly stack that acts SOONEST and is LARGEST
            float best = 0.0f;
            for (const CombatUnit& ally : myUnits) {
                if (!ally.isAlive()) continue;
                // Haste is useless on a unit that already acts next turn
                if (spell.effect == SpellEffect::Haste && ally.nextTurnBar >= 90) continue;
                // Bless is best on a large stack with wide damage spread
                float stackValue = ally.count * ally.attack * 0.1f;
                float urgency = 1.0f + (100 - ally.nextTurnBar) / 100.0f;
                float s = stackValue * urgency * spell.power;
                if (s > best) { best = s; bestTarget = ally.id; }
            }
            baseScore = best;
            reason = "buff ally";
            break;
        }

        case SpellArchetype::Debuff: {
            // Target the enemy stack with the highest (attack * speed * count)
            float best = 0.0f;
            for (const CombatUnit& enemy : enemyUnits) {
                if (!enemy.isAlive()) continue;
                // Slow is wasted on a unit that already acts last
                if (spell.effect == SpellEffect::Slow && enemy.speed <= 3) continue;
                float threat = enemy.attack * enemy.speed * enemy.count * 0.05f;
                // Curse / Weakness best on high-attack T5/T6 units
                if (spell.effect == SpellEffect::Curse && enemy.attack < 15) threat *= 0.5f;
                float s = threat * spell.power;
                if (s > best) { best = s; bestTarget = enemy.id; }
            }
            baseScore = best;
            reason = "debuff enemy";
            break;
        }

        case SpellArchetype::Heal:
        case SpellArchetype::Resurrect: {
            // Only cast if a friendly stack has lost meaningful HP
            float best = 0.0f;
            for (const CombatUnit& ally : myUnits) {
                if (!ally.isAlive()) continue;
                int hpLost = ally.maxHp - ally.currentHp;
                if (hpLost <= 0) continue;
                // Resurrection is much more valuable (brings back dead units)
                float valueMult = (spell.archetype == SpellArchetype::Resurrect) ? 2.5f : 1.0f;
                float hpRatio = (float)hpLost / ally.maxHp;
                if (hpRatio < 0.25f) continue; // don't heal scratches
                float stackValue = ally.count * ally.attack * 0.1f;
                float s = hpRatio * stackValue * valueMult * spell.power;
                // Urgent heal if the stack will die to the next enemy hit
                if (hpLost > enemyMaxSingleHit(ally)) s *= 1.5f;
                if (s > best) { best = s; bestTarget = ally.id; }
            }
            baseScore = best;
            reason = "heal/resurrect";
            break;
        }

        default:
            baseScore = spell.power * 0.5f; // generic fallback
            reason = "generic";
            break;
        }

        // --- Mana budget gate ---
        float manaRatio = (float)spell.manaCost / std::max(1, myMana);
        if (manaRatio > 0.6f) {
            // Expensive spell — only cast if it's a kill shot or we're dominant
            bool isKillShot = false;
            if (spell.archetype == SpellArchetype::DirectDamage && bestTarget > 0) {
                for (const CombatUnit& enemy : enemyUnits) {
                    if (enemy.id == bestTarget) {
                        int rawDmg = spell.calcDamage(hero, enemy);
                        if (rawDmg >= enemy.currentHp) isKillShot = true;
                        break;
                    }
                }
            }
            if (!isKillShot && !iAmDominant) baseScore *= 0.2f; // heavily penalize
        }

        // --- Desperation override ---
        // When losing badly, favor damage and debuff over conservation
        if (iAmLosing && (spell.archetype == SpellArchetype::DirectDamage ||
                          spell.archetype == SpellArchetype::Debuff)) {
            baseScore *= 1.4f;
        }

        // --- Faction personality weight ---
        // Mage-personality AI values spells more; Warrior values them less
        AiPersonality persona = m_aiPersonality[std::min<uint32_t>(hero.ownerId, 9)];
        if (persona == AiPersonality::Mage) baseScore *= 1.25f;
        else if (persona == AiPersonality::Warrior) baseScore *= 0.85f;

        if (baseScore > 0.0f) {
            spellScores.push_back({spell.id, bestTarget, baseScore, reason});
        }
    }

    // --- Pick and cast the highest-scoring spell ---
    if (!spellScores.empty()) {
        auto best = *std::max_element(spellScores.begin(), spellScores.end(),
            [](const SpellScore& a, const SpellScore& b) { return a.score < b.score; });

        gLog("[SPELL-AI] %s casts %s (score=%.1f, reason=%s, target=%d, mana left=%d)\n",
             hero.name.c_str(), getSpellName(best.spellId).c_str(),
             best.score, best.reason.c_str(), best.targetUnitId,
             myMana - getSpell(best.spellId).manaCost);

        castSpell(hero, best.spellId, best.targetUnitId);
    } else {
        gLog("[SPELL-AI] %s saves mana (no worthwhile spell, mana=%d)\n",
             hero.name.c_str(), myMana);
    }
    // ================================================================
*/

// --- HELPER FUNCTIONS to add (if not already present) ---
/*
    // Returns the maximum single-hit damage any enemy can deal to 'target' this turn
    static int enemyMaxSingleHit(const CombatUnit& target, const std::vector<CombatUnit>& enemyUnits) {
        int maxDmg = 0;
        for (const auto& e : enemyUnits) {
            if (!e.isAlive()) continue;
            if (e.hasActedThisRound) continue; // already acted, can't hit again
            int dmg = DamageCalc::estimate(e, target); // or your equivalent
            if (dmg > maxDmg) maxDmg = dmg;
        }
        return maxDmg;
    }

    // Counts enemy units adjacent to 'pos' for AOE clump bonus
    static int countAdjacentEnemies(HexCoord pos, const std::vector<CombatUnit>& enemyUnits) {
        int count = 0;
        for (auto& nb : HexGrid::neighbors(pos)) {
            for (const auto& e : enemyUnits) {
                if (e.isAlive() && e.pos == nb) { ++count; break; }
            }
        }
        return count;
    }
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6 --seed=123
// Check logs for:
//   - "[SPELL-AI] <hero> casts <spell> (score=..., reason=..., target=...)"
//   - Mage-personality heroes should cast more often and higher-scoring spells
//   - Warrior-personality heroes should cast less, preferring direct damage
//   - No "heal scratches" casts (heal only when hpRatio >= 0.25)
//   - No overkill damage spells (score penalized when rawDmg > 3x actualDmg)
// Before: AI casts first available spell every round, often wasted.
// After: AI evaluates board state and picks the highest-impact spell.
// =============================================================================
