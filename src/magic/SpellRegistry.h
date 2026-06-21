#pragma once
#include "SpellDef.h"

// clang-format off
static const SpellDef ALL_SPELLS[] = {
    // id                  name               desc                                            school                  target                        effect                     cost power
    // ── LIGHT ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {SPL::BLESS,          "Bless",           "Allied unit gains +attack this round",         SpellSchool::Light, SpellTarget::SingleAlly,    SpellEffect::AttackBuff,     4,  3 },
    {SPL::SMITE,          "Smite",           "Holy fire scorches one enemy",                 SpellSchool::Light, SpellTarget::SingleEnemy,   SpellEffect::Damage,         5,  15},
    {SPL::DIVINE_SHIELD,  "Divine Shield",   "Allied unit gains +defense this round",        SpellSchool::Light, SpellTarget::SingleAlly,    SpellEffect::DefenseBuff,    4,  3 },
    {SPL::RADIANCE,       "Radiance",        "Holy light strikes all enemies",               SpellSchool::Light, SpellTarget::AllEnemies,    SpellEffect::Damage,         10, 8 },

    // ── BLOOD ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {SPL::BLOOD_FRENZY,   "Blood Frenzy",    "Unit enters frenzy — +attack this round",      SpellSchool::Blood, SpellTarget::SingleAlly,    SpellEffect::AttackBuff,     5,  4 },
    {SPL::DRAIN_LIFE,     "Drain Life",      "Drains HP from an enemy; heals caster's unit", SpellSchool::Blood, SpellTarget::SingleEnemy,   SpellEffect::Damage,         6,  12},
    {SPL::ENERVATE,       "Enervate",        "Weakens an enemy — -defense this round",       SpellSchool::Blood, SpellTarget::SingleEnemy,   SpellEffect::DefenseDebuff,  5,  3 },
    {SPL::HEMORRHAGE,     "Hemorrhage",      "All enemies bleed — morale loss",              SpellSchool::Blood, SpellTarget::AllEnemies,    SpellEffect::MoraleDrain,    8,  15},

    // ── DEATH ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {SPL::CURSE,          "Curse",           "Reduces enemy attack this round",              SpellSchool::Death, SpellTarget::SingleEnemy,   SpellEffect::AttackDebuff,   4,  3 },
    {SPL::WITHER,         "Wither",          "Withering darkness damages one enemy",         SpellSchool::Death, SpellTarget::SingleEnemy,   SpellEffect::Damage,         5,  14},
    {SPL::DEATH_COIL,     "Death Coil",      "Dark bolt — damages foe, heals caster's unit", SpellSchool::Death, SpellTarget::SingleEnemy,   SpellEffect::Damage,         7,  12},
    {SPL::PLAGUE,         "Plague",          "Plague strikes all enemies",                   SpellSchool::Death, SpellTarget::AllEnemies,    SpellEffect::Damage,         12, 7 },
    {SPL::VENOMOUS_CLOUD, "Venomous Cloud",  "Noxious cloud poisons all enemies for 3 rounds", SpellSchool::Death, SpellTarget::AllEnemies, SpellEffect::Poison,         12, 8 },

    // ── NATURE ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {SPL::BARKSKIN,       "Barkskin",        "Ally unit gains natural armor this round",     SpellSchool::Nature,SpellTarget::SingleAlly,    SpellEffect::DefenseBuff,    4,  3 },
    {SPL::ENTANGLE,       "Entangle",        "Roots reduce enemy attack this round",         SpellSchool::Nature,SpellTarget::SingleEnemy,   SpellEffect::AttackDebuff,   5,  3 },
    {SPL::CALL_LIGHTNING, "Call Lightning",  "Lightning strikes one enemy",                  SpellSchool::Nature,SpellTarget::SingleEnemy,   SpellEffect::Damage,         6,  16},
    {SPL::REGROWTH,       "Regrowth",        "Restores HP to an allied unit",                SpellSchool::Nature,SpellTarget::SingleAlly,    SpellEffect::Heal,           5,  20},
    {SPL::SERPENT_VENOM,  "Serpent Venom",   "Injects venom — poisons one enemy for 3 rounds",    SpellSchool::Nature, SpellTarget::SingleEnemy, SpellEffect::Poison, 6, 12},

    // ── FORGE ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {SPL::REINFORCE,      "Reinforce",       "Constructs gain +defense this round",          SpellSchool::Forge, SpellTarget::SingleAlly,    SpellEffect::DefenseBuff,    4,  4 },
    {SPL::OVERCLOCK,      "Overclock",       "Overclocked unit gains +attack this round",    SpellSchool::Forge, SpellTarget::SingleAlly,    SpellEffect::AttackBuff,     5,  4 },
    {SPL::SHRAPNEL,       "Shrapnel",        "Shrapnel burst hits all enemies",              SpellSchool::Forge, SpellTarget::AllEnemies,    SpellEffect::Damage,         9,  8 },
    {SPL::HARDENED_SHELL, "Hardened Shell",  "All allies gain +defense this round",          SpellSchool::Forge, SpellTarget::AllAllies,     SpellEffect::DefenseBuff,    8,  2 },
    {SPL::NAPALM,         "Napalm",          "Incendiary burst — burns all enemies for 2 rounds", SpellSchool::Forge, SpellTarget::AllEnemies, SpellEffect::Burn,        10, 8 },

    // ── FLESH ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {SPL::FESTER,         "Fester",          "Flesh rot damages one enemy",                  SpellSchool::Flesh, SpellTarget::SingleEnemy,   SpellEffect::Damage,         5,  13},
    {SPL::MEND_FLESH,     "Mend Flesh",      "Regenerates HP of an allied unit",             SpellSchool::Flesh, SpellTarget::SingleAlly,    SpellEffect::Heal,           5,  18},
    {SPL::TOXIN,          "Toxin",           "Toxic cloud drains enemy morale",              SpellSchool::Flesh, SpellTarget::SingleEnemy,   SpellEffect::MoraleDrain,    4,  20},
    {SPL::GROWTH,         "Growth",          "All allies surge with life — morale boost",    SpellSchool::Flesh, SpellTarget::AllAllies,     SpellEffect::MoraleBoost,    7,  15},
    {SPL::ACID_SPRAY,     "Acid Spray",      "Corrosive acid — burns one enemy for 2 rounds",              SpellSchool::Flesh, SpellTarget::SingleEnemy, SpellEffect::Burn, 7, 12},

    // ── NEUTRAL (world-map only) ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {SPL::VISIONS,    "Visions",    "Reveals terrain, objects and enemy forces within 5 tiles",    SpellSchool::Neutral, SpellTarget::WorldMap, SpellEffect::WorldReveal,    5, 5 },
    {SPL::TOWN_PORTAL,"Town Portal","Teleport to a friendly town (requires full daily movement)",   SpellSchool::Neutral, SpellTarget::WorldMap, SpellEffect::WorldTeleport,  8, 0 },
    {SPL::FOUND_CITY, "Found City", "Convert a cleared Utopia into a town of your chosen faction (level 10; costs 10000 Gold + 10 each resource)", SpellSchool::Neutral, SpellTarget::WorldMap, SpellEffect::WorldFoundCity, 15, 0},
};
// clang-format on

static constexpr int SPELL_COUNT = static_cast<int>(sizeof(ALL_SPELLS) / sizeof(ALL_SPELLS[0]));

inline const SpellDef* findSpell(int id)
{
    for (int i = 0; i < SPELL_COUNT; ++i)
        if (ALL_SPELLS[i].id == id) return &ALL_SPELLS[i];
    return nullptr;
}
