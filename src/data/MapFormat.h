#pragma once
#include <string>
#include <vector>
#include "../world/HexMap.h"
#include "../town/Town.h"
#include "../data/ResourceNode.h"
#include "../world/WorldObject.h"

// ── Map metadata ──────────────────────────────────────────────────────────────
struct MapMetadata
{
    std::string name        = "Unnamed Map";
    std::string author;
    std::string description;
    int         playerCount = 2;     // 1-8
    MapSize     size        = MapSize::Medium;
    int         version     = 1;
};

// ── One trigger entry stored in map file ──────────────────────────────────────
struct MapTriggerEntry
{
    std::string type;       // "enterTile", "weekStart", etc.
    std::string funcName;   // Lua global function
    std::string condFunc;   // optional condition function
    int         q = 0, r = 0;
    bool        once = false;
    int         levelThreshold = 0;
    std::string scriptBody;  // inline Lua — loaded as a named function at map load
};

// ── Complete map file ─────────────────────────────────────────────────────────
struct MapFile
{
    MapMetadata                  meta;
    std::vector<MapTriggerEntry> triggers;

    // Static map data (terrain, placed towns/resources, hero starts)
    // Tile terrain stored as ints; fog not saved (always starts hidden)
    struct TileEntry {
        int q, r, terrain;
        uint32_t townId;
        uint32_t resourceId;
    };
    std::vector<TileEntry>    tiles;
    std::vector<Town>         towns;
    std::vector<ResourceNode> resources;
    std::vector<HexCoord>     heroStarts;  // one per player slot
    std::vector<WorldObject>  worldObjects;
};

// ── MapFormat ─────────────────────────────────────────────────────────────────
namespace MapFormat
{
    // Save a map to disk (JSON). Returns false on error.
    bool save(const std::string& path, const MapFile& mapFile);

    // Load a map file. Returns false on error.
    bool load(const std::string& path, MapFile& out);

    // Apply a loaded MapFile to a HexMap (creates map, sets terrain, tags tiles)
    void applyToMap(HexMap& map, const MapFile& mf,
                    std::vector<Town>& towns,
                    std::vector<ResourceNode>& resources);
}
