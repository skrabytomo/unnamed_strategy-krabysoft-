#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include "HexGrid.h"

// ── Terrain types ──────────────────────────────────────────────────────────────
enum class Terrain : uint8_t
{
    Plains = 0,
    Forest,
    Highland,
    Corrupted,
    Toxic,
    Sacred,
    Industrial,
    Rocky,
    Swamp,
    Water,
    Volcanic,
    Barren,
    Wasteland,
    CorruptedForest,
    FleshZone,
    Mountain,   // impassable cliff/rock — scenario barrier
    COUNT
};

// ── One tile on the map ────────────────────────────────────────────────────────
struct HexTile
{
    HexCoord coord;
    Terrain  terrain  = Terrain::Plains;
    bool     explored = false;   // fog of war
    bool     visible  = false;   // currently in vision range
    bool     blocked  = false;   // impassable Barrier WorldObject placed here
    int      elevation = 0;      // reserved for future

    // Entity IDs (0 = none) — filled by game systems
    uint32_t heroId     = 0;
    uint32_t townId     = 0;
    uint32_t resourceId = 0;
};

// ── Map sizes ──────────────────────────────────────────────────────────────────
enum class MapSize { Small, Medium, Large, XLarge };

inline int mapRadius(MapSize s) {
    switch (s) {
        case MapSize::Small:  return 24;
        case MapSize::Medium: return 36;
        case MapSize::Large:  return 52;
        case MapSize::XLarge: return 72;
    }
    return 36;
}

// ── HexMap ────────────────────────────────────────────────────────────────────
class HexMap
{
public:
    HexMap() = default;

    // Allocate a blank circular map of given size
    void create(MapSize size);

    // Tile access — returns nullptr if coord out of bounds
    HexTile*       getTile(HexCoord h);
    const HexTile* getTile(HexCoord h) const;

    bool inBounds(HexCoord h) const;

    int    radius() const { return m_radius; }
    size_t tileCount() const { return m_tiles.size(); }

    // Iterate all tiles
    using TileVisitor = std::function<void(HexTile&)>;
    void forEach(TileVisitor fn);

    // All tile coords in map
    const std::vector<HexCoord>& coords() const { return m_coords; }

private:
    int m_radius = 0;
    std::unordered_map<HexCoord, HexTile, HexCoordHash> m_tiles;
    std::vector<HexCoord> m_coords;
};
