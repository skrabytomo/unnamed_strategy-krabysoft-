#include "DamageCalc.h"
#include <cmath>
#include <algorithm>
#include <random>

static thread_local std::mt19937 s_rng{std::random_device{}()};

void DamageCalc::seedRng(uint32_t seed)
{
    s_rng.seed(seed);
}

// ── Damage roll ────────────────────────────────────────────────────────────────
int DamageCalc::rollDamage(int dmin, int dmax, int count)
{
    if (dmin >= dmax) return dmin * count;
    std::uniform_int_distribution<int> dist(dmin, dmax);
    int total = 0;
    for (int i = 0; i < count; ++i) total += dist(s_rng);
    return total;
}

// ── Tile modifiers ─────────────────────────────────────────────────────────────
float DamageCalc::tileAttackMod(const CombatTile* tile)
{
    if (!tile) return 1.0f;
    switch (tile->type) {
        case CombatTileType::Attack:  return 1.25f;
        case CombatTileType::Defense: return 0.85f;
        default: return 1.0f;
    }
}

float DamageCalc::tileDefenseMod(const CombatTile* tile)
{
    if (!tile) return 1.0f;
    switch (tile->type) {
        case CombatTileType::Defense: return 0.80f;  // damage reduced on defender
        case CombatTileType::Attack:  return 1.10f;  // defender takes more
        default: return 1.0f;
    }
}

