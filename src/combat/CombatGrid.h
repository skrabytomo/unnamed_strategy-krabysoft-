#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include "CombatUnit.h"
#include "../world/HexGrid.h"

// Standard HoMM3-style combat grid
// 11 columns x 9 rows (flat-top hexes, offset layout)
// Player units start on left side, enemy on right

class CombatGrid
{
public:
    static constexpr int COLS = 11;
    static constexpr int ROWS = 9;

    CombatGrid() = default;

    void init(float hexSize = 48.0f);

    // Tile access
    CombatTile*       getTile(HexCoord h);
    const CombatTile* getTile(HexCoord h) const;
    bool              inBounds(HexCoord h) const;

    // Unit access
    CombatUnit*       getUnit(uint32_t id);
    const CombatUnit* getUnit(uint32_t id) const;
    CombatUnit*       getUnitAt(HexCoord h);
    const CombatUnit* getUnitAt(HexCoord h) const;

    // Place unit on grid
    bool placeUnit(CombatUnit& unit, HexCoord h);

    // Move unit — clears old tile, sets new
    bool moveUnit(uint32_t unitId, HexCoord to);

    // Remove dead units
    void removeDeadUnits();

    // All units (both sides)
    std::vector<CombatUnit>& units() { return m_units; }
    const std::vector<CombatUnit>& units() const { return m_units; }

    // Add unit to grid
    uint32_t addUnit(const CombatUnit& u);

    // Pathfinding on combat grid
    // Returns hexes reachable within movePoints, excluding occupied
    std::vector<HexCoord> reachable(HexCoord from, int movePoints,
                                     bool flying) const;

    // Shortest path on combat grid
    std::vector<HexCoord> findPath(HexCoord from, HexCoord to,
                                    bool flying) const;

    // Hexes adjacent to a target (for melee positioning)
    std::vector<HexCoord> meleePositions(HexCoord target) const;

    const HexGrid& hexGrid() const { return m_hexGrid; }
    const std::vector<HexCoord>& allCoords() const { return m_coords; }

    // Special tile setup
    void setTileType(HexCoord h, CombatTileType type);
    void placeRandomSpecialTiles(int count, uint32_t seed);
    // Place Obstacle tiles in the middle area (cols 2 to COLS-3), skipping spawn zones
    void placeObstacleTiles(int count, uint32_t seed);
    // Siege: place wall structure at column 5 with gate at center row
    void placeSiegeWalls(int wallHP, int gateHP);
    // Damage a wall tile; returns true if wall is now breached
    bool damageWall(HexCoord h, int damage);
    // Find the gate hex (center wall tile)
    HexCoord gateHex() const;
    // Check if a hex is a wall/gate tile with HP remaining
    bool isWallTile(HexCoord h) const;

private:
    HexGrid m_hexGrid{ 48.0f };
    std::unordered_map<HexCoord, CombatTile, HexCoordHash> m_tiles;
    std::vector<HexCoord> m_coords;
    std::vector<CombatUnit> m_units;
    uint32_t m_nextId = 1;
};
