#include "MCTSHero.h"
#include "../ai/Pathfinder.h"
#include "../world/HexGrid.h"
#include "../combat/CombatEngine.h"
#include "../hero/SkillRegistry.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// ── Minimal LCG for rollout randomness (no global state) ─────────────────────
static uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }

// ── Internal helpers (mirrors FullGameSim internal logic) ─────────────────────
namespace {

static int unitHp(int defId, const std::vector<UnitDef>& udefs) {
    for (const auto& u : udefs) if (u.id == defId) return u.hp;
    return 10;
}
static int unitTier(int defId, const std::vector<UnitDef>& udefs) {
    for (const auto& u : udefs) if (u.id == defId) return u.tier;
    return 1;
}

static int heroStrength(const Hero& h, const std::vector<UnitDef>& udefs) {
    int s = 0;
    for (const auto& st : h.army)
        s += st.count * unitHp(st.defId, udefs) * (h.attack + 1) * unitTier(st.defId, udefs);
    return s;
}

static void moveToward(Hero& hero, HexCoord goal, HexMap& map) {
    auto costFn = [&](HexCoord c) -> int {
        const HexTile* t = map.getTile(c);
        if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
        return hero.moveCost(t->terrain);
    };
    auto path = Pathfinder::find(map, hero.pos, goal, costFn);
    for (HexCoord step : path) {
        const HexTile* t = map.getTile(step);
        if (!t) break;
        int cost = hero.moveCost(t->terrain);
        if (hero.movePool < cost) break;
        if (HexTile* old = map.getTile(hero.pos)) old->heroId = 0;
        hero.pos = step;
        hero.movePool -= cost;
        if (HexTile* nt = map.getTile(hero.pos)) nt->heroId = hero.id;
    }
}

static void collectAt(Hero& hero, GameSnapshot& s, HexMap& /*map*/) {
    for (auto& r : s.resources)
        if (r.pos == hero.pos && r.ownedBy != hero.id) r.ownedBy = hero.id;
    for (auto& obj : s.objects) {
        if (obj.collected || obj.pos != hero.pos) continue;
        obj.collected = true;
    }
    for (auto& t : s.towns) {
        if (t.pos == hero.pos && t.ownerId != hero.id) {
            if (t.ownerId == 0 || t.ownerId != hero.id)
                t.ownerId = hero.id;
        }
    }
}

// Recruit at nearest owned town if hero is on it.
static void recruitAt(Hero& hero, GameSnapshot& s, const std::vector<UnitDef>& udefs) {
    for (auto& t : s.towns) {
        if (t.ownerId != hero.id || t.pos != hero.pos) continue;
        for (auto& dw : t.dwellings) {
            if (dw.available <= 0) continue;
            for (const auto& ud : udefs) {
                if (ud.faction == t.faction && ud.tier == dw.tier && ud.path == dw.path) {
                    int n = dw.available; dw.available = 0;
                    bool merged = false;
                    for (auto& st : hero.army)
                        if (st.defId == ud.id) { st.count += n; merged = true; break; }
                    if (!merged && hero.army.size() < 7)
                        hero.army.push_back({ud.id, n});
                    break;
                }
            }
        }
    }
}

// Move hero toward best scored target for rolloutWeeks.
static void simHero(Hero& hero, Hero& opp, GameSnapshot& snap, HexMap& map,
                    const std::vector<UnitDef>& udefs, HexCoord fixedGoal,
                    bool useFixedGoal, int weeks)
{
    static const ResourceType kFacRes[9] = {
        ResourceType::FaithStones, ResourceType::FaithStones,
        ResourceType::VerdantSap,  ResourceType::Mercury,
        ResourceType::BloodEssence,ResourceType::Mercury,
        ResourceType::Iron,        ResourceType::BloodEssence,
        ResourceType::Gold,
    };
    int oppFIdx = static_cast<int>(opp.faction);
    ResourceType denialRes = (oppFIdx >= 0 && oppFIdx < 9) ? kFacRes[oppFIdx] : ResourceType::Gold;

    for (int w = 0; w < weeks; ++w) {
        hero.movePool = hero.maxMove;
        recruitAt(hero, snap, udefs);

        int myStr  = heroStrength(hero, udefs);
        int oppStr = heroStrength(opp, udefs);
        float strRatio = oppStr > 0 ? (float)myStr / oppStr : 99.f;
        bool hardRetreat = strRatio < 0.4f;

        HexCoord goal = fixedGoal;
        if (!useFixedGoal || w > 0) {
            // After first move, use score-based targeting
            struct C { HexCoord pos; float score; };
            std::vector<C> cands;
            auto add = [&](HexCoord pos, float val) {
                int d = std::max(1, HexGrid::distance(hero.pos, pos));
                cands.push_back({pos, val / d});
            };

            if (hardRetreat) {
                for (const auto& t : snap.towns)
                    if (t.ownerId == hero.id) add(t.pos, 500.f);
            } else {
                for (const auto& t : snap.towns) {
                    if (t.ownerId == 0)            add(t.pos, 150.f);
                    else if (t.ownerId != hero.id) add(t.pos, 200.f);
                    else {
                        bool hasU = false;
                        for (const auto& dw : t.dwellings) if (dw.available > 0) { hasU = true; break; }
                        if (hasU && (int)hero.army.size() < 7) add(t.pos, 250.f);
                    }
                }
                for (const auto& r : snap.resources) {
                    if (r.ownedBy == hero.id) continue;
                    add(r.pos, r.type == denialRes ? 120.f : 60.f);
                }
                for (const auto& obj : snap.objects) {
                    if (obj.collected) continue;
                    float val = 0.f;
                    if (obj.type == WorldObjectType::ArtifactChest)  val = 80.f;
                    else if (obj.type == WorldObjectType::TreasureChest) val = 70.f;
                    else if (obj.type == WorldObjectType::XPShrine ||
                             obj.type == WorldObjectType::ForestShrine ||
                             obj.type == WorldObjectType::StatShrine)    val = 50.f;
                    if (val > 0) add(obj.pos, val);
                }
                if (strRatio >= 0.6f) {
                    int dist = HexGrid::distance(hero.pos, opp.pos);
                    if (strRatio >= 1.2f || dist <= 8) add(opp.pos, 300.f);
                }
            }

            if (!cands.empty()) {
                auto best = std::max_element(cands.begin(), cands.end(),
                    [](const C& a, const C& b){ return a.score < b.score; });
                goal = best->pos;
            }
        }

        HexCoord prev = hero.pos;
        moveToward(hero, goal, map);
        if (hero.pos != prev) collectAt(hero, snap, map);
    }
}

// Resolve combat between h1 and h2, returns true if h1 wins.
static bool resolveCombat(Hero& h1, Hero& h2,
                           const std::vector<UnitDef>& udefs,
                           uint32_t seed)
{
    // Quick strength comparison with small RNG noise
    int s1 = heroStrength(h1, udefs);
    int s2 = heroStrength(h2, udefs);
    uint32_t r = seed;
    float noise = 0.85f + 0.3f * (float)(lcg(r) & 0xFF) / 255.f; // [0.85, 1.15]
    return (float)s1 * noise >= (float)s2;
}

} // namespace

