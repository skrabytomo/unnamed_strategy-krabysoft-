#include "FullGameSim.h"
#include "../world/WorldGen.h"
#include "../world/HexMap.h"
#include "../world/HexGrid.h"
#include "../town/BuildingRegistry.h"
#include "../combat/CombatEngine.h"
#include "../hero/Hero.h"
#include "../hero/HeroClass.h"
#include "../hero/SkillRegistry.h"
#include "../ai/Pathfinder.h"
#include "../data/ResourceNode.h"
#include "../data/Resources.h"
#include "../core/DevLog.h"
#include <algorithm>
#include <cstdio>

// ── Build priority order per faction (matches Game_WorldMap.cpp kBuildOrder) ──
#include "../town/BuildingRegistry.h"
namespace {

using namespace BID;

static const std::vector<int> kBuildOrder[9] = {
    { HO_HALL, HO_T1_BASE, FORT, MARKET, MAGE_GUILD, HO_LIGHT_SHRINE, HO_T2_BASE,
      TOWN_HALL, HO_T3_BASE, HO_T3_A, MAGE_GUILD_T2, HO_T4_BASE, HO_T4_A,
      CITY_HALL, HO_T5_BASE, HO_T5_A, HO_RELIQUARY, HO_T6_BASE, HO_T6_A },
    { CW_HALL, CW_T1, MARKET, CW_T2, CW_T2_A, FORT, CW_WARDEN_BRAND, CW_T3, CW_T3_A,
      TOWN_HALL, CW_DEATH_ALTAR, CW_T4, CW_T4_A, CITY_HALL, CW_T5, CW_T5_A, CW_T6 },
    { TK_GROVE_HEART, TK_T1, TK_SYMBIOSIS_WEB, MARKET, TK_T2, TK_T2_A,
      TK_ANCIENT_CIRCLE, FORT, TK_T3, TK_T3_A, TOWN_HALL, TK_T4, TK_T4_A,
      CITY_HALL, TK_T5, TK_T5_A, TK_T6 },
    { EE_THRONE, EE_T1, EE_NECROPOLIS, FORT, EE_T2, MARKET, EE_MONUMENT,
      EE_T3, EE_T3_A, TOWN_HALL, EE_T4, EE_T4_A, MAGE_GUILD, CITY_HALL,
      EE_T5, EE_T5_A, EE_T6 },
    { BS_WAR_HALL, BS_T1, BS_T1_A, BS_T2, BS_T2_A, FORT, MARKET, BS_T3, BS_T3_A,
      BS_BLOOD_ALTAR, BS_WAR_SHRINE, TOWN_HALL, BS_T4, BS_T4_A, CITY_HALL,
      BS_T5, BS_T5_A, BS_T6 },
    { VK_NEXUS, MARKET, VK_T1, MAGE_GUILD, VK_T2, VK_T2_A, FORT, VK_RIFT_GATE,
      VK_T3, VK_T3_A, TOWN_HALL, VK_VOID_LENS, VK_T4, VK_T4_A, CITY_HALL,
      VK_T5, VK_T5_A, VK_T6 },
    { IA_FORGE_HALL, FORT, IA_T1, IA_T1_A, IA_BLUEPRINT_VAULT, MARKET, IA_T2, IA_T2_A,
      WAREHOUSE, IA_OVERCLOCK, IA_T3, IA_T3_A, TOWN_HALL, WAREHOUSE_T2, IA_T4, IA_T4_A,
      CITY_HALL, IA_T5, IA_T5_A, IA_T6 },
    { AM_GRAFTING_HALL, AM_T1, AM_T1_A, MARKET, AM_T2, AM_T2_A, FORT, AM_MERGE_CHAMBER,
      AM_T3, AM_T3_A, TOWN_HALL, AM_FLESH_VAULT, AM_T4, AM_T4_A, CITY_HALL,
      AM_T5, AM_T5_A, AM_T6 },
    { CV_SYNTHESIS_HUB, MARKET, FORT, MAGE_GUILD, TOWN_HALL, CITY_HALL,
      CV_RESONANCE_WELL, CV_MIRROR_CHAMBER },
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static int heroStrength(const Hero& h, const std::vector<UnitDef>& udefs)
{
    int s = 0;
    for (const auto& st : h.army) {
        int tier = 1;
        for (const auto& ud : udefs)
            if (ud.id == st.defId) { tier = ud.tier; break; }
        int hp = 10;
        for (const auto& ud : udefs)
            if (ud.id == st.defId) { hp = ud.hp; break; }
        s += st.count * hp * (h.attack + 1) * tier;
    }
    return s;
}

static void aiLearnSkill(Hero& hero, const HeroClassRegistry& reg)
{
    const HeroClassDef* cls = reg.getClass(hero.classId);
    if (!cls || cls->skillPool.empty()) return;
    for (int sid : cls->skillPool) {
        if (SkillInstance* s = hero.skills.getSkill(sid)) {
            if (s->canUpgrade()) {
                int prevIdx = static_cast<int>(s->tier);
                s->upgrade();
                if (const SkillDef* def = findSkillDef(sid)) {
                    int delta = def->values[prevIdx + 1] - def->values[prevIdx];
                    if (def->effectType == SkillEffectType::MovementBonus)
                        { hero.maxMove += delta; hero.movePool = hero.maxMove; }
                    else if (def->effectType == SkillEffectType::VisionBonus)
                        hero.visionRange += delta;
                }
                return;
            }
        }
    }
    for (int sid : cls->skillPool) {
        if (!hero.skills.hasSkill(sid) && hero.skills.canLearn(sid)) {
            hero.skills.learn(sid);
            if (const SkillDef* def = findSkillDef(sid)) {
                if (def->effectType == SkillEffectType::MovementBonus)
                    { hero.maxMove += def->values[0]; hero.movePool = hero.maxMove; }
                else if (def->effectType == SkillEffectType::VisionBonus)
                    hero.visionRange += def->values[0];
            }
            return;
        }
    }
}

static Hero makeHero(FactionId faction, uint32_t heroId, HexCoord pos,
                     const HeroClassRegistry& reg, const std::vector<UnitDef>& udefs)
{
    Hero h;
    h.id      = heroId;
    h.faction = faction;
    h.pos     = pos;
    h.maxMove = 20;
    h.movePool= 20;
    h.attack  = 2;
    h.defense = 2;
    h.level   = 1;
    h.mana    = 10;
    h.maxMana = 10;
    h.name    = (heroId == 1) ? "P1 Hero" : "P2 Hero";

    // Pick first class that matches faction
    for (const auto& cls : reg.classes()) {
        if (cls.faction == faction) {
            h.classId = cls.id;
            if (cls.skillPool.size() >= 1) aiLearnSkill(h, reg);
            if (cls.skillPool.size() >= 2) aiLearnSkill(h, reg);
            break;
        }
    }

    // Starting army: 7 of faction's tier-1 base unit
    for (const auto& ud : udefs) {
        if (ud.faction == faction && ud.tier == 1 && ud.path == UpgradePath::None) {
            h.army.push_back({ud.id, 7});
            break;
        }
    }
    return h;
}

// Recruit available units from towns owned by this hero.
static void aiRecruit(Hero& hero, std::vector<Town>& towns,
                      const std::vector<UnitDef>& udefs)
{
    for (auto& t : towns) {
        if (t.ownerId != hero.id) continue;
        if (HexGrid::distance(hero.pos, t.pos) > 1) continue;
        for (auto& dw : t.dwellings) {
            if (dw.available <= 0) continue;
            for (const auto& ud : udefs) {
                if (ud.faction == t.faction && ud.tier == dw.tier && ud.path == dw.path) {
                    int n = dw.available;
                    dw.available = 0;
                    bool merged = false;
                    for (auto& s : hero.army)
                        if (s.defId == ud.id) { s.count += n; merged = true; break; }
                    if (!merged && hero.army.size() < 7)
                        hero.army.push_back({ud.id, n});
                    break;
                }
            }
        }
    }
}

// AI town builds: faction priority list, unlimited resources (AI cheats with income).
static void aiTownBuild(Town& town, Resources& res, const BuildingRegistry& reg, int /*week*/)
{
    // Give AI ample non-gold resources to not be blocked by secondaries
    Resources richRes = res;
    richRes.add(ResourceType::Iron,         9999);
    richRes.add(ResourceType::FaithStones,  9999);
    richRes.add(ResourceType::BloodEssence, 9999);
    richRes.add(ResourceType::VerdantSap,   9999);
    richRes.add(ResourceType::Mercury,      9999);

    const auto& allBuildings = reg.buildings();
    int fIdx = static_cast<int>(town.faction);
    bool built = false;

    if (fIdx >= 0 && fIdx < 9) {
        for (int bid : kBuildOrder[fIdx]) {
            Resources tmp = richRes;
            if (town.build(bid, allBuildings, tmp)) { built = true; break; }
        }
    }
    // Fallback: lowest tier dwelling
    for (int tier = 1; tier <= 6 && !built; ++tier) {
        for (const auto& def : allBuildings) {
            if (def.category != BuildingCategory::UnitDwelling) continue;
            if (def.faction != town.faction) continue;
            if (def.tier != tier || def.path != UpgradePath::None) continue;
            Resources tmp = richRes;
            if (town.build(def.id, allBuildings, tmp)) { built = true; break; }
        }
    }
}

// Move hero one step toward goal using simple pathfinding.
static void aiHeroMoveToward(Hero& hero, HexCoord goal, HexMap& map)
{
    auto costFn = [&](HexCoord c) -> int {
        const HexTile* t = map.getTile(c);
        if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
        return hero.moveCost(t->terrain);
    };

    auto path = Pathfinder::find(map, hero.pos, goal, costFn);
    if (path.empty()) return;

    for (HexCoord step : path) {
        const HexTile* t = map.getTile(step);
        if (!t) break;
        int cost = hero.moveCost(t->terrain);
        if (hero.movePool < cost) break;
        if (HexTile* old = map.getTile(hero.pos)) old->heroId = 0;
        hero.pos = step;
        hero.movePool -= cost;
        if (HexTile* nT = map.getTile(hero.pos)) nT->heroId = hero.id;
    }
}

// Collect XP shrines / resources as the hero passes over them.
static void aiCollectObjects(Hero& hero, std::vector<WorldObject>& objects,
                              std::vector<ResourceNode>& resources, HexMap& map,
                              const HeroClassRegistry& reg)
{
    // Claim resource node at current position
    const HexTile* tile = map.getTile(hero.pos);
    if (tile && tile->resourceId != 0) {
        for (auto& r : resources)
            if (r.id == tile->resourceId) { r.ownedBy = hero.id; break; }
    }

    // Collect world objects
    for (auto& obj : objects) {
        if (obj.collected || obj.pos != hero.pos) continue;
        obj.collected = true;
        if (obj.type == WorldObjectType::XPShrine ||
            obj.type == WorldObjectType::ForestShrine) {
            int prevLevel = hero.level;
            if (hero.addXp(obj.value) && hero.level > prevLevel) {
                int gained = hero.level - prevLevel;
                hero.attack  += (gained + 1) / 2;
                hero.defense += gained / 2;
                for (int g = 0; g < gained; ++g) aiLearnSkill(hero, reg);
            }
        }
    }
}

// Run all movement for one AI hero for its turn.
static void aiHeroTurn(Hero& hero, Hero& opponent,
                        std::vector<Town>& towns,
                        std::vector<ResourceNode>& resources,
                        std::vector<WorldObject>& objects,
                        HexMap& map,
                        const std::vector<UnitDef>& udefs,
                        const HeroClassRegistry& reg,
                        bool isPlayer2)
{
    hero.movePool = hero.maxMove;

    aiRecruit(hero, towns, udefs);

    int myStr  = heroStrength(hero, udefs);
    int oppStr = heroStrength(opponent, udefs);

    bool veryWeak  = (myStr * 10 < oppStr * 4);
    bool aggressive= (myStr * 10 >= oppStr * 5) || (oppStr > 0 && oppStr < 350);

    while (hero.movePool > 0) {
        HexCoord goal = {};
        bool goalSet  = false;

        auto tryGoal = [&](HexCoord pos, int bias = 0) {
            int d = HexGrid::distance(hero.pos, pos) - bias;
            if (!goalSet || d < HexGrid::distance(hero.pos, goal) - 0) {
                goal = pos; goalSet = true;
            }
        };

        if (veryWeak) {
            // Retreat to own town
            for (const auto& t : towns)
                if (t.ownerId == hero.id) tryGoal(t.pos, 5);
        } else {
            // Faction-key resource denial
            static const ResourceType kFacRes[9] = {
                ResourceType::FaithStones, ResourceType::FaithStones,
                ResourceType::VerdantSap,  ResourceType::Mercury,
                ResourceType::BloodEssence,ResourceType::Mercury,
                ResourceType::Iron,        ResourceType::BloodEssence,
                ResourceType::Gold,
            };
            int oppFIdx = static_cast<int>(opponent.faction);
            ResourceType denialRes = (oppFIdx >= 0 && oppFIdx < 9)
                                   ? kFacRes[oppFIdx] : ResourceType::Gold;

            for (const auto& r : resources) {
                if (r.ownedBy == hero.id) continue;
                int bias = (r.type == denialRes) ? 8 : 3;
                tryGoal(r.pos, bias);
            }
            for (const auto& obj : objects) {
                if (obj.collected) continue;
                if (obj.type == WorldObjectType::XPShrine ||
                    obj.type == WorldObjectType::ForestShrine ||
                    obj.type == WorldObjectType::StatShrine  ||
                    obj.type == WorldObjectType::ArtifactChest ||
                    obj.type == WorldObjectType::TreasureChest) {
                    tryGoal(obj.pos, 2);
                }
            }
            for (const auto& t : towns)
                if (t.ownerId == 0) tryGoal(t.pos);
            if (aggressive || !goalSet)
                tryGoal(opponent.pos);
        }

        if (!goalSet) break;

        HexCoord prevPos = hero.pos;
        aiHeroMoveToward(hero, goal, map);
        if (hero.pos == prevPos) break; // stuck

        aiCollectObjects(hero, objects, resources, map, reg);

        // Capture towns
        const HexTile* tile = map.getTile(hero.pos);
        if (tile && tile->townId != 0) {
            for (auto& t : towns) {
                if (t.id != tile->townId) continue;
                if (t.ownerId == 0)
                    t.ownerId = hero.id;
                break;
            }
        }

        if (hero.pos == opponent.pos) break; // combat will be resolved externally
    }
}

} // anonymous namespace

// ── FullGameSim::run ──────────────────────────────────────────────────────────
FullGameSim::Result FullGameSim::run(const Config& cfg)
{
    Result result;

    // Build registries
    BuildingRegistry reg;
    reg.init();
    HeroClassRegistry classReg;
    classReg.init();

    const auto& udefs = reg.units();

    // Generate map
    HexMap map;
    map.create(MapSize::Medium);
    WorldGenParams wgp;
    wgp.seed        = cfg.seed;
    wgp.playerCount = 2;
    wgp.shape       = MapShape::Hexagon;

    WorldGenResult world = WorldGen::generate(map, wgp);

    // Assign town factions and ownership
    if (world.towns.size() >= 1) {
        world.towns[0].faction = cfg.f1;
        world.towns[0].ownerId = 1;
    }
    if (world.towns.size() >= 2) {
        world.towns[1].faction = cfg.f2;
        world.towns[1].ownerId = 2;
    }

    // Create heroes
    HexCoord p1Start = world.startPositions.size() >= 1
                     ? world.startPositions[0] : HexCoord{0,0};
    HexCoord p2Start = world.startPositions.size() >= 2
                     ? world.startPositions[1] : HexCoord{3,0};

    Hero h1 = makeHero(cfg.f1, 1, p1Start, classReg, udefs);
    Hero h2 = makeHero(cfg.f2, 2, p2Start, classReg, udefs);
    if (HexTile* t = map.getTile(h1.pos)) t->heroId = h1.id;
    if (HexTile* t = map.getTile(h2.pos)) t->heroId = h2.id;

    Resources res1, res2;
    res1.set(ResourceType::Gold, 2000);
    res2.set(ResourceType::Gold, 2000);

    auto& towns   = world.towns;
    auto& resnodes= world.resources;
    auto& objects = world.worldObjects;

    int day  = 1;
    int week = 1;
    bool p1Alive = true;
    bool p2Alive = true;

    CombatEngine engine;
    engine.setSilent(true);

    for (week = 1; week <= cfg.maxWeeks && p1Alive && p2Alive; ++week) {
        // Weekly income for both players
        Resources inc1 = reg.buildings().empty() ? Resources{} :
            [&]() {
                Resources r;
                for (const auto& t : towns)
                    if (t.ownerId == 1) r.addAll(t.weeklyIncome);
                return r;
            }();
        Resources inc2 = [&]() {
            Resources r;
            for (const auto& t : towns)
                if (t.ownerId == 2) r.addAll(t.weeklyIncome);
            return r;
        }();
        // Mine income
        for (const auto& r : resnodes) {
            if (r.ownedBy == 1) res1.add(r.type, r.amount);
            else if (r.ownedBy == 2) res2.add(r.type, r.amount);
        }
        res1.addAll(inc1);
        res2.addAll(inc2);

        // Town builds
        for (auto& t : towns) {
            t.builtToday = 0;
            if (t.ownerId == 1) aiTownBuild(t, res1, reg, week);
            else if (t.ownerId == 2) aiTownBuild(t, res2, reg, week);
        }

        // Unit growth on new week
        for (auto& t : towns)
            t.onWeekStart(reg.buildings());

        // Hero turns — each day of the week (simplified: one pass per week)
        for (int d = 0; d < 7 && p1Alive && p2Alive; ++d) {
            h1.movePool = h1.maxMove;
            h2.movePool = h2.maxMove;

            aiHeroTurn(h1, h2, towns, resnodes, objects, map, udefs, classReg, false);
            aiHeroTurn(h2, h1, towns, resnodes, objects, map, udefs, classReg, true);

            // Resolve combat when heroes occupy same hex
            if (h1.pos == h2.pos && !h1.army.empty() && !h2.army.empty()) {
                // Build combat units from army stacks
                std::vector<CombatUnit> units1, units2;
                for (const auto& st : h1.army) {
                    for (const auto& ud : udefs) {
                        if (ud.id != st.defId) continue;
                        CombatUnit cu;
                        cu.defId     = ud.id;
                        cu.name      = ud.name;
                        cu.count     = st.count;
                        cu.hp        = ud.hp;
                        cu.maxHp     = ud.hp;
                        cu.attack    = ud.attack + h1.attack;
                        cu.defense   = ud.defense + h1.defense;
                        cu.speed     = ud.speed;
                        cu.damageMin = ud.damage_min;
                        cu.damageMax = ud.damage_max;
                        cu.range     = ud.range;
                        cu.shots     = ud.shots;
                        cu.shotsLeft = ud.shots;
                        cu.flying    = ud.flying;
                        cu.vampiric  = ud.vampiric;
                        cu.regenerates=ud.regenerates;
                        cu.tags      = ud.tags;
                        cu.isPlayer  = true;
                        units1.push_back(cu);
                        break;
                    }
                }
                for (const auto& st : h2.army) {
                    for (const auto& ud : udefs) {
                        if (ud.id != st.defId) continue;
                        CombatUnit cu;
                        cu.defId     = ud.id;
                        cu.name      = ud.name;
                        cu.count     = st.count;
                        cu.hp        = ud.hp;
                        cu.maxHp     = ud.hp;
                        cu.attack    = ud.attack + h2.attack;
                        cu.defense   = ud.defense + h2.defense;
                        cu.speed     = ud.speed;
                        cu.damageMin = ud.damage_min;
                        cu.damageMax = ud.damage_max;
                        cu.range     = ud.range;
                        cu.shots     = ud.shots;
                        cu.shotsLeft = ud.shots;
                        cu.flying    = ud.flying;
                        cu.vampiric  = ud.vampiric;
                        cu.regenerates=ud.regenerates;
                        cu.tags      = ud.tags;
                        cu.isPlayer  = false;
                        units2.push_back(cu);
                        break;
                    }
                }

                if (!units1.empty() && !units2.empty()) {
                    engine.startBattle(h1, units1, h2, units2);
                    engine.setPlayerAI(AIDifficulty::Standard);
                    engine.setEnemyAI(AIDifficulty::Standard);
                    CombatPhase finalPhase = engine.runHeadless(200);

                    // Extract surviving unit counts from the combat grid
                    h1.army.clear();
                    h2.army.clear();
                    int hp1 = 0, hp2 = 0;
                    for (const auto& cu : engine.grid().units()) {
                        if (!cu.alive || cu.count <= 0) continue;
                        if (cu.isPlayer) {
                            h1.army.push_back({cu.defId, cu.count});
                            hp1 += cu.count * cu.hp;
                        } else {
                            h2.army.push_back({cu.defId, cu.count});
                            hp2 += cu.count * cu.hp;
                        }
                    }

                    if (finalPhase == CombatPhase::Victory) {
                        h2.army.clear();
                        p2Alive = false;
                    } else if (finalPhase == CombatPhase::Defeat) {
                        h1.army.clear();
                        p1Alive = false;
                    } else {
                        // Timeout — tiebreak by remaining HP pool
                        if (hp1 >= hp2) {
                            h2.army.clear();
                            p2Alive = false;
                        } else {
                            h1.army.clear();
                            p1Alive = false;
                        }
                    }
                }
            }

            // Dead hero check (empty army with no recruitables nearby)
            auto canRecruit = [&](Hero& h) {
                for (const auto& t : towns) {
                    if (t.ownerId != h.id) continue;
                    for (const auto& dw : t.dwellings)
                        if (dw.available > 0) return true;
                }
                return false;
            };
            if (h1.army.empty() && !canRecruit(h1)) p1Alive = false;
            if (h2.army.empty() && !canRecruit(h2)) p2Alive = false;
        }

        if (cfg.recordSnapshots) {
            TurnSnapshot snap;
            snap.week       = week;
            snap.p1Gold     = res1.get(ResourceType::Gold);
            snap.p2Gold     = res2.get(ResourceType::Gold);
            for (int i = 1; i < RESOURCE_COUNT; ++i) {
                snap.p1OtherRes += res1.amounts[i];
                snap.p2OtherRes += res2.amounts[i];
            }
            snap.p1Strength = heroStrength(h1, udefs);
            snap.p2Strength = heroStrength(h2, udefs);
            result.snapshots.push_back(snap);
        }
    }

    // Determine winner — tiebreak by army strength if both/neither survived
    if (p1Alive && !p2Alive) {
        result.winner = 1; result.winFaction = cfg.f1;
    } else if (p2Alive && !p1Alive) {
        result.winner = 2; result.winFaction = cfg.f2;
    } else {
        // Both alive (timeout) or both dead — break tie by army strength
        int s1 = heroStrength(h1, udefs), s2 = heroStrength(h2, udefs);
        if (s1 > s2)      { result.winner = 1; result.winFaction = cfg.f1; }
        else if (s2 > s1) { result.winner = 2; result.winFaction = cfg.f2; }
        else              { result.winner = 0; result.winFaction = FactionId::None; }
    }

    result.endWeek = week - 1;
    return result;
}
