#include "../core/DevLog.h"
#include "WorldGen.h"
#include "Noise.h"
#include "HexGrid.h"
#include "../magic/SpellDef.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

// ── Terrain constants ─────────────────────────────────────────────────────────
static const Terrain kHomeTerrain[] = {
    Terrain::Sacred,          // HolyOrder      (0)
    Terrain::Highland,        // CrimsonWardens (1)
    Terrain::Forest,          // Thornkin       (2)
    Terrain::Toxic,           // EternalEmpire  (3)
    Terrain::Corrupted,       // Bloodsworn     (4)
    Terrain::CorruptedForest, // Voidkin        (5)
    Terrain::Industrial,      // IronAssembly   (6)
    Terrain::Wasteland,       // Amalgamate     (7)
    Terrain::Plains,          // Convergence    (8)
};

static const Terrain kNeutralTerrains[] = {
    Terrain::Volcanic,
    Terrain::Barren,
    Terrain::Rocky,
    Terrain::Swamp,
    Terrain::Wasteland,
    Terrain::Highland,
};
static constexpr int kNeutralTerrainCount = 6;

// ── Main entry point ──────────────────────────────────────────────────────────
WorldGenResult WorldGen::generate(HexMap& map, const WorldGenParams& p)
{
    gLog("WorldGen: seed=%u size=%d players=%d zone=%s\n",
           p.seed, static_cast<int>(p.size), p.playerCount,
           p.zoneBasedTerrain ? "yes" : "no");

    // 1. Fill map with noise-driven terrain
    passNoiseTerrain(map, p);

    // 1b. Apply shape constraints; returns bridge centres for JebusCross variants
    auto bridgeCentres = passShapeConstraints(map, p.shape);

    // 2. Pick player spawn zones
    auto spawnPos = pickSpawnPositions(map, p.playerCount, p.seed);

    uint32_t rng = p.seed ^ 0xDEADB00B;

    // Prepare zone data (used by both terrain and object passes)
    std::vector<HexCoord> neutralCenters;
    std::unordered_map<HexCoord, int, HexCoordHash> tileZones;
    std::vector<HexCoord> allCenters;
    std::vector<int>      zonePlayer;
    std::vector<Terrain>  zoneTerrain;

    if (p.zoneBasedTerrain) {
        tileZones = assignZones(map, spawnPos, neutralCenters, rng);

        // Build allCenters: player spawns first, then neutral centers
        allCenters = spawnPos;
        for (auto& nc : neutralCenters) allCenters.push_back(nc);

        // zonePlayer: player zones get index 0..N-1, neutral zones get -1
        for (int i = 0; i < (int)spawnPos.size(); ++i)
            zonePlayer.push_back(i);
        for (int i = 0; i < (int)neutralCenters.size(); ++i)
            zonePlayer.push_back(-1);

        // Zone terrain: player zones get home terrain, neutral zones get neutral terrains
        for (int i = 0; i < (int)spawnPos.size(); ++i)
            zoneTerrain.push_back(kHomeTerrain[i % 9]);
        for (int i = 0; i < (int)neutralCenters.size(); ++i)
            zoneTerrain.push_back(kNeutralTerrains[i % kNeutralTerrainCount]);

        passZoneTerrain(map, tileZones, zoneTerrain, rng);
    } else {
        passBiomeClusters(map, p, spawnPos);
    }

    // 3. Smooth coastlines
    passSmoothCoastlines(map, 2);
    passRemoveIsolatedWater(map);

    // 3b. Re-carve bridges after smoothing (smoothing may flood narrow land passages)
    if (!bridgeCentres.empty()) {
        for (const HexCoord& bc : bridgeCentres) {
            auto nearby = HexGrid::range(bc, 2);
            for (const HexCoord& nb : nearby) {
                if (HexTile* t = map.getTile(nb)) {
                    if (t->terrain == Terrain::Water)
                        t->terrain = Terrain::Highland;  // visually distinct pass terrain
                }
            }
        }
    }

    // 4. Place guaranteed player-zone mines (before global resource pass)
    uint32_t nextId = 1;
    WorldGenResult result;
    result.startPositions = spawnPos;

    if (p.zoneBasedTerrain) {
        placePlayerZoneMines(result, map, spawnPos, tileZones, p, nextId, rng);
    }

    // 5. Global resource placement
    auto resources = placeResources(map, p, nextId);
    result.resources = std::move(resources);

    // 6. Build towns
    buildTowns(result, map, spawnPos, nextId);

    // 7. World objects
    if (p.zoneBasedTerrain) {
        placeZoneObjects(result, map, allCenters, zonePlayer, tileZones, p, nextId, rng);
    } else {
        placeWorldObjects(result, map, p, nextId);
    }

    // 8. Chokepoint guards at bridge centres (JebusCross variants)
    if (!bridgeCentres.empty()) {
        uint32_t cgId = 8000;
        for (const HexCoord& bc : bridgeCentres) {
            WorldObject g;
            g.id   = cgId++;
            g.type = WorldObjectType::ChokeGuard;
            g.pos  = bc;
            g.value = 0;
            result.worldObjects.push_back(g);
        }

        // JebusCross3: 4 entry guards at the edges of the neutral centre zone
        if (p.shape == MapShape::JebusCross3) {
            int entryDist = map.radius() / 5 + 1;
            std::vector<HexCoord> entryPts = {
                { 0,          -entryDist },
                { 0,          +entryDist },
                { -entryDist,  0         },
                { +entryDist,  0         },
            };
            for (const HexCoord& ep : entryPts) {
                // Snap to nearest land tile if ep is in water
                HexCoord best = ep;
                if (const HexTile* t = map.getTile(ep); !t || t->terrain == Terrain::Water) {
                    auto ring = HexGrid::range(ep, 3);
                    for (const HexCoord& c : ring) {
                        if (const HexTile* ct = map.getTile(c);
                            ct && isLand(ct->terrain)) { best = c; break; }
                    }
                }
                WorldObject eg;
                eg.id   = cgId++;
                eg.type = WorldObjectType::ChokeGuard;
                eg.pos  = best;
                eg.value = 1;  // stronger variant for centre entries
                result.worldObjects.push_back(eg);
            }
        }
    }

    // 9. Shipyard placement for Jebus variants (one per player zone, coastal land)
    if (p.shape == MapShape::JebusCross || p.shape == MapShape::JebusCross3) {
        uint32_t syId = 9000;
        for (const HexCoord& spawn : spawnPos) {
            // Find a coastal land tile within 8-15 tiles of spawn adjacent to water
            auto candidates = HexGrid::range(spawn, 15);
            for (const HexCoord& c : candidates) {
                if (HexGrid::distance(c, spawn) < 8) continue;
                const HexTile* ct = map.getTile(c);
                if (!ct || !isLand(ct->terrain)) continue;
                bool coastal = false;
                for (const HexCoord& nb : HexGrid::range(c, 1)) {
                    const HexTile* nt = map.getTile(nb);
                    if (nt && nt->terrain == Terrain::Water) { coastal = true; break; }
                }
                if (!coastal) continue;
                bool taken = false;
                for (const auto& wo : result.worldObjects)
                    if (wo.pos == c) { taken = true; break; }
                if (taken) continue;
                WorldObject sy;
                sy.id   = syId++;
                sy.type = WorldObjectType::Shipyard;
                sy.pos  = c;
                sy.value = 0;  // no boats sold yet (per-player state in hero, not here)
                result.worldObjects.push_back(sy);
                break;
            }
        }
    }

    gLog("WorldGen: %zu towns, %zu resources, %zu world objects\n",
           result.towns.size(), result.resources.size(), result.worldObjects.size());
    return result;
}

