#pragma once
#include "../world/HexMap.h"
#include "../data/Resources.h"
#include "../hero/FactionId.h"

enum class WorldObjectType : uint8_t
{
    SpellScroll,    // teaches a spell to the hero (value = spellId)
    ArtifactChest,  // gives an artifact (value = artifactId)
    XPShrine,       // grants XP to the hero (value = xpAmount)
    ResourceCache,  // gives resources (value = amount, resourceType set below)
    Observatory,    // reveals FoW in radius; value=radius(tiles), resets weekly
    StatShrine,     // spend 1000g for hero +stat; value=which stat (0=atk,1=def,2=spd,3=lightPower); questState tracks uses (max 3 per visit)
    BanditCamp,     // fight bandits, get reward; value=difficulty(1-3)
    UnitDwelling,   // weekly recruitable pool; value=tier(1-6), faction field = faction
    QuestGiver,     // gives a quest; linkedId = QuestTarget obj id
    QuestTarget,    // destination; linkedId = QuestGiver obj id
    // Terrain-specific objects
    ForestShrine,   // Forest: +75 XP (value=75)
    HighlandRuin,   // Highland/Rocky: reveals radius (value=4), permanent
    HolyFountain,   // Sacred: restores hero mana; resets weekly
    Oasis,          // Barren/Wasteland: restores hero movePool; resets weekly
    Campfire,       // Plains: +150 gold (value=150)
    LavaCrystal,    // Volcanic: gives Mercury (value=3, resourceType=Mercury)
    SwampAltar,     // Swamp: teaches a spell (value=spellId)
    TreasureChest,  // Multi-choice reward: value=gold, questState=xp, faction=stat(0=ATK,1=DEF,2=SPD)
    Crypt,    // faction army (T1-T3) guards it → gold + scroll reward; faction=which faction, value=difficulty(1-3)
    Utopia,   // 4 T6 stacks guard it → choice of major rewards; faction=which faction, value=reward seed
    Landmark,       // historical site: permanent XP+lore on first visit; value=xpAmt
    CursedGround,   // damages hero army on entry each pass; questState=charges remaining; value=dmgPerCharge
    NeutralOutpost, // guarded; capture gives weekly T1 production; faction=garrison faction; value=tier
    WitchHut,       // teaches one random secondary skill; questState=skillId taught; revisitable
    Stables,        // permanently increases hero maxMove by 3; one-time per playthrough
    TreeOfKnowledge,// pay 2000 gold to gain +1 level, or free XP; one-time per playthrough
    Barrier,        // impassable tile blocker for scenarios; sets tile->blocked; can be removed via trigger (collected=true)
    ChokeGuard,     // fixed-power guardian blocking a map passage; cleared when beaten
    Shipyard,       // hero buys a boat here; value=boats already sold (cost = 2000+value*1000 gold + 10 Iron)
    FishingHouse,   // built from a boat on land; gives +150 gold/day; faction field = ownerId (1=player)
};

struct WorldObject
{
    uint32_t        id           = 0;
    WorldObjectType type         = WorldObjectType::XPShrine;
    HexCoord        pos          = {0, 0};
    int             value        = 0;
    ResourceType    resourceType = ResourceType::Gold;
    bool            collected    = false;
    // New fields for extended types
    uint8_t         faction      = 0;   // UnitDwelling: cast to FactionId
    int             available    = 0;   // UnitDwelling: units ready to recruit this week
    uint32_t        linkedId     = 0;   // QuestGiver<->QuestTarget cross-reference
    int             questState   = 0;   // QuestGiver: 0=idle,1=active,2=complete; StatShrine: uses remaining
};
