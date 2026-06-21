#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include "HexMap.h"
#include "../data/ResourceNode.h"
#include "../town/Town.h"
#include "../world/WorldObject.h"

// ── Map shape ─────────────────────────────────────────────────────────────────
enum class MapShape : uint8_t
{
    Hexagon,     // circular island — default
    JebusCross,  // narrow water corridors + chokepoint guards; connected islands (ideal 4p)
    JebusCross3, // Jebus Cross + rich sacred neutral centre zone guarded by 4 monsters
    Ring,        // donut — center is water, players on perimeter (ideal 3p)
};

// ── Named templates ───────────────────────────────────────────────────────────
struct MapTemplate
{
    const char* name;
    const char* desc;
    MapShape    shape;
    MapSize     size;
    int         playerCount;
    float       waterRatio;
};

static const MapTemplate kMapTemplates[] = {
    { "Balanced Hexagon", "2-player circular island",              MapShape::Hexagon,     MapSize::Medium, 2, 0.15f },
    { "Jebus Cross 2.0",  "4-player, connected bridges + guards",  MapShape::JebusCross,  MapSize::Large,  4, 0.20f },
    { "Jebus Cross 3.0",  "4-player + sacred neutral centre",      MapShape::JebusCross3, MapSize::Large,  4, 0.20f },
    { "Large Jebus",      "8-player large quadrant map",           MapShape::JebusCross,  MapSize::XLarge, 8, 0.20f },
    { "Ring Island",      "3-player perimeter ring",               MapShape::Ring,        MapSize::Medium, 3, 0.25f },
};
static constexpr int kMapTemplateCount = 5;

// ── Generation parameters ──────────────────────────────────────────────────────
struct WorldGenParams
{
    uint32_t seed            = 42;
    MapSize  size            = MapSize::Medium;
    int      playerCount     = 2;     // 1-8, one starting town each
    float    resourceDensity = 1.0f;  // multiplier; 1.0 = default
    bool     balancedStart   = true;  // equidistant player towns
    float    waterRatio      = 0.15f; // target fraction of map that is water
    bool     richNeutralZones = true;  // extra objects in neutral zones
    bool     zoneBasedTerrain = true;  // use zone terrain vs old noise biomes
    MapShape shape            = MapShape::Hexagon; // overall map layout
};

// ── Generation results ─────────────────────────────────────────────────────────
struct WorldGenResult
{
    std::vector<Town>         towns;          // one per player slot
    std::vector<ResourceNode> resources;      // all resource nodes
    std::vector<HexCoord>     startPositions; // hero spawn coords (one per player)
    std::vector<WorldObject>  worldObjects;   // observatories, shrines, dwellings, etc.
};

// ── WorldGen ──────────────────────────────────────────────────────────────────
// Call map.create(params.size) BEFORE calling generate().
class WorldGen
{
public:
    static WorldGenResult generate(HexMap& map, const WorldGenParams& params);

private:
    static void passNoiseTerrain(HexMap& map, const WorldGenParams& p);
    static std::vector<HexCoord> passShapeConstraints(HexMap& map, MapShape shape);
    // Returns bridge centre positions (empty for Hexagon/Ring)
    static void passBiomeClusters(HexMap& map, const WorldGenParams& p,
                                  const std::vector<HexCoord>& centers);
    static void passSmoothCoastlines(HexMap& map, int iterations = 2);
    static void passRemoveIsolatedWater(HexMap& map);

    static std::vector<HexCoord> pickSpawnPositions(const HexMap& map,
                                                    int count, uint32_t seed);
    static std::vector<ResourceNode> placeResources(HexMap& map,
                                                    const WorldGenParams& p,
                                                    uint32_t& nextId);
    static void buildTowns(WorldGenResult& result,
                           const HexMap& map,
                           const std::vector<HexCoord>& positions,
                           uint32_t& nextId);
    static void placeWorldObjects(WorldGenResult&, HexMap&, const WorldGenParams&, uint32_t&);

    // ── Zone-based generation ─────────────────────────────────────────────────
    // Returns zoneId per tile coord (-1=water, 0..N-1 player zones, N..N+M-1 neutral)
    static std::unordered_map<HexCoord, int, HexCoordHash> assignZones(
        const HexMap& map,
        const std::vector<HexCoord>& playerSpawns,
        std::vector<HexCoord>& neutralCentersOut,
        uint32_t& rng);

    static void passZoneTerrain(
        HexMap& map,
        const std::unordered_map<HexCoord, int, HexCoordHash>& tileZones,
        const std::vector<Terrain>& zoneTerrain,
        uint32_t& rng);

    static void placePlayerZoneMines(
        WorldGenResult& result, HexMap& map,
        const std::vector<HexCoord>& spawns,
        const std::unordered_map<HexCoord, int, HexCoordHash>& tileZones,
        const WorldGenParams& p, uint32_t& nextId, uint32_t& rng);

    static void placeZoneObjects(
        WorldGenResult& result, HexMap& map,
        const std::vector<HexCoord>& allCenters,
        const std::vector<int>& zonePlayer,   // -1=neutral, 0..N=player idx
        const std::unordered_map<HexCoord, int, HexCoordHash>& tileZones,
        const WorldGenParams& p, uint32_t& nextId, uint32_t& rng);

    static Terrain heightToTerrain(float h, float waterCutoff);
    static bool    isLand(Terrain t);
    static bool    isSuitable(const HexMap& map, HexCoord c, int minDist,
                              const std::vector<HexCoord>& occupied);
    static ResourceType terrainResource(Terrain t, uint32_t rng);

    // LCG for deterministic random inside generation
    static uint32_t lcg(uint32_t& state) {
        state = state * 1664525u + 1013904223u;
        return state;
    }
};