// ── Weakness bonus ─────────────────────────────────────────────────────────────
float DamageCalc::weaknessBonus(UnitTag attackerTags, UnitTag defenderTags,
                                 bool attackerIsHoly, bool defenderIsUndead)
{
    float bonus = 1.0f;

    // Holy units deal bonus damage to Undead, BloodBound, Mechanical, Beast, Humanoid, OrganicMech
    if (hasTag(attackerTags, UnitTag::Holy)) {
        if (hasTag(defenderTags, UnitTag::Undead))      bonus *= 1.10f;
        if (hasTag(defenderTags, UnitTag::BloodBound))  bonus *= 1.05f;
        if (hasTag(defenderTags, UnitTag::Mechanical))  bonus *= 1.07f;
        if (hasTag(defenderTags, UnitTag::Beast))       bonus *= 1.07f;
        if (hasTag(defenderTags, UnitTag::Humanoid))    bonus *= 1.02f;
        if (hasTag(defenderTags, UnitTag::OrganicMech)) bonus *= 1.05f;
    }

    // Humanoid units deal bonus damage to Holy and BloodBound
    if (hasTag(attackerTags, UnitTag::Humanoid)) {
        if (hasTag(defenderTags, UnitTag::Holy))       bonus *= 1.05f;
        if (hasTag(defenderTags, UnitTag::BloodBound)) bonus *= 1.04f;
    }

    // BloodBound deal bonus damage to Holy (primal blood magic corrupts divine light)
    if (hasTag(attackerTags, UnitTag::BloodBound)) {
        if (hasTag(defenderTags, UnitTag::Holy)) bonus *= 1.04f;
    }

    // Void units deal bonus damage to Holy, Undead, Mechanical, Humanoid, OrganicMech, and Beast
    if (hasTag(attackerTags, UnitTag::Void)) {
        if (hasTag(defenderTags, UnitTag::Holy))
            bonus *= 1.06f;
        else if (hasTag(defenderTags, UnitTag::Undead))
            bonus *= 1.05f;
        else if (hasTag(defenderTags, UnitTag::Mechanical))
            bonus *= 1.04f;
        else if (hasTag(defenderTags, UnitTag::Humanoid))
            bonus *= 1.05f;
        else if (hasTag(defenderTags, UnitTag::OrganicMech))
            bonus *= 1.10f;
        else if (hasTag(defenderTags, UnitTag::Beast))
            bonus *= 1.05f;
    }

    // Organic-Mech (Amalgamate) deals bonus damage to Mechanical, Humanoid, Beast, and Void
    if (hasTag(attackerTags, UnitTag::OrganicMech)) {
        if (hasTag(defenderTags, UnitTag::Mechanical))    bonus *= 1.10f;
        else if (hasTag(defenderTags, UnitTag::Humanoid)) bonus *= 1.02f;
        else if (hasTag(defenderTags, UnitTag::Beast))    bonus *= 1.06f;
        else if (hasTag(defenderTags, UnitTag::Void))     bonus *= 1.02f;
        else if (hasTag(defenderTags, UnitTag::Undead))   bonus *= 1.06f;
    }

    // Undead deal bonus damage to Beast, Humanoid, and OrganicMech
    if (hasTag(attackerTags, UnitTag::Undead)) {
        if (hasTag(defenderTags, UnitTag::Beast))       bonus *= 1.04f;
        if (hasTag(defenderTags, UnitTag::OrganicMech)) bonus *= 1.02f;
        if (hasTag(defenderTags, UnitTag::Humanoid))    bonus *= 1.05f;
    }

    // BloodBound deal bonus damage to Beast, Undead, OrganicMech, and Void
    // Organized tactics and unit cohesion partially blunt berserker aggression vs Humanoids
    if (hasTag(attackerTags, UnitTag::BloodBound)) {
        if (hasTag(defenderTags, UnitTag::Beast))       bonus *= 1.06f;
        if (hasTag(defenderTags, UnitTag::Undead))      bonus *= 1.10f;
        if (hasTag(defenderTags, UnitTag::OrganicMech)) bonus *= 1.06f;
        if (hasTag(defenderTags, UnitTag::Void))        bonus *= 1.05f;
        if (hasTag(defenderTags, UnitTag::Humanoid)
            && !hasTag(defenderTags, UnitTag::BloodBound)) bonus *= 0.95f;
    }

    // Humanoid numbers and tactics overcome Undead resilience
    if (hasTag(attackerTags, UnitTag::Humanoid)) {
        if (hasTag(defenderTags, UnitTag::Undead)) bonus *= 1.06f;
    }

    // Beast deals bonus damage to Void, Undead, and Mechanical (primal instinct disrupts clockwork)
    if (hasTag(attackerTags, UnitTag::Beast)) {
        if (hasTag(defenderTags, UnitTag::Void))       bonus *= 1.05f;
        if (hasTag(defenderTags, UnitTag::Undead))     bonus *= 1.07f;
        if (hasTag(defenderTags, UnitTag::Mechanical)) bonus *= 1.10f;
    }

    // Void units deal bonus damage to BloodBound (entropic void unravels blood-bonds)
    if (hasTag(attackerTags, UnitTag::Void)) {
        if (hasTag(defenderTags, UnitTag::BloodBound)) bonus *= 1.03f;
    }

    // Void energy seeps through ordered Humanoid defenses (non-Holy, non-BloodBound only)
    if (hasTag(attackerTags, UnitTag::Void)
        && hasTag(defenderTags, UnitTag::Humanoid)
        && !hasTag(defenderTags, UnitTag::Holy)
        && !hasTag(defenderTags, UnitTag::BloodBound)) {
        bonus *= 1.10f;
    }

    // Holy light sears Void energy — counters VO's widespread advantage
    if (hasTag(attackerTags, UnitTag::Holy)) {
        if (hasTag(defenderTags, UnitTag::Void)) bonus *= 1.04f;
    }

    // Humanoid ingenuity overcomes Mechanical brute-force
    if (hasTag(attackerTags, UnitTag::Humanoid)) {
        if (hasTag(defenderTags, UnitTag::Mechanical)) bonus *= 1.04f;
    }

    // Mechanical and Holy have a mutual rivalry — precision disrupts void, faith resists steel
    // Mechanical targeting systems override organic evasion (Humanoid) and grinding clockwork
    // tears through undead frames
    if (hasTag(attackerTags, UnitTag::Mechanical)) {
        if (hasTag(defenderTags, UnitTag::Holy))     bonus *= 0.97f;
        if (hasTag(defenderTags, UnitTag::Void))     bonus *= 1.04f;
        if (hasTag(defenderTags, UnitTag::Humanoid)) bonus *= 1.07f;
        if (hasTag(defenderTags, UnitTag::Undead))   bonus *= 1.10f;
    }

    return bonus;
}