// ── Pass 1: Noise terrain ─────────────────────────────────────────────────────
void WorldGen::passNoiseTerrain(HexMap& map, const WorldGenParams& p)
{
    Noise2D noise(p.seed);

    float scale = 2.0f / static_cast<float>(map.radius());
    float waterCutoff = -0.4f + p.waterRatio * 0.8f;

    HexGrid grid(40.f);

    const int maxR = map.radius();
    map.forEach([&](HexTile& tile) {
        int dist = HexGrid::distance(tile.coord, {0, 0});

        if (dist >= maxR - 3) {
            tile.terrain = Terrain::Water;
            return;
        }

        float wx, wy;
        grid.hexToWorld(tile.coord, wx, wy);

        float h = noise.fbm(wx * scale, wy * scale, 5, 2.0f, 0.5f);

        float r    = static_cast<float>(dist);
        float maxRf = static_cast<float>(maxR);
        float edge = 1.0f - (r / maxRf);
        float bias = 2.5f * (edge - 0.5f);
        h += bias * 0.55f;

        tile.terrain = heightToTerrain(h, waterCutoff);
    });
}

Terrain WorldGen::heightToTerrain(float h, float waterCutoff)
{
    if (h < waterCutoff - 0.05f) return Terrain::Water;
    if (h < waterCutoff + 0.02f) return Terrain::Swamp;
    if (h < waterCutoff + 0.12f) return Terrain::Plains;
    if (h < waterCutoff + 0.22f) return Terrain::Forest;
    if (h < waterCutoff + 0.35f) return Terrain::Highland;
    if (h < waterCutoff + 0.50f) return Terrain::Rocky;
    if (h < waterCutoff + 0.65f) return Terrain::Industrial;
    if (h < waterCutoff + 0.80f) return Terrain::Barren;
    return Terrain::Volcanic;
}

bool WorldGen::isLand(Terrain t)
{
    return t != Terrain::Water;
}

// ── Shape constraints: force water corridors or inner hole ────────────────────
std::vector<HexCoord> WorldGen::passShapeConstraints(HexMap& map, MapShape shape)
{
    std::vector<HexCoord> bridges; // returned for chokepoint guard placement

    if (shape == MapShape::Hexagon) return bridges;

    int R = map.radius();

    if (shape == MapShape::JebusCross || shape == MapShape::JebusCross3) {
        // Narrower corridors than original (R/10 instead of R/8)
        int W       = std::max(2, R / 10);
        int bridgeR = R / 3;  // distance from centre to each bridge
        // Bridge half-widths: 3 tiles across (|axis| <= 1), 5 tiles along (|other| <= 2)
        const int BW = 1;
        const int BL = 2;

        // Collect bridge centre positions
        bridges = {
            { 0,       -bridgeR },  // north bridge (upper q-arm)
            { 0,       +bridgeR },  // south bridge (lower q-arm)
            { -bridgeR, 0       },  // west bridge  (left  r-arm)
            { +bridgeR, 0       },  // east bridge  (right r-arm)
        };

        for (auto& c : map.coords()) {
            int q = c.q, r = c.r;
            bool inQarm = std::abs(q) <= W;
            bool inRarm = std::abs(r) <= W;
            if (!inQarm && !inRarm) continue;

            // Bridge exception: land passage within each arm at ±bridgeR
            if (inQarm) {
                bool onBridge = std::abs(q) <= BW &&
                                (std::abs(r + bridgeR) <= BL ||
                                 std::abs(r - bridgeR) <= BL);
                if (onBridge) continue;  // keep as land
            }
            if (inRarm) {
                bool onBridge = std::abs(r) <= BW &&
                                (std::abs(q + bridgeR) <= BL ||
                                 std::abs(q - bridgeR) <= BL);
                if (onBridge) continue;  // keep as land
            }

            if (HexTile* t = map.getTile(c))
                t->terrain = Terrain::Water;
        }

        // JebusCross3: sacred neutral centre zone (inner R/5 radius)
        if (shape == MapShape::JebusCross3) {
            int centreR = R / 5;
            for (auto& c : map.coords()) {
                int q = c.q, r = c.r, s = -q - r;
                int dist = std::max({std::abs(q), std::abs(r), std::abs(s)});
                if (dist <= centreR) {
                    if (HexTile* t = map.getTile(c))
                        t->terrain = Terrain::Sacred;
                }
            }
        }

    } else if (shape == MapShape::Ring) {
        int innerR = R * 2 / 5;
        for (auto& c : map.coords()) {
            int q = c.q, r = c.r, s = -q - r;
            int dist = std::max({std::abs(q), std::abs(r), std::abs(s)});
            if (dist <= innerR) {
                if (HexTile* t = map.getTile(c))
                    t->terrain = Terrain::Water;
            }
        }
    }

    return bridges;
}