// ── MCTSHero::rollout ─────────────────────────────────────────────────────────
float MCTSHero::rollout(
    HexCoord             goal,
    GameSnapshot         snap,
    HexMap&              map,
    const std::vector<UnitDef>& udefs,
    const BuildingRegistry& /*reg*/,
    const HeroClassRegistry& /*classReg*/,
    int                  rolloutWeeks,
    uint32_t             seed)
{
    Hero& hero = snap.hero;
    Hero& opp  = snap.opponent;

    // Simulate hero moving toward goal, opp using score-based
    for (int w = 0; w < rolloutWeeks; ++w) {
        bool useFixed = (w == 0);
        simHero(hero, opp, snap, map, udefs, goal, useFixed, 1);
        simHero(opp,  hero, snap, map, udefs, {}, false, 1);

        // Resolve combat if same hex
        if (hero.pos == opp.pos && !hero.army.empty() && !opp.army.empty()) {
            bool heroWins = resolveCombat(hero, opp, udefs, seed + w);
            if (heroWins) { opp.army.clear(); return 1.f; }
            else          { hero.army.clear(); return 0.f; }
        }
        if (hero.army.empty()) return 0.f;
        if (opp.army.empty())  return 1.f;
    }

    // Evaluate by strength ratio
    int s1 = heroStrength(hero, udefs);
    int s2 = heroStrength(opp,  udefs);
    if (s1 + s2 == 0) return 0.5f;
    return (float)s1 / (s1 + s2);
}

// ── MCTSHero::selectGoal ──────────────────────────────────────────────────────
HexCoord MCTSHero::selectGoal(
    const std::vector<HexCoord>& candidates,
    const GameSnapshot&          snap,
    HexMap&                      map,
    const std::vector<UnitDef>&  udefs,
    const BuildingRegistry&      reg,
    const HeroClassRegistry&     classReg,
    const Params&                params)
{
    if (candidates.empty()) return snap.hero.pos;
    if (candidates.size() == 1 || params.simulations <= 0)
        return candidates[0];

    // Limit candidates to top 5 to keep runtime sane
    int nCands = std::min((int)candidates.size(), 5);

    std::vector<float> scores(nCands, 0.f);
    uint32_t rng = static_cast<uint32_t>(params.seed ^ (snap.hero.pos.q * 31 + snap.hero.pos.r));

    for (int c = 0; c < nCands; ++c) {
        for (int s = 0; s < params.simulations; ++s) {
            uint32_t seed = lcg(rng);
            scores[c] += rollout(candidates[c], snap, map, udefs, reg, classReg,
                                 params.rolloutWeeks, seed);
        }
        scores[c] /= params.simulations;
    }

    int best = static_cast<int>(
        std::max_element(scores.begin(), scores.end()) - scores.begin());
    return candidates[best];
}
