#pragma once
#include <string>
#include <vector>
#include <array>
#include <utility>
#include "../world/HexMap.h"
#include "../hero/Hero.h"
#include "../town/Town.h"
#include "../data/Resources.h"
#include "../data/ResourceNode.h"
#include "../world/WorldObject.h"

// ── Tile save data (only fields that change at runtime) ────────────────────────
struct TileSave
{
    int  q, r;
    int  terrain;      // Terrain enum as int
    bool explored;
    bool visible;
    uint32_t heroId;
    uint32_t townId;
    uint32_t resourceId;
};

// ── Skill instance save ────────────────────────────────────────────────────────
struct SkillInstSave { int defId = 0; int tier = 0; };

// ── Resource node save ────────────────────────────────────────────────────────
struct ResourceNodeSave
{
    uint32_t id           = 0;
    int      posQ         = 0;
    int      posR         = 0;
    int      type         = 0;   // ResourceType as int
    int      amount       = 0;
    bool     depleted     = false;
    uint32_t ownedBy      = 0;
    bool     guardBeaten  = false;
};

// ── World object save ──────────────────────────────────────────────────────────
struct WorldObjectSave
{
    uint32_t id         = 0;
    int      type       = 0;  // WorldObjectType as int
    int      posQ       = 0;
    int      posR       = 0;
    int      value      = 0;
    int      resType    = 0;  // ResourceType as int
    bool     collected  = false;
    int      questState = 0;  // quest progress
    uint32_t linkedId   = 0;  // linked quest-target id
    int      available  = 0;  // dwelling units available
    int      faction    = 0;  // dwelling faction
};

// ── Hero save data ─────────────────────────────────────────────────────────────
struct HeroSave
{
    uint32_t    id;
    std::string name;
    int         faction;   // FactionId as int
    int         classId;
    int         posQ, posR;
    int         movePool;
    int         maxMove;
    int         level;
    int         attack;
    int         defense;
    int         visionRange;
    // Stats
    int         xp;
    int         xpToNext;
    int         hp;
    int         maxHp;
    int         mana;
    int         maxMana;
    // Casting stats
    int         lightPower;
    int         bloodPower;
    int         deathPower;
    int         naturePower;
    int         forgePower;
    int         fleshPower;
    // Spells, skills, artifacts
    std::vector<int>           knownSpells;
    std::vector<SkillInstSave> skillSlots;
    std::array<int,8>          artifactEquipped = {};
    std::vector<int>           artifactInventory;
    // Army
    std::vector<std::pair<int,int>> army; // {defId, count}
    // Specialty progression
    int battlesWon      = 0;
    int specialtyAtk    = 0;
    int recyclerBonus   = 0;
    int livingRuneBonus = 0;
    // World-map hero state
    bool isGarrisoned        = false;
    bool onBoat              = false;
    int  boatCount           = 0;
    // Persistent world-map specialty flags
    bool phylacteryUsed      = false;
    bool ghostWalkSpecialty  = false;
    bool blightAuraSpecialty  = false;
    bool infestationSpecialty = false;
    bool efficientSpecialty   = false;
    bool bloodScentSpecialty  = false;
};

// ── Dwelling save ──────────────────────────────────────────────────────────────
struct DwellingSave
{
    int  buildingId;
    int  tier;
    int  path;         // UpgradePath as int
    int  available;
    int  accumulated;
};

// ── Town save data ─────────────────────────────────────────────────────────────
struct TownSave
{
    uint32_t    id;
    std::string name;
    int         faction;   // FactionId as int
    int         posQ, posR;
    uint32_t    ownerId;
    std::vector<int>         builtBuildings;
    std::vector<DwellingSave> dwellings;
    int         fortHP;
    int         fortMaxHP;
    std::vector<std::pair<int,int>> garrison; // {defId, count}
    // Weekly income stored
    std::array<int, RESOURCE_COUNT> weeklyIncomeAmounts;
};

// ── Campaign save state ────────────────────────────────────────────────────────
struct CampaignSaveState
{
    bool     active      = false;
    int      missionIdx  = 0;
    int      orderScore  = 0;
    int      lightScore  = 0;
    std::vector<std::pair<uint32_t, int>> decisions; // {decisionId, choiceIdx}
};

// ── Full game save ─────────────────────────────────────────────────────────────
struct GameSaveData
{
    int version = 3;

    // Turn state
    int day  = 1;
    int week = 1;

    // Game settings persisted in save
    int difficulty    = 1;  // 0=Easy, 1=Normal, 2=Hard
    int activeHeroIdx = 0;  // which hero is currently selected

    // Map
    int mapRadius   = 0;
    int mapSizeEnum = 0;   // MapSize as int

    // Player resources
    std::array<int, RESOURCE_COUNT> resourceAmounts = {};

    // Entities
    std::vector<HeroSave>        heroes;
    std::vector<HeroSave>        enemyHeroes;
    std::vector<HeroSave>        defeatedHeroes;
    std::vector<TownSave>        towns;
    std::vector<WorldObjectSave> worldObjects;
    std::vector<ResourceNodeSave> resourceNodes;
    uint32_t                     nextObjId = 1;

    // Tile fog/entity state
    std::vector<TileSave>  tiles;

    // Campaign state (optional — only populated when a campaign is active)
    CampaignSaveState campaign;

    // 2P hotseat state (only populated when numHumanPlayers >= 2)
    int numHumanPlayers  = 1;
    int currentPlayerIdx = 0;   // 0=P1's turn, 1=P2's turn at save time
    std::array<int, RESOURCE_COUNT> p2ResourceAmounts = {};
    std::vector<HeroSave> p2Heroes;
    std::vector<HeroSave> p2DefeatedHeroes;
    int p2ActiveHeroIdx = 0;
};

// ── Save / Load API ────────────────────────────────────────────────────────────
namespace SaveLoad
{
    bool saveGame(const std::string& path, const GameSaveData& data);
    bool loadGame(const std::string& path, GameSaveData& out);

    // Convenience: pack/unpack live game objects
    GameSaveData packState(const HexMap& map,
                           const std::vector<Hero>& heroes,
                           const std::vector<Hero>& enemyHeroes,
                           const std::vector<Hero>& defeatedHeroes,
                           const std::vector<Town>& towns,
                           const std::vector<WorldObject>& worldObjects,
                           const std::vector<ResourceNode>& resourceNodes,
                           uint32_t nextObjId,
                           const Resources& playerRes,
                           int day, int week,
                           MapSize mapSize,
                           int difficulty = 1,
                           int activeHeroIdx = 0);

    void unpackState(const GameSaveData& save,
                     HexMap& map,
                     std::vector<Hero>& heroes,
                     std::vector<Hero>& enemyHeroes,
                     std::vector<Hero>& defeatedHeroes,
                     std::vector<Town>& towns,
                     std::vector<WorldObject>& worldObjects,
                     std::vector<ResourceNode>& resourceNodes,
                     uint32_t& nextObjId,
                     Resources& playerRes,
                     int& day, int& week);

    // Hero list pack/unpack helpers (for 2P backup state)
    std::vector<HeroSave> packHeroes(const std::vector<Hero>& heroes);
    std::vector<Hero>     unpackHeroes(const std::vector<HeroSave>& saves);
}