// ── Pass 2a: Biome clusters (legacy path) ─────────────────────────────────────
void WorldGen::passBiomeClusters(HexMap& map, const WorldGenParams& p,
                                  const std::vector<HexCoord>& centers)
{
    uint32_t rng = p.seed ^ 0xDEADBEEF;
    int clusterRadius = map.radius() / 5 + 1;

    for (int i = 0; i < static_cast<int>(centers.size()); ++i) {
        Terrain home = kHomeTerrain[i % 9];
        auto cells = HexGrid::range(centers[i], clusterRadius);
        Noise2D cNoise(rng + static_cast<uint32_t>(i) * 13);

        for (auto& c : cells) {
            HexTile* tile = map.getTile(c);
            if (!tile || !isLand(tile->terrain)) continue;

            float dist = static_cast<float>(HexGrid::distance(c, centers[i]));
            float frac = 1.0f - dist / static_cast<float>(clusterRadius);
            float n = cNoise.sample(c.q * 0.3f, c.r * 0.3f) * 0.5f + 0.5f;
            if (n < frac * 0.8f)
                tile->terrain = home;
        }
        lcg(rng);
    }
}

// ── Pass 2b: Zone-based terrain assignment ────────────────────────────────────
std::unordered_map<HexCoord, int, HexCoordHash> WorldGen::assignZones(
    const HexMap& map,
    const std::vector<HexCoord>& playerSpawns,
    std::vector<HexCoord>& neutralCentersOut,
    uint32_t& rng)
{
    int nPlayers = static_cast<int>(playerSpawns.size());

    // Compute neutral zone centers between adjacent player pairs (by angle)
    int neutralCount = std::max(1, nPlayers / 2);
    neutralCentersOut.clear();

    // Sort players by angle from map center (0,0)
    std::vector<std::pair<float,int>> byAngle;
    for (int i = 0; i < nPlayers; ++i) {
        float angle = std::atan2((float)playerSpawns[i].r,
                                  (float)playerSpawns[i].q);
        byAngle.push_back({angle, i});
    }
    std::sort(byAngle.begin(), byAngle.end());

    for (int i = 0; i < neutralCount; ++i) {
        int ia = byAngle[i % nPlayers].second;
        int ib = byAngle[(i + 1) % nPlayers].second;

        // Midpoint in cube coords
        int qa = playerSpawns[ia].q, ra = playerSpawns[ia].r;
        int qb = playerSpawns[ib].q, rb = playerSpawns[ib].r;

        // Average and round
        HexCoord mid;
        mid.q = (qa + qb) / 2;
        mid.r = (ra + rb) / 2;

        // Find nearest land tile to that midpoint
        HexCoord best = mid;
        int bestDist = INT32_MAX;
        for (auto& c : map.coords()) {
            const HexTile* t = map.getTile(c);
            if (!t || !isLand(t->terrain)) continue;
            int d = HexGrid::distance(c, mid);
            if (d < bestDist) { bestDist = d; best = c; }
        }
        neutralCentersOut.push_back(best);
    }

    // Assign each land tile to nearest center (player zones 0..N-1, neutral N..N+M-1)
    std::vector<HexCoord> allCenters = playerSpawns;
    for (auto& nc : neutralCentersOut) allCenters.push_back(nc);
    int totalZones = static_cast<int>(allCenters.size());

    std::unordered_map<HexCoord, int, HexCoordHash> tileZones;
    tileZones.reserve(2048);

    for (auto& c : map.coords()) {
        const HexTile* tile = map.getTile(c);
        if (!tile || !isLand(tile->terrain)) {
            tileZones[c] = -1;
            continue;
        }
        int bestZ = 0;
        int bestD = INT32_MAX;
        for (int z = 0; z < totalZones; ++z) {
            int d = HexGrid::distance(c, allCenters[z]);
            if (d < bestD) { bestD = d; bestZ = z; }
        }
        tileZones[c] = bestZ;
    }

    (void)rng;
    return tileZones;
}

void WorldGen::passZoneTerrain(
    HexMap& map,
    const std::unordered_map<HexCoord, int, HexCoordHash>& tileZones,
    const std::vector<Terrain>& zoneTerrain,
    uint32_t& rng)
{
    // For each land tile, compute distance to nearest tile with a different zone
    // Then assign terrain based on that distance
    std::unordered_map<HexCoord, int, HexCoordHash> borderDist;
    borderDist.reserve(tileZones.size());

    for (auto& [c, zid] : tileZones) {
        if (zid < 0) { borderDist[c] = 0; continue; }
        auto nbrs = HexGrid::neighbors(c);
        bool isBorder = false;
        for (auto& n : nbrs) {
            auto it = tileZones.find(n);
            if (it == tileZones.end()) { isBorder = true; break; }
            if (it->second != zid) { isBorder = true; break; }
        }
        borderDist[c] = isBorder ? 1 : 2;  // 1=border, 2=interior (simplified)
    }

    for (auto& [c, zid] : tileZones) {
        if (zid < 0) continue;
        HexTile* tile = map.getTile(c);
        if (!tile || !isLand(tile->terrain)) continue;

        if (zid >= (int)zoneTerrain.size()) continue;
        Terrain zt = zoneTerrain[zid];

        int bd = borderDist[c];
        if (bd >= 2) {
            // Interior: always set to zone terrain
            tile->terrain = zt;
        } else {
            // Border band: 70% zone terrain, 30% keep noise
            uint32_t r = lcg(rng) % 100;
            if (r < 70) tile->terrain = zt;
        }
    }
}

// ── Pass 3: Smooth coastlines ─────────────────────────────────────────────────
void WorldGen::passSmoothCoastlines(HexMap& map, int iterations)
{
    for (int iter = 0; iter < iterations; ++iter) {
        std::vector<std::pair<HexCoord, Terrain>> changes;

        for (auto c : map.coords()) {
            HexTile* tile = map.getTile(c);
            if (!tile) continue;

            auto neighbors = HexGrid::neighbors(c);
            int waterCount = 0, landCount = 0;
            Terrain commonLand = Terrain::Plains;

            for (auto n : neighbors) {
                const HexTile* nt = map.getTile(n);
                if (!nt) continue;
                if (nt->terrain == Terrain::Water) ++waterCount;
                else { ++landCount; commonLand = nt->terrain; }
            }

            if (tile->terrain == Terrain::Water && waterCount <= 1 && landCount >= 4)
                changes.push_back({c, commonLand});
            else if (isLand(tile->terrain) && landCount <= 1 && waterCount >= 4)
                changes.push_back({c, Terrain::Water});
        }

        for (auto& [coord, t] : changes)
            if (HexTile* tile = map.getTile(coord)) tile->terrain = t;
    }
}

