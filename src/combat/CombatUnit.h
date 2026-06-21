#pragma once
#include <string>
#include <vector>
#include "../town/UnitDef.h"
#include "../world/HexMap.h"  // HexCoord

// ── Special tile types on combat grid ─────────────────────────────────────────
enum class CombatTileType : uint8_t
{
    Normal = 0,
    Attack,         // bonus damage while standing here
    Defense,        // damage reduction while standing here
    Speed,          // movement bonus
    SpeedPenalty,   // movement penalty
    Obstacle,       // impassable
    Wall,           // siege wall — has HP
};

struct CombatTile
{
    HexCoord      coord;
    CombatTileType type    = CombatTileType::Normal;
    bool          occupied = false;
    uint32_t      unitId   = 0;   // which unit is standing here
    int           wallHP   = 0;   // only for Wall tiles
};

// ── Combat unit stack ──────────────────────────────────────────────────────────
struct CombatUnit
{
    uint32_t    id          = 0;
    std::string name;
    int         defId       = 0;    // UnitDef id
    int         factionHint = -1;   // display faction when defId==0 (-1=use fallback)

    // Stack state
    int         count       = 1;    // units in stack
    int         hp          = 10;   // current HP of top unit in stack
    int         maxHp       = 10;

    // Combat stats (copied from UnitDef, modified by hero aura etc.)
    int         attack      = 2;
    int         defense     = 2;
    int         damageMin   = 1;
    int         damageMax   = 3;
    int         speed       = 4;
    int         range       = 0;
    int         shots       = 0;
    int         shotsLeft   = 0;
    bool        flying      = false;
    bool        vampiric    = false;  // heals for damage dealt
    bool        regenerates = false;  // restores full HP at start of own turn
    UnitTag     tags        = UnitTag::Humanoid;

    // Position on combat grid
    HexCoord    pos         = {0, 0};

    // Ownership
    bool        isPlayer    = true;   // player or enemy side
    int         stackSlot   = 0;      // 0-6

    // Turn state
    bool        hasMoved    = false;
    bool        hasActed    = false;
    bool        waitUsed    = false;  // used Wait this round
    bool        alive       = true;

    // Retaliation state
    bool        canRetaliate = true;  // resets each round

    // Morale meter (0-100)
    int         morale      = 50;
    bool        moraleImmune = false; // Undead, Forge, Fleshcraft

    // Luck (0-5): each point gives +5% chance of a lucky double-damage hit
    int         luck        = 0;

    // Second life (Eternal Empire)
    bool        hasSecondLife      = false;
    bool        secondLifeUsed     = false;
    bool        secondLifeFullHeal = false; // EternalLegion: revive at full HP instead of half
    int         secondLifeStrBonus = 0;    // ETERNAL_CMD: % attack/defense gain on revival

    // Per-round temporary bonuses — persist across rounds while duration > 0
    int         roundAttackBonus  = 0;
    int         roundDefenseBonus = 0;
    int         buffAttackRounds  = 0;   // rounds remaining for roundAttackBonus
    int         buffDefenseRounds = 0;   // rounds remaining for roundDefenseBonus
    bool        moraleSurgedThisRound = false;

    // Damage-over-time effects
    int         poisonDamage = 0;   // HP damage per round
    int         poisonRounds = 0;   // rounds remaining
    int         burnDamage   = 0;   // HP damage per round
    int         burnRounds   = 0;   // rounds remaining

    // Defend buff — lasts up to 3 rounds, refreshed if Defend used again
    int         defendRoundsLeft   = 0;
    int         defendDefenseBonus = 0;

    // Faction-specific state
    int         desperationMeter  = 0;   // Holy Order: charges under duress, surges on full
    bool        isLastOfType      = false;

    // Amalgamate adaptation state
    int         hitsTaken         = 0;   // hits received since last adaptation
    int         adaptationsGained = 0;   // total adaptations gained (max 6)
    bool        rapidEvolution    = false; // Evolver specialty / Adaptation Master: adapt after every hit
    bool        adaptationFast    = false; // Adaptation Basic: adapt after 2 hits (not 3)
    bool        adaptationDouble  = false; // Adaptation Advanced: gain +2 stat per adaptation (not +1)

    // Possession (Voidkin) — unit fights for the other side for possessedRoundsLeft rounds
    bool        possessed           = false;
    int         possessedRoundsLeft = 0;

    // Siege engine fields (isSiegeEngine=true units are placed on turn 1, act from turn 2)
    bool isSiegeEngine   = false;
    int  wallDamage      = 0;     // damage dealt to wall tiles per attack
    bool gateOnly        = false; // Battering Ram: can only attack gate hex
    bool wallBypass      = false; // Siege Drill: placed behind the wall automatically
    bool transportMode   = false; // Siege Tower: allies standing adjacent can cross walls

    // ── Methods ───────────────────────────────────────────────────────────────
    bool canAct()   const { return alive && !hasActed; }
    bool canMove()  const { return alive && !hasMoved; }

    // Total HP across whole stack
    int totalHp() const { return (count - 1) * maxHp + hp; }

    // Apply damage — returns units killed
    int applyDamage(int dmg);

    // Reset for new round
    void newRound();
};
