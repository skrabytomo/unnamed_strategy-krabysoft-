#include "CombatUnit.h"
#include <algorithm>

int CombatUnit::applyDamage(int dmg)
{
    if (dmg <= 0 || !alive) return 0;

    int killed = 0;
    int remaining = dmg;

    // Damage top unit first
    hp -= remaining;

    while (hp <= 0 && count > 0) {
        count--;
        killed++;
        if (count > 0)
            hp += maxHp; // next unit in stack
        else
            hp = 0;
    }

    if (count <= 0) {
        if (hasSecondLife && !secondLifeUsed) {
            // Eternal Empire second-life: revive at 1 unit
            count = 1;
            // EternalLegion specialty: revive at full HP instead of half
            hp = secondLifeFullHeal ? maxHp : std::max(1, maxHp / 2);
            secondLifeUsed = true;
            // ETERNAL_CMD: reraised units gain % stat bonus
            if (secondLifeStrBonus > 0) {
                attack  += attack  * secondLifeStrBonus / 100;
                defense += defense * secondLifeStrBonus / 100;
            }
        } else {
            count = 0;
            hp    = 0;
            alive = false;
        }
    }

    return killed;
}

void CombatUnit::newRound()
{
    hasMoved          = false;
    hasActed          = false;
    waitUsed          = false;
    canRetaliate      = true;
    moraleSurgedThisRound = false;

    // Tick buff durations; clear bonus when counter reaches zero
    if (buffAttackRounds > 0) {
        if (--buffAttackRounds == 0) roundAttackBonus  = 0;
    }
    if (buffDefenseRounds > 0) {
        if (--buffDefenseRounds == 0) roundDefenseBonus = 0;
    }

    // Tick defend buff down; remove it when it expires
    if (defendRoundsLeft > 0) {
        --defendRoundsLeft;
        if (defendRoundsLeft == 0) {
            defense -= defendDefenseBonus;
            defendDefenseBonus = 0;
        }
    }
}