// ── Pass 4: Remove isolated water specks ─────────────────────────────────────
void WorldGen::passRemoveIsolatedWater(HexMap& map)
{
    for (auto c : map.coords()) {
        HexTile* tile = map.getTile(c);
        if (!tile || tile->terrain != Terrain::Water) continue;

        auto neighbors = HexGrid::neighbors(c);
        bool allLand = true;
        for (auto n : neighbors) {
            const HexTile* nt = map.getTile(n);
            if (!nt || nt->terrain == Terrain::Water) { allLand = false; break; }
        }
        if (allLand) tile->terrain = Terrain::Swamp;
    }
}

// ── Spawn position selection ──────────────────────────────────────────────────
std::vector<HexCoord> WorldGen::pickSpawnPositions(const HexMap& map,
                                                    int count, uint32_t seed)
{
    std::vector<HexCoord> candidates, result;
    int radius = map.radius();

    for (auto c : map.coords()) {
        const HexTile* tile = map.getTile(c);
        if (!tile || !isLand(tile->terrain)) continue;
        int d = HexGrid::distance(c, {0, 0});
        if (d < radius / 4 || d > radius * 3 / 4) continue;
        candidates.push_back(c);
    }

    if (candidates.empty()) {
        for (int i = 0; i < count; ++i) {
            float angle = 2.0f * 3.14159f * i / count;
            int q = static_cast<int>(std::round(radius * 0.6f * std::cos(angle)));
            int r = static_cast<int>(std::round(radius * 0.6f * std::sin(angle)));
            result.push_back({q, r});
        }
        return result;
    }

    uint32_t rng = seed ^ 0xBEEF1234;

    if (count <= 1) {
        uint32_t idx = lcg(rng) % static_cast<uint32_t>(candidates.size());
        result.push_back(candidates[idx]);
        return result;
    }

    {
        uint32_t idx = lcg(rng) % static_cast<uint32_t>(candidates.size());
        result.push_back(candidates[idx]);
    }

    while (static_cast<int>(result.size()) < count) {
        HexCoord best = candidates[0];
        int bestDist = 0;

        for (auto& c : candidates) {
            int minD = INT32_MAX;
            for (auto& r : result) minD = std::min(minD, HexGrid::distance(c, r));
            if (minD > bestDist) { bestDist = minD; best = c; }
        }
        result.push_back(best);
    }

    return result;
}

// ── Resource placement ────────────────────────────────────────────────────────
std::vector<ResourceNode> WorldGen::placeResources(HexMap& map,
                                                    const WorldGenParams& p,
                                                    uint32_t& nextId)
{
    std::vector<ResourceNode> nodes;
    uint32_t rng = p.seed ^ 0xCAFE0000;
    int radius = map.radius();

    int baseCount = static_cast<int>(radius * radius * 0.08f * p.resourceDensity);
    baseCount = std::max(baseCount, 6);

    auto allCoords = map.coords();
    for (size_t i = allCoords.size() - 1; i > 0; --i) {
        uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
        std::swap(allCoords[i], allCoords[j]);
    }

    int placed = 0;
    for (auto& c : allCoords) {
        if (placed >= baseCount) break;

        HexTile* tile = map.getTile(c);
        if (!tile || !isLand(tile->terrain)) continue;
        if (tile->resourceId != 0) continue;

        bool tooClose = false;
        for (auto& n : nodes) {
            if (HexGrid::distance(c, n.pos) < 3) { tooClose = true; break; }
        }
        if (tooClose) continue;

        ResourceNode node;
        node.id     = nextId++;
        node.pos    = c;
        node.type   = terrainResource(tile->terrain, lcg(rng));
        if (node.type == ResourceType::Gold)
            node.amount = 250;
        else
            node.amount = 2 + static_cast<int>(lcg(rng) % 4);

        tile->resourceId = node.id;
        nodes.push_back(node);
        ++placed;
    }

    return nodes;
}

ResourceType WorldGen::terrainResource(Terrain t, uint32_t rng)
{
    switch (t) {
        case Terrain::Sacred:
        case Terrain::Plains:       return ResourceType::FaithStones;
        case Terrain::Corrupted:
        case Terrain::Toxic:        return ResourceType::BloodEssence;
        case Terrain::Forest:
        case Terrain::CorruptedForest: return ResourceType::VerdantSap;
        case Terrain::Industrial:
        case Terrain::Rocky:        return ResourceType::Iron;
        case Terrain::Swamp:        return ResourceType::Mercury;
        case Terrain::Highland:     return (rng & 1) ? ResourceType::Iron
                                                     : ResourceType::Gold;
        default:                    return ResourceType::Gold;
    }
}

// ── Faction resource per player index ─────────────────────────────────────────
static ResourceType factionPrimaryResource(int playerIdx)
{
    // Matches faction order in kHomeTerrain
    // HO/CW=FaithStones, TK/VK=VerdantSap, EE/CV=Mercury, BS/AM=BloodEssence, IA=Iron
    switch (playerIdx % 9) {
        case 0: case 1: return ResourceType::FaithStones;   // HolyOrder, CrimsonWardens
        case 2: case 5: return ResourceType::VerdantSap;    // Thornkin, Voidkin
        case 3: case 8: return ResourceType::Mercury;       // EternalEmpire, Convergence
        case 4: case 7: return ResourceType::BloodEssence;  // Bloodsworn, Amalgamate
        case 6: default: return ResourceType::Iron;          // IronAssembly
    }
}