// ── Damage estimate (no RNG, no side effects) ─────────────────────────────────
DamageCalc::DamageEstimate DamageCalc::estimate(const CombatUnit& atk, const CombatUnit& def,
                                                  const CombatGrid& grid)
{
    DamageEstimate est;
    if (!atk.alive || !def.alive || atk.count <= 0) return est;

    int diff = (atk.attack + atk.roundAttackBonus)
             - (def.defense + def.roundDefenseBonus);
    float modifier = 1.0f;
    if (diff > 0)
        modifier = 1.0f + std::min(diff * 0.05f, 3.0f);
    else if (diff < 0)
        modifier = std::max(1.0f + diff * 0.025f, 0.30f);

    const CombatTile* atkTile = grid.getTile(atk.pos);
    const CombatTile* defTile = grid.getTile(def.pos);
    modifier *= tileAttackMod(atkTile);
    modifier *= tileDefenseMod(defTile);
    modifier *= weaknessBonus(atk.tags, def.tags,
                              hasTag(atk.tags, UnitTag::Holy),
                              hasTag(def.tags, UnitTag::Undead));

    int dmin = static_cast<int>(atk.damageMin * atk.count * modifier);
    int dmax = static_cast<int>(atk.damageMax * atk.count * modifier);
    dmin = std::max(1, dmin);
    dmax = std::max(dmin, dmax);

    est.minDmg = dmin;
    est.maxDmg = dmax;

    // Estimate kills: top unit dies first (costs def.hp damage), then each maxHp
    // of overflow kills one more unit. ceil formula was missing the +1 for the
    // top unit itself, causing a systematic undercount by 1 whenever top unit dies.
    if (def.maxHp > 0) {
        int topHp = def.hp;
        auto killsFor = [&](int dmg) -> int {
            if (dmg < topHp) return 0;
            return 1 + (dmg - topHp) / def.maxHp;
        };
        est.minKills = std::min(killsFor(dmin), def.count);
        est.maxKills = std::min(killsFor(dmax), def.count);
    }

    return est;
}

