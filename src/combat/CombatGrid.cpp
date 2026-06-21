#include "CombatGrid.h"
#include <algorithm>
#include <queue>
#include <random>
#include <unordered_map>

void CombatGrid::init(float hexSize)
{
    m_hexGrid = HexGrid(hexSize);
    m_tiles.clear();
    m_coords.clear();
    m_units.clear();
    m_nextId = 1;

    // Build rectangular combat grid in axial coordinates
    // Flat-top offset: even columns shifted
    for (int col = 0; col < COLS; ++col) {
        for (int row = 0; row < ROWS; ++row) {
            // Convert offset to axial
            int q = col;
            int r = row - (col - (col & 1)) / 2;
            HexCoord h{q, r};

            CombatTile tile;
            tile.coord = h;
            m_tiles[h] = tile;
            m_coords.push_back(h);
        }
    }
}

CombatTile* CombatGrid::getTile(HexCoord h)
{
    auto it = m_tiles.find(h);
    return it != m_tiles.end() ? &it->second : nullptr;
}
const CombatTile* CombatGrid::getTile(HexCoord h) const
{
    auto it = m_tiles.find(h);
    return it != m_tiles.end() ? &it->second : nullptr;
}
bool CombatGrid::inBounds(HexCoord h) const { return m_tiles.count(h) > 0; }

CombatUnit* CombatGrid::getUnit(uint32_t id)
{
    for (auto& u : m_units) if (u.id == id) return &u;
    return nullptr;
}
const CombatUnit* CombatGrid::getUnit(uint32_t id) const
{
    for (const auto& u : m_units) if (u.id == id) return &u;
    return nullptr;
}
CombatUnit* CombatGrid::getUnitAt(HexCoord h)
{
    auto* tile = getTile(h);
    if (!tile || !tile->occupied) return nullptr;
    return getUnit(tile->unitId);
}
const CombatUnit* CombatGrid::getUnitAt(HexCoord h) const
{
    auto* tile = getTile(h);
    if (!tile || !tile->occupied) return nullptr;
    for (auto& u : m_units) if (u.id == tile->unitId) return &u;
    return nullptr;
}

uint32_t CombatGrid::addUnit(const CombatUnit& u)
{
    CombatUnit copy = u;
    copy.id = m_nextId++;
    m_units.push_back(copy);
    return copy.id;
}

bool CombatGrid::placeUnit(CombatUnit& unit, HexCoord h)
{
    auto* tile = getTile(h);
    if (!tile || tile->occupied || tile->type == CombatTileType::Obstacle) return false;
    tile->occupied = true;
    tile->unitId   = unit.id;
    unit.pos       = h;
    return true;
}

bool CombatGrid::moveUnit(uint32_t unitId, HexCoord to)
{
    CombatUnit* unit = getUnit(unitId);
    if (!unit) return false;

    // Clear old tile
    auto* oldTile = getTile(unit->pos);
    if (oldTile) { oldTile->occupied = false; oldTile->unitId = 0; }

    // Set new tile
    auto* newTile = getTile(to);
    if (!newTile || (newTile->occupied && newTile->unitId != unitId)) {
        // Restore old tile
        if (oldTile) { oldTile->occupied = true; oldTile->unitId = unitId; }
        return false;
    }
    newTile->occupied = true;
    newTile->unitId   = unitId;
    unit->pos         = to;
    return true;
}

void CombatGrid::removeDeadUnits()
{
    for (auto& unit : m_units) {
        if (!unit.alive) {
            auto* tile = getTile(unit.pos);
            if (tile) { tile->occupied = false; tile->unitId = 0; }
        }
    }
    m_units.erase(
        std::remove_if(m_units.begin(), m_units.end(),
            [](const CombatUnit& u){ return !u.alive; }),
        m_units.end());
}

// ── Reachable (BFS, respects obstacles and occupied tiles) ────────────────────
std::vector<HexCoord> CombatGrid::reachable(HexCoord from, int movePoints,
                                              bool flying) const
{
    struct Node { HexCoord h; int cost; };
    std::queue<Node> q;
    std::unordered_map<HexCoord, int, HexCoordHash> visited;

    q.push({from, 0});
    visited[from] = 0;

    std::vector<HexCoord> result;

    while (!q.empty()) {
        auto [cur, cost] = q.front(); q.pop();

        for (auto& nb : HexGrid::neighbors(cur)) {
            if (!inBounds(nb)) continue;

            const CombatTile* tile = getTile(nb);
            if (!tile) continue;
            if (tile->type == CombatTileType::Obstacle) continue;
            if (tile->type == CombatTileType::Wall && tile->wallHP > 0) continue;
            if (tile->occupied) continue; // blocked by another unit

            int stepCost = 1;
            if (!flying) {
                if (tile->type == CombatTileType::SpeedPenalty) stepCost = 2;
            }

            int newCost = cost + stepCost;
            if (newCost > movePoints) continue;

            auto it = visited.find(nb);
            if (it == visited.end() || newCost < it->second) {
                visited[nb] = newCost;
                q.push({nb, newCost});
                result.push_back(nb);
            }
        }
    }
    return result;
}

