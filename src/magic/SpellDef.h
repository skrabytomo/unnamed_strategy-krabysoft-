#pragma once
#include <cstdint>

enum class SpellSchool : uint8_t { Light, Blood, Death, Nature, Forge, Flesh, Neutral };

// Who the spell targets
enum class SpellTarget : uint8_t
{
    SingleEnemy,   // one enemy unit
    SingleAlly,    // one friendly unit
    AllEnemies,    // all enemy units
    AllAllies,     // all friendly units
    Self,          // the caster's hero
    WorldMap,      // cast on the adventure map (not usable in combat)
};

// What the spell does
enum class SpellEffect : uint8_t
{
    Damage,        // deal (power + school_power) damage; school_power = hero's matching stat
    Heal,          // restore HP proportional to power
    AttackBuff,    // +power roundAttackBonus for 1 round
    DefenseBuff,   // +power roundDefenseBonus for 1 round
    AttackDebuff,  // -power roundAttackBonus for 1 round
    DefenseDebuff, // -power roundDefenseBonus for 1 round
    MoraleBoost,   // +power to morale of target
    MoraleDrain,   // -power from morale of target
    Poison,        // apply poison DoT: power damage/round for 3 rounds
    Burn,          // apply burn DoT:   power damage/round for 2 rounds
    WorldReveal,   // reveal fog in radius on the world map
    WorldTeleport, // teleport hero to chosen friendly town
    WorldFoundCity,// convert a cleared Utopia into a player town
};

struct SpellDef
{
    int         id       = 0;
    const char* name     = "";
    const char* desc     = "";
    SpellSchool school;
    SpellTarget target;
    SpellEffect effect;
    int         manaCost = 5;
    int         power    = 10;   // base potency; scaled by hero school stat
};

// ── Spell ID constants ─────────────────────────────────────────────────────────
namespace SPL
{
    // Light
    static constexpr int BLESS         = 1;
    static constexpr int SMITE         = 2;
    static constexpr int DIVINE_SHIELD = 3;
    static constexpr int RADIANCE      = 4;

    // Blood
    static constexpr int BLOOD_FRENZY  = 10;
    static constexpr int DRAIN_LIFE    = 11;
    static constexpr int ENERVATE      = 12;
    static constexpr int HEMORRHAGE    = 13;

    // Death
    static constexpr int CURSE         = 20;
    static constexpr int WITHER        = 21;
    static constexpr int DEATH_COIL    = 22;
    static constexpr int PLAGUE        = 23;

    // Nature
    static constexpr int BARKSKIN      = 30;
    static constexpr int ENTANGLE      = 31;
    static constexpr int CALL_LIGHTNING= 32;
    static constexpr int REGROWTH      = 33;

    // Forge
    static constexpr int REINFORCE     = 40;
    static constexpr int OVERCLOCK     = 41;
    static constexpr int SHRAPNEL      = 42;
    static constexpr int HARDENED_SHELL= 43;

    // Flesh
    static constexpr int FESTER        = 50;
    static constexpr int MEND_FLESH    = 51;
    static constexpr int TOXIN         = 52;
    static constexpr int GROWTH        = 53;
    static constexpr int ACID_SPRAY    = 54;  // Burn DoT, single enemy

    // Death (new)
    static constexpr int VENOMOUS_CLOUD = 24;  // Poison DoT, all enemies

    // Nature (new)
    static constexpr int SERPENT_VENOM  = 34;  // Poison DoT, single enemy

    // Forge (new)
    static constexpr int NAPALM         = 44;  // Burn DoT, all enemies

    // Neutral (world-map spells — no school bonus, any hero can learn)
    static constexpr int VISIONS     = 60;  // reveal fog + contents in radius
    static constexpr int TOWN_PORTAL = 61;  // teleport to chosen friendly town
    static constexpr int FOUND_CITY  = 62;  // convert cleared Utopia → town (level 10)
}
