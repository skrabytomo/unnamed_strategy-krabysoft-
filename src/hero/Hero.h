#pragma once
#include <vector>
#include <string>
#include "../world/HexMap.h"
#include "FactionId.h"
#include "Skills.h"
#include "Artifacts.h"

// Movement cost per terrain (indexed by Terrain enum)
static constexpr int BASE_MOVE_COST[] = {
    2,  // Plains
    3,  // Forest
    3,  // Highland
    3,  // Corrupted
    4,  // Toxic
    2,  // Sacred
    2,  // Industrial
    3,  // Rocky
    5,  // Swamp
    99, // Water
    4,  // Volcanic
    4,  // Barren
    4,  // Wasteland
    3,  // CorruptedForest
    3,  // FleshZone
    99, // Mountain
};

struct UnitStack
{
    int defId = 0;
    int count = 0;
};

struct Hero
{
    uint32_t    id       = 0;
    std::string name;
    FactionId   faction  = FactionId::None;
    int         classId  = 0;    // HeroClassDef id

    // Which player slot (1-based, matches Town::ownerId) this hero belongs to.
    // Only meaningfully set for AI heroes (m_enemyHeroes) — each AI player slot
    // gets its own economy/strategy now instead of every bot sharing one pool.
    // 0 = unset (human heroes don't need this; m_heroes is swapped per active
    // player in hot-seat instead).
    uint32_t    ownerId  = 0;

    // Set when this hero loses an off-screen AI-vs-AI field battle; it is
    // pruned from m_enemyHeroes at the top of the next doEndTurn() call.
    // Never persisted (an eliminated hero is erased, not saved).
    bool        eliminated = false;

    HexCoord    pos      = {0, 0};
    int         movePool = 0;
    int         maxMove  = 16;   // reduced from 20: a hero can no longer cross
                                 // enough hexes to reach & take a town on day 1

    std::vector<HexCoord> path;
    int pathStep = 0;

    int level       = 1;
    int xp          = 0;
    int xpToNext    = 100;
    int attack      = 2;
    int defense     = 2;
    int visionRange = 5;

    HeroSkills    skills;
    HeroArtifacts artifacts;

    // Casting stats (grow via class scaling on level-up)
    int lightPower  = 0;
    int bloodPower  = 0;
    int deathPower  = 0;
    int naturePower = 0;
    int forgePower  = 0;
    int fleshPower  = 0;

    // Hero mana pool
    int mana    = 10;
    int maxMana = 10;

    // Hero HP (for targeted abilities)
    int heroHp    = 100;
    int heroMaxHp = 100;

    // Known spell IDs (learned via spellbooks on map or town buildings)
    std::vector<int> knownSpells;

    // Collected artifact IDs not yet equipped
    std::vector<int> artifactInventory;

    // Army — up to 7 unit stacks (indexed by slot)
    std::vector<UnitStack> army;

    // Garrison: hero digs in at current tile, blocks enemy passage, +2 def in combat
    bool isGarrisoned = false;

    // Naval movement
    bool onBoat    = false;  // hero is aboard a boat — can traverse Water tiles
    // AI target-lock: when an AI hero commits to marching on a distant enemy
    // town, it stores the town position here and keeps heading there instead of
    // re-evaluating every turn (which caused target oscillation — a strong hero
    // "eyeing" 6 towns at 200+ hexes and marching to none). Cleared on arrival,
    // when the town is captured/changes owner, or after a timeout.
    bool        hasMarchGoal = false;
    HexCoord    marchGoal{};
    int         marchGoalTurns = 0;   // turns spent committed (timeout guard)
    int  boatCount = 0;      // boats built so far (cost escalates per boat)

    // Specialty progression: tracked stats for specialty effects
    int battlesWon = 0;         // total combat victories (Veteran specialty)
    int specialtyAtk = 0;       // accumulated specialty attack bonus
    int recyclerBonus = 0;      // Salvage Lord: permanent ATK bonus to units (max 5)
    int livingRuneBonus = 0;    // Runesmith: permanent ATK+DEF bonus to hero (max 5 each)

