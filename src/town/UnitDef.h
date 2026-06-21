#pragma once
#include <string>
#include <vector>
#include "../data/Resources.h"
#include "../hero/Hero.h"
#include "BuildingDef.h"  // UpgradePath

// ── Unit type tags (weakness matrix) ──────────────────────────────────────────
enum class UnitTag : uint32_t
{
    None        = 0,
    Undead      = 1 << 0,
    Construct   = 1 << 1,
    Beast       = 1 << 2,
    Humanoid    = 1 << 3,
    Flying      = 1 << 4,
    Mechanical  = 1 << 5,
    OrganicMech = 1 << 6,
    Holy        = 1 << 7,
    BloodBound  = 1 << 8,
    Void        = 1 << 9,
};

constexpr UnitTag operator|(UnitTag a, UnitTag b) {
    return static_cast<UnitTag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool hasTag(UnitTag tags, UnitTag check) {
    return (static_cast<uint32_t>(tags) & static_cast<uint32_t>(check)) != 0;
}

// ── Static unit definition ─────────────────────────────────────────────────────
struct UnitDef
{
    int         id          = 0;
    std::string name;
    FactionId   faction     = FactionId::None;
    int         tier        = 1;        // 1-6
    UpgradePath path        = UpgradePath::None; // base, A, or B variant

    // Combat stats
    int   hp          = 10;
    int   attack      = 2;
    int   defense     = 2;
    int   damage_min  = 1;
    int   damage_max  = 3;
    int   speed       = 4;     // initiative / movement on combat grid
    int   range       = 0;     // 0 = melee, >0 = ranged hex distance
    int   shots       = 0;     // ranged shots per combat (0 = melee only)
    bool  flying      = false;
    bool  vampiric    = false;  // heals attacker for damage dealt
    bool  regenerates = false;  // restores full HP at start of own turn

    // Faction-specific unit properties
    bool  hasSecondLife      = false; // Eternal Empire PathA: revive once per battle
    bool  secondLifeFullHeal = false; // revive at full HP (T4/T6 PathA only)
    bool  moraleImmune       = false; // immune to morale effects
    bool  rapidEvolution     = false; // Amalgamate PathA: adapt after every hit
    bool  adaptationDouble   = false; // Amalgamate PathB: +2 stat per adaptation

    UnitTag tags = UnitTag::Humanoid;

    // Recruitment cost per unit
    Resources cost;

    // Forge faction: crafting cost (resources per batch)
    bool       isCrafted    = false;
    int        craftBatch   = 1;       // units per craft action
    Resources  craftCost;
};