// ── Player-zone mine placement ────────────────────────────────────────────────
void WorldGen::placePlayerZoneMines(
    WorldGenResult& result, HexMap& map,
    const std::vector<HexCoord>& spawns,
    const std::unordered_map<HexCoord, int, HexCoordHash>& tileZones,
    const WorldGenParams& p, uint32_t& nextId, uint32_t& rng)
{
    for (int pi = 0; pi < (int)spawns.size(); ++pi) {
        HexCoord spawn = spawns[pi];
        ResourceType factionRes = factionPrimaryResource(pi);
        std::vector<HexCoord> placed;

        // Shuffle map coords for picking
        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }

        // 2 faction-resource mines, 3-6 tiles from spawn, same zone, min 3 apart
        int factionMinesPlaced = 0;
        for (auto& c : allCoords) {
            if (factionMinesPlaced >= 2) break;
            int d = HexGrid::distance(c, spawn);
            if (d < 3 || d > 6) continue;
            HexTile* tile = map.getTile(c);
            if (!tile || !isLand(tile->terrain)) continue;
            if (tile->resourceId != 0) continue;

            // Same zone
            auto it = tileZones.find(c);
            if (it == tileZones.end() || it->second != pi) continue;

            // Min 3 apart from other placed mines
            bool tooClose = false;
            for (auto& pp : placed)
                if (HexGrid::distance(c, pp) < 3) { tooClose = true; break; }
            if (tooClose) continue;

            ResourceNode node;
            node.id     = nextId++;
            node.pos    = c;
            node.type   = factionRes;
            node.amount = (factionRes == ResourceType::Gold) ? 250
                        : 2 + static_cast<int>(lcg(rng) % 4);
            tile->resourceId = node.id;
            result.resources.push_back(node);
            placed.push_back(c);
            ++factionMinesPlaced;
        }

        // 1 gold mine, 4-7 tiles from spawn, min 3 from any placed mine
        for (auto& c : allCoords) {
            int d = HexGrid::distance(c, spawn);
            if (d < 4 || d > 7) continue;
            HexTile* tile = map.getTile(c);
            if (!tile || !isLand(tile->terrain)) continue;
            if (tile->resourceId != 0) continue;

            bool tooClose = false;
            for (auto& pp : placed)
                if (HexGrid::distance(c, pp) < 3) { tooClose = true; break; }
            if (tooClose) continue;

            ResourceNode node;
            node.id     = nextId++;
            node.pos    = c;
            node.type   = ResourceType::Gold;
            node.amount = 250;
            tile->resourceId = node.id;
            result.resources.push_back(node);
            placed.push_back(c);
            break;
        }
    }
}

// ── Town building ─────────────────────────────────────────────────────────────
void WorldGen::buildTowns(WorldGenResult& result,
                           const HexMap& map,
                           const std::vector<HexCoord>& positions,
                           uint32_t& nextId)
{
    static const FactionId kFactions[] = {
        FactionId::HolyOrder,   FactionId::CrimsonWardens,
        FactionId::Thornkin,    FactionId::EternalEmpire,
        FactionId::Bloodsworn,  FactionId::Voidkin,
        FactionId::IronAssembly,FactionId::Amalgamate,
        FactionId::Convergence,
    };

    static const char* kTownNames[] = {
        "Sanctuary",   "Ironhold",   "Thornwald",  "Greyspire",
        "Crimsongate", "Voidhaven",  "Forge City", "The Meld",
        "The Nexus",
    };

    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        Town t;
        t.id      = nextId++;
        t.name    = kTownNames[i % 9];
        t.faction = kFactions[i % 9];
        t.pos     = positions[i];
        t.ownerId = static_cast<uint32_t>(i + 1);
        result.towns.push_back(t);
    }
}

bool WorldGen::isSuitable(const HexMap& map, HexCoord c, int minDist,
                           const std::vector<HexCoord>& occupied)
{
    const HexTile* tile = map.getTile(c);
    if (!tile || !isLand(tile->terrain)) return false;
    for (auto& o : occupied)
        if (HexGrid::distance(c, o) < minDist) return false;
    return true;
}

