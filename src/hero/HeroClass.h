#pragma once
#include <string>
#include <vector>
#include "../hero/Hero.h"

// ── Hero specialty — unique passive ───────────────────────────────────────────
enum class SpecialtyType
{
    // Holy Order
    HeresyDetection,    // Inquisitor — nullifies one enemy spell per battle
    LastRites,          // Confessor — enemy kills charge nearby Holy allies' Desperation +20
    Veteran,            // Crusader — hero gains +1 ATK+DEF per battle won (max 5)
    BloodPenance,       // Flagellant Marshal — all units +2 ATK; hero loses 5 HP/round

    // Bloodsworn
    Feast,              // Blood Prince — drain 2 HP from largest unit/round to heal hero
    Exsanguinate,       // Crimson Mage — one Blood spell free per battle
    Swarm,              // Thrall Master — Fledglings start at ascension threshold
    Predator,           // Assassin Lord — permanent Attack per hero killed

    // Thornkin
    WildGrowth,         // Beastcaller — dead companions respawn as spirit
    Overgrowth,         // Pathfinder — place 3 forest tiles before battle
    LightningRod,       // Stormbark — first enemy spell redirected back
    Harmony,            // Warsinger — all pairs +1 stats while hero lives

    // Eternal Empire
    SoulHarvest,        // Death Herald — enemy kills heal hero 5 HP per unit
    EternalLegion,      // Iron General — reraised units keep formation bonuses
    Phylactery,         // Lich — hero respawns next battle at half stats
    NegotiatedWeakness, // Grave Diplomat — reveals enemy specialty before battle

    // Crimson Wardens
    CoordinatedStrike,  // Warden Captain — marked target bonus from all attackers
    Elixir,             // Blood Sage — once per battle fully heal one unit
    BloodWeb,           // Oathmaster — all allies heal 4 HP per enemy unit killed
    BloodScent,         // Inquisitor Hunter — always knows Bloodsworn hero location

    // Voidkin
    VoidLink,           // Void Weaver — Void ally death: adj enemies -1 ATK, nearby Void +1 ATK
    GhostWalk,          // Shadow Stalker — hero invisible on world map
    BlightAura,         // Blight Caller — corrupts sacred terrain passively
    Wither,             // Fell Druid — enemies lose 1 stat per round in aura

    // Iron Assembly
    Efficient,          // Master Engineer — units cost 20% less to craft
    IronDiscipline,     // Warlord Mechanic — constructs immune to morale/fear
    Recycler,           // Salvage Lord — all units +1 ATK permanently per battle won (max 5)
    LivingRune,         // Runesmith — hero +1 ATK+DEF permanently per battle won (max 5)

    // Amalgamate
    RapidEvolution,     // Evolver — adaptations after 1 hit
    Collective,         // Hive Controller — OrganicMech share best adaptation at round start
    Infestation,        // Flesh Architect — flesh terrain spreads every round
    Apex,               // Apex Hunter — hero starts with all Evolved's adaptations

    // Convergence
    Radiance,           // Lightbringer — buff/debuff spells last 4 rounds instead of 2
    Covenant,           // Oathbound — adjacent allies get half buff when a buff is cast
    PredatorMirror,     // Shadowlord — first spell per battle costs no mana
    Corruption,         // Voidcaller — enemies -1 DEF each round from round 2
    Synthesis,          // Ironweaver — +2 mana regen per round
    AdaptationMirror,   // Fleshbinder — ally death grants all OrganicMech an adaptation

    None
};

// ── Hero class definition ──────────────────────────────────────────────────────
struct HeroClassDef
{
    int           id          = 0;
    std::string   name;
    FactionId     faction     = FactionId::None;
    SpecialtyType specialty   = SpecialtyType::None;
    std::string   specialtyDesc;

    // Primary casting stats this class scales
    bool scalesAttack      = false;
    bool scalesLightPower  = false;
    bool scalesBloodPower  = false;
    bool scalesDeathPower  = false;
    bool scalesNaturePower = false;
    bool scalesForgePower  = false;
    bool scalesFleshPower  = false;

    // Skill pool — IDs of skills this class can offer on level up
    std::vector<int> skillPool;

    // Stat growth per level (base values, RNG picks from these)
    int attackGrowth  = 1;   // +1 attack every N levels
    int defenseGrowth = 1;
    int manaGrowth    = 1;
};

// ── Class registry ─────────────────────────────────────────────────────────────
class HeroClassRegistry
{
public:
    void init();

    const HeroClassDef* getClass(int id)  const;
    std::vector<const HeroClassDef*> getClassesForFaction(FactionId f) const;

    const std::vector<HeroClassDef>& classes() const { return m_classes; }

private:
    std::vector<HeroClassDef> m_classes;
};
