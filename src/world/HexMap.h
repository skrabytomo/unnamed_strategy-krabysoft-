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

// Radii scaled by sqrt(10) from the original 24/36/52/72 so each named size
// carries ~10x the hex count (tile count grows with radius^2), paired with a
// proportionally smaller hexSize (see Game_Core.cpp) so the map's overall
// on-screen reach at default zoom stays about the same — denser, not just bigger.
inline int mapRadius(MapSize s) {
    switch (s) {
        case MapSize::Small:  return 76;
        case MapSize::Medium: return 114;
        case MapSize::Large:  return 164;
        case MapSize::XLarge: return 228;
    }
    return 114;
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