// ── Zone-based world object placement ─────────────────────────────────────────
void WorldGen::placeZoneObjects(
    WorldGenResult& result, HexMap& map,
    const std::vector<HexCoord>& allCenters,
    const std::vector<int>& zonePlayer,
    const std::unordered_map<HexCoord, int, HexCoordHash>& tileZones,
    const WorldGenParams& p, uint32_t& nextId, uint32_t& rng)
{
    std::vector<HexCoord> occupied;
    for (const auto& t : result.towns)     occupied.push_back(t.pos);
    for (const auto& r : result.resources) occupied.push_back(r.pos);

    auto tryPlace = [&](WorldObject& obj, int minDist) -> bool {
        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }
        for (auto& c : allCoords) {
            if (!isSuitable(map, c, minDist, occupied)) continue;
            bool used = false;
            for (const auto& wo : result.worldObjects)
                if (wo.pos == c) { used = true; break; }
            if (used) continue;
            obj.pos = c;
            occupied.push_back(c);
            return true;
        }
        return false;
    };

    auto tryPlaceNear = [&](WorldObject& obj, HexCoord center,
                            int minR, int maxR, int minDist) -> bool {
        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }
        for (auto& c : allCoords) {
            int d = HexGrid::distance(c, center);
            if (d < minR || d > maxR) continue;
            if (!isSuitable(map, c, minDist, occupied)) continue;
            bool used = false;
            for (const auto& wo : result.worldObjects)
                if (wo.pos == c) { used = true; break; }
            if (used) continue;
            obj.pos = c;
            occupied.push_back(c);
            return true;
        }
        return false;
    };

    int nPlayers = 0;
    for (int zp : zonePlayer) if (zp >= 0) ++nPlayers;

    // ── Per-player zones ──────────────────────────────────────────────────────
    for (int zi = 0; zi < (int)allCenters.size(); ++zi) {
        if (zonePlayer[zi] < 0) continue; // skip neutral zones here
        int pi = zonePlayer[zi];
        HexCoord spawnCenter = allCenters[zi];

        // Determine zone's terrain for ambient object picking
        Terrain zt = (zi < (int)p.playerCount) ? kHomeTerrain[pi % 9] : Terrain::Plains;

        // 2 ambient terrain objects
        for (int n = 0; n < 2; ++n) {
            WorldObjectType ambType = WorldObjectType::XPShrine;
            int ambVal = 50;
            switch (zt) {
                case Terrain::Forest:        ambType = WorldObjectType::ForestShrine; ambVal = 75;  break;
                case Terrain::Sacred:        ambType = WorldObjectType::HolyFountain; ambVal = 0;   break;
                case Terrain::Barren:
                case Terrain::Wasteland:     ambType = WorldObjectType::Oasis;        ambVal = 0;   break;
                case Terrain::Plains:        ambType = WorldObjectType::Campfire;     ambVal = 150; break;
                case Terrain::Highland:
                case Terrain::Rocky:         ambType = WorldObjectType::HighlandRuin; ambVal = 4;   break;
                default:                     ambType = WorldObjectType::XPShrine;     ambVal = 50;  break;
            }

            WorldObject obj;
            obj.id    = nextId++;
            obj.type  = ambType;
            obj.value = ambVal;
            if (!tryPlaceNear(obj, spawnCenter, 3, 7, 3)) --nextId;
            else result.worldObjects.push_back(obj);
        }

        // 1 UnitDwelling (tier 1 or 2 of player's faction)
        {
            WorldObject obj;
            obj.id        = nextId++;
            obj.type      = WorldObjectType::UnitDwelling;
            obj.value     = 1 + static_cast<int>(lcg(rng) % 2); // tier 1 or 2
            obj.faction   = static_cast<uint8_t>(pi % 9);
            obj.available = 4 + obj.value * 2;
            if (!tryPlaceNear(obj, spawnCenter, 4, 7, 3)) --nextId;
            else result.worldObjects.push_back(obj);
        }
    }

    // ── Zone border areas ─────────────────────────────────────────────────────
    // Collect border tiles (tiles whose zone differs from at least one neighbor's zone)
    std::vector<HexCoord> borderTiles;
    for (auto& [c, zid] : tileZones) {
        if (zid < 0) continue;
        auto nbrs = HexGrid::neighbors(c);
        for (auto& n : nbrs) {
            auto it = tileZones.find(n);
            if (it == tileZones.end() || it->second != zid) {
                if (it != tileZones.end() && it->second >= 0) {
                    borderTiles.push_back(c);
                    break;
                }
            }
        }
    }

    // BanditCamp at zone-pair borders (1 per zone-pair, max nPlayers)
    int campCount = nPlayers;
    int campPlaced = 0;
    // Shuffle border tiles
    for (size_t i = borderTiles.size() > 1 ? borderTiles.size() - 1 : 0; i > 0; --i) {
        uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
        std::swap(borderTiles[i], borderTiles[j]);
    }
    for (auto& c : borderTiles) {
        if (campPlaced >= campCount) break;
        if (!isSuitable(map, c, 4, occupied)) continue;
        bool used = false;
        for (const auto& wo : result.worldObjects)
            if (wo.pos == c) { used = true; break; }
        if (used) continue;

        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::BanditCamp;
        obj.value = 1; // difficulty 1 at borders
        obj.pos   = c;
        occupied.push_back(c);
        result.worldObjects.push_back(obj);
        ++campPlaced;
    }

    // NeutralOutpost at borders: 1 per 2 players
    int outpostCount = std::max(1, nPlayers / 2);
    int outpostPlaced = 0;
    for (auto& c : borderTiles) {
        if (outpostPlaced >= outpostCount) break;
        if (!isSuitable(map, c, 5, occupied)) continue;
        bool used = false;
        for (const auto& wo : result.worldObjects)
            if (wo.pos == c) { used = true; break; }
        if (used) continue;

        WorldObject obj;
        obj.id      = nextId++;
        obj.type    = WorldObjectType::NeutralOutpost;
        obj.faction = static_cast<uint8_t>(lcg(rng) % 9);
        obj.value   = 1;
        obj.pos     = c;
        occupied.push_back(c);
        result.worldObjects.push_back(obj);
        ++outpostPlaced;
    }

    // ── Neutral zones ─────────────────────────────────────────────────────────
    for (int zi = 0; zi < (int)allCenters.size(); ++zi) {
        if (zonePlayer[zi] >= 0) continue; // skip player zones

        HexCoord nCenter = allCenters[zi];

        // 2-3 TreasureChests
        int chestCount = 2 + static_cast<int>(lcg(rng) % 2);
        for (int n = 0; n < chestCount; ++n) {
            WorldObject obj;
            obj.id         = nextId++;
            obj.type       = WorldObjectType::TreasureChest;
            obj.value      = 500 + static_cast<int>(lcg(rng) % 501);
            obj.questState = 300 + static_cast<int>(lcg(rng) % 301);
            obj.faction    = static_cast<uint8_t>(lcg(rng) % 3);
            if (!tryPlaceNear(obj, nCenter, 2, 8, 3)) --nextId;
            else result.worldObjects.push_back(obj);
        }

        // 1 Crypt
        {
            WorldObject obj;
            obj.id      = nextId++;
            obj.type    = WorldObjectType::Crypt;
            obj.value   = 2; // difficulty 2
            obj.faction = static_cast<uint8_t>(lcg(rng) % 9);
            if (!tryPlaceNear(obj, nCenter, 2, 8, 4)) --nextId;
            else result.worldObjects.push_back(obj);
        }

        // 1 WitchHut
        {
            static const int kWitchSkills[] = { 101, 102, 103, 104, 105, 106, 107, 108, 109, 110 };
            WorldObject obj;
            obj.id         = nextId++;
            obj.type       = WorldObjectType::WitchHut;
            obj.questState = kWitchSkills[lcg(rng) % 10];
            if (!tryPlaceNear(obj, nCenter, 2, 8, 4)) --nextId;
            else result.worldObjects.push_back(obj);
        }

        // 1 Utopia (if richNeutralZones)
        if (p.richNeutralZones) {
            WorldObject obj;
            obj.id      = nextId++;
            obj.type    = WorldObjectType::Utopia;
            obj.value   = static_cast<int>(lcg(rng));
            obj.faction = static_cast<uint8_t>(lcg(rng) % 9);
            if (!tryPlaceNear(obj, nCenter, 2, 8, 7)) --nextId;
            else result.worldObjects.push_back(obj);
        }
    }

    // ── Global placement (far from zone centers) ──────────────────────────────

    // 2 Observatories
    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::Observatory;
        obj.value = 5;
        if (!tryPlace(obj, map.radius() / 3)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    // 2 Trees of Knowledge
    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::TreeOfKnowledge;
        if (!tryPlace(obj, 6)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    // 3 Landmarks
    for (int i = 0; i < 3; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::Landmark;
        obj.value = 100 + i * 50;
        if (!tryPlace(obj, 5)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    // 2 CursedGrounds
    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id         = nextId++;
        obj.type       = WorldObjectType::CursedGround;
        obj.value      = 3;
        obj.questState = 5;
        if (!tryPlace(obj, 5)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    // 4 StatShrines (one each of stat type 0-3)
    for (int i = 0; i < 4; ++i) {
        WorldObject obj;
        obj.id         = nextId++;
        obj.type       = WorldObjectType::StatShrine;
        obj.value      = i;
        obj.questState = 3;
        if (!tryPlace(obj, 4)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    // 2 Stables
    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::Stables;
        obj.value = 3;
        if (!tryPlace(obj, 6)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    // UnitDwellings: factions 0-8, tiers 1-3 (global)
    for (int faction = 0; faction < 9; ++faction) {
        for (int tier = 1; tier <= 3; ++tier) {
            WorldObject obj;
            obj.id        = nextId++;
            obj.type      = WorldObjectType::UnitDwelling;
            obj.value     = tier;
            obj.faction   = static_cast<uint8_t>(faction);
            obj.available = 4 + tier * 2;
            if (tryPlace(obj, 3)) result.worldObjects.push_back(obj);
            else --nextId;
        }
    }

    // Terrain-specific ambient supplement from kTerrainSpecs
    struct TerrainObjSpec {
        Terrain         terrain;
        WorldObjectType type;
        int             value;
        ResourceType    rtype;
        int             count;
    };
    static const TerrainObjSpec kTerrainSpecs[] = {
        { Terrain::Forest,          WorldObjectType::ForestShrine,  75,                    ResourceType::Gold,         2 },
        { Terrain::Highland,        WorldObjectType::HighlandRuin,   4,                    ResourceType::Gold,         1 },
        { Terrain::Rocky,           WorldObjectType::HighlandRuin,   3,                    ResourceType::Gold,         1 },
        { Terrain::Sacred,          WorldObjectType::HolyFountain,   0,                    ResourceType::Gold,         1 },
        { Terrain::Barren,          WorldObjectType::Oasis,          0,                    ResourceType::Gold,         1 },
        { Terrain::Wasteland,       WorldObjectType::Oasis,          0,                    ResourceType::Gold,         1 },
        { Terrain::Plains,          WorldObjectType::Campfire,      150,                   ResourceType::Gold,         2 },
        { Terrain::Volcanic,        WorldObjectType::LavaCrystal,    3,                    ResourceType::Mercury,      2 },
        { Terrain::Swamp,           WorldObjectType::SwampAltar,    SPL::CURSE,            ResourceType::Gold,         1 },
        { Terrain::Corrupted,       WorldObjectType::SpellScroll,   SPL::DEATH_COIL,       ResourceType::Gold,         1 },
        { Terrain::CorruptedForest, WorldObjectType::SpellScroll,   SPL::VENOMOUS_CLOUD,   ResourceType::Gold,         1 },
        { Terrain::Industrial,      WorldObjectType::ResourceCache,  250,                   ResourceType::Iron,         1 },
        { Terrain::Toxic,           WorldObjectType::ResourceCache,    5,                   ResourceType::BloodEssence, 1 },
    };

    auto tryPlaceOnTerrain = [&](WorldObject& obj, Terrain terrain, int minDist) -> bool {
        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }
        for (auto& c : allCoords) {
            const HexTile* t = map.getTile(c);
            if (!t || t->terrain != terrain) continue;
            if (!isSuitable(map, c, minDist, occupied)) continue;
            bool used = false;
            for (const auto& wo : result.worldObjects)
                if (wo.pos == c) { used = true; break; }
            if (used) continue;
            obj.pos = c;
            occupied.push_back(c);
            return true;
        }
        return false;
    };

    for (const auto& spec : kTerrainSpecs) {
        for (int n = 0; n < spec.count; ++n) {
            WorldObject obj;
            obj.id           = nextId++;
            obj.type         = spec.type;
            obj.value        = spec.value;
            obj.resourceType = spec.rtype;
            if (tryPlaceOnTerrain(obj, spec.terrain, 3))
                result.worldObjects.push_back(obj);
            else
                --nextId;
        }
    }

    // 2 QuestGiver pairs
    for (int q = 0; q < 2; ++q) {
        WorldObject giver;
        giver.id   = nextId++;
        giver.type = WorldObjectType::QuestGiver;
        giver.questState = 0;

        if (!tryPlace(giver, 5)) continue;

        WorldObject target;
        target.id   = nextId++;
        target.type = WorldObjectType::QuestTarget;

        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }
        bool found = false;
        for (auto& c : allCoords) {
            if (HexGrid::distance(c, giver.pos) < 8) continue;
            if (!isSuitable(map, c, 4, occupied)) continue;
            bool used = false;
            for (const auto& wo : result.worldObjects)
                if (wo.pos == c) { used = true; break; }
            if (used) continue;
            target.pos = c;
            occupied.push_back(c);
            found = true;
            break;
        }
        if (!found) { nextId -= 2; continue; }

        giver.linkedId  = target.id;
        target.linkedId = giver.id;

        result.worldObjects.push_back(giver);
        result.worldObjects.push_back(target);
    }
}

// ── Legacy world object placement (used when zoneBasedTerrain=false) ──────────
void WorldGen::placeWorldObjects(WorldGenResult& result, HexMap& map,
                                  const WorldGenParams& p, uint32_t& nextId)
{
    uint32_t rng = p.seed ^ 0xDEADB00B;
    int radius = map.radius();

    std::vector<HexCoord> occupied;
    for (const auto& t : result.towns)    occupied.push_back(t.pos);
    for (const auto& r : result.resources) occupied.push_back(r.pos);

    auto tryPlace = [&](WorldObject& obj, int minDist) -> bool {
        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }
        for (auto& c : allCoords) {
            if (!isSuitable(map, c, minDist, occupied)) continue;
            bool used = false;
            for (const auto& wo : result.worldObjects)
                if (wo.pos == c) { used = true; break; }
            if (used) continue;
            obj.pos = c;
            occupied.push_back(c);
            return true;
        }
        return false;
    };

    {
        int chestCount = 8 + static_cast<int>(lcg(rng) % 3);
        static const int kStatTypes[] = { 0, 1, 2 };
        for (int i = 0; i < chestCount; ++i) {
            WorldObject obj;
            obj.id         = nextId++;
            obj.type       = WorldObjectType::TreasureChest;
            obj.value      = 500 + static_cast<int>(lcg(rng) % 501);
            obj.questState = 300 + static_cast<int>(lcg(rng) % 301);
            obj.faction    = static_cast<uint8_t>(kStatTypes[lcg(rng) % 3]);
            if (tryPlace(obj, 3)) result.worldObjects.push_back(obj);
        }
    }

    for (int i = 0; i < 5; ++i) {
        WorldObject obj;
        obj.id      = nextId++;
        obj.type    = WorldObjectType::Crypt;
        obj.value   = 1 + static_cast<int>(lcg(rng) % 3);
        obj.faction = static_cast<uint8_t>(lcg(rng) % 9u);
        if (!tryPlace(obj, 4)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 3; ++i) {
        WorldObject obj;
        obj.id      = nextId++;
        obj.type    = WorldObjectType::Utopia;
        obj.value   = static_cast<int>(lcg(rng));
        obj.faction = static_cast<uint8_t>(lcg(rng) % 9u);
        if (!tryPlace(obj, 7)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::Observatory;
        obj.value = 5;
        if (tryPlace(obj, radius / 3)) result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 6; ++i) {
        WorldObject obj;
        obj.id         = nextId++;
        obj.type       = WorldObjectType::StatShrine;
        obj.value      = i % 6;
        obj.questState = 3;
        if (tryPlace(obj, 4)) result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 3; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::BanditCamp;
        obj.value = 1 + static_cast<int>(lcg(rng) % 3);
        if (tryPlace(obj, 4)) result.worldObjects.push_back(obj);
    }

    static const int kWitchSkills[] = { 101, 102, 103, 104, 105, 106, 107, 108, 109, 110 };
    for (int i = 0; i < 4; ++i) {
        WorldObject obj;
        obj.id         = nextId++;
        obj.type       = WorldObjectType::WitchHut;
        obj.questState = kWitchSkills[lcg(rng) % 10];
        if (!tryPlace(obj, 4)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::Stables;
        obj.value = 3;
        if (!tryPlace(obj, 6)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::TreeOfKnowledge;
        if (!tryPlace(obj, 6)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 3; ++i) {
        WorldObject obj;
        obj.id    = nextId++;
        obj.type  = WorldObjectType::Landmark;
        obj.value = 100 + i * 50;
        if (!tryPlace(obj, 5)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 2; ++i) {
        WorldObject obj;
        obj.id         = nextId++;
        obj.type       = WorldObjectType::CursedGround;
        obj.value      = 3;
        obj.questState = 5;
        if (!tryPlace(obj, 5)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int i = 0; i < 3; ++i) {
        WorldObject obj;
        obj.id      = nextId++;
        obj.type    = WorldObjectType::NeutralOutpost;
        obj.faction = static_cast<uint8_t>(lcg(rng) % 9);
        obj.value   = 1;
        if (!tryPlace(obj, 5)) --nextId;
        else result.worldObjects.push_back(obj);
    }

    for (int faction = 0; faction < 9; ++faction) {
        for (int tier = 1; tier <= 3; ++tier) {
            WorldObject obj;
            obj.id        = nextId++;
            obj.type      = WorldObjectType::UnitDwelling;
            obj.value     = tier;
            obj.faction   = static_cast<uint8_t>(faction);
            obj.available = 4 + tier * 2;
            if (tryPlace(obj, 3)) result.worldObjects.push_back(obj);
        }
    }

    for (int q = 0; q < 2; ++q) {
        WorldObject giver;
        giver.id   = nextId++;
        giver.type = WorldObjectType::QuestGiver;
        giver.questState = 0;

        if (!tryPlace(giver, 5)) continue;

        WorldObject target;
        target.id   = nextId++;
        target.type = WorldObjectType::QuestTarget;

        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }
        bool found = false;
        for (auto& c : allCoords) {
            if (HexGrid::distance(c, giver.pos) < 8) continue;
            if (!isSuitable(map, c, 4, occupied)) continue;
            bool used = false;
            for (const auto& wo : result.worldObjects)
                if (wo.pos == c) { used = true; break; }
            if (used) continue;
            target.pos = c;
            occupied.push_back(c);
            found = true;
            break;
        }
        if (!found) { nextId -= 2; continue; }

        giver.linkedId  = target.id;
        target.linkedId = giver.id;

        result.worldObjects.push_back(giver);
        result.worldObjects.push_back(target);
    }

    struct TerrainObjSpec {
        Terrain         terrain;
        WorldObjectType type;
        int             value;
        ResourceType    rtype;
        int             count;
    };
    static const TerrainObjSpec kTerrainSpecs[] = {
        { Terrain::Forest,          WorldObjectType::ForestShrine,  75,                    ResourceType::Gold,         3 },
        { Terrain::Highland,        WorldObjectType::HighlandRuin,   4,                    ResourceType::Gold,         2 },
        { Terrain::Rocky,           WorldObjectType::HighlandRuin,   3,                    ResourceType::Gold,         2 },
        { Terrain::Sacred,          WorldObjectType::HolyFountain,   0,                    ResourceType::Gold,         2 },
        { Terrain::Barren,          WorldObjectType::Oasis,          0,                    ResourceType::Gold,         2 },
        { Terrain::Wasteland,       WorldObjectType::Oasis,          0,                    ResourceType::Gold,         1 },
        { Terrain::Plains,          WorldObjectType::Campfire,      150,                   ResourceType::Gold,         4 },
        { Terrain::Volcanic,        WorldObjectType::LavaCrystal,    3,                    ResourceType::Mercury,      2 },
        { Terrain::Swamp,           WorldObjectType::SwampAltar,    SPL::CURSE,            ResourceType::Gold,         2 },
        { Terrain::Corrupted,       WorldObjectType::SpellScroll,   SPL::DEATH_COIL,      ResourceType::Gold,         2 },
        { Terrain::CorruptedForest, WorldObjectType::SpellScroll,   SPL::VENOMOUS_CLOUD,  ResourceType::Gold,         2 },
        { Terrain::Industrial,      WorldObjectType::ResourceCache,  250,                   ResourceType::Iron,         2 },
        { Terrain::Toxic,           WorldObjectType::ResourceCache,    5,                   ResourceType::BloodEssence, 2 },
    };

    auto tryPlaceOnTerrain = [&](WorldObject& obj, Terrain terrain, int minDist) -> bool {
        auto allCoords = map.coords();
        for (size_t i = allCoords.size() - 1; i > 0; --i) {
            uint32_t j = lcg(rng) % static_cast<uint32_t>(i + 1);
            std::swap(allCoords[i], allCoords[j]);
        }
        for (auto& c : allCoords) {
            const HexTile* t = map.getTile(c);
            if (!t || t->terrain != terrain) continue;
            if (!isSuitable(map, c, minDist, occupied)) continue;
            bool used = false;
            for (const auto& wo : result.worldObjects)
                if (wo.pos == c) { used = true; break; }
            if (used) continue;
            obj.pos = c;
            occupied.push_back(c);
            return true;
        }
        return false;
    };

    for (const auto& spec : kTerrainSpecs) {
        for (int n = 0; n < spec.count; ++n) {
            WorldObject obj;
            obj.id           = nextId++;
            obj.type         = spec.type;
            obj.value        = spec.value;
            obj.resourceType = spec.rtype;
            if (tryPlaceOnTerrain(obj, spec.terrain, 3))
                result.worldObjects.push_back(obj);
            else
                --nextId;
        }
    }
}
