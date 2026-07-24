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
    if (tile->type == CombatTileType::Wall && tile->wallHP > 0) return false;
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

    // Set new tile. Nothing may END a move on an obstacle or an intact wall
    // segment — findPath deliberately exempts its destination hex from those
    // checks (so units can path *toward* a wall or an occupied enemy and stop
    // short), which means this is the last line of defence against a unit
    // literally standing on the battlements ("units fight atop castle walls").
    auto* newTile = getTile(to);
    if (!newTile || (newTile->occupied && newTile->unitId != unitId)
        || newTile->type == CombatTileType::Obstacle
        || (newTile->type == CombatTileType::Wall && newTile->wallHP > 0)) {
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
            // Intact walls stop ground troops dead; flyers sail OVER them
            // (genre convention — wings are how you assault an unbreached
            // castle). Nobody may END a move on the battlements, so wall
            // tiles are traversed but never offered as destinations.
            bool wallTile = (tile->type == CombatTileType::Wall && tile->wallHP > 0);
            if (wallTile && !flying) continue;
            if (tile->occupied) continue; // blocked by another unit

            int stepCost = 1;
            if (!flying) {
                if (tile->type == CombatTileType::SpeedPenalty ||
                    tile->type == CombatTileType::Moat) stepCost = 2;
            }

            int newCost = cost + stepCost;
            if (newCost > movePoints) continue;

            auto it = visited.find(nb);
            if (it == visited.end() || newCost < it->second) {
                visited[nb] = newCost;
                q.push({nb, newCost});
                if (!wallTile) result.push_back(nb);
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
                // Flyers cross intact walls (mirrors reachable()); walkers
                // are stopped by them. The destination hex stays exempt so
                // callers can path TOWARD a wall/occupied hex and stop short
                // — landing legality is enforced by moveUnit/landableSteps.
                if (tile->type == CombatTileType::Wall && tile->wallHP > 0
                    && !flying) continue;
                if (tile->occupied) continue;
            }
            int tentG = gCur + ((!flying && tile->type == CombatTileType::Moat) ? 2 : 1);
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
        if (!t || t->occupied || t->type == CombatTileType::Obstacle) continue;
        // An intact wall segment is not a place to stand. Without this, a
        // melee attacker striking anything near the wall line (including the
        // gate, whose neighbours ARE wall hexes) would happily park itself
        // on the battlements — the "units fight atop castle walls" bug.
        if (t->type == CombatTileType::Wall && t->wallHP > 0) continue;
        result.push_back(nb);
    }
    return result;
}

int CombatGrid::landableSteps(const std::vector<HexCoord>& path, int steps) const
{
    // Farthest count of steps (<= steps) along `path` whose final hex a unit
    // may END on. Needed because flyers may CROSS intact walls mid-path and
    // findPath exempts its destination hex from wall/occupancy checks — so
    // the s-th hex of a valid path is not automatically a valid landing spot.
    for (int s = std::min<int>(steps, static_cast<int>(path.size())); s >= 1; --s) {
        const CombatTile* t = getTile(path[static_cast<size_t>(s) - 1]);
        if (!t) continue;
        if (t->type == CombatTileType::Obstacle) continue;
        if (t->type == CombatTileType::Wall && t->wallHP > 0) continue;
        if (t->occupied) continue;
        return s;
    }
    return 0;
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
    // Moat in front of the walls (attacker side, column 4): slow to cross
    // and -3 DEF while standing in it — assaulting the breach costs blood.
    const int moatCol = wallCol - 1;
    for (int row = 0; row < ROWS; ++row) {
        int q = moatCol;
        int r = row - (q - (q & 1)) / 2;
        if (auto* tile = getTile({q, r}))
            if (tile->type == CombatTileType::Normal)
                tile->type = CombatTileType::Moat;
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