// ── Main attack formula ────────────────────────────────────────────────────────
// HoMM3-style: damage = (attacker.attack - defender.defense) modifier * roll
// attack > defense: +5% per point (max +300%)
// defense > attack: -2.5% per point (min 30% damage)
DamageResult DamageCalc::attack(CombatUnit& attacker, CombatUnit& defender,
                                  const CombatGrid& grid, bool isRetaliation)
{
    DamageResult result;

    // Desperation surge (Holy Order): full meter → +3 effective ATK for this strike
    int desperationAtkBonus = 0;
    if (!isRetaliation && hasTag(attacker.tags, UnitTag::Holy)
        && attacker.desperationMeter >= 100) {
        attacker.desperationMeter = 0;
        desperationAtkBonus = 3;
        result.desperationSurge = true;
    }

    // Base damage roll
    int baseDmg = rollDamage(attacker.damageMin, attacker.damageMax, attacker.count);

    // Attack vs defense modifier (include per-round bonuses + desperation bonus)
    int diff = (attacker.attack + attacker.roundAttackBonus + desperationAtkBonus)
             - (defender.defense + defender.roundDefenseBonus);
    float modifier = 1.0f;
    if (diff > 0)
        modifier = 1.0f + std::min(diff * 0.05f, 3.0f);
    else if (diff < 0)
        modifier = std::max(1.0f + diff * 0.025f, 0.30f);

    // Tile modifiers
    const CombatTile* atkTile = grid.getTile(attacker.pos);
    const CombatTile* defTile = grid.getTile(defender.pos);
    modifier *= tileAttackMod(atkTile);
    modifier *= tileDefenseMod(defTile);

    // Weakness matrix
    bool attackerHoly   = hasTag(attacker.tags, UnitTag::Holy);
    bool defenderUndead = hasTag(defender.tags, UnitTag::Undead);
    modifier *= weaknessBonus(attacker.tags, defender.tags, attackerHoly, defenderUndead);

    // Retaliation is weaker
    if (isRetaliation) modifier *= 0.50f;

    int finalDmg = static_cast<int>(baseDmg * modifier);
    finalDmg = std::max(1, finalDmg);

    // Luck roll — each luck point gives +5% chance of a double-damage lucky hit
    if (!isRetaliation && attacker.luck > 0) {
        std::uniform_int_distribution<int> d100(1, 100);
        if (d100(s_rng) <= attacker.luck * 5) {
            finalDmg *= 2;
            result.luckTrigger = true;
        }
    }

    result.damage = finalDmg;
    result.killed = defender.applyDamage(finalDmg);

    // Desperation meter charging: Holy defenders build meter when taking damage
    if (finalDmg > 0 && defender.alive && hasTag(defender.tags, UnitTag::Holy)) {
        int stackHp = std::max(1, defender.count * defender.maxHp);
        int gain    = std::max(5, finalDmg * 30 / stackHp);
        gain = std::min(gain, 25);
        defender.desperationMeter = std::min(100, defender.desperationMeter + gain);
    }

    // Amalgamate adaptation: OrganicMech units gain stats after taking enough hits
    // Void energy disrupts the flesh-graft bonding — Void attackers don't trigger adaptation
    if (finalDmg > 0 && defender.alive && hasTag(defender.tags, UnitTag::OrganicMech)
        && !hasTag(attacker.tags, UnitTag::Void)
        && defender.adaptationsGained < 2) {
        defender.hitsTaken++;
        int threshold = defender.rapidEvolution ? 1 : (defender.adaptationFast ? 2 : 3);
        if (defender.hitsTaken >= threshold) {
            defender.hitsTaken = 0;
            int gain = defender.adaptationDouble ? 2 : 1;
            // Alternate ATK and DEF gains
            if (defender.adaptationsGained % 2 == 0) {
                defender.attack += gain;
                result.adaptationGained = true;
                result.adaptationStat   = 1;  // ATK
            } else {
                defender.defense += gain;
                result.adaptationGained = true;
                result.adaptationStat   = -1; // DEF
            }
            defender.adaptationsGained++;
        }
    }

    // Vampiric drain — heal attacker by damage dealt (not during retaliation)
    if (!isRetaliation && attacker.vampiric && finalDmg > 0) {
        int heal = finalDmg / std::max(1, attacker.count);
        heal = std::min(heal, attacker.maxHp - attacker.hp);
        if (heal > 0) {
            attacker.hp += heal;
            result.vampireHeal = heal;
        }
    }

    // Morale update — attacker gains morale on kill (not during retaliation; at most one surge/round)
    if (result.killed > 0 && !isRetaliation && !attacker.moraleImmune
        && !attacker.moraleSurgedThisRound) {
        attacker.morale += std::min(10 * result.killed, 60);
        if (attacker.morale >= MORALE_THRESHOLD) {
            attacker.morale = 0;  // full reset — prevents cascade on bonus action
            result.moraleTrigger = true;
            attacker.moraleSurgedThisRound = true;
        }
    }

    // Defender loses morale from taking losses
    if (result.killed > 0 && !defender.moraleImmune) {
        defender.morale = std::max(0, defender.morale - 8);
    }

    // Retaliation — defender hits back if it hasn't acted yet this round
    if (!isRetaliation && defender.alive && defender.canRetaliate && !defender.hasActed) {
        DamageResult ret = attack(defender, attacker, grid, true);
        defender.canRetaliate = false;
        result.retaliated = true;
        (void)ret; // retaliation damage already applied
    }

    return result;
}
