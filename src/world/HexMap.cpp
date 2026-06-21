#include "HexMap.h"
#include <functional>

void HexMap::create(MapSize size)
{
    m_radius = mapRadius(size);
    m_tiles.clear();
    m_coords.clear();

    // Circular map — all hexes within radius
    auto hexes = HexGrid::range({0, 0}, m_radius);
    m_coords = hexes;

    for (auto& h : hexes) {
        HexTile tile;
        tile.coord = h;
        m_tiles[h] = tile;
    }
}

HexTile* HexMap::getTile(HexCoord h)
{
    auto it = m_tiles.find(h);
    return it != m_tiles.end() ? &it->second : nullptr;
}

const HexTile* HexMap::getTile(HexCoord h) const
{
    auto it = m_tiles.find(h);
    return it != m_tiles.end() ? &it->second : nullptr;
}

bool HexMap::inBounds(HexCoord h) const
{
    return m_tiles.count(h) > 0;
}

void HexMap::forEach(TileVisitor fn)
{
    for (auto& [coord, tile] : m_tiles)
        fn(tile);
}
