#pragma once
#include "CombatUnit.h"
#include "CombatGrid.h"

struct DamageResult
{
    int   damage     = 0;
    int   killed     = 0;
    bool  retaliated      = false;
    bool  moraleTrigger   = false;  // attacker got bonus action
    bool  luckTrigger     = false;  // lucky hit — double damage
    int   vampireHeal     = 0;      // HP healed by vampiric drain
    bool  desperationSurge  = false; // Holy attacker had full meter — got bonus ATK
    bool  adaptationGained  = false; // OrganicMech defender gained an adaptation
    int   adaptationStat    = 0;     // +1: ATK gained; -1: DEF gained (for log msg)
};

class DamageCalc
{
public:
    // Standard melee/ranged attack. isRanged=true suppresses retaliation (HoMM rule).
    static DamageResult attack(CombatUnit& attacker, CombatUnit& defender,
                                const CombatGrid& grid, bool isRetaliation = false,
                                bool isRanged = false);

    // Tile modifier for a unit standing on a tile
    // Returns damage multiplier (1.0 = normal)
    static float tileAttackMod(const CombatTile* tile);
    static float tileDefenseMod(const CombatTile* tile);

    // Weakness matrix — does attacker tag counter defender tag?
    // Returns bonus damage multiplier (1.0 = no bonus)
    static float weaknessBonus(UnitTag attackerTags, UnitTag defenderTags,
                                bool attackerIsHolyFaction, bool defenderIsUndeadFaction);

    // Damage preview: returns (minDmg, maxDmg) estimate without RNG or side effects
    struct DamageEstimate { int minDmg = 0; int maxDmg = 0; int minKills = 0; int maxKills = 0; };
    static DamageEstimate estimate(const CombatUnit& attacker, const CombatUnit& defender,
                                    const CombatGrid& grid);

    // Morale bonus action threshold
    static constexpr int MORALE_THRESHOLD = 160;

    static void seedRng(uint32_t seed);

private:
    static int rollDamage(int dmin, int dmax, int count);
};
