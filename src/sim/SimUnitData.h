#pragma once
#include "../combat/CombatUnit.h"
#include "../hero/Hero.h"

// Balance-tunable stat table for all 9 factions × 6 tiers.
// Edit these values to tune faction balance — the simulator will re-run automatically.
// Stats are per single unit (stack count comes from ArmyBuilder weekly growth).
//
// unlockWeek: first week the dwelling produces units (building order assumption).
// weeklyGrowth: units added to pool each week once unlocked.
// goldCost: recruitment cost per unit (used for cost-efficiency reporting).

struct UnitSimData
{
    const char* name;
    FactionId   faction;
    int         tier;
    int         hp;
    int         attack;
    int         defense;
    int         damageMin;
    int         damageMax;
    int         speed;
    int         range;        // 0 = melee
    int         shots;        // 0 = melee
    bool        flying;
    bool        hasSecondLife; // Eternal Command faction passive
    UnitTag     tags;
    int         weeklyGrowth;
    int         unlockWeek;
    int         goldCost;     // per-unit gold cost at recruitment
};

// clang-format off
// Balance pass notes (post-sim):
//   Holy Order    — baseline, minor HP lead from Desperation mechanic (modeled in base stats)
//   Crimson Wardens — ranged at T2+T5, fast Vampire T4; tuned to ~50% avg
//   Thornkin      — no ranged above T3, compensated by Symbiosis HP/ATK (+1 modeled in base stats)
//   Eternal Empire — ALL units have hasSecondLife (faction passive: Eternal Command).
//                    Effective HP doubles in combat; base stats deliberately lower to compensate.
//                    Conscript speed raised 4→5 (speed 4 was unplayably slow vs flying factions).
//   Bloodsworn    — fast melee, high T6 damage; balanced by low defense
//   Voidkin       — ALL units fly (huge positional advantage). T4-T6 speeds NERFED:
//                    Stalker 10→8, Wraith 12→10, Colossus 13→11.
//                    Rift Archer shots 5→3. Phase Walker def 6→5.
//   Iron Assembly — strong ranged line; Colossus Prime damage trimmed 26-42→22-38
//   Amalgamate    — no faction mechanic modeled in sim; buffed base stats significantly.
//                    T1 HP 11→15, T2 HP 18→25, T3 HP 28→38, T5 HP 95→115, T6 HP 155→180.
//                    T1/T2 ATK +1. Growth bumped T1 12→14, T2 9→11.
//   Convergence   — Mirroring not modeled; kept at average stats.
// T4 unlocks week 5, T5 week 7, T6 week 9. Stack count = min(growth*(w-unlock+1), 80).
static constexpr UnitSimData SIM_UNITS[] = {
    // ── Holy Order ────────────────────────────────────────────────────────────
    //  name                 faction                tier  hp  atk def dmn dmx spd rng sht fly 2nd  tags                                          gr  uw  gold
    {"Penitent",        FactionId::HolyOrder, 1, 14, 3, 3, 1,  3, 5,  0,  0, false, false, UnitTag::Humanoid|UnitTag::Holy,  14, 1,   65},
    {"Torch Bearer",    FactionId::HolyOrder, 2, 20, 4, 5, 2,  5, 5,  0,  0, false, false, UnitTag::Humanoid|UnitTag::Holy,  10, 2,  115},
    {"Plague Doctor",   FactionId::HolyOrder, 3, 28, 5, 5, 4,  7, 6,  5, 10, false, false, UnitTag::Humanoid|UnitTag::Holy,   7, 3,  205},
    {"Penitent Knight", FactionId::HolyOrder, 4, 58, 9, 9, 7, 13, 7,  0,  0, true,  false, UnitTag::Humanoid|UnitTag::Holy,   5, 5,  350},
    {"Seraph",          FactionId::HolyOrder, 5, 95,13,12,14, 26, 9,  0,  0, true,  false, UnitTag::Humanoid|UnitTag::Holy,   3, 7,  620},
    {"Winged Hussar",   FactionId::HolyOrder, 6,165,17,15,23, 38,12,  0,  0, true,  false, UnitTag::Humanoid|UnitTag::Holy,   2, 9, 1200},

    // ── Crimson Wardens ───────────────────────────────────────────────────────
    {"Skeleton",        FactionId::CrimsonWardens, 1, 12, 2, 3, 1,  3, 5, 0,  0, false, false, UnitTag::Undead,  13, 1,   60},
    {"Bone Archer",     FactionId::CrimsonWardens, 2, 22, 2, 4, 2,  4, 5, 5, 10, false, false, UnitTag::Undead,  10, 2,  100},
    {"Wight",           FactionId::CrimsonWardens, 3, 50, 6, 6, 6, 10, 6, 0,  0, false, false, UnitTag::Undead,   7, 3,  210},
    {"Vampire",         FactionId::CrimsonWardens, 4, 55, 9, 6, 8, 14,10, 0,  0, true,  false, UnitTag::Undead,   5, 5,  360},
    {"Lich",            FactionId::CrimsonWardens, 5, 85,12, 9,11, 19, 8, 6, 10, false, false, UnitTag::Undead,   3, 7,  640},
    {"Bone Dragon",     FactionId::CrimsonWardens, 6,168,17,14,25, 42,11, 0,  0, true,  false, UnitTag::Undead,   2, 9, 1300},

    // ── Thornkin ─────────────────────────────────────────────────────────────
    // Base stats include rough Symbiosis +1 equivalent (not applied in sim).
    {"Sproutling",      FactionId::Thornkin, 1, 12, 2, 3, 1,  3, 5, 0, 0, false, false, UnitTag::Beast,  14, 1,   55},
    {"Briar",           FactionId::Thornkin, 2, 20, 4, 4, 3,  6, 5, 0, 0, false, false, UnitTag::Beast,  10, 2,  110},
    {"Vine Crawler",    FactionId::Thornkin, 3, 25, 5, 5, 5,  9, 5, 0, 0, false, false, UnitTag::Beast,   7, 3,  185},
    {"Grove Guardian",  FactionId::Thornkin, 4, 58, 9, 8, 9, 15, 7, 0, 0, false, false, UnitTag::Beast,   5, 5,  370},
    {"Ancient Oak",     FactionId::Thornkin, 5, 92,12,11,13, 22, 6, 0, 0, false, false, UnitTag::Beast,   3, 7,  630},
    {"World Thorn",     FactionId::Thornkin, 6,158,16,14,24, 38, 7, 0, 0, false, false, UnitTag::Beast,   2, 9, 1300},

    // ── Eternal Empire ────────────────────────────────────────────────────────
    // Faction passive: Eternal Command — ALL units have second life (revives at count=1).
    // secondLife is a morale/action sink, not HP doubling. Upper-tier dmg buffed so EE
    // can crack high-HP Thornkin/IA targets; T1-T3 stats stay modest.
    {"Conscript",       FactionId::EternalEmpire, 1, 12, 2, 3, 1,  3, 5, 0, 0, false, true,  UnitTag::Humanoid|UnitTag::Undead,   13, 1,   65},
    {"Revenant",        FactionId::EternalEmpire, 2, 20, 4, 4, 3,  6, 5, 0, 0, false, true,  UnitTag::Undead,                     10, 2,  120},
    {"Shade Archer",    FactionId::EternalEmpire, 3, 32, 5, 5, 3,  6, 6, 5,10, false, true,  UnitTag::Undead,                      7, 3,  210},
    {"Steel Guardian",  FactionId::EternalEmpire, 4, 50, 9,10,10, 16, 7, 0, 0, false, true,  UnitTag::Construct|UnitTag::Undead,   4, 5,  370},
    {"Phantom Knight",  FactionId::EternalEmpire, 5, 78,12,11,13, 22, 8, 0, 0, true,  true,  UnitTag::Undead,                      3, 7,  590},
    {"Immortal",        FactionId::EternalEmpire, 6,120,14,12,22, 34,10, 0, 0, true,  true,  UnitTag::Undead,                      2, 9, 1090},

    // ── Bloodsworn ────────────────────────────────────────────────────────────
    {"Bloodling",       FactionId::Bloodsworn, 1, 13, 3, 2, 2,  4, 5, 0, 0, false, false, UnitTag::Humanoid|UnitTag::BloodBound,  14, 1,   65},
    {"Berserker",       FactionId::Bloodsworn, 2, 20, 4, 4, 3,  6, 6, 0, 0, false, false, UnitTag::Humanoid|UnitTag::BloodBound,  10, 2,  110},
    {"Blood Shaman",    FactionId::Bloodsworn, 3, 27, 5, 5, 3,  6, 6, 4,10, false, false, UnitTag::Humanoid|UnitTag::BloodBound,   7, 3,  185},
    {"Ravager",         FactionId::Bloodsworn, 4, 54,10, 7,10, 18, 8, 0, 0, false, false, UnitTag::Humanoid|UnitTag::BloodBound,   5, 5,  340},
    {"Bloodtide Warlord",FactionId::Bloodsworn,5, 72,13, 8,14, 25, 9, 0, 0, false, false, UnitTag::Humanoid|UnitTag::BloodBound,   3, 7,  590},
    {"Crimson Avatar",  FactionId::Bloodsworn, 6,140,18, 9,25, 42,11, 0, 0, false, false, UnitTag::Humanoid|UnitTag::BloodBound,   2, 9, 1150},

    // ── Voidkin ───────────────────────────────────────────────────────────────
    // ALL units fly. Trade-off: HP BELOW faction average (glass cannon fliers).
    // T1 hp 18→14, T2 hp 24→20, T5 hp 72→60 spd 10→9, T6 hp 135→118 atk 16→15 spd 11→10.
    {"Void Wisp",       FactionId::Voidkin, 1, 14, 3, 3, 1,  3, 6, 0, 0, true,  false, UnitTag::Void,  13, 1,   70},
    {"Phase Walker",    FactionId::Voidkin, 2, 20, 4, 5, 2,  6, 6, 0, 0, true,  false, UnitTag::Void,  10, 2,  130},
    {"Rift Archer",     FactionId::Voidkin, 3, 28, 5, 5, 3,  6, 7, 5,10, true,  false, UnitTag::Void,   7, 3,  225},
    {"Void Stalker",    FactionId::Voidkin, 4, 44, 9, 9, 9, 15, 8, 0, 0, true,  false, UnitTag::Void,   5, 5,  360},
    {"Entropy Wraith",  FactionId::Voidkin, 5, 60,12,11,13, 22, 9, 0, 0, true,  false, UnitTag::Void,   3, 7,  640},
    {"Void Colossus",   FactionId::Voidkin, 6,118,15,14,22, 34,10, 0, 0, true,  false, UnitTag::Void,   2, 9, 1220},

    // ── Iron Assembly ─────────────────────────────────────────────────────────
    // Strong ranged line. Colossus Prime damage trimmed 26-42→22-38.
    {"Automaton",       FactionId::IronAssembly, 1, 12, 2, 4, 1,  2, 5, 3, 10, false, false, UnitTag::Mechanical,  12, 1,   75},
    {"Gun Construct",   FactionId::IronAssembly, 2, 20, 3, 5, 2,  3, 5, 5, 10, false, false, UnitTag::Mechanical,   9, 2,  140},
    {"Steam Walker",    FactionId::IronAssembly, 3, 30, 6, 6, 5, 10, 5, 0,  0, false, false, UnitTag::Mechanical,   6, 3,  215},
    {"Siege Bot",       FactionId::IronAssembly, 4, 52, 7, 9, 5,  9, 4, 5, 10, false, false, UnitTag::Mechanical,   5, 5,  390},
    {"Titan Construct", FactionId::IronAssembly, 5, 70,10,11,15, 24, 5, 0,  0, false, false, UnitTag::Mechanical,   3, 7,  635},
    {"Colossus Prime",  FactionId::IronAssembly, 6,120,15,14,22, 38, 6, 0,  0, false, false, UnitTag::Mechanical,   2, 9, 1275},

    // ── Amalgamate ────────────────────────────────────────────────────────────
    // Adaptation: OrganicMech → +ATK/DEF every 2 hits (hero ADAPTATION Basic),
    // alternating up to 6 (+3 ATK +3 DEF max). Starts average; adapts above avg.
    // T1 ATK 2→3 (normalize), T3 shots 3→5 (sustained ranged), T4 DEF 7→8.
    {"Flesh Crawler",   FactionId::Amalgamate, 1, 14, 3, 3, 1,  4, 5, 0, 0, false, false, UnitTag::OrganicMech,  12, 1,   65},
    {"Graft Soldier",   FactionId::Amalgamate, 2, 23, 4, 4, 3,  6, 5, 0, 0, false, false, UnitTag::OrganicMech,   9, 2,  120},
    {"Bone Machine",    FactionId::Amalgamate, 3, 28, 5, 5, 4,  7, 6, 3,10, false, false, UnitTag::OrganicMech,   8, 3,  195},
    {"Fleshwork Knight",FactionId::Amalgamate, 4, 60, 9, 8, 9, 16, 7, 0, 0, true,  false, UnitTag::OrganicMech,   4, 5,  360},
    {"Undying Juggernaut",FactionId::Amalgamate,5, 95,12,10,14, 23, 8, 0, 0, false, false, UnitTag::OrganicMech,   3, 7,  600},
    {"Convergence Spawn",FactionId::Amalgamate,6,155,16,14,23, 37, 9, 0, 0, true,  false, UnitTag::OrganicMech,   2, 9, 1200},

    // ── Convergence ──────────────────────────────────────────────────────────
    // Mirroring mechanic not modeled in sim. Stats kept at solid average.
    {"Awakened",        FactionId::Convergence, 1, 12, 3, 3, 2,  4, 5, 0, 0, false, false, UnitTag::Humanoid,  11, 1,   70},
    {"Synthesized",     FactionId::Convergence, 2, 21, 5, 5, 3,  6, 6, 0, 0, false, false, UnitTag::Humanoid,   8, 2,  125},
    {"Harmonized",      FactionId::Convergence, 3, 34, 6, 7, 4,  8, 6, 4,10, false, false, UnitTag::Humanoid,   7, 3,  215},
    {"Resonant",        FactionId::Convergence, 4, 62,10,10, 9, 16, 8, 0, 0, true,  false, UnitTag::Humanoid,   4, 5,  385},
    {"Transcendent",    FactionId::Convergence, 5, 88,12,13,14, 23,10, 0, 0, true,  false, UnitTag::Humanoid,   3, 7,  670},
    {"Unified Form",    FactionId::Convergence, 6,150,16,15,23, 37,12, 0, 0, true,  false, UnitTag::Humanoid,   2, 9, 1300},
};
// clang-format on

static constexpr int SIM_UNIT_COUNT = static_cast<int>(sizeof(SIM_UNITS) / sizeof(SIM_UNITS[0]));