    // Transient per-battle specialty flags (set in enterCombat, not persisted)
    bool feastSpecialty      = false;  // Blood Prince — drain own units, heal hero per round
    bool witherSpecialty     = false;  // Fell Druid — enemies -1 ATK per round
    bool ironDiscipline      = false;  // Warlord Mechanic — own units immune to morale loss
    bool exsanguinate        = false;  // Crimson Mage — one Blood spell costs no mana
    bool exsanguinateUsed    = false;  // tracks if free cast was used this battle
    bool heresyDetection     = false;  // Inquisitor — negate first enemy spell cast
    bool heresyDetectionUsed = false;  // tracks if negation was used this battle
    bool lightningRodSpecialty = false; // Stormbark — first enemy spell reflected back
    bool lightningRodUsed      = false;
    bool harmonySpecialty      = false; // Warsinger — adjacent same-side pairs +1 ATK/DEF
    bool elixirSpecialty       = false; // Blood Sage — once per battle fully heal one unit
    bool elixirUsed            = false;
    bool coordinatedStrikeSpecialty   = false; // Warden Captain — marked target +2 ATK for all player attacks
    bool bloodPenanceSpecialty        = false; // Flagellant Marshal — units +2 ATK, hero -5 HP/round
    bool negotiatedWeaknessSpecialty  = false; // Grave Diplomat — reveals enemy specialty before battle
    bool wildGrowthSpecialty          = false; // Beastcaller — dead Beast units respawn as Ghosts
    bool overgrowthSpecialty          = false; // Pathfinder — place 3 Speed tiles before battle
    bool swarmSpecialty               = false; // Thrall Master — BloodBound start at full morale
    bool livingRuneSpecialty          = false; // Runesmith — +1 hero ATK/DEF per battle won
    bool efficientSpecialty           = false; // Master Engineer — units 20% cheaper to recruit
    bool bloodWebSpecialty            = false; // Oathmaster — allies heal on any friendly kill
    bool phylacterySpecialty          = false; // Lich — escape one defeat per campaign at half stats
    bool phylacteryUsed               = false; // persistent: tracks if Phylactery was consumed
    bool bloodScentSpecialty          = false; // Inquisitor Hunter — Bloodsworn heroes always visible
    bool lastRitesSpecialty           = false; // Confessor — friendly deaths charge Holy units' Desperation
    bool voidLinkSpecialty            = false; // Void Weaver — Void unit death disrupts/buffs adjacent
    bool infestationSpecialty         = false; // Flesh Architect — FleshZone spreads each turn on world map
    bool eternalLegionSpecialty       = false; // Iron General — second-life revives at full HP
    bool rapidEvolutionSpecialty      = false; // Evolver (Amalgamate) — OrganicMech adapt on every hit
    bool radianceSpecialty            = false; // Lightbringer — spell buffs/debuffs last +2 rounds
    bool predatorMirrorSpecialty      = false; // Shadowlord — first spell each battle costs no mana
    bool predatorMirrorUsed           = false;
    bool covenantSpecialty            = false; // Oathbound — when unit buffed, adjacent allies gain half buff
    bool collectiveSpecialty          = false; // Hive Controller — OrganicMech share best adaptation at round start

    // Persistent world-map specialty flags (set at hero creation)
    bool ghostWalkSpecialty           = false; // Shadow Stalker — hero invisible on world map
    bool blightAuraSpecialty          = false; // Blight Caller — Sacred terrain converts to Corrupted near hero

    // Transient per-battle specialty flags
    bool soulHarvestSpecialty         = false; // Death Herald — enemy kills restore hero HP (+5/kill)
    bool recyclerSpecialty            = false; // Salvage Lord — units gain +1 ATK after each battle won
    bool apexSpecialty                = false; // Apex Hunter — OrganicMech start with max adaptations
    bool corruptionSpecialty          = false; // Voidcaller — enemy units lose -1 DEF per round
    bool synthesisSpecialty           = false; // Ironweaver — hero regenerates +2 mana per round
    bool adaptationMirrorSpecialty    = false; // Fleshbinder — OrganicMech gain adaptation when any ally dies

    // ── Siege camp ────────────────────────────────────────────────────────────
    bool     isSiegeCamping    = false;  // hero is laying siege (parked outside enemy town)
    uint32_t siegeTargetTownId = 0;      // which town they're besieging

    // ── March ability ─────────────────────────────────────────────────────────
    int  marchCooldownWeek = 0;   // week when cooldown expires (0 = ready)
    bool marchBonusActive  = false; // hero has +10% move bonus this week

    static int xpRequired(int lvl) { return 100 * lvl * lvl; }

    // Returns true if the hero leveled up
    bool addXp(int amount) {
        xp += amount;
        bool leveled = false;
        while (xp >= xpToNext) {
            xp -= xpToNext;
            level++;
            xpToNext = xpRequired(level);
            leveled = true;
        }
        return leveled;
    }

    int moveCost(Terrain t) const;
    bool canEnter(Terrain t) const;
};