// ── Path on combat grid (A*) ──────────────────────────────────────────────────
std::vector<HexCoord> CombatGrid::findPath(HexCoord from, HexCoord to,
                                             bool flying) const
{
    if (from == to) return {};

    struct Node { int f; HexCoord h; bool operator>(const Node& o) const { return f > o.f; } };
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> open;
    std::unordered_map<HexCoord,HexCoord,HexCoordHash> cameFrom;
    std::unordered_map<HexCoord,int,HexCoordHash> g;

    g[from] = 0;
    open.push({HexGrid::distance(from,to), from});

    while (!open.empty()) {
        auto [f,cur] = open.top(); open.pop();
        if (cur == to) {
            std::vector<HexCoord> path;
            HexCoord c = to;
            while (!(c == from)) { path.push_back(c); c = cameFrom[c]; }
            std::reverse(path.begin(), path.end());
            return path;
        }
        int gCur = g.count(cur) ? g[cur] : 9999;
        for (auto& nb : HexGrid::neighbors(cur)) {
            if (!inBounds(nb)) continue;
            const CombatTile* tile = getTile(nb);
            if (!tile) continue;
            if (nb != to) {
                if (tile->type == CombatTileType::Obstacle) continue;
                if (tile->type == CombatTileType::Wall && tile->wallHP > 0) continue;
                if (tile->occupied) continue;
            }
            int tentG = gCur + 1;
            int prevG = g.count(nb) ? g[nb] : 9999;
            if (tentG < prevG) {
                cameFrom[nb] = cur;
                g[nb] = tentG;
                open.push({tentG + HexGrid::distance(nb,to), nb});
            }
        }
    }
    return {};
}

std::vector<HexCoord> CombatGrid::meleePositions(HexCoord target) const
{
    std::vector<HexCoord> result;
    for (auto& nb : HexGrid::neighbors(target)) {
        if (!inBounds(nb)) continue;
        const CombatTile* t = getTile(nb);
        if (t && !t->occupied && t->type != CombatTileType::Obstacle)
            result.push_back(nb);
    }
    return result;
}

void CombatGrid::placeSiegeWalls(int wallHP, int gateHP)
{
    // Vertical wall barrier at column 5 (middle of 11-col grid)
    // Center row (row 4) = gate with lower HP
    const int wallCol = 5;
    for (int row = 0; row < ROWS; ++row) {
        int q = wallCol;
        int r = row - (q - (q & 1)) / 2;
        HexCoord h{q, r};
        auto* tile = getTile(h);
        if (!tile) continue;
        tile->type   = CombatTileType::Wall;
        tile->wallHP = (row == ROWS / 2) ? gateHP : wallHP;
    }
}

bool CombatGrid::damageWall(HexCoord h, int damage)
{
    auto* tile = getTile(h);
    if (!tile || tile->type != CombatTileType::Wall) return false;
    tile->wallHP -= damage;
    if (tile->wallHP <= 0) {
        tile->wallHP = 0;
        tile->type   = CombatTileType::Normal; // breached
        return true;
    }
    return false;
}

HexCoord CombatGrid::gateHex() const
{
    const int wallCol = 5;
    int q = wallCol;
    int r = (ROWS / 2) - (q - (q & 1)) / 2;
    return {q, r};
}

bool CombatGrid::isWallTile(HexCoord h) const
{
    const auto* tile = getTile(h);
    return tile && tile->type == CombatTileType::Wall && tile->wallHP > 0;
}

void CombatGrid::setTileType(HexCoord h, CombatTileType type)
{
    auto* tile = getTile(h);
    if (tile) tile->type = type;
}

void CombatGrid::placeObstacleTiles(int count, uint32_t seed)
{
    std::mt19937 rng{seed};
    int placed = 0, attempts = 0;
    while (placed < count && attempts < 300) {
        ++attempts;
        int idx = static_cast<int>(rng() % static_cast<uint32_t>(m_coords.size()));
        HexCoord h = m_coords[idx];
        auto* tile = getTile(h);
        if (!tile || tile->type != CombatTileType::Normal) continue;
        // Keep cols 0-1 (player spawn) and cols 9-10 (enemy spawn) clear
        if (h.q < 2 || h.q > COLS - 3) continue;
        // Keep wall column clear in case of siege
        if (h.q == 5) continue;
        tile->type = CombatTileType::Obstacle;
        ++placed;
    }
}

void CombatGrid::placeRandomSpecialTiles(int count, uint32_t seed)
{
    std::mt19937 rng{seed};
    static const CombatTileType types[] = {
        CombatTileType::Attack,
        CombatTileType::Defense,
        CombatTileType::Speed,
        CombatTileType::SpeedPenalty,
    };
    int placed = 0;
    int attempts = 0;
    while (placed < count && attempts < 100) {
        ++attempts;
        int idx = static_cast<int>(rng() % static_cast<uint32_t>(m_coords.size()));
        HexCoord h = m_coords[idx];
        auto* tile = getTile(h);
        if (!tile || tile->type != CombatTileType::Normal) continue;

        // Don't place on far left or right columns (spawn zones)
        if (h.q < 1 || h.q > COLS - 2) continue;

        tile->type = types[rng() % 4];
        ++placed;
    }
}
