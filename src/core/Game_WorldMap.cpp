#include "Game.h"
#include "../hero/LevelUpSystem.h"
#include "../hero/SkillRegistry.h"
#include "../hero/HeroClass.h"
#include "../hero/Artifacts.h"
#include "../magic/SpellRegistry.h"
#include "../world/HexGrid.h"
#include "../town/UnitDef.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <stdio.h>

static constexpr float MOVE_SPEED = 4.0f;


// ── Per-faction combat unit templates ─────────────────────────────────────────
static std::vector<CombatUnit> makeFactionUnits(FactionId faction, bool isPlayer)
{
    std::vector<CombatUnit> out;
    int nextId = isPlayer ? 1 : 50;
    auto add = [&](const char* name, int count, int hp, int atk, int def, int spd, int rng = 0) {
        CombatUnit u;
        u.id = nextId++; u.name = name; u.count = count;
        u.maxHp = u.hp = hp; u.attack = atk; u.defense = def;
        u.speed = spd; u.range = rng; u.shotsLeft = rng > 0 ? 8 : 0;
        u.isPlayer = isPlayer;
        out.push_back(u);
    };
    switch (faction) {
    case FactionId::HolyOrder:
        add("Penitent",       10, 6, 3, 2, 5);
        add("Priest",          5, 4, 2, 1, 6, 3);
        break;
    case FactionId::Bloodsworn:
        add("Raider",         10, 5, 4, 1, 7);
        add("Bone Lancer",     6, 8, 3, 3, 4);
        break;
    case FactionId::Thornkin:
        add("Thornling",      10, 4, 2, 3, 4);
        add("Bark Sentinel",   5, 12, 3, 5, 3);
        break;
    case FactionId::EternalEmpire:
        add("Skeleton",       10, 4, 2, 1, 4);
        add("Shadow Archer",   6, 4, 4, 1, 5, 4);
        break;
    case FactionId::CrimsonWardens:
        add("Warden Scout",    8, 5, 3, 2, 6);
        add("Iron Warden",     5, 10, 4, 4, 4);
        break;
    case FactionId::Voidkin:
        add("Void Wraith",     8, 5, 3, 1, 6);
        add("Rift Stalker",    4, 8, 5, 2, 7);
        break;
    case FactionId::IronAssembly:
        add("Automaton",       7, 8, 3, 3, 3);
        add("Rifleman",        6, 5, 4, 1, 5, 5);
        break;
    case FactionId::Amalgamate:
        add("Flesh Spawn",     9, 5, 2, 2, 4);
        add("Plague Bearer",   5, 8, 3, 2, 5);
        break;
    default:
        add("Soldier",        10, 5, 3, 2, 5);
        break;
    }
    return out;
}

// Generate mine guard units deterministically from mine position + resource type
static std::vector<CombatUnit> makeMineGuardUnits(const ResourceNode& r, int week = 1)
{
    uint32_t seed = (uint32_t)(r.pos.q * 73856093u)
                  ^ (uint32_t)(r.pos.r * 19349663u)
                  ^ (uint32_t)((int)r.type * 83492791u);
    auto xr = [&]() -> uint32_t {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
    };
    auto rnd = [&](int lo, int hi) -> int {
        return lo + (int)(xr() % (unsigned)(hi - lo + 1));
    };

    // Map resource type → faction hint for sprite display
    // Gold=any(6=IronAssembly), Iron=IronAssembly, FaithStones=HolyOrder,
    // BloodEssence=Bloodsworn, VerdantSap=Thornkin, Mercury=EternalEmpire
    static const int kFactionHint[6] = { 6, 6, 0, 4, 2, 3 }; // indexed by ResourceType
    int rtype = std::clamp((int)r.type, 0, 5);
    int faction = kFactionHint[rtype];

    struct GN { const char* light; const char* heavy; };
    static const GN gNames[6] = {
        {"Iron Mercenary",  "Iron Sentinel"},    // Gold / Iron → IronAssembly look
        {"Iron Custodian",  "Forge Warden"},     // Iron → IronAssembly
        {"Temple Guard",    "Holy Warden"},      // FaithStones → HolyOrder
        {"Blood Hunter",    "Crimson Reaver"},   // BloodEssence → Bloodsworn
        {"Grove Warden",    "Thornborn"},        // VerdantSap → Thornkin
        {"Spectral Keeper", "Wraith Sentinel"},  // Mercury → EternalEmpire
    };
    int ni   = rtype;
    int tier = std::clamp(r.amount, 1, 5);

    // Scale guard counts by week: +15% per week, capped at 3× at week 15
    float weekMult = std::min(3.0f, 1.0f + (week - 1) * 0.15f);

    CombatUnit g1;
    g1.name        = gNames[ni].light;
    g1.factionHint = faction;
    g1.count       = static_cast<int>(std::round(rnd(3 + tier * 2, 6 + tier * 3) * weekMult));
    g1.maxHp       = g1.hp = 5 + tier * 2;
    g1.attack      = 2 + tier;
    g1.defense     = 1 + tier;
    g1.speed       = 4;
    g1.isPlayer    = false;

    CombatUnit g2;
    g2.name        = gNames[ni].heavy;
    g2.factionHint = faction;
    g2.count       = static_cast<int>(std::round(rnd(2 + tier, 4 + tier * 2) * weekMult));
    g2.maxHp       = g2.hp = 8 + tier * 3;
    g2.attack      = 3 + tier;
    g2.defense     = 2 + tier;
    g2.speed       = 3;
    g2.isPlayer    = false;

    return {g1, g2};
}

// Build CombatUnits from hero's actual army; falls back to faction template if army empty
static std::vector<CombatUnit> makeHeroUnits(const Hero& hero,
    const std::vector<UnitDef>& defs, bool isPlayer)
{
    if (hero.army.empty())
        return makeFactionUnits(hero.faction, isPlayer);

    std::vector<CombatUnit> out;
    int nextId = isPlayer ? 1 : 50;
    for (const auto& stack : hero.army) {
        if (stack.count <= 0) continue;
        const UnitDef* ud = nullptr;
        for (const auto& d : defs) if (d.id == stack.defId) { ud = &d; break; }
        if (!ud) continue;
        CombatUnit u;
        u.id = nextId++;
        u.defId = ud->id;
        u.name = ud->name;
        u.count = stack.count;
        u.hp = u.maxHp = ud->hp;
        u.attack   = ud->attack;
        u.defense  = ud->defense;
        u.damageMin = ud->damage_min;
        u.damageMax = ud->damage_max;
        u.speed    = ud->speed;
        u.range    = ud->range;
        u.shots    = u.shotsLeft = ud->shots;
        u.flying             = ud->flying;
        u.vampiric           = ud->vampiric;
        u.regenerates        = ud->regenerates;
        u.tags               = ud->tags;
        u.isPlayer           = isPlayer;
        u.hasSecondLife      = ud->hasSecondLife || (hero.faction == FactionId::EternalEmpire && ud->path != UpgradePath::PathB);
        u.secondLifeFullHeal = ud->secondLifeFullHeal;
        u.moraleImmune       = ud->moraleImmune || hasTag(ud->tags, UnitTag::Undead) || hasTag(ud->tags, UnitTag::Mechanical);
        u.rapidEvolution     = ud->rapidEvolution;
        u.adaptationDouble   = ud->adaptationDouble;
        out.push_back(u);
    }
    return out.empty() ? makeFactionUnits(hero.faction, isPlayer) : out;
}

// Estimated combat strength: sum(count * hp * attack) per stack
static int heroStrength(const Hero& hero, const std::vector<UnitDef>& defs)
{
    if (hero.army.empty()) {
        auto units = makeFactionUnits(hero.faction, true);
        int s = 0;
        for (auto& u : units) s += u.count * u.hp * u.attack;
        return s;
    }
    int s = 0;
    for (const auto& stack : hero.army) {
        if (stack.count <= 0) continue;
        for (const auto& d : defs)
            if (d.id == stack.defId) { s += stack.count * d.hp * d.attack; break; }
    }
    return s;
}

// ── World map update ──────────────────────────────────────────────────────────
void Game::watchAiMovePlayerHero()
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    const auto& udefs = m_registry.units();

    hero.movePool = hero.maxMove;

    int myStr = heroStrength(hero, udefs);
    int bestOppStr = 0;
    for (const auto& eh : m_enemyHeroes) {
        int s = heroStrength(eh, udefs);
        if (s > bestOppStr) bestOppStr = s;
    }
    float strRatio   = bestOppStr > 0 ? (float)myStr / bestOppStr : 99.f;
    bool veryWeak    = strRatio < 0.4f;
    bool softRetreat = strRatio < 0.6f;
    bool dominant    = strRatio >= 1.2f;

    while (hero.movePool > 0) {
        struct Cand { HexCoord pos; float score; };
        std::vector<Cand> cands;
        auto add = [&](HexCoord pos, float val) {
            if (pos == hero.pos) return;
            int d = std::max(1, HexGrid::distance(hero.pos, pos));
            cands.push_back({pos, val / d});
        };

        if (veryWeak) {
            // Retreat to own town
            for (const auto& t : m_towns)
                if (t.ownerId == 1) add(t.pos, 500.f);
        } else {
            // Own town to recruit
            for (const auto& t : m_towns) {
                if (t.ownerId != 1) continue;
                bool hasU = false;
                for (const auto& dw : t.dwellings) if (dw.available > 0) { hasU = true; break; }
                if (hasU && (int)hero.army.size() < 7) add(t.pos, 250.f);
            }
            // Towns
            for (const auto& t : m_towns) {
                if (t.ownerId == 0)  add(t.pos, 150.f);
                else if (t.ownerId != 1) add(t.pos, 200.f); // enemy town
            }
            // Resources
            for (const auto& r : m_resources) {
                if (r.ownedBy == 1) continue;
                add(r.pos, 60.f);
            }
            // World objects
            for (const auto& obj : m_worldObjects) {
                if (obj.collected) continue;
                float val = 0.f;
                if (obj.type == WorldObjectType::ArtifactChest)   val = 80.f;
                else if (obj.type == WorldObjectType::TreasureChest) val = 70.f;
                else if (obj.type == WorldObjectType::XPShrine    ||
                         obj.type == WorldObjectType::ForestShrine ||
                         obj.type == WorldObjectType::StatShrine   ||
                         obj.type == WorldObjectType::SpellScroll  ||
                         obj.type == WorldObjectType::SwampAltar)   val = 50.f;
                if (val > 0.f) add(obj.pos, val);
            }
            // Enemy heroes
            if (!softRetreat && !m_enemyHeroes.empty()) {
                for (const auto& eh : m_enemyHeroes) {
                    int dist = HexGrid::distance(hero.pos, eh.pos);
                    if (dominant || dist <= 8)
                        add(eh.pos, 300.f);
                }
            }
        }

        if (cands.empty()) break;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b){ return a.score > b.score; });
        HexCoord goal = cands[0].pos;

        auto costFn = [&](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
            int base = hero.moveCost(t->terrain);
            if (m_roadHexes.count(c)) base = std::max(1, base / 2);
            return base;
        };
        auto path = Pathfinder::find(m_map, hero.pos, goal, costFn);
        if (path.empty()) break;

        HexCoord next = path[0];
        const HexTile* nt = m_map.getTile(next);
        if (!nt) break;
        int cost = hero.moveCost(nt->terrain);
        if (hero.movePool < cost) break;

        if (HexTile* old = m_map.getTile(hero.pos)) old->heroId = 0;
        hero.pos = next;
        hero.movePool -= cost;
        if (HexTile* nT = m_map.getTile(hero.pos)) nT->heroId = hero.id;

        // Claim resource
        if (nt->resourceId != 0) {
            for (auto& r : m_resources)
                if (r.id == nt->resourceId) { r.ownedBy = 1; break; }
        }
        // Claim or visit town
        if (nt->townId != 0) {
            for (auto& t : m_towns) {
                if (t.id != nt->townId) continue;
                if (t.ownerId == 0) t.ownerId = 1; // capture neutral
                if (t.ownerId == 1) {
                    // Recruit all available units from player-owned town
                    for (auto& dw : t.dwellings) {
                        if (dw.available <= 0) continue;
                        for (const auto& ud : udefs) {
                            if (ud.faction == t.faction && ud.tier == dw.tier
                                && ud.path == dw.path) {
                                int recruited = dw.available;
                                dw.available = 0;
                                bool merged = false;
                                for (auto& s : hero.army)
                                    if (s.defId == ud.id) { s.count += recruited; merged = true; break; }
                                if (!merged && hero.army.size() < 7)
                                    hero.army.push_back({ud.id, recruited});
                                break;
                            }
                        }
                    }
                }
                break;
            }
        }
        // Collect world objects
        for (auto& obj : m_worldObjects) {
            if (obj.collected || obj.pos != hero.pos) continue;
            obj.collected = true;
        }

        // Combat: stepped onto enemy hero
        for (auto& eHero : m_enemyHeroes) {
            if (eHero.pos != hero.pos) continue;
            m_lastCombatEnemyId = eHero.id;
            auto pUnits = makeHeroUnits(hero,  udefs, true);
            auto eUnits = makeHeroUnits(eHero, udefs, false);
            // Auto-resolve combat in watch AI mode
            m_fromBattleSim = true;
            m_simAutoPlay   = true;
            m_simAutoPlayTimer = 0.f;
            enterCombat(hero, pUnits, eHero, eUnits);
            return;
        }
    }
}

void Game::updateWorldMap(float dt)
{
    m_mapTime += dt;
    m_hexRenderer.update(dt);

    // Watch AI auto-advance end-turn
    if (m_watchingAI) {
        // Auto-dismiss any blocking modals so the sim can continue
        if (m_showVictory || m_showDefeat) {
            m_watchingAI = false; // game over — stop the sim
            return;
        }
        if (m_showCombatResult) {
            m_showCombatResult = false;
        }
        if (m_showLevelUpModal && !m_levelUpOffers.empty() && !m_heroes.empty()) {
            // Auto-pick first skill offer
            Hero& lvlHero = m_heroes[m_activeHeroIdx];
            const auto& offer = m_levelUpOffers[0];
            int prevTier = 0;
            if (const SkillInstance* existing = lvlHero.skills.getSkill(offer.skillId))
                prevTier = static_cast<int>(existing->tier);
            LevelUpSystem::applyOffer(offer, lvlHero.skills);
            // Apply passive skill bonuses
            if (const SkillDef* sd = findSkillDef(offer.skillId)) {
                int v = offer.isUpgrade ? (sd->values[prevTier+1] - sd->values[prevTier]) : sd->values[0];
                if (sd->effectType == SkillEffectType::MovementBonus) {
                    lvlHero.maxMove += v; lvlHero.movePool = std::max(lvlHero.movePool, lvlHero.maxMove);
                } else if (sd->effectType == SkillEffectType::VisionBonus) {
                    lvlHero.visionRange += v;
                }
            }
            m_levelUpOffers.clear();
            if (m_pendingLevelUps > 1) {
                m_pendingLevelUps--;
                const HeroClassDef* ncls = m_classRegistry.getClass(lvlHero.classId);
                if (ncls) {
                    std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                    m_levelUpOffers = LevelUpSystem::generateOffers(
                        *ncls, lvlHero.skills, lvlHero.level, allSkills, lvlHero.faction);
                }
                if (m_levelUpOffers.empty())
                    m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
            } else {
                m_pendingLevelUps = 0;
                m_showLevelUpModal = false;
            }
        }

        m_watchAITimer -= dt;
        if (m_watchAITimer <= 0.f) {
            m_watchAITimer = 1.0f / m_watchAISpeed;
            if (!m_showCombatResult && !m_showLevelUpModal) {
                watchAiMovePlayerHero();
                doEndTurn();
            }
        }
        return;
    }

    const auto& mouse = m_input.mouse();

    if (mouse.wheelY != 0.0f)
        m_camera.zoomBy(mouse.wheelY > 0 ? 1.12f : 0.88f);

    if (mouse.middle)
        m_camera.pan(-static_cast<float>(mouse.dx), -static_cast<float>(mouse.dy));

    const float PAN = 200.0f * dt;
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (m_input.keyHeld(SDLK_LEFT))  m_camera.pan(-PAN, 0);
        if (m_input.keyHeld(SDLK_RIGHT)) m_camera.pan( PAN, 0);
        if (m_input.keyHeld(SDLK_UP))    m_camera.pan(0, -PAN);
        if (m_input.keyHeld(SDLK_DOWN))  m_camera.pan(0,  PAN);
    }

    // Lambda that clamps camera so map edge never scrolls past viewport edge
    auto clampCamera = [this]() {
        const float hs    = m_hexRenderer.grid().hexSize();
        const float R     = static_cast<float>(m_map.radius());
        const float zoom  = m_camera.zoom();
        const float halfW = static_cast<float>(m_width)  / (2.0f * zoom);
        const float halfH = static_cast<float>(m_height) / (2.0f * zoom);
        const float mapExtX = R * hs * 1.5f + hs;
        const float mapExtY = R * hs * 1.732f + hs;
        const float limX = std::max(0.0f, mapExtX - halfW);
        const float limY = std::max(0.0f, mapExtY - halfH);
        float cx = std::clamp(m_camera.x(), -limX, limX);
        float cy = std::clamp(m_camera.y(), -limY, limY);
        if (cx != m_camera.x() || cy != m_camera.y())
            m_camera.setPosition(cx, cy);
    };
    clampCamera();

    {
        float wx, wy;
        m_camera.screenToWorld(static_cast<float>(mouse.x),
                               static_cast<float>(mouse.y), wx, wy);
        HexCoord h = m_hexRenderer.grid().worldToHex(wx, wy);
        m_hovered = m_map.inBounds(h) ? h : HexCoord{-999,-999};
    }

    // Cursor: fight if enemy hovered, otherwise arrow
    if (m_cursorArrow && m_cursorFight) {
        bool fight = false;
        if (m_map.inBounds(m_hovered)) {
            const HexTile* ht = m_map.getTile(m_hovered);
            if (ht && ht->visible) {
                for (const auto& e : m_enemyHeroes)
                    if (e.id == ht->heroId) { fight = true; break; }
                if (!fight && ht->townId != 0)
                    for (const auto& t : m_towns)
                        if (t.id == ht->townId && t.ownerId > 1) { fight = true; break; }
            }
        }
        SDL_SetCursor(fight ? m_cursorFight : m_cursorArrow);
    }

    if (mouse.leftDown && !ImGui::GetIO().WantCaptureMouse) {
        bool uiHandled = m_worldHUD.onMouseDown(
            static_cast<float>(mouse.x), static_cast<float>(mouse.y));

        // Screen-space hero click: reliable regardless of hex-coordinate rounding
        // First click → center camera + select hero; second click on same hero → inspect
        if (!uiHandled) {
            bool heroClickHandled = false;

            // Hot-seat P2 turn: clicks select/move enemy heroes instead of player heroes
            if (m_hotSeatMode && m_hotSeatP2Turn) {
                // Click on an enemy hero sprite → select it
                for (int ehi = 0; ehi < (int)m_enemyHeroes.size(); ++ehi) {
                    const Hero& eh = m_enemyHeroes[ehi];
                    float wx, wy; m_hexRenderer.grid().hexToWorld(eh.pos, wx, wy);
                    float sx, sy; m_camera.worldToScreen(wx, wy, sx, sy);
                    float dx = static_cast<float>(mouse.x) - sx;
                    float dy = static_cast<float>(mouse.y) - sy;
                    if (dx * dx + dy * dy < 20.0f * 20.0f) {
                        m_selectedEnemyHero = ehi;
                        heroClickHandled = true;
                        uiHandled = true;
                        break;
                    }
                }
                // No hero clicked → move selected enemy hero to hovered tile
                if (!heroClickHandled && m_selectedEnemyHero >= 0
                    && m_selectedEnemyHero < (int)m_enemyHeroes.size()
                    && m_map.inBounds(m_hovered)) {
                    Hero& eh = m_enemyHeroes[m_selectedEnemyHero];
                    auto moveCost = [&](HexCoord to) -> int {
                        const HexTile* t = m_map.getTile(to);
                        if (!t || t->terrain == Terrain::Water) return 9999;
                        return BASE_MOVE_COST[static_cast<int>(t->terrain)];
                    };
                    auto p2path = Pathfinder::find(m_map, eh.pos, m_hovered, moveCost);
                    if (!p2path.empty()) {
                        eh.path     = p2path;
                        eh.pathStep = 0;
                    }
                    heroClickHandled = true;
                    uiHandled = true;
                }
            } else {
                // Normal P1 turn hero selection
                for (int hi = 0; hi < static_cast<int>(m_heroes.size()); ++hi) {
                    const Hero& h = m_heroes[hi];
                    float wx, wy;
                    m_hexRenderer.grid().hexToWorld(h.pos, wx, wy);
                    float sx, sy;
                    m_camera.worldToScreen(wx, wy, sx, sy);
                    float dx = static_cast<float>(mouse.x) - sx;
                    float dy = static_cast<float>(mouse.y) - sy;
                    if (dx * dx + dy * dy < 20.0f * 20.0f) {
                        if (m_heroClickTarget == hi) {
                            m_showHeroInspect = true;
                            m_heroClickTarget = -1;
                        } else {
                            m_heroClickTarget = hi;
                            m_activeHeroIdx   = hi;
                        }
                        heroClickHandled = true;
                        uiHandled = true;
                        break;
                    }
                }
                if (!heroClickHandled) m_heroClickTarget = -1;
            }
        }

        // Minimap click: pan camera to clicked map position
        if (!uiHandled && m_map.radius() > 0) {
            constexpr float MINI_W = 150.0f, MINI_H = 150.0f, PAD = 10.0f;
            const float mm_left = PAD;
            const float mm_top  = static_cast<float>(m_height) - MINI_H - PAD;
            const float mx = static_cast<float>(mouse.x);
            const float my = static_cast<float>(mouse.y);
            if (mx >= mm_left && mx <= mm_left + MINI_W &&
                my >= mm_top  && my <= mm_top  + MINI_H) {
                const float mm_cx  = mm_left + MINI_W * 0.5f;
                const float mm_cy  = mm_top  + MINI_H * 0.5f;
                const float R      = static_cast<float>(m_map.radius());
                const float scaleX = MINI_W * 0.5f / R;
                const float scaleY = MINI_H * 0.5f / R;
                const float hs     = m_hexRenderer.grid().hexSize();
                float q_f   = (mx - mm_cx) / scaleX;
                float rq_f  = (my - mm_cy) / scaleY;
                m_camera.setPosition(hs * 1.5f * q_f, hs * 1.7320508f * rq_f);
                clampCamera();
                uiHandled = true;
            }
        }

        if (!uiHandled && m_map.inBounds(m_hovered))
            onTileClicked(m_hovered);
        else if (!uiHandled)
            m_selected = {-999,-999};
    }

    // Right-click on a mine tile — show guard/income info popup
    if (mouse.rightDown && !ImGui::GetIO().WantCaptureMouse) {
        m_showMineInfoPopup = false;
        if (m_map.inBounds(m_hovered)) {
            const HexTile* ht = m_map.getTile(m_hovered);
            if (ht && ht->resourceId != 0) {
                m_mineInfoId        = ht->resourceId;
                m_showMineInfoPopup = true;
            }
        }
    }

    if (mouse.leftUp)
        m_worldHUD.onMouseUp(static_cast<float>(mouse.x), static_cast<float>(mouse.y));

    m_worldHUD.onMouseMove(static_cast<float>(mouse.x),
                           static_cast<float>(mouse.y));

    updateHeroMovement(dt);

    // Update particles
    m_particles.update(dt);

    // Advance pickup effects (float upward, fade out)
    for (auto& e : m_pickupEffects) e.t -= dt;
    m_pickupEffects.erase(
        std::remove_if(m_pickupEffects.begin(), m_pickupEffects.end(),
            [](const PickupEffect& ef){ return ef.t <= 0.0f; }),
        m_pickupEffects.end());

    // Update world-map hero animators (lazy-init on first seen)
    for (const auto& h : m_heroes) {
        if (m_heroMapAnimators.find(h.id) == m_heroMapAnimators.end()) {
            SpriteAnimator a;
            a.faction = std::min(static_cast<int>(h.faction), NUM_FACTIONS - 1);
            a.tier = 1;
            a.setState(AnimState::Idle);
            m_heroMapAnimators[h.id] = a;
        }
        m_heroMapAnimators[h.id].update(dt);
    }
    for (const auto& h : m_enemyHeroes) {
        if (m_heroMapAnimators.find(h.id) == m_heroMapAnimators.end()) {
            SpriteAnimator a;
            a.faction = std::min(static_cast<int>(h.faction), NUM_FACTIONS - 1);
            a.tier = 1; a.mirror = true;
            a.setState(AnimState::Idle);
            m_heroMapAnimators[h.id] = a;
        }
        m_heroMapAnimators[h.id].update(dt);
    }

    if (m_input.keyDown(SDLK_F6)) m_showHideoutScreen   = !m_showHideoutScreen;
    if (m_input.keyDown(SDLK_F7)) m_showArtifactPanel   = !m_showArtifactPanel;
    if (m_input.keyDown(SDLK_F8)) m_showHeroInspect     = !m_showHeroInspect;
    if (m_input.keyDown(SDLK_m))  m_showMinimap         = !m_showMinimap;

    // G — toggle garrison (hero digs in, blocks passage until defeated)
    if (m_input.keyDown(SDLK_g) && !m_heroes.empty()) {
        Hero& h = m_heroes[m_activeHeroIdx];
        h.isGarrisoned = !h.isGarrisoned;
        gLog("Hero %s %s garrison\n", h.name.c_str(),
               h.isGarrisoned ? "dug in at" : "left");
    }

    // F — Build Fishing House (hero recently disembarked from a boat on land)
    if (m_input.keyDown(SDLK_f) && !m_heroes.empty()) {
        Hero& h = m_heroes[m_activeHeroIdx];
        const HexTile* ft = m_map.getTile(h.pos);
        bool onLand = ft && ft->terrain != Terrain::Water;
        bool hasBoatHistory = h.boatCount > 0;  // built at least one boat this game
        if (onLand && hasBoatHistory) {
            int buildCost = 500;
            if (m_playerResources.get(ResourceType::Gold) >= buildCost) {
                // Check no existing FishingHouse on this tile
                bool already = false;
                for (const auto& wo : m_worldObjects)
                    if (wo.pos == h.pos && wo.type == WorldObjectType::FishingHouse)
                    { already = true; break; }
                if (!already) {
                    m_playerResources.add(ResourceType::Gold, -buildCost);
                    WorldObject fh;
                    fh.id          = m_nextObjId++;
                    fh.type        = WorldObjectType::FishingHouse;
                    fh.pos         = h.pos;
                    fh.faction     = 1;     // owned by player
                    fh.value       = 150;   // daily gold
                    m_worldObjects.push_back(fh);
                    pushPickupEffect(h.pos, "Fishing House built! (+150g/day)", IM_COL32(80, 220, 120, 255));
                    m_audio.playSound("pickup");
                }
            } else {
                pushPickupEffect(h.pos, "Need 500 Gold!", IM_COL32(255, 80, 80, 255));
            }
        }
    }

    // Tab — cycle to next player hero
    if (m_input.keyDown(SDLK_TAB) && !m_heroes.empty()) {
        m_activeHeroIdx = (m_activeHeroIdx + 1) % static_cast<int>(m_heroes.size());
        const Hero& nextHero = m_heroes[m_activeHeroIdx];
        float hx2, hy2;
        m_hexRenderer.grid().hexToWorld(nextHero.pos, hx2, hy2);
        m_camera.setPosition(hx2, hy2);
        clampCamera();
        m_selected = {-999, -999};
        auto costFn2 = [this, &nextHero](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            if (!t || !nextHero.canEnter(t->terrain) || t->blocked) return 999;
            int base = nextHero.moveCost(t->terrain);
            if (m_roadHexes.count(c)) base = std::max(1, base / 2);
            return base;
        };
        m_reachable = Pathfinder::reachable(m_map, nextHero.pos, costFn2, nextHero.movePool);
    }

    if (m_input.keyDown(SDLK_SPACE)) {
        doEndTurn();
    }
}

// ── End Turn — full turn logic (SPACE key + HUD button) ───────────────────────
void Game::doEndTurn()
{
    // Reset per-day build limit for all towns
    for (auto& t : m_towns) t.builtToday = 0;

    // ── Hot-seat: alternate between Player 1 and Player 2 ────────────────────
    if (m_hotSeatMode) {
        // Restore movement for whichever side is about to play
        for (auto& h : m_heroes)      h.movePool = h.maxMove;
        for (auto& h : m_enemyHeroes) {
            h.movePool = h.maxMove;
            int mr = std::max(2, 2 + h.maxMana / 10);
            h.mana = std::min(h.maxMana, h.mana + mr);
        }

        if (!m_hotSeatP2Turn) {
            // P1 ended their day → hand off to P2
            m_hotSeatP2Turn     = true;
            m_hotSeatHandoff    = true;
            m_selectedEnemyHero = m_enemyHeroes.empty() ? -1 : 0;
            // Rebuild fog from P2 hero positions
            if (!m_fogDisabled) {
                FogOfWar::hideAll(m_map);
                for (auto& h : m_enemyHeroes) FogOfWar::updateVision(m_map, h);
            }
            return;   // don't run AI or week processing until P2 also ends
        } else {
            // P2 ended their day → hand off back to P1
            m_hotSeatP2Turn     = false;
            m_hotSeatHandoff    = true;
            m_selectedEnemyHero = -1;
            // Rebuild fog from P1 hero positions
            if (!m_fogDisabled) {
                FogOfWar::hideAll(m_map);
                for (auto& h : m_heroes) FogOfWar::updateVision(m_map, h);
            }
            // FALL THROUGH — process week/income as normal
        }
    }

    // FishingHouse daily income (+150 gold per player-owned house)
    for (const auto& wo : m_worldObjects) {
        if (wo.type != WorldObjectType::FishingHouse) continue;
        if (wo.collected) continue;
        if (wo.faction != 1) continue;   // faction field holds ownerId; 1 = player
        m_playerResources.add(ResourceType::Gold, 150);
    }

    // Restore hero movement pools and daily mana regen for enemy heroes
    if (!m_hotSeatMode) {
        for (auto& h : m_heroes)      h.movePool = h.maxMove;
        for (auto& h : m_enemyHeroes) {
            h.movePool = h.maxMove;
            int manaRegen = std::max(2, 2 + h.maxMana / 10);
            h.mana = std::min(h.maxMana, h.mana + manaRegen);
        }
    }

        // Enemy hero AI — omniscient (full map visibility, no fog), faction-optimal
        // Skipped in hot-seat mode: P2 controls their own heroes manually.
        if (!m_hotSeatMode && !m_heroes.empty()) {
            Hero& playerHero = m_heroes[m_activeHeroIdx];
            const auto& unitDefs = m_registry.units();
            bool combatTriggered = false;

            // ── Helper: apply a skill's world-map stat effects ────────────────
            auto aiApplySkillBonus = [](Hero& hero, const SkillDef* def, int v) {
                if (!def) return;
                if (def->effectType == SkillEffectType::MovementBonus) {
                    hero.maxMove += v;
                    hero.movePool = std::max(hero.movePool, hero.maxMove);
                } else if (def->effectType == SkillEffectType::VisionBonus) {
                    hero.visionRange += v;
                } else if (def->effectType == SkillEffectType::MagicSchoolBonus) {
                    if      (def->statName == "lightPower")  hero.lightPower  += v;
                    else if (def->statName == "bloodPower")  hero.bloodPower  += v;
                    else if (def->statName == "deathPower")  hero.deathPower  += v;
                    else if (def->statName == "naturePower") hero.naturePower += v;
                    else if (def->statName == "forgePower")  hero.forgePower  += v;
                    else if (def->statName == "fleshPower")  hero.fleshPower  += v;
                }
            };

            // ── Helper: advance one skill for an AI hero on level-up ─────────
            // Prioritises upgrading existing skills, then learns next pool skill.
            auto aiLearnNextSkill = [this, &aiApplySkillBonus](Hero& hero) {
                const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                if (!cls || cls->skillPool.empty()) return;
                // First: upgrade any upgradeable skill already learned (most efficient)
                for (int sid : cls->skillPool) {
                    if (SkillInstance* s = hero.skills.getSkill(sid)) {
                        if (s->canUpgrade()) {
                            int prevTierIdx = static_cast<int>(s->tier);
                            s->upgrade();
                            if (const SkillDef* def = findSkillDef(sid)) {
                                int delta = def->values[prevTierIdx + 1] - def->values[prevTierIdx];
                                aiApplySkillBonus(hero, def, delta);
                            }
                            return;
                        }
                    }
                }
                // Then: learn next unlearned skill from pool
                for (int sid : cls->skillPool) {
                    if (!hero.skills.hasSkill(sid) && hero.skills.canLearn(sid)) {
                        hero.skills.learn(sid);
                        if (const SkillDef* def = findSkillDef(sid))
                            aiApplySkillBonus(hero, def, def->values[0]);
                        return;
                    }
                }
            };

            // ── Omniscient threat state ───────────────────────────────────────
            int plStr = heroStrength(playerHero, unitDefs);
            // Weak player = just fought a battle (army below expected for this week)
            bool playerIsWeak = (plStr > 0 && plStr < m_turns.week() * 350);

            // Player's key faction resource — AI denies these mines first
            static const ResourceType kFactionResource[9] = {
                ResourceType::FaithStones,   // HolyOrder
                ResourceType::FaithStones,   // CrimsonWardens
                ResourceType::VerdantSap,    // Thornkin
                ResourceType::Mercury,       // EternalEmpire
                ResourceType::BloodEssence,  // Bloodsworn
                ResourceType::Mercury,       // Voidkin
                ResourceType::Iron,          // IronAssembly
                ResourceType::BloodEssence,  // Amalgamate
                ResourceType::Gold,          // Convergence
            };
            int plFidx = static_cast<int>(playerHero.faction);
            ResourceType denialRes = (plFidx >= 0 && plFidx < 9)
                                   ? kFactionResource[plFidx] : ResourceType::Gold;

            for (int ehi = 0; ehi < static_cast<int>(m_enemyHeroes.size()); ++ehi) {
                if (combatTriggered) break;
                auto& eHero = m_enemyHeroes[ehi];

                // ── Hero roles: raider hunts player, economic grabs map, defender guards towns
                bool isRaider   = (ehi == 0);
                bool isDefender = (ehi >= 2);

                // Recruit from any owned town within 1 tile (free for AI)
                for (auto& t : m_towns) {
                    if (t.ownerId != eHero.id) continue;
                    if (HexGrid::distance(eHero.pos, t.pos) > 1) continue;
                    for (auto& dw : t.dwellings) {
                        if (dw.available <= 0) continue;
                        for (const auto& ud : unitDefs) {
                            if (ud.faction == t.faction && ud.tier == dw.tier
                                && ud.path == dw.path) {
                                int recruited = dw.available;
                                dw.available = 0;
                                bool merged = false;
                                for (auto& s : eHero.army)
                                    if (s.defId == ud.id) { s.count += recruited; merged = true; break; }
                                if (!merged && eHero.army.size() < 7)
                                    eHero.army.push_back({ud.id, recruited});
                                break;
                            }
                        }
                    }
                }

                int eiStr = heroStrength(eHero, unitDefs);
                // Raider: attack if ≥50% strength OR player is wounded; Economic: only if 1.5×; Defender: never
                bool aggressive = isDefender ? false
                                : isRaider   ? (playerIsWeak || eiStr * 10 >= plStr * 5)
                                :              (eiStr * 10 >= plStr * 15);
                // Retreat when very weak regardless of role
                bool veryWeak   = (eiStr * 10 <  plStr * 4);

                // Graduated retreat thresholds
                float strRatio = plStr > 0 ? (float)eiStr / plStr : 99.f;
                bool softRetreat = strRatio < 0.6f;
                bool dominant    = strRatio >= 1.2f;
                bool playerGhostWalk = playerHero.ghostWalkSpecialty;

                // Pinned by siege camp: enemy hero can't leave their besieged town
                bool pinnedBySiege = false;
                for (const auto& t : m_towns) {
                    if (t.ownerId != eHero.id || !t.underSiege) continue;
                    if (HexGrid::distance(eHero.pos, t.pos) <= 1) { pinnedBySiege = true; break; }
                }
                if (pinnedBySiege) { eHero.movePool = 0; }

                while (eHero.movePool > 0) {
                    // Score-based candidate selection: value / distance
                    struct Cand { HexCoord pos; float score; };
                    std::vector<Cand> cands;
                    auto add = [&](HexCoord pos, float val) {
                        int d = std::max(1, HexGrid::distance(eHero.pos, pos));
                        cands.push_back({pos, val / d});
                    };

                    if (veryWeak) {
                        for (const auto& t : m_towns)
                            if (t.ownerId == eHero.id) add(t.pos, 500.f);
                    } else if (isDefender) {
                        for (const auto& r : m_resources)
                            if (r.ownedBy != eHero.id) add(r.pos, 60.f);
                        for (const auto& t : m_towns)
                            if (t.ownerId == eHero.id) add(t.pos, 80.f);
                    } else {
                        // Own town to recruit
                        for (const auto& t : m_towns) {
                            if (t.ownerId != eHero.id) continue;
                            bool hasU = false;
                            for (const auto& dw : t.dwellings) if (dw.available > 0) { hasU = true; break; }
                            if (hasU && (int)eHero.army.size() < 7) add(t.pos, 250.f);
                        }
                        // Towns
                        for (const auto& t : m_towns) {
                            if (t.ownerId == 0)  add(t.pos, 150.f);
                            else if (t.ownerId == 1) add(t.pos, 200.f);
                        }
                        // Resources
                        for (const auto& r : m_resources) {
                            if (r.ownedBy == eHero.id) continue;
                            add(r.pos, r.type == denialRes ? 120.f : 60.f);
                        }
                        // World objects
                        for (const auto& obj : m_worldObjects) {
                            if (obj.collected) continue;
                            float val = 0.f;
                            if (obj.type == WorldObjectType::ArtifactChest)  val = 80.f;
                            else if (obj.type == WorldObjectType::TreasureChest) val = 70.f;
                            else if (obj.type == WorldObjectType::XPShrine   ||
                                     obj.type == WorldObjectType::ForestShrine||
                                     obj.type == WorldObjectType::StatShrine  ||
                                     obj.type == WorldObjectType::SpellScroll ||
                                     obj.type == WorldObjectType::SwampAltar)  val = 50.f;
                            if (val > 0.f) add(obj.pos, val);
                        }
                        // Player hero
                        if (!softRetreat && !playerGhostWalk) {
                            int dist = HexGrid::distance(eHero.pos, playerHero.pos);
                            if (dominant || dist <= 8 || isRaider)
                                add(playerHero.pos, 300.f);
                        }
                    }

                    if (cands.empty()) break;
                    std::sort(cands.begin(), cands.end(),
                              [](const Cand& a, const Cand& b){ return a.score > b.score; });
                    HexCoord goal = cands[0].pos;

                    auto costFn = [this, &eHero, aggressive](HexCoord c) -> int {
                        const HexTile* t = m_map.getTile(c);
                        if (!t || !eHero.canEnter(t->terrain) || t->blocked) return 999;
                        // Only block passage through player towns, not destination
                        if (!aggressive && t->townId != 0) {
                            for (const auto& town : m_towns)
                                if (town.id == t->townId && town.ownerId == 1) return 999;
                        }
                        int base = eHero.moveCost(t->terrain);
                        if (m_roadHexes.count(c)) base = std::max(1, base / 2);
                        return base;
                    };
                    auto path = Pathfinder::find(m_map, eHero.pos, goal, costFn);
                    if (path.empty()) break;

                    HexCoord next = path[0];
                    const HexTile* nextTile = m_map.getTile(next);
                    if (!nextTile) break;
                    int cost = eHero.moveCost(nextTile->terrain);
                    if (eHero.movePool < cost) break;

                    // Move
                    if (HexTile* old = m_map.getTile(eHero.pos)) old->heroId = 0;
                    eHero.pos = next;
                    eHero.movePool -= cost;
                    if (HexTile* nT = m_map.getTile(eHero.pos)) nT->heroId = eHero.id;

                    // Combat with player?
                    if (eHero.pos == playerHero.pos) {
                        m_lastCombatEnemyId = eHero.id;
                        auto pUnits = makeHeroUnits(playerHero, unitDefs, true);
                        auto eUnits = makeHeroUnits(eHero, unitDefs, false);
                        if (m_watchingAI) {
                            m_fromBattleSim = true;
                            m_simAutoPlay   = true;
                            m_simAutoPlayTimer = 0.f;
                        }
                        enterCombat(playerHero, pUnits, eHero, eUnits);
                        combatTriggered = true;
                        break;
                    }

                    // Collect world objects — apply meaningful effects to enemy hero
                    for (auto& obj : m_worldObjects) {
                        if (obj.collected || obj.pos != eHero.pos) continue;
                        obj.collected = true;
                        if (obj.type == WorldObjectType::XPShrine) {
                            int prevLevel = eHero.level;
                            if (eHero.addXp(obj.value) && eHero.level > prevLevel) {
                                int gained = eHero.level - prevLevel;
                                eHero.attack  += (gained + 1) / 2;
                                eHero.defense += gained / 2;
                                for (int g = 0; g < gained; ++g) aiLearnNextSkill(eHero);
                            }
                            gLog("Enemy %s gained %d XP from shrine\n",
                                   eHero.name.c_str(), obj.value);
                        } else if (obj.type == WorldObjectType::SpellScroll) {
                            bool already = false;
                            for (int sid : eHero.knownSpells)
                                if (sid == obj.value) { already = true; break; }
                            if (!already) eHero.knownSpells.push_back(obj.value);
                        } else if (obj.type == WorldObjectType::StatShrine) {
                            // Alternate ATK and DEF based on current stats
                            if (eHero.attack <= eHero.defense) eHero.attack++;
                            else eHero.defense++;
                        } else if (obj.type == WorldObjectType::ArtifactChest) {
                            eHero.artifactInventory.push_back(obj.value);
                        } else if (obj.type == WorldObjectType::ForestShrine) {
                            int prevLevel = eHero.level;
                            if (eHero.addXp(obj.value) && eHero.level > prevLevel) {
                                int gained = eHero.level - prevLevel;
                                eHero.attack  += (gained + 1) / 2;
                                eHero.defense += gained / 2;
                                for (int g = 0; g < gained; ++g) aiLearnNextSkill(eHero);
                            }
                            gLog("Enemy %s gained %d XP from forest shrine\n",
                                   eHero.name.c_str(), obj.value);
                        } else if (obj.type == WorldObjectType::SwampAltar) {
                            bool already = false;
                            for (int sid : eHero.knownSpells)
                                if (sid == obj.value) { already = true; break; }
                            if (!already) eHero.knownSpells.push_back(obj.value);
                        } else if (obj.type == WorldObjectType::TreasureChest) {
                            // AI always takes gold from multi-choice chests
                            // (no popup, instant collect)
                            gLog("Enemy %s looted chest: +%d gold\n",
                                   eHero.name.c_str(), obj.value);
                        }
                    }

                    // Claim resource node (mine control)
                    if (nextTile->resourceId != 0) {
                        for (auto& r : m_resources) {
                            if (r.id == nextTile->resourceId) {
                                r.ownedBy = eHero.id;
                                break;
                            }
                        }
                    }

                    // Capture neutral towns / siege player towns / garrison at own towns
                    if (nextTile->townId != 0) {
                        for (auto& t : m_towns) {
                            if (t.id != nextTile->townId) continue;
                            if (t.ownerId == eHero.id && veryWeak && !eHero.army.empty()) {
                                // Retreating hero deposits their smallest stack as garrison
                                int weakIdx = 0;
                                for (int i = 1; i < (int)eHero.army.size(); ++i)
                                    if (eHero.army[i].count < eHero.army[weakIdx].count) weakIdx = i;
                                auto& stack = eHero.army[weakIdx];
                                int deposit = stack.count / 2;
                                if (deposit > 0 && t.garrison.size() < 7) {
                                    bool merged = false;
                                    for (auto& gs : t.garrison)
                                        if (gs.defId == stack.defId) { gs.count += deposit; merged = true; break; }
                                    if (!merged) t.garrison.push_back({stack.defId, deposit});
                                    stack.count -= deposit;
                                    if (stack.count == 0)
                                        eHero.army.erase(eHero.army.begin() + weakIdx);
                                }
                                eHero.movePool = 0; // done retreating for this turn
                            } else if (t.ownerId == eHero.id) {
                                // Stepped into own town — recruit immediately and keep moving
                                for (auto& dw : t.dwellings) {
                                    if (dw.available <= 0) continue;
                                    for (const auto& ud : unitDefs) {
                                        if (ud.faction == t.faction && ud.tier == dw.tier && ud.path == dw.path) {
                                            int n = dw.available; dw.available = 0;
                                            bool merged = false;
                                            for (auto& s : eHero.army)
                                                if (s.defId == ud.id) { s.count += n; merged = true; break; }
                                            if (!merged && eHero.army.size() < 7)
                                                eHero.army.push_back({ud.id, n});
                                            break;
                                        }
                                    }
                                }
                            } else if (t.ownerId == 0) {
                                t.ownerId = eHero.id;
                                gLog("Enemy %s captured %s\n", eHero.name.c_str(), t.name.c_str());
                            } else if (t.ownerId == 1) {
                                // Off-screen siege: compare attacker vs garrison strength
                                Hero garHero;
                                garHero.faction = t.faction;
                                garHero.army    = t.garrison;
                                int atkStr = heroStrength(eHero, unitDefs);
                                int defStr = heroStrength(garHero, unitDefs);
                                if (t.hasBuilding(BID::FORT)) defStr = defStr * 3 / 2;
                                if (atkStr > defStr) {
                                    t.ownerId = eHero.id;
                                    t.garrison.clear();
                                    m_lostTownName       = t.name;
                                    m_showTownLostPopup  = true;
                                    gLog("Enemy %s sieged and captured your town %s!\n",
                                           eHero.name.c_str(), t.name.c_str());
                                } else {
                                    gLog("Enemy %s failed to siege %s\n",
                                           eHero.name.c_str(), t.name.c_str());
                                }
                                eHero.movePool = 0; // siege exhausts movement
                            }
                            break;
                        }
                    }
                }
                if (combatTriggered) return;
            }
        }

        // Infestation specialty (Flesh Architect/Amalgamate): FleshZone spreads each turn
        auto applyInfestation = [&](std::vector<Hero>& heroList) {
            for (auto& hero : heroList) {
                if (!hero.infestationSpecialty) continue;
                constexpr int INFEST_RADIUS = 2;
                std::vector<HexCoord> toInfest;
                for (const auto& coord : m_map.coords()) {
                    if (HexGrid::distance(hero.pos, coord) > INFEST_RADIUS) continue;
                    HexTile* t = m_map.getTile(coord);
                    if (!t || t->terrain == Terrain::FleshZone) continue;
                    bool adjacentFlesh = false;
                    for (const auto& nb : HexGrid::neighbors(coord)) {
                        const HexTile* nt = m_map.getTile(nb);
                        if (nt && nt->terrain == Terrain::FleshZone) { adjacentFlesh = true; break; }
                    }
                    Terrain ter = t->terrain;
                    if (adjacentFlesh && (ter == Terrain::Plains || ter == Terrain::Wasteland
                                          || ter == Terrain::Corrupted || ter == Terrain::Barren)) {
                        toInfest.push_back(coord);
                    }
                }
                if (!toInfest.empty()) {
                    int converted = 0;
                    for (const auto& c : toInfest) {
                        if (converted >= 2) break;
                        HexTile* t = m_map.getTile(c);
                        if (t) { t->terrain = Terrain::FleshZone; converted++; }
                    }
                    if (converted > 0) {
                        char buf[48];
                        std::snprintf(buf, sizeof(buf), "Infestation: +%d FleshZone", converted);
                        pushPickupEffect(hero.pos, buf, IM_COL32(180, 100, 60, 255));
                    }
                }
            }
        };
        applyInfestation(m_heroes);
        applyInfestation(m_enemyHeroes);

        // BlightAura specialty (Blight Caller/Voidkin): Sacred terrain near the hero
        // is passively corrupted each turn. Applies to both player and enemy heroes.
        auto applyBlightAura = [&](std::vector<Hero>& heroList) {
            for (auto& hero : heroList) {
                if (!hero.blightAuraSpecialty) continue;
                constexpr int BLIGHT_RADIUS = 3;
                int corrupted = 0;
                for (const auto& coord : m_map.coords()) {
                    if (HexGrid::distance(hero.pos, coord) > BLIGHT_RADIUS) continue;
                    HexTile* t = m_map.getTile(coord);
                    if (t && t->terrain == Terrain::Sacred) {
                        t->terrain = Terrain::Corrupted;
                        corrupted++;
                    }
                }
                if (corrupted > 0) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "BlightAura: %d Sacred → Corrupted", corrupted);
                    pushPickupEffect(hero.pos, buf, IM_COL32(160, 80, 200, 255));
                }
            }
        };
        applyBlightAura(m_heroes);
        applyBlightAura(m_enemyHeroes);

        // ── Siege camp resolution ─────────────────────────────────────────────
        // Update underSiege flag for every town
        for (auto& t : m_towns) t.underSiege = false;
        for (const auto& h : m_heroes) {
            if (!h.isSiegeCamping || h.siegeTargetTownId == 0) continue;
            for (auto& t : m_towns)
                if (t.id == h.siegeTargetTownId) t.underSiege = true;
        }
        // Trigger siege combat for any town that has camped heroes this turn
        for (auto& t : m_towns) {
            if (!t.underSiege) continue;
            // Reset fortify flag for next turn
            bool fortified = t.siegeFortified;
            t.siegeFortified = false;
            triggerSiegeCombat(t.id);
            // triggerSiegeCombat may change game state; stop processing if combat started
            if (m_state == GameState::Combat) return;
        }

        bool newWeek = m_turns.endTurn(m_towns, m_heroes,
                                       m_playerResources, m_registry);
        if (newWeek) {
            // Capture income totals for week summary popup before adding them
            m_weekSummaryIncome = m_turns.calculateWeeklyIncome(m_towns, 1);
            for (const auto& r : m_resources)
                if (r.ownedBy == 1) m_weekSummaryIncome.add(r.type, r.amount);
            m_cachedWeeklyIncome = m_weekSummaryIncome;
            m_weekSummaryWeek = m_turns.week();
            if (!m_watchingAI) m_showWeekSummary = true;

            // Mine income for player-controlled resource nodes
            for (const auto& r : m_resources)
                if (r.ownedBy == 1) m_playerResources.add(r.type, r.amount);

            // Hot-seat: apply income for P2 towns and mines
            if (m_hotSeatMode) {
                Resources p2income = m_turns.calculateWeeklyIncome(m_towns, 2);
                m_player2Resources.addAll(p2income);
                for (const auto& r : m_resources)
                    if (!m_enemyHeroes.empty() && r.ownedBy == m_enemyHeroes[0].id)
                        m_player2Resources.add(r.type, r.amount);
            }

            // Garrison upkeep — 350 gold/week per garrisoned player hero
            {
                int garrisonCount = 0;
                for (const auto& h : m_heroes)
                    if (h.isGarrisoned) ++garrisonCount;
                if (garrisonCount > 0) {
                    int upkeep = garrisonCount * 350;
                    m_playerResources.add(ResourceType::Gold, -upkeep);
                    gLog("Garrison upkeep: -%dg (%d hero%s dug in)\n",
                         upkeep, garrisonCount, garrisonCount == 1 ? "" : "es");
                }
            }

            // Apply March bonus (10% move) for heroes who used March last week
            for (auto& h : m_heroes) {
                h.marchBonusActive = false;  // reset; will re-enable if cooldown was set last week
                if (h.marchCooldownWeek == m_turns.week()) {
                    // Cooldown expires this week — grant the bonus move pool
                    h.movePool = std::min(h.maxMove + h.maxMove / 10, h.movePool + h.maxMove / 10);
                }
            }

            gLog("New week %d — income applied\n", m_turns.week());

            // Enemy hero weekly reinforcements — scale with week number so they stay relevant
            {
                int week = m_turns.week();
                int reinforceCount = 2 + week;  // 3 on week 1, grows by 1 per week
                for (auto& eHero : m_enemyHeroes) {
                    if (eHero.army.empty()) continue;
                    // Count towns owned by this enemy hero
                    int ownedTowns = 0;
                    for (const auto& t : m_towns)
                        if (t.ownerId == eHero.id) ownedTowns++;
                    if (ownedTowns == 0) continue;  // no base → no reinforcements
                    // Add reinforceCount units to the smallest stack (per owned town)
                    int total = reinforceCount * ownedTowns;
                    int smallestIdx = 0;
                    for (int i = 1; i < (int)eHero.army.size(); ++i)
                        if (eHero.army[i].count < eHero.army[smallestIdx].count)
                            smallestIdx = i;
                    eHero.army[smallestIdx].count = std::min(50, eHero.army[smallestIdx].count + total);
                    gLog("Enemy %s reinforced +%d units (week %d, %d towns)\n",
                           eHero.name.c_str(), total, week, ownedTowns);
                }
            }

            // Auto-save at start of each new week
            if (m_state == GameState::Campaign)
                saveGame("saves/campaign" + std::to_string(m_campaignActiveSlot) + ".json");
            else
                saveGame("saves/save" + std::to_string(m_activeSlot) + ".json");

            // ── AI town building: faction-specific priority order ─────────────────
            {
                Resources richRes;
                richRes.add(ResourceType::Gold,         999999);
                richRes.add(ResourceType::Iron,            9999);
                richRes.add(ResourceType::FaithStones,     9999);
                richRes.add(ResourceType::BloodEssence,    9999);
                richRes.add(ResourceType::VerdantSap,      9999);
                richRes.add(ResourceType::Mercury,         9999);

                const auto& allBuildings = m_registry.buildings();

                // Faction-specific build order — first buildable entry wins each week.
                // Entries tried in order; first that satisfies prereqs & resources gets built.
                // PathA upgrade dwellings are included for key tiers (T3-T5).
                static const std::vector<int> kBuildOrder[9] = {
                    // HolyOrder (0): Hall + T1 dwelling first, then Fort
                    { BID::HO_HALL, BID::HO_T1_BASE, BID::FORT, BID::MARKET,
                      BID::MAGE_GUILD, BID::HO_LIGHT_SHRINE, BID::HO_T2_BASE,
                      BID::TOWN_HALL, BID::HO_T3_BASE, BID::HO_T3_A,
                      BID::MAGE_GUILD_T2, BID::HO_T4_BASE, BID::HO_T4_A,
                      BID::CITY_HALL, BID::HO_T5_BASE, BID::HO_T5_A,
                      BID::HO_RELIQUARY, BID::HO_T6_BASE, BID::HO_T6_A },
                    // CrimsonWardens (1): Economy first, T1_A upgrade, T2 early (ranged), Warden Brand
                    { BID::CW_HALL, BID::CW_T1, BID::CW_T1_A, BID::MARKET, BID::CW_T2, BID::CW_T2_A,
                      BID::FORT, BID::CW_WARDEN_BRAND, BID::CW_T3, BID::CW_T3_A,
                      BID::TOWN_HALL, BID::CW_DEATH_ALTAR, BID::CW_T4, BID::CW_T4_A,
                      BID::CITY_HALL, BID::CW_T5, BID::CW_T5_A, BID::CW_T6, BID::CW_T6_A },
                    // Thornkin (2): Symbiosis Web early, T1_A upgrade, key upgrades (PathA=paired)
                    { BID::TK_GROVE_HEART, BID::TK_T1, BID::TK_T1_A, BID::TK_SYMBIOSIS_WEB,
                      BID::MARKET, BID::TK_T2, BID::TK_T2_A,
                      BID::TK_ANCIENT_CIRCLE, BID::FORT, BID::TK_T3, BID::TK_T3_A,
                      BID::TOWN_HALL, BID::TK_T4, BID::TK_T4_A,
                      BID::CITY_HALL, BID::TK_T5, BID::TK_T5_A, BID::TK_T6, BID::TK_T6_A },
                    // EternalEmpire (3): Necropolis + Monument (second-life) ASAP, T1/T2 upgrades
                    { BID::EE_THRONE, BID::EE_T1, BID::EE_T1_A, BID::EE_NECROPOLIS,
                      BID::FORT, BID::EE_T2, BID::EE_T2_A, BID::MARKET,
                      BID::EE_MONUMENT, BID::EE_T3, BID::EE_T3_A,
                      BID::TOWN_HALL, BID::EE_T4, BID::EE_T4_A,
                      BID::MAGE_GUILD, BID::CITY_HALL, BID::EE_T5, BID::EE_T5_A,
                      BID::EE_T6, BID::EE_T6_A },
                    // Bloodsworn (4): Aggression-first — fast T1/T2, Blood Altar early
                    { BID::BS_WAR_HALL, BID::BS_T1, BID::BS_T1_A, BID::BS_T2, BID::BS_T2_A,
                      BID::FORT, BID::MARKET, BID::BS_T3, BID::BS_T3_A,
                      BID::BS_BLOOD_ALTAR, BID::BS_WAR_SHRINE,
                      BID::TOWN_HALL, BID::BS_T4, BID::BS_T4_A,
                      BID::CITY_HALL, BID::BS_T5, BID::BS_T5_A, BID::BS_T6, BID::BS_T6_A },
                    // Voidkin (5): Market gate, Mage Guild (spell-dependent), T1_A, Void Lens
                    { BID::VK_NEXUS, BID::MARKET, BID::VK_T1, BID::VK_T1_A,
                      BID::MAGE_GUILD, BID::VK_T2, BID::VK_T2_A,
                      BID::FORT, BID::VK_RIFT_GATE, BID::VK_T3, BID::VK_T3_A,
                      BID::TOWN_HALL, BID::VK_VOID_LENS, BID::VK_T4, BID::VK_T4_A,
                      BID::CITY_HALL, BID::VK_T5, BID::VK_T5_A, BID::VK_T6, BID::VK_T6_A },
                    // IronAssembly (6): Blueprint Vault early, Overclock, PathA (Runic line)
                    { BID::IA_FORGE_HALL, BID::FORT, BID::IA_T1, BID::IA_T1_A,
                      BID::IA_BLUEPRINT_VAULT, BID::MARKET, BID::IA_T2, BID::IA_T2_A,
                      BID::WAREHOUSE, BID::IA_OVERCLOCK, BID::IA_T3, BID::IA_T3_A,
                      BID::TOWN_HALL, BID::WAREHOUSE_T2, BID::IA_T4, BID::IA_T4_A,
                      BID::CITY_HALL, BID::IA_T5, BID::IA_T5_A, BID::IA_T6, BID::IA_T6_A },
                    // Amalgamate (7): Merge Chamber early (Adaptation), economy
                    { BID::AM_GRAFTING_HALL, BID::AM_T1, BID::AM_T1_A, BID::MARKET,
                      BID::AM_T2, BID::AM_T2_A, BID::FORT, BID::AM_MERGE_CHAMBER,
                      BID::AM_T3, BID::AM_T3_A, BID::TOWN_HALL, BID::AM_FLESH_VAULT,
                      BID::AM_T4, BID::AM_T4_A, BID::CITY_HALL,
                      BID::AM_T5, BID::AM_T5_A, BID::AM_T6, BID::AM_T6_A },
                    // Convergence (8): Economy + all dwellings + path-A upgrades
                    { BID::CV_SYNTHESIS_HUB, BID::CV_T1, BID::CV_T1_A, BID::MARKET,
                      BID::CV_T2, BID::CV_T2_A, BID::FORT, BID::MAGE_GUILD,
                      BID::CV_T3, BID::CV_T3_A, BID::TOWN_HALL,
                      BID::CV_T4, BID::CV_T4_A, BID::CV_RESONANCE_WELL, BID::CITY_HALL,
                      BID::CV_T5, BID::CV_T5_A, BID::CV_MIRROR_CHAMBER, BID::CV_T6, BID::CV_T6_A },
                };

                for (auto& town : m_towns) {
                    if (town.ownerId == 0) continue;
                    // In Watch AI mode also build for player towns using actual resources
                    if (town.ownerId == 1 && !m_watchingAI) continue;
                    town.builtToday = 0;

                    bool built = false;
                    int fIdx = static_cast<int>(town.faction);
                    // Player towns spend real resources; enemy towns have infinite
                    Resources& buildRes = (town.ownerId == 1) ? m_playerResources : richRes;

                    // Try faction priority list first
                    if (fIdx >= 0 && fIdx < 9) {
                        for (int bid : kBuildOrder[fIdx]) {
                            if (town.build(bid, allBuildings, buildRes)) {
                                gLog("AI %s built BID=%d (priority)\n", town.name.c_str(), bid);
                                built = true; break;
                            }
                        }
                    }

                    // Fallback: lowest unbought base dwelling tier
                    for (int tier = 1; tier <= 6 && !built; ++tier) {
                        for (const auto& def : allBuildings) {
                            if (def.category != BuildingCategory::UnitDwelling) continue;
                            if (def.faction != town.faction) continue;
                            if (def.tier != tier) continue;
                            if (def.path != UpgradePath::None) continue;
                            if (town.build(def.id, allBuildings, buildRes)) {
                                gLog("AI %s built %s\n", town.name.c_str(), def.name.c_str());
                                built = true; break;
                            }
                        }
                    }
                    // Last resort: any fort or support building
                    if (!built) {
                        for (const auto& def : allBuildings) {
                            if (def.faction != town.faction && def.faction != FactionId::None) continue;
                            if (def.category != BuildingCategory::Fort &&
                                def.category != BuildingCategory::Support) continue;
                            if (town.build(def.id, allBuildings, buildRes)) {
                                gLog("AI %s built %s\n", town.name.c_str(), def.name.c_str());
                                break;
                            }
                        }
                    }
                }
            }

            // ── Weekly random event ────────────────────────────────────────────
            m_weeklyEventHeadline.clear();
            m_weeklyEventBody.clear();
            m_weekChoiceOptions.clear();
            // Use week number + a pseudo-hash for varied but deterministic events
            int evtRoll = ((m_turns.week() * 2654435761u) >> 8) % 24;
            switch (evtRoll) {
                case 0: { // no event
                    break;
                }
                case 1: { // Merchant's Gift — bonus gold
                    m_playerResources.add(ResourceType::Gold, 500);
                    m_weeklyEventHeadline = "A Merchant's Gift";
                    m_weeklyEventBody = "A wandering trader pays 500 Gold for safe passage through your lands.";
                    break;
                }
                case 2: { // Wandering Wizard — learn a random unknown spell
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        for (int i = 0; i < SPELL_COUNT; ++i) {
                            int sid = ALL_SPELLS[i].id;
                            bool known = false;
                            for (int s : h.knownSpells) if (s == sid) { known = true; break; }
                            if (!known) {
                                h.knownSpells.push_back(sid);
                                m_weeklyEventHeadline = "Wandering Wizard";
                                m_weeklyEventBody = std::string("A sage teaches your hero: ")
                                                  + ALL_SPELLS[i].name + "!";
                                break;
                            }
                        }
                    }
                    break;
                }
                case 3: { // Bandit Raid — lose gold
                    int lost = std::min(200, m_playerResources.get(ResourceType::Gold));
                    m_playerResources.add(ResourceType::Gold, -lost);
                    m_weeklyEventHeadline = "Bandit Raid!";
                    m_weeklyEventBody = "Raiders struck your supply wagons, stealing "
                                      + std::to_string(lost) + " Gold.";
                    break;
                }
                case 4: { // Rich Harvest — bonus resources
                    m_playerResources.add(ResourceType::Gold, 200);
                    m_playerResources.add(ResourceType::Iron, 3);
                    m_weeklyEventHeadline = "Rich Harvest";
                    m_weeklyEventBody = "Abundant yields from your territories: +200 Gold, +3 Iron.";
                    break;
                }
                case 5: { // Heroic Inspiration — XP boost
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        int xpGain = 150;
                        int oldLvl5 = h.level;
                        if (h.addXp(xpGain)) {
                            const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                            if (cls) {
                                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                m_levelUpOffers = LevelUpSystem::generateOffers(
                                    *cls, h.skills, h.level, allSkills, h.faction);
                            }
                            if (m_levelUpOffers.empty())
                                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                            m_pendingLevelUps = h.level - oldLvl5;
                            m_showLevelUpModal = true;
                            { ScriptContext lvCtx; lvCtx.heroId = h.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                            // Level-up particle burst at hero's screen position
                            {
                                float hwx, hwy;
                                m_hexRenderer.grid().hexToWorld(h.pos, hwx, hwy);
                                float hsx, hsy;
                                m_camera.worldToScreen(hwx, hwy, hsx, hsy);
                                m_particles.emit(hsx, hsy, ParticlePreset::LevelUp);
                            }
                        }
                        m_weeklyEventHeadline = "Battle Hardened";
                        m_weeklyEventBody = "Tales of your deeds spread: +"
                                           + std::to_string(xpGain) + " XP.";
                    }
                    break;
                }
                case 6: { // Arcane Font — bonus mana for the hero
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        h.maxMana = std::min(h.maxMana + 5, 99);
                        h.mana    = h.maxMana;
                        m_weeklyEventHeadline = "Arcane Font";
                        m_weeklyEventBody = "A ley-line resonance permanently expands your hero's mana pool by 5.";
                    }
                    break;
                }
                case 7: { // Ancient Armory — hero gains +1 Attack
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        h.attack++;
                        m_weeklyEventHeadline = "Ancient Armory";
                        m_weeklyEventBody = "You unearth a cache of fine weapons from an old war. Your hero gains +1 Attack.";
                    }
                    break;
                }
                case 8: { // Rally! — strongest army stack grows
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        int bestCount = 0; int bestIdx = -1;
                        for (int i = 0; i < (int)h.army.size(); ++i)
                            if (h.army[i].count > bestCount) { bestCount = h.army[i].count; bestIdx = i; }
                        if (bestIdx >= 0) {
                            h.army[bestIdx].count += 5;
                            m_weeklyEventHeadline = "Rally!";
                            m_weeklyEventBody = "Volunteers flock to your banner, reinforcing your ranks with 5 more fighters.";
                        }
                    }
                    break;
                }
                case 9: { // Magical Storm — enemy heroes lose mana
                    for (auto& eh : m_enemyHeroes) eh.mana = std::max(0, eh.mana - 5);
                    m_weeklyEventHeadline = "Magical Storm";
                    m_weeklyEventBody = "A surge of wild magic disperses spell reserves. Enemy heroes lose 5 mana.";
                    break;
                }
                case 10: { // Tribute from Vassals — multi-resource bonus
                    m_playerResources.add(ResourceType::Gold,        300);
                    m_playerResources.add(ResourceType::FaithStones,   2);
                    m_playerResources.add(ResourceType::VerdantSap,    2);
                    m_weeklyEventHeadline = "Tribute from Vassals";
                    m_weeklyEventBody = "Subject villages send tribute: +300 Gold, +2 Faith Stones, +2 Verdant Sap.";
                    break;
                }
                case 11: { // Plague — garrison defenders weakened
                    int lostTotal = 0;
                    for (auto& t : m_towns) {
                        if (t.ownerId != 1 || t.garrison.empty()) continue;
                        for (auto& s : t.garrison) {
                            int lost = std::max(0, s.count / 5);
                            s.count -= lost;
                            lostTotal += lost;
                        }
                    }
                    m_weeklyEventHeadline = "Plague Sweeps the Land!";
                    m_weeklyEventBody = "A virulent sickness culls your town garrisons. "
                        + (lostTotal > 0 ? std::to_string(lostTotal) + " garrison troops perished."
                                         : "Your towns were untouched — no garrison losses.");
                    break;
                }
                case 12: { // Fallen Knight — hero gains +1 Defense
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        h.defense++;
                        m_weeklyEventHeadline = "Fallen Knight's Legacy";
                        m_weeklyEventBody = "You bury a fallen champion and claim his mantle. Your hero gains +1 Defense.";
                    }
                    break;
                }
                case 13: { // Mercenary Camp — strongest stack grows by 8
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        int best = 0, bestIdx = -1;
                        for (int i = 0; i < (int)h.army.size(); ++i)
                            if (h.army[i].count > best) { best = h.army[i].count; bestIdx = i; }
                        if (bestIdx >= 0) {
                            h.army[bestIdx].count += 8;
                            m_weeklyEventHeadline = "Mercenary Camp";
                            m_weeklyEventBody = "Hired blades swell your ranks: +8 fighters join your strongest unit.";
                        }
                    }
                    break;
                }
                case 14: { // Scouting Report — enemy hero mana drained + player gets gold
                    for (auto& eh : m_enemyHeroes) eh.mana = std::max(0, eh.mana - 8);
                    m_playerResources.add(ResourceType::Gold, 150);
                    m_weeklyEventHeadline = "Spy Network Pays Off";
                    m_weeklyEventBody = "Your agents disrupt enemy supply lines: +150 Gold, enemy heroes lose 8 mana.";
                    break;
                }
                case 15: { // Alchemy — rare resources
                    m_playerResources.add(ResourceType::Mercury, 2);
                    m_playerResources.add(ResourceType::BloodEssence, 1);
                    m_weeklyEventHeadline = "Alchemist's Discovery";
                    m_weeklyEventBody = "A rogue alchemist delivers rare reagents: +2 Mercury, +1 Blood Essence.";
                    break;
                }
                case 16: { // Divine Favour — hero fully restores mana
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        h.mana = h.maxMana;
                        m_weeklyEventHeadline = "Divine Favour";
                        m_weeklyEventBody = "A radiant vision renews your hero's magical reserves. Mana fully restored.";
                    }
                    break;
                }
                case 17: { // Enemy Deserters — XP + small unit join
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        int xp = 100;
                        int oldLvl17 = h.level;
                        if (h.addXp(xp)) {
                            const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                            if (cls) {
                                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                m_levelUpOffers = LevelUpSystem::generateOffers(
                                    *cls, h.skills, h.level, allSkills, h.faction);
                            }
                            if (m_levelUpOffers.empty())
                                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                            m_pendingLevelUps = h.level - oldLvl17;
                            m_showLevelUpModal = true;
                            { ScriptContext lvCtx; lvCtx.heroId = h.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                        }
                        // Also add 3 to hero's weakest stack
                        int least = INT32_MAX, leastIdx = -1;
                        for (int i = 0; i < (int)h.army.size(); ++i)
                            if (h.army[i].count > 0 && h.army[i].count < least)
                                { least = h.army[i].count; leastIdx = i; }
                        if (leastIdx >= 0) h.army[leastIdx].count += 3;
                    }
                    m_weeklyEventHeadline = "Enemy Deserters";
                    m_weeklyEventBody = "Enemy soldiers defect to your cause, bringing +100 XP and 3 recruits for your smallest unit.";
                    break;
                }
                case 18: { // Tax Revolt — gold halved (one-time penalty)
                    int lost = m_playerResources.get(ResourceType::Gold) / 2;
                    m_playerResources.add(ResourceType::Gold, -lost);
                    m_weeklyEventHeadline = "Tax Revolt!";
                    m_weeklyEventBody = "Overtaxed peasants revolt and seize half your treasury. Lost: "
                        + std::to_string(lost) + " Gold.";
                    break;
                }
                case 19: { // Titan's Favour — hero max HP +15
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        h.heroMaxHp += 15;
                        h.heroHp = std::min(h.heroHp + 15, h.heroMaxHp);
                        m_weeklyEventHeadline = "Titan's Favour";
                        m_weeklyEventBody = "A titan spirit blesses your hero's endurance. Max HP permanently increased by 15.";
                    }
                    break;
                }
                case 20: { // Merchant's Offer — choice: buy/pass
                    m_weeklyEventHeadline = "Travelling Merchant";
                    m_weeklyEventBody = "A well-stocked merchant arrives at your camp offering rare supplies. He wants gold for his wares.";
                    m_weekChoiceOptions.clear();
                    m_weekChoiceOptions.push_back({"Buy supplies (-800 Gold)", "+4 Iron, +3 faction resource", [this](){
                        if (m_playerResources.get(ResourceType::Gold) >= 800) {
                            m_playerResources.add(ResourceType::Gold, -800);
                            m_playerResources.add(ResourceType::Iron, 4);
                            if (!m_heroes.empty()) {
                                FactionId f = m_heroes[m_activeHeroIdx].faction;
                                ResourceType fRes = ResourceType::Gold;
                                switch (f) {
                                    case FactionId::HolyOrder:
                                    case FactionId::CrimsonWardens:  fRes = ResourceType::FaithStones;  break;
                                    case FactionId::Thornkin:
                                    case FactionId::Voidkin:         fRes = ResourceType::VerdantSap;   break;
                                    case FactionId::EternalEmpire:   fRes = ResourceType::Mercury;      break;
                                    case FactionId::Bloodsworn:
                                    case FactionId::Amalgamate:      fRes = ResourceType::BloodEssence; break;
                                    default: break;
                                }
                                if (fRes != ResourceType::Gold) m_playerResources.add(fRes, 3);
                            }
                        }
                    }});
                    m_weekChoiceOptions.push_back({"Sell surplus (+500 Gold)", "+500 Gold from your excess supplies", [this](){
                        m_playerResources.add(ResourceType::Gold, 500);
                    }});
                    m_weekChoiceOptions.push_back({"Send him away", "Nothing happens.", [](){}});
                    break;
                }
                case 21: { // Mercenary Company — choice: hire/partial/decline
                    m_weeklyEventHeadline = "Mercenary Company";
                    m_weeklyEventBody = "A veteran mercenary company approaches, offering their swords for coin. They have seen many battles.";
                    m_weekChoiceOptions.clear();
                    m_weekChoiceOptions.push_back({"Hire them (-1200 Gold)", "+15 fighters join your largest stack", [this](){
                        if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 1200) {
                            m_playerResources.add(ResourceType::Gold, -1200);
                            Hero& h = m_heroes[m_activeHeroIdx];
                            int best = 0, bestIdx = -1;
                            for (int i = 0; i < (int)h.army.size(); ++i)
                                if (h.army[i].count > best) { best = h.army[i].count; bestIdx = i; }
                            if (bestIdx >= 0) h.army[bestIdx].count += 15;
                        }
                    }});
                    m_weekChoiceOptions.push_back({"Partial hire (-500 Gold)", "+6 fighters join your largest stack", [this](){
                        if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 500) {
                            m_playerResources.add(ResourceType::Gold, -500);
                            Hero& h = m_heroes[m_activeHeroIdx];
                            int best = 0, bestIdx = -1;
                            for (int i = 0; i < (int)h.army.size(); ++i)
                                if (h.army[i].count > best) { best = h.army[i].count; bestIdx = i; }
                            if (bestIdx >= 0) h.army[bestIdx].count += 6;
                        }
                    }});
                    m_weekChoiceOptions.push_back({"Decline", "The mercenaries move on.", [](){}});
                    break;
                }
                case 22: { // Rogue Scholar — choice: buy spell/buy stat/ignore
                    m_weeklyEventHeadline = "Rogue Scholar";
                    m_weeklyEventBody = "An exiled mage-scholar approaches your camp. He offers his forbidden knowledge for coin.";
                    m_weekChoiceOptions.clear();
                    m_weekChoiceOptions.push_back({"Buy a spell (-1500 Gold)", "Learn a new spell you don't know yet", [this](){
                        if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 1500) {
                            m_playerResources.add(ResourceType::Gold, -1500);
                            Hero& h = m_heroes[m_activeHeroIdx];
                            for (int i = 0; i < SPELL_COUNT; ++i) {
                                int sid = ALL_SPELLS[i].id;
                                bool known = false;
                                for (int s : h.knownSpells) if (s == sid) { known = true; break; }
                                if (!known) { h.knownSpells.push_back(sid); break; }
                            }
                        }
                    }});
                    m_weekChoiceOptions.push_back({"Buy tactical insight (-800 Gold)", "+2 to hero Attack and Defense", [this](){
                        if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 800) {
                            m_playerResources.add(ResourceType::Gold, -800);
                            m_heroes[m_activeHeroIdx].attack  += 2;
                            m_heroes[m_activeHeroIdx].defense += 2;
                        }
                    }});
                    m_weekChoiceOptions.push_back({"Chase him off", "You don't trust outlaws.", [](){}});
                    break;
                }
                case 23: { // Ancient Oracle — choice: pay for info/free lesser boon
                    m_weeklyEventHeadline = "Ancient Oracle";
                    m_weeklyEventBody = "An ancient seer emerges from the mist, offering visions of the near future -- for a price.";
                    m_weekChoiceOptions.clear();
                    m_weekChoiceOptions.push_back({"Pay the Oracle (-600 Gold)", "+250 XP and hero mana fully restored", [this](){
                        if (!m_heroes.empty() && m_playerResources.get(ResourceType::Gold) >= 600) {
                            m_playerResources.add(ResourceType::Gold, -600);
                            Hero& h = m_heroes[m_activeHeroIdx];
                            h.mana = h.maxMana;
                            int oldLvl = h.level;
                            if (h.addXp(250) && h.level > oldLvl) {
                                const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                                if (cls) {
                                    std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                    m_levelUpOffers = LevelUpSystem::generateOffers(*cls, h.skills, h.level, allSkills, h.faction);
                                }
                                if (m_levelUpOffers.empty())
                                    m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                                m_pendingLevelUps = h.level - oldLvl;
                                m_showLevelUpModal = true;
                            }
                        }
                    }});
                    m_weekChoiceOptions.push_back({"Accept a free omen", "+100 XP, the seer warns of coming danger", [this](){
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            int oldLvl = h.level;
                            if (h.addXp(100) && h.level > oldLvl) {
                                const HeroClassDef* cls = m_classRegistry.getClass(h.classId);
                                if (cls) {
                                    std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                    m_levelUpOffers = LevelUpSystem::generateOffers(*cls, h.skills, h.level, allSkills, h.faction);
                                }
                                if (m_levelUpOffers.empty())
                                    m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                                m_pendingLevelUps = h.level - oldLvl;
                                m_showLevelUpModal = true;
                            }
                        }
                    }});
                    m_weekChoiceOptions.push_back({"Walk away", "You have no time for riddles.", [](){}});
                    break;
                }
            }

            ScriptContext ctx; ctx.heroId = 0;
            m_triggers.fire(TriggerType::WeekStart, ctx);
            if (m_turns.week() >= 10)
                m_hideout.completeMilestone(Milestone::WEEK_10_REACHED);
            if (m_state == GameState::Campaign) {
                m_campaign.onWeekStart(m_turns.week(), m_lua);
                for (int rt = 0; rt < RESOURCE_COUNT; ++rt) {
                    auto type = static_cast<ResourceType>(rt);
                    m_campaign.onResourcesChecked(type, m_playerResources.get(type));
                }
            }
        }
        if (newWeek) {
            // Add weekly growth to unit dwellings
            for (auto& obj : m_worldObjects) {
                if (obj.type == WorldObjectType::UnitDwelling && !obj.collected) {
                    int tier = obj.value;
                    obj.available += 3 + tier;  // T1=4, T6=9 per week
                }
                // Observatory resets (allow re-use each week)
                if (obj.type == WorldObjectType::Observatory)
                    obj.collected = false;
                // HolyFountain / Oasis reset weekly
                if (obj.type == WorldObjectType::HolyFountain ||
                    obj.type == WorldObjectType::Oasis)
                    obj.collected = false;
                // Captured NeutralOutpost produces 4 T1 units per week
                if (obj.type == WorldObjectType::NeutralOutpost && obj.collected)
                    obj.available += 4;
                // CursedGround: restore one charge per week (up to original max=5)
                if (obj.type == WorldObjectType::CursedGround && obj.questState < 5)
                    obj.questState++;
            }

            // Auto-save at week start if enabled
            if (m_settingsAutoSave) {
                if (m_state == GameState::Campaign)
                    saveGame("saves/campaign" + std::to_string(m_campaignActiveSlot) + ".json");
                else if (m_activeSlot >= 0)
                    saveGame("saves/save" + std::to_string(m_activeSlot) + ".json");
            }
        }
    // Watch AI: passive victory/defeat check (needed when last enemy town captured without combat)
    if (m_watchingAI && !m_showVictory && !m_showDefeat) {
        bool noEnemyHeroes = m_enemyHeroes.empty();
        bool noEnemyTowns  = true;
        for (const auto& t : m_towns)
            if (t.ownerId > 1) { noEnemyTowns = false; break; }
        if (noEnemyHeroes && noEnemyTowns && !m_heroes.empty()) {
            m_showVictory = true;
            m_audio.playSound("victory");
        }
        if (!m_showVictory) {
            bool anyTown = false;
            for (const auto& t : m_towns) if (t.ownerId == 1) { anyTown = true; break; }
            bool anyArmy = !m_heroes.empty() && !m_heroes[m_activeHeroIdx].army.empty();
            if (!anyTown && !anyArmy) { m_showDefeat = true; m_finalDefeat = true; }
        }
    }
    }

// ── World map render ──────────────────────────────────────────────────────────
void Game::renderWorldMapImGui()
{
    m_ui.beginFrame();
    m_worldHUD.draw(m_ui,
                    (m_hotSeatMode && m_hotSeatP2Turn) ? m_player2Resources : m_playerResources,
                    m_cachedWeeklyIncome,
                    m_turns, m_heroes, m_activeHeroIdx, m_towns);
    m_ui.endFrame();
    m_ui.flushText(ImGui::GetBackgroundDrawList());
    renderWorldOverlay();

    // Non-game-state overlays (can coexist with most things)
    if (m_showWorldSpellPanel)    renderWorldSpellPanel();
    if (m_showKingdomPanel)       renderKingdomPanel();
    if (m_showTownPortalPopup)    renderTownPortalPopup();
    if (m_showFoundCityPopup)     renderFoundCityPopup();
    if (m_showHideoutScreen)      renderHideoutScreen();
    if (m_showArtifactPanel)      renderArtifactPanel();
    if (m_showHeroInspect)        renderHeroInspect();
    if (m_showUnitExchange)       renderUnitExchange();
    if (m_showDwellingPopup)      renderDwellingPopup();
    if (m_showStatShrinePopup)    renderStatShrinePopup();
    if (m_showQuestPopup)         renderQuestPopup();
    if (m_showTreasureChestPopup) renderTreasureChestPopup();
    if (m_showCryptPopup)         renderCryptPopup();
    if (m_showUtopiaPopup)        renderUtopiaPopup();
    if (m_showMineInfoPopup)      renderMineInfoPopup();
    if (m_showTreeKnowledgePopup) renderTreeOfKnowledgePopup();
    if (m_showShipyardPopup)      renderShipyardPopup();
    if (m_showEncounterPrompt)    renderEncounterPrompt();
    if (m_showTownLostPopup)      renderTownLostPopup();
    if (m_showWeekSummary)        renderWeekSummary();
    if (m_hotSeatHandoff)         renderHotSeatHandoff();
    if (m_showSiegeCampPrompt)    renderSiegeCampPrompt();
    renderSiegeIndicator();
    renderMarchButton();
    if (m_showPauseMenu)          renderPauseMenu();
    // Modal popups — only one at a time (ImGui popup stack conflict otherwise)
    // In campaign mode, victory/defeat are handled by the campaign HUD, not these modals.
    bool inCampaign = (m_state == GameState::Campaign);
    if (m_showCombatResult)                     renderCombatResultPopup();
    else if (m_showVictory && !inCampaign)      renderVictoryModal();
    else if (m_showDefeat  && !inCampaign)      renderDefeatModal();
    else if (m_showLevelUpModal)                renderLevelUpModal();

    // Watch AI overlay — minimal HUD showing current week + stop button
    if (m_watchingAI) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 8), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.80f);
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##watchai_hud", nullptr, wf)) {
            static const char* kFacNames[] = {
                "Holy Order","Crimson Wardens","Thornkin","Eternal Empire",
                "Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"
            };
            ImGui::TextColored({1.f,0.82f,0.2f,1.f}, "WATCH AI vs AI");
            ImGui::SameLine(0, 16);
            ImGui::Text("Week %d | %s vs %s",
                m_turns.week(),
                kFacNames[m_watchAIFaction1], kFacNames[m_watchAIFaction2]);
            ImGui::SameLine(0, 16);
            ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("Speed##waispeed", &m_watchAISpeed, 0.25f, 8.0f, "%.1fx");
            ImGui::SameLine(0, 8);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop##waistop")) {
                m_watchingAI = false;
                m_state = GameState::MainMenu;
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }
}

void Game::renderWorldMap()
{
    // Ocean blue fills gaps at the circular map boundary (corners of screen have no hexes)
    glClearColor(0.04f, 0.12f, 0.30f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_hexRenderer.render(m_map, m_camera, m_hovered, m_selected, m_fogDisabled);

    float proj[16];
    m_camera.getMatrix(proj);
    m_batch.begin(proj);
    m_batch.end();

    for (auto& hero : m_heroes)
        drawHero(hero);
    for (auto& hero : m_enemyHeroes)
        drawHero(hero);

    beginImGuiFrame();
    renderWorldMapImGui();
    endImGuiFrame();
}

// ── State transition ──────────────────────────────────────────────────────────
void Game::enterWorldMap()
{
    m_state = GameState::WorldMap;
    gLog("Entered world map\n");
}

// ── Tile click ────────────────────────────────────────────────────────────────
void Game::onTileClicked(HexCoord h)
{
    const HexTile* tile = m_map.getTile(h);
    if (!tile) return;

    if (m_heroes.empty()) return;

    // Left-click on a player-owned town: open if hero is on/adjacent, else path there
    if (tile->townId != 0) {
        Hero& clickHero = m_heroes[m_activeHeroIdx];
        for (auto& t : m_towns) {
            if (t.id != tile->townId || t.ownerId != 1) continue;
            if (clickHero.pos == h || HexGrid::distance(clickHero.pos, h) <= 1) {
                enterTown(&t);
                return;
            }
            break; // far away — fall through to pathfinding
        }
    }

    Hero& hero = m_heroes[m_activeHeroIdx];
    if (!hero.canEnter(tile->terrain) || tile->blocked) return;
    if (m_moveT < 1.0f) return;

    auto costFn = [this, &hero](HexCoord c) -> int {
        const HexTile* t = m_map.getTile(c);
        if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
        int base = hero.moveCost(t->terrain);
        if (m_roadHexes.count(c)) base = std::max(1, base / 2);
        return base;
    };

    auto path = Pathfinder::find(m_map, hero.pos, h, costFn);
    if (path.empty()) return;

    hero.path     = path;
    hero.pathStep = 0;
    m_selected    = h;

    m_reachable = Pathfinder::reachable(m_map, hero.pos, costFn, hero.movePool);
}

// ── Hero movement ─────────────────────────────────────────────────────────────
void Game::updateHeroMovement(float dt)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];

    if (m_moveT < 1.0f) {
        m_moveT = std::min(1.0f, m_moveT + dt * MOVE_SPEED);
        if (m_moveT >= 1.0f)
            checkTileEvents();
        return;
    }

    if (!hero.path.empty() && hero.pathStep < static_cast<int>(hero.path.size())) {
        HexCoord next = hero.path[hero.pathStep];
        const HexTile* tile = m_map.getTile(next);
        int cost = tile ? hero.moveCost(tile->terrain) : 999;
        if (m_roadHexes.count(next)) cost = std::max(1, cost / 2);

        if (hero.movePool < cost) {
            hero.path.clear();
            hero.pathStep = 0;
            return;
        }

        float sx, sy, dx, dy;
        m_hexRenderer.grid().hexToWorld(hero.pos, sx, sy);
        m_hexRenderer.grid().hexToWorld(next, dx, dy);

        m_moveSrcX = sx; m_moveSrcY = sy;
        m_moveDstX = dx; m_moveDstY = dy;
        m_moveT    = 0.0f;

        // Update tile hero IDs for this player hero
        if (HexTile* oldT = m_map.getTile(hero.pos)) oldT->heroId = 0;
        hero.pos = next;
        hero.movePool -= cost;
        hero.pathStep++;
        if (HexTile* newT = m_map.getTile(hero.pos)) newT->heroId = hero.id;

        // Disembark when stepping from water onto land
        if (hero.onBoat && tile && tile->terrain != Terrain::Water)
            hero.onBoat = false;

        FogOfWar::updateVision(m_map, hero);

        if (hero.pathStep >= static_cast<int>(hero.path.size())) {
            hero.path.clear();
            hero.pathStep = 0;
            m_selected = {-999,-999};
        }
    }

    // ── Hot-seat P2: animate selected enemy hero movement ─────────────────────
    if (m_hotSeatMode && m_hotSeatP2Turn
        && m_selectedEnemyHero >= 0
        && m_selectedEnemyHero < (int)m_enemyHeroes.size()) {
        Hero& eh = m_enemyHeroes[m_selectedEnemyHero];
        if (!eh.path.empty() && eh.pathStep < (int)eh.path.size()) {
            HexCoord next = eh.path[eh.pathStep];
            const HexTile* tile = m_map.getTile(next);
            int cost = tile ? eh.moveCost(tile->terrain) : 999;
            if (eh.movePool < cost) {
                eh.path.clear(); eh.pathStep = 0;
            } else {
                if (HexTile* oldT = m_map.getTile(eh.pos)) oldT->heroId = 0;
                eh.pos = next;
                eh.movePool -= cost;
                eh.pathStep++;
                if (HexTile* newT = m_map.getTile(eh.pos)) newT->heroId = eh.id;
                if (!m_fogDisabled) FogOfWar::updateVision(m_map, eh);
                // Collect nearby resources/objects (simplified)
                for (auto& r : m_resources) {
                    if (r.pos == eh.pos && r.ownedBy == 0) {
                        r.ownedBy = eh.id;
                        m_player2Resources.add(r.type, r.amount);
                        char buf[32]; std::snprintf(buf, sizeof(buf), "+%d", r.amount);
                        pushPickupEffect(eh.pos, buf, IM_COL32(100, 200, 255, 255));
                    }
                }
                if (eh.pathStep >= (int)eh.path.size()) {
                    eh.path.clear(); eh.pathStep = 0;
                }
            }
        }
    }
}

// ── Tile events ───────────────────────────────────────────────────────────────
void Game::checkTileEvents()
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    const HexTile* tile = m_map.getTile(hero.pos);
    if (!tile) return;

    ScriptContext ctx;
    ctx.heroId     = static_cast<int>(hero.id);
    ctx.tileQ      = hero.pos.q;
    ctx.tileR      = hero.pos.r;
    ctx.playerSide = true;
    m_triggers.fireTileEnter(hero.pos, ctx);
    m_triggers.fire(TriggerType::EnterTile, ctx);

    if (m_state == GameState::Campaign)
        m_campaign.onTileReached(hero.pos);

    // ── Terrain traversal effects ────────────────────────────────────────────
    {
        // Count total army strength to scale danger
        int totalCount = 0;
        for (const auto& s : hero.army) totalCount += s.count;

        switch (tile->terrain) {
        case Terrain::Toxic: {
            // Poisonous vapors — kill one unit from the smallest non-empty stack
            int smallestCount = INT32_MAX, smallestIdx = -1;
            for (int i = 0; i < (int)hero.army.size(); ++i)
                if (hero.army[i].count > 0 && hero.army[i].count < smallestCount)
                    { smallestCount = hero.army[i].count; smallestIdx = i; }
            if (smallestIdx >= 0 && totalCount > 1) {
                hero.army[smallestIdx].count = std::max(0, hero.army[smallestIdx].count - 1);
                pushPickupEffect(hero.pos, "Toxic vapors — 1 unit lost!", IM_COL32(130, 200, 50, 255));
            }
            hero.mana = std::max(0, hero.mana - 1);
            break;
        }
        case Terrain::Volcanic: {
            // Lava heat — kill one unit from a random stack (not the last one)
            if (totalCount > 2 && !hero.army.empty()) {
                int idx = static_cast<int>((hero.pos.q * 31 + hero.pos.r * 17) % (int)hero.army.size());
                for (int i = 0; i < (int)hero.army.size(); ++i) {
                    int try_ = (idx + i) % (int)hero.army.size();
                    if (hero.army[try_].count > 0) {
                        hero.army[try_].count--;
                        pushPickupEffect(hero.pos, "Volcanic heat — 1 unit slain!", IM_COL32(220, 80, 30, 255));
                        break;
                    }
                }
            }
            break;
        }
        case Terrain::Corrupted:
        case Terrain::CorruptedForest:
            // Dark energy — mana drain
            if (hero.mana > 0) {
                hero.mana = std::max(0, hero.mana - 2);
                pushPickupEffect(hero.pos, "Corrupted — -2 mana", IM_COL32(160, 60, 200, 255));
            }
            break;
        case Terrain::Sacred:
            // Holy ground — restore 1 unit to weakest stack and heal hero HP
            if (!hero.army.empty()) {
                int leastCount = INT32_MAX, leastIdx = -1;
                for (int i = 0; i < (int)hero.army.size(); ++i)
                    if (hero.army[i].count > 0 && hero.army[i].count < leastCount)
                        { leastCount = hero.army[i].count; leastIdx = i; }
                if (leastIdx >= 0) hero.army[leastIdx].count++;
            }
            hero.heroHp = std::min(hero.heroMaxHp, hero.heroHp + 5);
            pushPickupEffect(hero.pos, "Sacred ground — healing", IM_COL32(200, 255, 180, 255));
            break;
        case Terrain::Industrial:
            // Machine district — passive gold income
            m_playerResources.add(ResourceType::Gold, 10);
            pushPickupEffect(hero.pos, "+10 Gold", IM_COL32(255, 215, 50, 255));
            break;
        default: break;
        }
    }

    // World objects (scrolls, chests, shrines, etc.)
    for (auto& obj : m_worldObjects) {
        if (obj.pos != hero.pos) continue;

        switch (obj.type) {
        case WorldObjectType::SpellScroll:
            if (!obj.collected) {
                obj.collected = true;
                bool already = false;
                for (int sid : hero.knownSpells) if (sid == obj.value) { already = true; break; }
                if (!already) {
                    hero.knownSpells.push_back(obj.value);
                    const SpellDef* sp = findSpell(obj.value);
                    char sBuf[64];
                    std::snprintf(sBuf, sizeof(sBuf), "Learned: %s!", sp ? sp->name : "Spell");
                    pushPickupEffect(obj.pos, sBuf, IM_COL32(180, 120, 255, 255));
                    m_audio.playSound("spell");
                    gLog("Hero learned spell %d from scroll\n", obj.value);
                }
            }
            break;
        case WorldObjectType::ArtifactChest:
            if (!obj.collected) {
                obj.collected = true;
                hero.artifactInventory.push_back(obj.value);
                pushPickupEffect(obj.pos, "Artifact found!", IM_COL32(255, 200, 80, 255));
                m_audio.playSound("pickup");
                gLog("Hero picked up artifact %d\n", obj.value);
            }
            break;
        case WorldObjectType::XPShrine:
            if (!obj.collected) {
                obj.collected = true;
                char xpBuf[32]; std::snprintf(xpBuf, sizeof(xpBuf), "+%d XP", obj.value);
                pushPickupEffect(obj.pos, xpBuf, IM_COL32(160, 255, 160, 255));
                m_audio.playSound("pickup");
                {
                    int oldLvlXP = hero.level;
                    if (hero.addXp(obj.value)) {
                        const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                        if (cls) {
                            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                            m_levelUpOffers = LevelUpSystem::generateOffers(
                                *cls, hero.skills, hero.level, allSkills, hero.faction);
                        }
                        if (m_levelUpOffers.empty())
                            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                        m_pendingLevelUps = hero.level - oldLvlXP;
                        m_showLevelUpModal = true;
                        m_audio.playSound("levelup");
                        { ScriptContext lvCtx; lvCtx.heroId = hero.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                    }
                }
                gLog("Hero gained %d XP from shrine\n", obj.value);
            }
            break;
        case WorldObjectType::ResourceCache:
            if (!obj.collected) {
                obj.collected = true;
                m_playerResources.add(obj.resourceType, obj.value);
                char resBuf[48]; std::snprintf(resBuf, sizeof(resBuf), "+%d %s",
                    obj.value, resourceName(obj.resourceType));
                pushPickupEffect(obj.pos, resBuf, IM_COL32(255, 215, 80, 255));
                m_audio.playSound("pickup");
                gLog("Hero found resource cache: %d %s\n", obj.value, resourceName(obj.resourceType));
            }
            break;
        case WorldObjectType::Observatory:
            if (!obj.collected) {
                obj.collected = true;  // will reset weekly
                // Reveal tiles in radius
                auto cells = HexGrid::range(hero.pos, obj.value);
                for (auto& c : cells) {
                    if (HexTile* t = m_map.getTile(c)) {
                        t->explored = true;
                        t->visible  = true;
                    }
                }
                pushPickupEffect(obj.pos, "Map revealed!", IM_COL32(220, 200, 120, 255));
                m_audio.playSound("pickup");
                gLog("Observatory: revealed %d tiles in radius %d\n",
                       static_cast<int>(cells.size()), obj.value);
            }
            break;
        case WorldObjectType::StatShrine:
            if (obj.questState > 0) {
                m_pendingObjId = obj.id;
                m_showStatShrinePopup = true;
            }
            break;
        case WorldObjectType::BanditCamp:
            if (!obj.collected) {
                int diff = obj.value;
                int weekScale = std::max(1, (m_turns.week() + 1) / 2); // doubles every 2 weeks
                Hero banditHero;
                banditHero.id      = 0;
                banditHero.name    = "Bandit Leader";
                banditHero.faction = FactionId::None;
                std::vector<CombatUnit> banditUnits;
                {
                    CombatUnit u;
                    u.id = 50; u.name = "Bandit"; u.count = 5 * diff * weekScale;
                    u.maxHp = u.hp = 5; u.attack = 2 + diff; u.defense = 1 + diff;
                    u.speed = 5; u.range = 0; u.shotsLeft = 0;
                    u.isPlayer = false;
                    banditUnits.push_back(u);
                    if (diff >= 2) {
                        CombatUnit u2;
                        u2.id = 51; u2.name = "Bandit Archer"; u2.count = 3 * diff * weekScale;
                        u2.maxHp = u2.hp = 4; u2.attack = 3; u2.defense = 1;
                        u2.speed = 4; u2.range = 4; u2.shotsLeft = u2.shots = 8;
                        u2.isPlayer = false;
                        banditUnits.push_back(u2);
                    }
                }
                // Build encounter description for prompt
                std::string desc = "Bandits x" + std::to_string(banditUnits[0].count);
                if (banditUnits.size() > 1)
                    desc += " + Archers x" + std::to_string(banditUnits[1].count);
                uint32_t objId = obj.id;
                m_encounterTitle        = "Bandit Camp (Difficulty " + std::to_string(diff) + ")";
                m_pendingEncounterHero  = banditHero;
                m_pendingEncounterUnits = banditUnits;
                m_encounterOnAccept = [this, objId]() {
                    for (auto& o : m_worldObjects) {
                        if (o.id != objId) continue;
                        o.collected = true;
                        m_lastBanditCampId     = objId;
                        m_lastCombatEnemyId    = 0;
                        m_pendingTownCaptureId = 0;
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                            enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                        }
                        break;
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) {
                        auto& h = m_heroes[m_activeHeroIdx];
                        h.path.clear(); h.pathStep = 0;
                    }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;
        case WorldObjectType::UnitDwelling:
            if (obj.available > 0) {
                m_pendingObjId = obj.id;
                m_showDwellingPopup = true;
            }
            break;
        case WorldObjectType::QuestGiver:
            if (obj.questState == 0) {
                m_pendingObjId = obj.id;
                m_showQuestPopup = true;
            } else if (obj.questState == 1) {
                // Check if QuestTarget was collected
                for (const auto& other : m_worldObjects) {
                    if (other.id == obj.linkedId && other.collected) {
                        // Quest complete — reward scales with hero level
                        const_cast<WorldObject&>(obj).questState = 2;
                        if (m_heroes.empty()) break;
                        Hero& qHero = m_heroes[m_activeHeroIdx];
                        int goldReward = 300 + qHero.level * 100;
                        // Bonus: rare resource or XP
                        int xpReward = 50 + qHero.level * 20;
                        m_playerResources.add(ResourceType::Gold, goldReward);
                        int oldLvlQ = qHero.level;
                        char qBuf[48];
                        std::snprintf(qBuf, sizeof(qBuf), "+%dg +%dXP Quest!", goldReward, xpReward);
                        pushPickupEffect(obj.pos, qBuf, IM_COL32(255, 215, 50, 255));
                        m_audio.playSound("levelup");
                        if (qHero.addXp(xpReward)) {
                            const HeroClassDef* cls = m_classRegistry.getClass(qHero.classId);
                            if (cls) {
                                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                                m_levelUpOffers = LevelUpSystem::generateOffers(
                                    *cls, qHero.skills, qHero.level, allSkills, qHero.faction);
                            }
                            if (m_levelUpOffers.empty())
                                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                            m_pendingLevelUps = qHero.level - oldLvlQ;
                            m_showLevelUpModal = true;
                            { ScriptContext lvCtx; lvCtx.heroId = qHero.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                        }
                        gLog("Quest complete! Rewarded %d gold + %d XP\n", goldReward, xpReward);
                        break;
                    }
                }
            }
            break;
        case WorldObjectType::QuestTarget:
            if (!obj.collected) {
                obj.collected = true;
                pushPickupEffect(obj.pos, "Target reached!", IM_COL32(180, 255, 140, 255));
                for (auto& other : m_worldObjects) {
                    if (other.id == obj.linkedId) {
                        if (other.questState == 1)
                            gLog("Quest target reached! Return to quest giver.\n");
                        break;
                    }
                }
            }
            break;
        case WorldObjectType::ForestShrine:
            if (!obj.collected) {
                obj.collected = true;
                char buf[32]; std::snprintf(buf, sizeof(buf), "+%d XP", obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(120, 220, 120, 255));
                m_audio.playSound("pickup");
                {
                    int oldLvlFS = hero.level;
                    if (hero.addXp(obj.value)) {
                        const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                        if (cls) {
                            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                            m_levelUpOffers = LevelUpSystem::generateOffers(
                                *cls, hero.skills, hero.level, allSkills, hero.faction);
                        }
                        if (m_levelUpOffers.empty())
                            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                        m_pendingLevelUps = hero.level - oldLvlFS;
                        m_showLevelUpModal = true;
                        { ScriptContext lvCtx; lvCtx.heroId = hero.id; m_triggers.fire(TriggerType::HeroLevel, lvCtx); }
                    }
                }
            }
            break;
        case WorldObjectType::HighlandRuin:
            if (!obj.collected) {
                obj.collected = true;
                auto cells = HexGrid::range(hero.pos, obj.value);
                for (auto& c : cells) {
                    if (HexTile* t = m_map.getTile(c)) { t->explored = true; t->visible = true; }
                }
                pushPickupEffect(obj.pos, "Revealed!", IM_COL32(200, 180, 120, 255));
            }
            break;
        case WorldObjectType::HolyFountain:
            if (!obj.collected) {
                obj.collected = true;
                hero.mana = hero.maxMana;
                pushPickupEffect(obj.pos, "Mana restored!", IM_COL32(100, 180, 255, 255));
                m_audio.playSound("spell");
            }
            break;
        case WorldObjectType::Oasis:
            if (!obj.collected) {
                obj.collected = true;
                hero.movePool = hero.maxMove;
                pushPickupEffect(obj.pos, "Movement!", IM_COL32(160, 220, 100, 255));
                m_audio.playSound("pickup");
            }
            break;
        case WorldObjectType::Campfire:
            if (!obj.collected) {
                obj.collected = true;
                m_playerResources.add(ResourceType::Gold, obj.value);
                char buf[32]; std::snprintf(buf, sizeof(buf), "+%d Gold", obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(255, 215, 0, 255));
                m_audio.playSound("pickup");
            }
            break;
        case WorldObjectType::LavaCrystal:
            if (!obj.collected) {
                obj.collected = true;
                m_playerResources.add(obj.resourceType, obj.value);
                char resBuf2[32]; std::snprintf(resBuf2, sizeof(resBuf2), "+%d %s",
                    obj.value, resourceName(obj.resourceType));
                pushPickupEffect(obj.pos, resBuf2, IM_COL32(200, 80, 80, 255));
                m_audio.playSound("pickup");
            }
            break;
        case WorldObjectType::SwampAltar:
            if (!obj.collected) {
                obj.collected = true;
                bool already = false;
                for (int sid : hero.knownSpells) if (sid == obj.value) { already = true; break; }
                if (!already) hero.knownSpells.push_back(obj.value);
                pushPickupEffect(obj.pos, "Spell learned!", IM_COL32(180, 100, 255, 255));
                m_audio.playSound("spell");
            }
            break;
        case WorldObjectType::TreasureChest:
            if (!obj.collected) {
                // Floor sub-minimum values (old saves, editor-placed chests)
                if (obj.value < 500)
                    obj.value = 500 + hero.level * 50;
                if (obj.questState < 300)
                    obj.questState = 300 + hero.level * 30;
                m_pendingChestId          = obj.id;
                m_showTreasureChestPopup  = true;
                // Don't mark collected yet — the popup choice does that
            }
            break;

        case WorldObjectType::Crypt:
            if (!obj.collected) {
                Hero cryptHero;
                cryptHero.id      = 0;
                cryptHero.name    = "Crypt Keeper";
                cryptHero.faction = static_cast<FactionId>(obj.faction % 9);
                int diff = std::max(1, obj.value);
                float wm = std::min(3.0f, 1.0f + (m_turns.week() - 1) * 0.15f);
                std::vector<CombatUnit> cryptUnits;
                {
                    static const char* kCryptNames[] = {
                        "Skeleton Warrior","Zombie","Cursed Knight","Wraithling","Bone Golem"
                    };
                    for (int si = 0; si < 3; ++si) {
                        CombatUnit cu;
                        cu.name    = kCryptNames[(obj.faction + si) % 5];
                        cu.count   = static_cast<int>(std::round((4 + si * 2) * diff * wm));
                        cu.maxHp   = cu.hp = 5 + si * 4;
                        cu.attack  = 2 + si + diff;
                        cu.defense = 1 + si + diff / 2;
                        cu.speed   = 5 - si;
                        if (si == 2) { cu.range = 3; cu.shots = cu.shotsLeft = 5; }
                        cu.isPlayer = false;
                        cryptUnits.push_back(cu);
                    }
                }
                uint32_t objId = obj.id;
                m_encounterTitle        = std::string("Crypt (") + cryptHero.name + ")";
                m_pendingEncounterHero  = cryptHero;
                m_pendingEncounterUnits = cryptUnits;
                m_encounterOnAccept = [this, objId]() {
                    m_pendingCryptId       = objId;
                    m_lastCombatEnemyId    = 0;
                    m_pendingTownCaptureId = 0;
                    m_lastBanditCampId     = 0;
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                        enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;

        case WorldObjectType::Utopia:
            if (!obj.collected) {
                Hero utopiaHero;
                utopiaHero.id      = 0;
                utopiaHero.name    = "Ancient Guardian";
                utopiaHero.faction = static_cast<FactionId>(obj.faction % 9);
                static const char* kUtoNames[] = {
                    "Titan","Dragon","Archon","Behemoth","Leviathan",
                    "Void Lord","Colossus","Flesh Titan","Eternal"
                };
                float wm = std::min(3.0f, 1.0f + (m_turns.week() - 1) * 0.12f);
                std::vector<CombatUnit> utoUnits;
                // obj.value == -1 marks a tutorial Utopia (weakened guards)
                bool tutorialUtopia = (obj.value == -1);
                if (tutorialUtopia) {
                    CombatUnit c1; c1.id=50; c1.name="Ruin Warden";   c1.count=6; c1.maxHp=c1.hp=18; c1.attack=6; c1.defense=4; c1.speed=4; c1.isPlayer=false; utoUnits.push_back(c1);
                    CombatUnit c2; c2.id=51; c2.name="Ruin Sentinel"; c2.count=4; c2.maxHp=c2.hp=28; c2.attack=9; c2.defense=6; c2.speed=3; c2.isPlayer=false; utoUnits.push_back(c2);
                } else {
                for (int si = 0; si < 4; ++si) {
                    CombatUnit cu;
                    cu.name    = kUtoNames[(obj.faction + si) % 9];
                    cu.count   = static_cast<int>(std::round((8 + si * 3) * wm));
                    cu.maxHp   = cu.hp = 40 + si * 10;
                    cu.attack  = 18 + si * 4;
                    cu.defense = 14 + si * 3;
                    cu.speed   = 6 + si;
                    if (si >= 2) { cu.flying = true; }
                    if (si == 3) { cu.range = 5; cu.shots = cu.shotsLeft = 4; }
                    cu.isPlayer = false;
                    utoUnits.push_back(cu);
                }
                }
                uint32_t objId = obj.id;
                m_encounterTitle        = "Utopia (Ancient Guardian)";
                m_pendingEncounterHero  = utopiaHero;
                m_pendingEncounterUnits = utoUnits;
                m_encounterOnAccept = [this, objId]() {
                    m_pendingUtopiaId      = objId;
                    m_lastCombatEnemyId    = 0;
                    m_pendingTownCaptureId = 0;
                    m_lastBanditCampId     = 0;
                    if (!m_heroes.empty()) {
                        Hero& h = m_heroes[m_activeHeroIdx];
                        auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                        enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;

        case WorldObjectType::Landmark:
            if (!obj.collected) {
                obj.collected = true;
                // Gain XP from visiting this historically significant site
                static const char* kLandmarkNames[] = {
                    "Ancient Monolith","Ruined Temple","Forgotten Citadel",
                    "Crumbling Observatory","Sunken Shrine"
                };
                int nameIdx = (obj.pos.q * 7 + obj.pos.r * 3) % 5;
                char buf[80];
                std::snprintf(buf, sizeof(buf), "%s: +%d XP",
                    kLandmarkNames[nameIdx], obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(220, 200, 120, 255));
                m_audio.playSound("pickup");
                int oldLvl = hero.level;
                if (hero.addXp(obj.value)) {
                    const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                    if (cls) {
                        std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                        m_levelUpOffers = LevelUpSystem::generateOffers(
                            *cls, hero.skills, hero.level, allSkills, hero.faction);
                    }
                    if (m_levelUpOffers.empty())
                        m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                    m_pendingLevelUps = hero.level - oldLvl;
                    m_showLevelUpModal = true;
                    m_audio.playSound("levelup");
                    ScriptContext lvCtx; lvCtx.heroId = hero.id;
                    m_triggers.fire(TriggerType::HeroLevel, lvCtx);
                }
            }
            break;

        case WorldObjectType::CursedGround:
            if (obj.questState > 0) {
                // Each crossing triggers a curse charge
                obj.questState--;
                int totalArmy = 0;
                for (const auto& s : hero.army) totalArmy += s.count;
                if (totalArmy > 1) {
                    // Kill units proportional to damage value
                    int dmg = obj.value;
                    for (auto& s : hero.army) {
                        if (s.count > 0 && dmg > 0) {
                            int kill = std::min(s.count - (totalArmy > s.count ? 0 : 1), dmg / 10);
                            kill = std::max(0, kill);
                            s.count -= kill;
                            dmg -= kill * 10;
                        }
                    }
                }
                char buf[48];
                std::snprintf(buf, sizeof(buf), "Cursed! -%d HP (charges: %d)",
                    obj.value, obj.questState);
                pushPickupEffect(obj.pos, buf, IM_COL32(160, 40, 200, 255));
                m_audio.playSound("hit");
            }
            break;

        case WorldObjectType::NeutralOutpost:
            if (!obj.collected) {
                Hero outpostHero;
                outpostHero.id      = 0;
                outpostHero.name    = "Outpost Guard";
                outpostHero.faction = static_cast<FactionId>(obj.faction % 9);
                float wm = std::min(2.5f, 1.0f + (m_turns.week() - 1) * 0.12f);
                std::vector<CombatUnit> outUnits;
                {
                    CombatUnit ou;
                    ou.name    = "Outpost Sentry";
                    ou.count   = static_cast<int>(std::round((6 + obj.value * 3) * wm));
                    ou.maxHp   = ou.hp = 5 + obj.value * 2;
                    ou.attack  = 2 + obj.value;
                    ou.defense = 1 + obj.value;
                    ou.speed   = 4;
                    ou.isPlayer = false; ou.factionHint = obj.faction;
                    outUnits.push_back(ou);
                    CombatUnit ou2;
                    ou2.name   = "Outpost Archer";
                    ou2.count  = static_cast<int>(std::round((3 + obj.value * 2) * wm));
                    ou2.maxHp  = ou2.hp = 4;
                    ou2.attack = 3; ou2.defense = 1;
                    ou2.speed  = 5; ou2.range = 4;
                    ou2.shots  = ou2.shotsLeft = 8;
                    ou2.isPlayer = false; ou2.factionHint = obj.faction;
                    outUnits.push_back(ou2);
                }
                uint32_t objId = obj.id;
                m_encounterTitle        = "Neutral Outpost";
                m_pendingEncounterHero  = outpostHero;
                m_pendingEncounterUnits = outUnits;
                m_encounterOnAccept = [this, objId]() {
                    for (auto& o : m_worldObjects) {
                        if (o.id != objId) continue;
                        o.collected            = true;
                        m_pendingObjId         = objId;
                        m_lastCombatEnemyId    = 0;
                        m_pendingTownCaptureId = 0;
                        m_lastBanditCampId     = 0;
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                            enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                        }
                        break;
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            } else {
                // Already captured — produce T1 units weekly (handled via obj.available)
                if (obj.available > 0) {
                    pushPickupEffect(obj.pos, "Outpost: recruit available!", IM_COL32(180, 255, 140, 255));
                    m_pendingObjId = obj.id;
                    m_showDwellingPopup = true;
                }
            }
            break;

        case WorldObjectType::WitchHut:
            {
                // questState holds the skillId this hut teaches; never permanently collected
                int skillId = obj.questState;
                const SkillDef* sd = findSkillDef(skillId);
                const char* sname = sd ? sd->name.c_str() : "Unknown";
                if (hero.skills.hasSkill(skillId)) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "You know: %s", sname);
                    pushPickupEffect(obj.pos, buf, IM_COL32(180, 120, 255, 255));
                } else if (!hero.skills.canLearn(skillId)) {
                    pushPickupEffect(obj.pos, "Skill slots full", IM_COL32(150, 150, 150, 255));
                } else {
                    hero.skills.learn(skillId);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "Learned: %s!", sname);
                    pushPickupEffect(obj.pos, buf, IM_COL32(200, 100, 255, 255));
                    m_audio.playSound("levelup");
                }
            }
            break;

        case WorldObjectType::Stables:
            if (!obj.collected) {
                obj.collected = true;
                hero.maxMove  += obj.value;
                hero.movePool = std::min(hero.movePool + obj.value, hero.maxMove);
                char buf[48];
                std::snprintf(buf, sizeof(buf), "+%d Max Move!", obj.value);
                pushPickupEffect(obj.pos, buf, IM_COL32(200, 160, 80, 255));
                m_audio.playSound("pickup");
            }
            break;

        case WorldObjectType::TreeOfKnowledge:
            if (!obj.collected) {
                m_pendingTreeId          = obj.id;
                m_showTreeKnowledgePopup = true;
            }
            break;
        case WorldObjectType::Barrier:
            break;  // impassable — hero cannot enter this tile anyway

        case WorldObjectType::ChokeGuard:
        {
            if (!obj.collected) {
                Hero guardHero;
                guardHero.id      = 0;
                guardHero.name    = "Pass Guardian";
                guardHero.faction = FactionId::None;
                std::vector<CombatUnit> guardUnits;
                // obj.value==1 → stronger variant (centre-entry guards in Jebus 3.0)
                int stacks    = 5;
                int perStack  = (obj.value == 1) ? 30 : 20;
                int atk       = (obj.value == 1) ? 22 : 18;
                int def       = (obj.value == 1) ? 17 : 14;
                int hp        = (obj.value == 1) ? 280 : 200;
                for (int s = 0; s < stacks; ++s) {
                    CombatUnit u;
                    u.id = 60 + s;  u.name = "Ancient Guardian";
                    u.count = perStack; u.maxHp = u.hp = hp;
                    u.attack = atk;  u.defense = def;  u.speed = 5;
                    u.range = 0;     u.shotsLeft = 0;  u.isPlayer = false;
                    guardUnits.push_back(u);
                }
                uint32_t objId = obj.id;
                m_encounterTitle       = "Pass Guardian";
                m_pendingEncounterHero = guardHero;
                m_pendingEncounterUnits = guardUnits;
                m_encounterOnAccept = [this, objId]() {
                    for (auto& o : m_worldObjects) {
                        if (o.id != objId) continue;
                        o.collected          = true;
                        m_lastBanditCampId   = objId;
                        m_lastCombatEnemyId  = 0;
                        m_pendingTownCaptureId = 0;
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                            enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                        }
                        break;
                    }
                };
                m_showEncounterPrompt = true;
                return;
            }
            break;
        }

        case WorldObjectType::Shipyard:
        {
            if (!obj.collected) {
                // Show build-boat popup (handled in ImGui popup section)
                m_pendingObjId      = obj.id;
                m_showShipyardPopup = true;
            }
            break;
        }

        case WorldObjectType::FishingHouse:
            // passive income — no interaction when stepped on
            break;
        }
    }

    // Resource node — claim mine (guards if unbeaten)
    if (tile->resourceId != 0) {
        for (auto& r : m_resources) {
            if (r.id != tile->resourceId || r.ownedBy == 1) continue;
            if (!r.guardBeaten) {
                // Mine is guarded — show encounter prompt before committing
                Hero guardHero;
                guardHero.id      = 0;
                guardHero.name    = "Mine Guardian";
                guardHero.faction = FactionId::None;
                std::vector<CombatUnit> guardUnits = makeMineGuardUnits(r, m_turns.week());
                m_encounterTitle        = std::string("Mine Guardian (") + resourceName(r.type) + " Mine)";
                m_pendingEncounterHero  = guardHero;
                m_pendingEncounterUnits = guardUnits;
                uint32_t resId = r.id;
                m_encounterOnAccept = [this, resId]() {
                    for (auto& res : m_resources) {
                        if (res.id != resId) continue;
                        res.guardBeaten        = true;
                        m_lastCombatEnemyId    = 0;
                        m_pendingTownCaptureId = 0;
                        m_lastBanditCampId     = 0;
                        if (!m_heroes.empty()) {
                            Hero& h = m_heroes[m_activeHeroIdx];
                            auto pUnits = makeHeroUnits(h, m_registry.units(), true);
                            enterCombat(h, pUnits, m_pendingEncounterHero, m_pendingEncounterUnits);
                        }
                        break;
                    }
                };
                m_encounterOnDecline = [this]() {
                    if (!m_heroes.empty()) { auto& h = m_heroes[m_activeHeroIdx]; h.path.clear(); h.pathStep = 0; }
                };
                m_showEncounterPrompt = true;
                return;
            }
            // Guards beaten (or already ours) — capture
            r.ownedBy = 1;
            m_playerResources.add(r.type, r.amount);
            m_cachedWeeklyIncome.add(r.type, r.amount);
            char mineBuf[48];
            std::snprintf(mineBuf, sizeof(mineBuf), "+%d %s/week", r.amount, resourceName(r.type));
            pushPickupEffect(hero.pos, mineBuf, IM_COL32(255, 220, 80, 255));
            m_audio.playSound("buy");
            gLog("Claimed mine: +%d %s/week\n", r.amount, resourceName(r.type));
            break;
        }
    }

    // Town entry / capture
    if (tile->townId != 0) {
        for (auto& t : m_towns) {
            if (t.id != tile->townId) continue;
            if (t.ownerId != 1) {
                // If player is adjacent (not ON the town yet) and town has garrison → offer Siege or Attack
                bool alreadyCamping = false;
                for (const auto& h2 : m_heroes)
                    if (h2.isSiegeCamping && h2.siegeTargetTownId == t.id) { alreadyCamping = true; break; }

                if (!t.garrison.empty() && !alreadyCamping
                    && HexGrid::distance(hero.pos, t.pos) == 1) {
                    // Offer: attack now OR lay siege camp
                    m_siegePromptTownId     = t.id;
                    m_showSiegeCampPrompt   = true;
                    return;
                }

                // Fight the garrison if one exists (direct attack or hero already on town tile)
                if (!t.garrison.empty()) {
                    // Build garrison CombatUnits as the "enemy"
                    Hero garrisonHero; // dummy hero for the garrison
                    garrisonHero.id     = 0;
                    garrisonHero.name   = t.name + " Garrison";
                    garrisonHero.faction = t.faction;
                    garrisonHero.army   = t.garrison;
                    m_lastCombatEnemyId = 0; // no real enemy hero
                    m_pendingTownCaptureId = t.id;
                    auto pUnits = makeHeroUnits(hero, m_registry.units(), true);
                    auto gUnits = makeHeroUnits(garrisonHero, m_registry.units(), false);
                    enterCombat(hero, pUnits, garrisonHero, gUnits);
                    return;
                }
                // No garrison — capture immediately
                t.ownerId = 1;
                t.garrison.clear();
                gLog("Captured town: %s\n", t.name.c_str());
                m_capturedTownName = t.name;
                m_showCapturePopup = true;
                m_hideout.completeMilestone(Milestone::FIRST_TOWN_CAPTURED);
                {
                    ScriptContext tCtx;
                    tCtx.townId = t.id;
                    m_triggers.fire(TriggerType::TownCaptured, tCtx);
                }
                m_campaign.onTownCaptured(t.id);
            }
            enterTown(&t);
            return;
        }
    }

    // Hero collision — player meets player → unit exchange; player meets enemy → combat
    // NOTE: tile->heroId is already overwritten with the player's own id at this point,
    // so we check by position rather than by heroId.
    {
        // Check allied heroes (by position)
        for (int i = 0; i < static_cast<int>(m_heroes.size()); ++i) {
            if (i == m_activeHeroIdx) continue;
            if (m_heroes[i].pos == hero.pos) {
                m_showUnitExchange = true;
                m_exchangeHeroIdx  = i;
                m_exchangeSelSlotA = -1;
                m_exchangeSelSlotB = -1;
                return;
            }
        }
        // Enemy hero (by position)
        Hero* enemyPtr = nullptr;
        for (auto& e : m_enemyHeroes)
            if (e.pos == hero.pos) { enemyPtr = &e; break; }
        if (enemyPtr) {
            // Move enemy off this tile so the player can stand here after combat
            if (HexTile* et = m_map.getTile(enemyPtr->pos)) et->heroId = 0;
            m_lastCombatEnemyId = enemyPtr->id;
            auto pUnits = makeHeroUnits(hero, m_registry.units(), true);
            auto eUnits = makeHeroUnits(*enemyPtr, m_registry.units(), false);
            enterCombat(hero, pUnits, *enemyPtr, eUnits);
        }
    }
}

void Game::drawHero(const Hero& hero)
{
    // Hero markers (circles + name labels) are drawn in renderWorldOverlay()
    // via ImGui's background DrawList, with animated position for the active hero.
    // This function is reserved for sprite-batch rendering once a tileset exists.
    (void)hero;
}

// ── World entity overlay (ImGui DrawList markers) ─────────────────────────────
void Game::renderWorldOverlay()
{
    if (!m_imguiReady) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    auto project = [&](HexCoord h, float& sx, float& sy) {
        float wx, wy;
        m_hexRenderer.grid().hexToWorld(h, wx, wy);
        m_camera.worldToScreen(wx, wy, sx, sy);
    };

    // Returns true if a label at (lx, ly) of approx width lw is in a safe area,
    // i.e. not overlapping the top bar, bottom bar, or right-side panels.
    const float HUD_TOP    = 68.0f;
    const float HUD_BOTTOM = static_cast<float>(m_height) - 100.0f;
    const float HUD_RIGHT  = static_cast<float>(m_width)  - 185.0f;
    auto labelOK = [&](float lx, float ly, float lw = 0.0f) -> bool {
        if (ly < HUD_TOP)    return false;
        if (ly > HUD_BOTTOM) return false;
        if (lx + lw > HUD_RIGHT) return false;
        return true;
    };

    // Helper: draw one icon from the atlas centered at (sx,sy) with half-size hs
    const bool hasIcons = m_iconTex.ok();
    ImTextureID iconTex = hasIcons
        ? (ImTextureID)(uintptr_t)m_iconTex.id()
        : (ImTextureID)(uintptr_t)0;

    auto addIcon = [&](int idx, float sx, float sy, float hs) {
        if (!hasIcons) return;
        float col = static_cast<float>(idx % 8);
        float row = static_cast<float>(idx / 8);
        ImVec2 uv0 = { col / 8.0f,          row / 6.0f };
        ImVec2 uv1 = { (col + 1.0f) / 8.0f, (row + 1.0f) / 6.0f };
        dl->AddImage(iconTex, {sx - hs, sy - hs}, {sx + hs, sy + hs}, uv0, uv1);
    };

    // Icon atlas indices
    enum : int {
        ICO_HERO_PLAYER  = 0, ICO_HERO_ENEMY   = 1,
        ICO_TOWN_PLAYER  = 2, ICO_TOWN_ENEMY   = 3, ICO_TOWN_NEUTRAL = 4,
        ICO_SCROLL       = 5, ICO_ARTIFACT     = 6, ICO_XP           = 7,
        ICO_CACHE        = 8, ICO_RES_GOLD     = 9, ICO_RES_IRON     = 10,
        ICO_RES_FAITH    = 11,ICO_RES_BLOOD    = 12,ICO_RES_SAP      = 13,
        ICO_RES_MERCURY  = 14,
        ICO_OBSERVATORY  = 16, ICO_STAT_SHRINE = 17, ICO_BANDIT_CAMP = 18,
        ICO_DWELLING     = 19, ICO_QUEST_GIVER = 20, ICO_QUEST_TARGET = 21,
        ICO_FOREST_SHRINE = 22, ICO_HIGHLAND_RUIN = 23,
        ICO_HOLY_FOUNTAIN = 24, ICO_OASIS         = 25,
        ICO_CAMPFIRE      = 26, ICO_LAVA_CRYSTAL  = 27, ICO_SWAMP_ALTAR = 28,
        ICO_CRYPT        = 29,
        ICO_UTOPIA       = 30,
        ICO_TREASURE     = 31,
    };

    // ── Road network ──────────────────────────────────────────────────────────
    // Draw dirt-road paths connecting towns — render before towns/icons so they
    // appear underneath map objects
    if (!m_roadHexes.empty()) {
        for (const auto& rc : m_roadHexes) {
            const HexTile* rt = m_map.getTile(rc);
            if (!rt || !rt->explored) continue;
            float sx, sy;
            project(rc, sx, sy);
            if (sy < HUD_TOP + 22.0f || sy > HUD_BOTTOM) continue;
            // Base dirt circle
            dl->AddCircleFilled({sx, sy}, 18.0f, IM_COL32(160, 130, 85, 140));
            // Draw line segments to each explored road neighbor for continuity
            for (const auto& nb : HexGrid::neighbors(rc)) {
                if (m_roadHexes.count(nb)) {
                    const HexTile* nt = m_map.getTile(nb);
                    if (!nt || !nt->explored) continue;
                    float nx, ny;
                    project(nb, nx, ny);
                    float mx = (sx + nx) * 0.5f, my = (sy + ny) * 0.5f;
                    if (my < HUD_TOP + 22.0f || my > HUD_BOTTOM) continue;
                    dl->AddLine({sx, sy}, {mx, my}, IM_COL32(160, 130, 85, 120), 10.0f);
                }
            }
        }
    }

    // ── Movement range highlight ───────────────────────────────────────────────
    // Draw a soft green overlay on every hex the active hero can reach this turn
    if (!m_heroes.empty() && !m_reachable.empty()) {
        for (const auto& rc : m_reachable) {
            float sx, sy;
            project(rc, sx, sy);
            if (sy < HUD_TOP || sy > HUD_BOTTOM) continue;
            dl->AddCircleFilled({sx, sy}, 20.0f, IM_COL32(80, 220, 100, 35));
            dl->AddCircle({sx, sy}, 20.0f, IM_COL32(80, 220, 100, 110), 0, 1.2f);
        }
    }

    // ── Towns ─────────────────────────────────────────────────────────────────
    for (const auto& town : m_towns) {
        const HexTile* ttile = m_map.getTile(town.pos);
        if (!m_fogDisabled && ttile && !ttile->explored) continue;

        float sx, sy;
        project(town.pos, sx, sy);

        bool isPlayer = (town.ownerId == 1);
        bool isEnemy  = (town.ownerId > 1);
        int  fid      = std::clamp(static_cast<int>(town.faction), 0, NUM_FACTIONS - 1);

        ImU32 ringCol = isPlayer ? IM_COL32(120, 180, 255, 255)
                      : isEnemy  ? IM_COL32(255,  90,  90, 255)
                                 : IM_COL32(210, 165,  50, 255);
        ImU32 flagCol = isPlayer ? IM_COL32( 80, 140, 255, 230)
                      : isEnemy  ? IM_COL32(220,  60,  60, 230)
                                 : IM_COL32(190, 145,  30, 230);

        ImTextureID townArt = m_townTex[fid].ok()
            ? (ImTextureID)(uintptr_t)m_townTex[fid].id() : nullptr;

        // Scale icon with zoom but cap at half a tile so it never overflows the hex
        const float tileR = m_hexRenderer.grid().hexSize() * m_camera.zoom() * 0.5f;
        const float CS    = std::min(townArt ? 46.0f : 44.0f, tileR * 0.88f);
        const float glow  = std::min(12.0f, CS * 0.25f);

        // Outer glow
        dl->AddRectFilled({sx - CS - glow, sy - CS - glow},
                          {sx + CS + glow, sy + CS + glow},
                          (ringCol & 0x00FFFFFFu) | 0x28000000u, 10.0f);

        if (townArt) {
            // ── Faction art image ──────────────────────────────────────────
            dl->AddImageRounded(townArt, {sx - CS, sy - CS}, {sx + CS, sy + CS},
                                {0,0}, {1,1}, IM_COL32(255,255,255,230), 6.0f);
        } else {
            // ── Procedural silhouette fallback ─────────────────────────────
            ImU32 bgCol   = isPlayer ? IM_COL32(12, 22, 55, 240)
                          : isEnemy  ? IM_COL32(55, 12, 12, 240)
                                     : IM_COL32(45, 35, 10, 240);
            ImU32 wallCol = isPlayer ? IM_COL32(70,110,210,255)
                          : isEnemy  ? IM_COL32(210,65, 65, 255)
                                     : IM_COL32(185,150, 45, 255);
            dl->AddRectFilled({sx-CS, sy-CS}, {sx+CS, sy+CS}, bgCol, 5.0f);
            const float TW=13.f, TH=CS*0.85f, KW=CS*0.38f, KH=CS;
            dl->AddRectFilled({sx-CS+5,     sy-TH*.55f},{sx-CS+5+TW, sy+TH*.45f}, wallCol,2.f);
            dl->AddRectFilled({sx+CS-5-TW,  sy-TH*.55f},{sx+CS-5,    sy+TH*.45f}, wallCol,2.f);
            dl->AddRectFilled({sx-KW, sy-KH*.5f},{sx+KW, sy+KH*.5f}, wallCol, 2.f);
            for (float bx=sx-KW+2; bx<sx+KW-4; bx+=11.5f)
                dl->AddRectFilled({bx,sy-KH*.5f-7},{bx+5.5f,sy-KH*.5f}, wallCol);
            dl->AddRectFilled({sx-4.5f,sy+KH*.5f-14},{sx+4.5f,sy+KH*.5f},IM_COL32(8,8,8,220));
        }

        // ── Ownership border + flag pole ──────────────────────────────────
        dl->AddRect({sx - CS, sy - CS}, {sx + CS, sy + CS}, ringCol, 6.0f, 0, 2.5f);

        // Flag pole (top-center)
        float poleX = sx + CS - 10.f, poleY1 = sy - CS - 18.f, poleY2 = sy - CS + 2.f;
        dl->AddLine({poleX, poleY1}, {poleX, poleY2}, IM_COL32(180,160,100,220), 2.0f);
        dl->AddTriangleFilled({poleX, poleY1}, {poleX + 16.f, poleY1 + 6.f},
                              {poleX, poleY1 + 12.f}, flagCol);

        // ── Town name ─────────────────────────────────────────────────────
        float nameW = town.name.size() * 5.0f;
        float nameX = sx - nameW, nameY = sy + CS + 5.0f;
        if (labelOK(nameX, nameY, nameW * 2.0f)) {
            dl->AddText(ImGui::GetFont(), 14.f, {nameX+1, nameY+1}, IM_COL32(0,0,0,200), town.name.c_str());
            dl->AddText(ImGui::GetFont(), 14.f, {nameX,   nameY},   IM_COL32(210,230,255,255), town.name.c_str());
        }
    }

    // ── World objects ──────────────────────────────────────────────────────────
    for (int oi = 0; oi < static_cast<int>(m_worldObjects.size()); ++oi) {
        const auto& obj = m_worldObjects[oi];
        if (obj.type == WorldObjectType::Barrier) continue;  // drawn as blocked terrain
        // FishingHouse is always visible once built
        bool isFishHouse = (obj.type == WorldObjectType::FishingHouse);
        bool isShipyard  = (obj.type == WorldObjectType::Shipyard);
        // NeutralOutpost, WitchHut, Shipyard, FishingHouse stay visible after collection
        if (obj.collected && obj.type != WorldObjectType::NeutralOutpost
            && obj.type != WorldObjectType::WitchHut
            && !isFishHouse && !isShipyard) continue;
        if (obj.type == WorldObjectType::NeutralOutpost && obj.collected && obj.available <= 0) continue;
        const HexTile* otile = m_map.getTile(obj.pos);
        if (!m_fogDisabled && (!otile || !otile->explored)) continue;
        float sx, sy;
        project(obj.pos, sx, sy);
        int ico;
        switch (obj.type) {
        case WorldObjectType::SpellScroll:   ico = ICO_SCROLL;          break;
        case WorldObjectType::ArtifactChest: ico = ICO_ARTIFACT;        break;
        case WorldObjectType::XPShrine:      ico = ICO_XP;              break;
        case WorldObjectType::ResourceCache: ico = ICO_CACHE;           break;
        case WorldObjectType::Observatory:   ico = ICO_OBSERVATORY;     break;
        case WorldObjectType::StatShrine:    ico = ICO_STAT_SHRINE;     break;
        case WorldObjectType::BanditCamp:    ico = ICO_BANDIT_CAMP;     break;
        case WorldObjectType::UnitDwelling:  ico = ICO_DWELLING;        break;
        case WorldObjectType::QuestGiver:    ico = ICO_QUEST_GIVER;     break;
        case WorldObjectType::QuestTarget:   ico = ICO_QUEST_TARGET;    break;
        case WorldObjectType::ForestShrine:  ico = ICO_FOREST_SHRINE;   break;
        case WorldObjectType::HighlandRuin:  ico = ICO_HIGHLAND_RUIN;   break;
        case WorldObjectType::HolyFountain:  ico = ICO_HOLY_FOUNTAIN;   break;
        case WorldObjectType::Oasis:         ico = ICO_OASIS;           break;
        case WorldObjectType::Campfire:      ico = ICO_CAMPFIRE;        break;
        case WorldObjectType::LavaCrystal:   ico = ICO_LAVA_CRYSTAL;    break;
        case WorldObjectType::SwampAltar:    ico = ICO_SWAMP_ALTAR;     break;
        case WorldObjectType::TreasureChest: ico = ICO_TREASURE;         break;
        case WorldObjectType::Crypt:         ico = ICO_CRYPT;        break;
        case WorldObjectType::Utopia:        ico = ICO_UTOPIA;       break;
        case WorldObjectType::Landmark:       ico = 38;              break; // row4 col6
        case WorldObjectType::CursedGround:  ico = 39;               break; // row4 col7
        case WorldObjectType::NeutralOutpost: ico = 40;              break; // row5 col0
        case WorldObjectType::WitchHut:       ico = 41;              break; // row5 col1
        case WorldObjectType::Stables:        ico = 42;              break; // row5 col2
        case WorldObjectType::TreeOfKnowledge:ico = 43;              break; // row5 col3
        case WorldObjectType::ChokeGuard:    ico = 15;               break; // default icon
        case WorldObjectType::Shipyard:      ico = 15;               break;
        case WorldObjectType::FishingHouse:  ico = 15;               break;
        default:                             ico = 15;               break;
        }
        // Idle glow pulse
        float pulse = 0.5f + 0.5f * sinf(m_mapTime * 2.0f + oi * 1.1f);
        float gR    = 10.0f + pulse * 3.0f;
        ImU32 glowC = IM_COL32(255, 240, 180, static_cast<int>(pulse * 90 + 40));

        if (obj.type == WorldObjectType::Crypt) {
            // Dark stone arch icon: charcoal square with purple glow
            glowC = IM_COL32(160, 80, 255, static_cast<int>(pulse * 100 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(25, 15, 35, 220));
            dl->AddRectFilled({sx-8,sy-10},{sx+8,sy+6}, IM_COL32(80,60,110,240), 2.0f);
            dl->AddRect({sx-8,sy-10},{sx+8,sy+6}, IM_COL32(160,80,255,200), 2.0f);
            // Skull-like dots
            dl->AddCircleFilled({sx-3.5f,sy-5.5f}, 2.0f, IM_COL32(200,180,240,220));
            dl->AddCircleFilled({sx+3.5f,sy-5.5f}, 2.0f, IM_COL32(200,180,240,220));
        } else if (obj.type == WorldObjectType::Utopia) {
            // Golden star/diamond icon
            glowC = IM_COL32(255, 215, 0, static_cast<int>(pulse * 140 + 60));
            dl->AddCircleFilled({sx, sy}, 16.0f, IM_COL32(35, 25, 5, 220));
            // Diamond shape
            ImVec2 diam[4] = {{sx, sy-13.0f},{sx+9.0f,sy},{sx, sy+13.0f},{sx-9.0f,sy}};
            dl->AddConvexPolyFilled(diam, 4, IM_COL32(200, 160, 20, 240));
            dl->AddPolyline(diam, 4, IM_COL32(255, 230, 80, 220), ImDrawFlags_Closed, 1.5f);
            // Inner gem
            ImVec2 gem[4] = {{sx, sy-7.0f},{sx+4.5f,sy},{sx, sy+7.0f},{sx-4.5f,sy}};
            dl->AddConvexPolyFilled(gem, 4, IM_COL32(255, 245, 160, 220));
        } else if (obj.type == WorldObjectType::TreasureChest) {
            // Chest icon: brown rectangle with gold latch
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(15, 10, 5, 200));
            dl->AddRectFilled({sx-9,sy-7},{sx+9,sy+7}, IM_COL32(120,80,30,240), 2.0f);
            dl->AddRectFilled({sx-9,sy-7},{sx+9,sy-2}, IM_COL32(90,55,20,240), 2.0f);
            dl->AddRect({sx-9,sy-7},{sx+9,sy+7}, IM_COL32(200,170,50,200), 2.0f);
            dl->AddRectFilled({sx-3,sy-4},{sx+3,sy+1}, IM_COL32(220,190,50,255), 2.0f);
        } else if (obj.type == WorldObjectType::Landmark) {
            // Ancient pillar — stone column silhouette, golden glow
            glowC = IM_COL32(220, 200, 120, static_cast<int>(pulse * 100 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 18, 10, 210));
            dl->AddRectFilled({sx-4,sy-11},{sx+4,sy+7}, IM_COL32(160,140,90,240), 2.0f);
            dl->AddRectFilled({sx-6,sy-11},{sx+6,sy-9}, IM_COL32(180,160,100,240), 2.0f);
            dl->AddRectFilled({sx-6,sy+5},{sx+6,sy+8}, IM_COL32(180,160,100,240), 2.0f);
            dl->AddRect({sx-6,sy-11},{sx+6,sy+8}, IM_COL32(220,200,120,180), 1.0f);
        } else if (obj.type == WorldObjectType::CursedGround) {
            // Skull icon — dark purple aura
            glowC = IM_COL32(130, 30, 180, static_cast<int>(pulse * 120 + 50));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 5, 25, 230));
            dl->AddCircleFilled({sx, sy-2.0f}, 8.0f, IM_COL32(180, 120, 200, 220));
            dl->AddRectFilled({sx-5,sy+4},{sx+5,sy+8}, IM_COL32(180,120,200,200), 1.0f);
            // Eye sockets
            dl->AddCircleFilled({sx-2.5f,sy-3.5f}, 1.8f, IM_COL32(20,5,25,255));
            dl->AddCircleFilled({sx+2.5f,sy-3.5f}, 1.8f, IM_COL32(20,5,25,255));
            // Charge counter
            if (obj.questState > 0) {
                char chBuf[12]; std::snprintf(chBuf, sizeof(chBuf), "%d", obj.questState);
                dl->AddText(ImGui::GetFont(), 9.0f, {sx+6,sy-12}, IM_COL32(220,180,255,255), chBuf);
            }
        } else if (obj.type == WorldObjectType::NeutralOutpost) {
            // Flag + tower icon
            glowC = obj.collected
                ? IM_COL32(120, 200, 255, static_cast<int>(pulse * 100 + 40))
                : IM_COL32(200, 200, 100, static_cast<int>(pulse * 80 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(15, 15, 10, 210));
            dl->AddRectFilled({sx-8,sy-4},{sx+8,sy+8}, IM_COL32(100,90,70,240), 2.0f);
            dl->AddLine({sx-3,sy-10},{sx-3,sy-4}, IM_COL32(160,140,100,255), 1.5f);
            ImU32 flagC = obj.collected ? IM_COL32(80,160,255,255) : IM_COL32(200,180,60,255);
            dl->AddTriangleFilled({sx-3,sy-10},{sx+5,sy-7},{sx-3,sy-4}, flagC);
            if (obj.collected && obj.available > 0) {
                char aBuf[12]; std::snprintf(aBuf, sizeof(aBuf), "%d", obj.available);
                dl->AddText(ImGui::GetFont(), 9.0f, {sx+6,sy-12}, IM_COL32(120,220,255,255), aBuf);
            }
        } else if (obj.type == WorldObjectType::WitchHut) {
            // Purple cauldron-hut: dark circle with a cauldron shape
            glowC = IM_COL32(180, 80, 255, static_cast<int>(pulse * 110 + 45));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 10, 30, 220));
            // Hut triangle roof
            ImVec2 roof[3] = {{sx-9,sy-2},{sx+9,sy-2},{sx,sy-12}};
            dl->AddTriangleFilled(roof[0], roof[1], roof[2], IM_COL32(100, 50, 150, 240));
            // Cauldron bowl
            dl->AddRectFilled({sx-6,sy-2},{sx+6,sy+7}, IM_COL32(60,30,100,240), 2.0f);
            dl->AddRect({sx-6,sy-2},{sx+6,sy+7}, IM_COL32(200,120,255,200), 1.5f);
            // Steam dots
            dl->AddCircleFilled({sx-2,sy-4}, 1.5f, IM_COL32(220,180,255,150));
            dl->AddCircleFilled({sx+3,sy-5}, 1.5f, IM_COL32(220,180,255,150));
        } else if (obj.type == WorldObjectType::Stables) {
            // Brown horseshoe / barn: dark circle with barn silhouette
            glowC = IM_COL32(200, 150, 60, static_cast<int>(pulse * 100 + 40));
            dl->AddCircleFilled({sx, sy}, 14.0f, IM_COL32(20, 12, 5, 215));
            // Barn body
            dl->AddRectFilled({sx-7,sy-2},{sx+7,sy+7}, IM_COL32(140,90,40,240), 2.0f);
            // Roof peak
            ImVec2 broof[3] = {{sx-9,sy-2},{sx+9,sy-2},{sx,sy-10}};
            dl->AddTriangleFilled(broof[0], broof[1], broof[2], IM_COL32(110,65,25,255));
            dl->AddTriangle(broof[0], broof[1], broof[2], IM_COL32(200,150,60,200), 1.5f);
            dl->AddRect({sx-7,sy-2},{sx+7,sy+7}, IM_COL32(200,150,60,200), 1.5f);
            // Door
            dl->AddRectFilled({sx-2,sy+2},{sx+2,sy+7}, IM_COL32(60,35,10,255), 1.0f);
        } else if (obj.type == WorldObjectType::TreeOfKnowledge) {
            // Ancient oak: deep green with glowing leaves
            glowC = IM_COL32(60, 200, 80, static_cast<int>(pulse * 120 + 50));
            dl->AddCircleFilled({sx, sy}, 15.0f, IM_COL32(5, 18, 5, 220));
            // Trunk
            dl->AddRectFilled({sx-2.5f,sy+2},{sx+2.5f,sy+9}, IM_COL32(100,65,25,240));
            // Canopy (three overlapping circles)
            dl->AddCircleFilled({sx,     sy-5},  8.0f, IM_COL32(30,120,40,240));
            dl->AddCircleFilled({sx-5.5f,sy-1},  6.5f, IM_COL32(25,100,35,240));
            dl->AddCircleFilled({sx+5.5f,sy-1},  6.5f, IM_COL32(25,100,35,240));
            // Highlight
            dl->AddCircleFilled({sx-2.5f,sy-7},  3.5f, IM_COL32(80,200,90,180));
        } else {
            addIcon(ico, sx, sy, 22.0f);
        }
        dl->AddCircle({sx, sy}, gR + 8.0f, glowC, 0, 1.5f);

        // WitchHut: show skill name label so player knows what it teaches
        if (obj.type == WorldObjectType::WitchHut) {
            const SkillDef* sd = findSkillDef(obj.questState);
            if (sd) {
                float lw = sd->name.size() * 5.5f;
                float labelY = sy + gR + 10.0f;
                if (labelOK(sx - lw - 2, labelY, lw * 2.0f + 4.0f)) {
                    dl->AddRectFilled({sx-lw-2,labelY},{sx+lw+2,labelY+12}, IM_COL32(20,5,35,180), 3.0f);
                    dl->AddText({sx-lw,labelY+1}, IM_COL32(200,130,255,255), sd->name.c_str());
                }
            }
        }
        // TreeOfKnowledge: show "2000g or XP" hint
        if (obj.type == WorldObjectType::TreeOfKnowledge) {
            const char* hint = "2000g / free XP";
            float lw = strlen(hint) * 5.5f;
            float labelY = sy + gR + 10.0f;
            if (labelOK(sx - lw - 2, labelY, lw * 2.0f + 4.0f)) {
                dl->AddRectFilled({sx-lw-2,labelY},{sx+lw+2,labelY+12}, IM_COL32(5,20,5,180), 3.0f);
                dl->AddText({sx-lw,labelY+1}, IM_COL32(100,220,110,255), hint);
            }
        }
    }

    // ── Resource nodes (mines) ────────────────────────────────────────────────
    for (const auto& r : m_resources) {
        const HexTile* rtile = m_map.getTile(r.pos);
        if (!m_fogDisabled && (!rtile || !rtile->explored)) continue;
        float sx, sy;
        project(r.pos, sx, sy);
        // Clip icon against HUD zones
        if (sy < 68.0f || sy > static_cast<float>(m_height) - 52.0f
            || sx > static_cast<float>(m_width) - 185.0f) continue;
        int ico;
        switch (r.type) {
        case ResourceType::Gold:         ico = ICO_RES_GOLD;    break;
        case ResourceType::Iron:         ico = ICO_RES_IRON;    break;
        case ResourceType::FaithStones:  ico = ICO_RES_FAITH;   break;
        case ResourceType::BloodEssence: ico = ICO_RES_BLOOD;   break;
        case ResourceType::VerdantSap:   ico = ICO_RES_SAP;     break;
        case ResourceType::Mercury:      ico = ICO_RES_MERCURY; break;
        default:                         ico = 15;               break;
        }
        // Scale mine icon with zoom, capped to fit inside one tile
        const float mineR = std::min(28.0f, m_hexRenderer.grid().hexSize() * m_camera.zoom() * 0.46f);
        const float mineGlow = mineR + 2.0f;
        // Glow backdrop so mine is visible against any terrain
        ImU32 bgGlow = IM_COL32(0, 0, 0, 150);
        dl->AddCircleFilled({sx, sy}, mineGlow, bgGlow);
        addIcon(ico, sx, sy, mineR);
        // Ownership ring
        ImU32 ring = r.ownedBy == 1 ? IM_COL32(120, 200, 255, 255)
                   : r.ownedBy >  1 ? IM_COL32(255, 100, 100, 255)
                                    : IM_COL32(255, 210,  60, 200);
        dl->AddCircle({sx, sy}, mineGlow, ring, 0, 2.0f);
        // Resource name + weekly amount shown below the icon (if not inside a HUD panel)
        const char* resName = resourceName(r.type);
        char label[32];
        std::snprintf(label, sizeof(label), "%s +%d", resName, r.amount);
        float lw = strlen(label) * 5.5f;
        float labelY = sy + mineGlow + 2.0f;
        if (labelOK(sx - lw - 2, labelY, lw * 2.0f + 4.0f)) {
            dl->AddRectFilled({sx - lw - 2, labelY}, {sx + lw + 2, labelY + 12},
                              IM_COL32(0, 0, 0, 170), 3.0f);
            dl->AddText({sx - lw, labelY + 1},
                        r.ownedBy == 1 ? IM_COL32(140, 210, 255, 255)
                                       : IM_COL32(255, 230, 120, 255),
                        label);
        }
    }

    // BloodScent: any player hero with this specialty reveals Bloodsworn enemies
    bool playerHasBloodScent = false;
    for (const auto& ph : m_heroes) {
        if (ph.bloodScentSpecialty) { playerHasBloodScent = true; break; }
    }

    // ── Enemy heroes (only if tile is visible, or revealed by BloodScent; GhostWalk heroes are hidden) ─────
    for (const auto& hero : m_enemyHeroes) {
        // GhostWalk: Voidkin Shadow Stalker is invisible on the world map
        if (hero.ghostWalkSpecialty) continue;
        const HexTile* etile = m_map.getTile(hero.pos);
        bool revealedByBloodScent = playerHasBloodScent && hero.faction == FactionId::Bloodsworn;
        if (!m_fogDisabled && (!etile || (!etile->visible && !revealedByBloodScent))) continue;
        float sx, sy;
        project(hero.pos, sx, sy);

        int fac = std::min(static_cast<int>(hero.faction), NUM_FACTIONS - 1);
        auto ait = m_heroMapAnimators.find(hero.id);
        if (ait != m_heroMapAnimators.end() && m_unitTex[fac][0].ok()) {
            float u0, v0, u1, v1;
            ait->second.getUV(u0, v0, u1, v1);
            ImTextureID tex = (ImTextureID)(uintptr_t)m_unitTex[fac][0].id();
            dl->AddImage(tex, {sx - 16, sy - 20}, {sx + 16, sy + 12}, {u0,v0}, {u1,v1});
        } else {
            addIcon(ICO_HERO_ENEMY, sx, sy, 13.0f);
        }
        dl->AddCircle({sx, sy}, 14.0f, IM_COL32(255, 140, 140, 180), 0, 1.5f);
        {
            float lx = sx - (float)hero.name.size() * 3.0f;
            if (labelOK(lx, sy + 15, hero.name.size() * 6.0f))
                dl->AddText({lx, sy + 15}, IM_COL32(255, 160, 160, 200), hero.name.c_str());
        }
        if (hero.isGarrisoned && labelOK(sx - 10.0f, sy - 30.0f))
            dl->AddText({sx - 10.0f, sy - 30.0f}, IM_COL32(255, 80, 80, 255), "[G]");
    }

    // ── Player heroes ─────────────────────────────────────────────────────────
    for (int i = 0; i < static_cast<int>(m_heroes.size()); ++i) {
        const auto& hero = m_heroes[i];
        float wx, wy;
        if (i == m_activeHeroIdx && m_moveT < 1.0f) {
            wx = m_moveSrcX + (m_moveDstX - m_moveSrcX) * m_moveT;
            wy = m_moveSrcY + (m_moveDstY - m_moveSrcY) * m_moveT;
        } else {
            m_hexRenderer.grid().hexToWorld(hero.pos, wx, wy);
        }
        float sx, sy;
        m_camera.worldToScreen(wx, wy, sx, sy);

        bool  active = (i == m_activeHeroIdx);
        ImU32 ring   = active ? IM_COL32(255, 255, 160, 255) : IM_COL32(200, 200, 80, 200);

        int fac = std::min(static_cast<int>(hero.faction), NUM_FACTIONS - 1);
        auto ait = m_heroMapAnimators.find(hero.id);
        if (ait != m_heroMapAnimators.end() && m_unitTex[fac][0].ok()) {
            float u0, v0, u1, v1;
            ait->second.getUV(u0, v0, u1, v1);
            ImTextureID tex = (ImTextureID)(uintptr_t)m_unitTex[fac][0].id();
            dl->AddImage(tex, {sx - 16, sy - 20}, {sx + 16, sy + 12}, {u0,v0}, {u1,v1});
        } else {
            addIcon(ICO_HERO_PLAYER, sx, sy, 13.0f);
        }
        dl->AddCircle({sx, sy}, 14.0f, ring, 0, active ? 2.0f : 1.2f);
        {
            float lx = sx - (float)hero.name.size() * 3.0f;
            if (labelOK(lx, sy + 15, hero.name.size() * 6.0f))
                dl->AddText({lx, sy + 15},
                    active ? IM_COL32(255, 230, 100, 220) : IM_COL32(200, 200, 100, 160),
                    hero.name.c_str());
        }
        if (hero.isGarrisoned && labelOK(sx - 10.0f, sy - 30.0f))
            dl->AddText({sx - 10.0f, sy - 30.0f}, IM_COL32(255, 200, 60, 255), "[G]");
    }

    // ── Particles ────────────────────────────────────────────────────────────
    m_particles.render(dl);

    // ── Pickup effects (floating text) ────────────────────────────────────────
    for (const auto& e : m_pickupEffects) {
        float alpha = std::min(1.0f, e.t);
        if (alpha <= 0.0f) continue;
        float rise = (2.0f - e.t) * 30.0f;
        float sx, sy;
        m_camera.worldToScreen(e.wx, e.wy, sx, sy);
        sy -= rise;
        int   a   = static_cast<int>(alpha * 255);
        ImU32 col = (e.col & 0x00FFFFFFu) | (static_cast<ImU32>(a) << 24);
        dl->AddText({sx - static_cast<float>(e.text.size()) * 3.5f, sy}, col, e.text.c_str());
    }

    // ── Planned path visualization ────────────────────────────────────────────
    if (!m_heroes.empty()) {
        const Hero& activeHero = m_heroes[m_activeHeroIdx];
        if (!activeHero.path.empty() && activeHero.pathStep < static_cast<int>(activeHero.path.size())) {
            // Draw remaining path steps as small dots
            for (int pi = activeHero.pathStep; pi < static_cast<int>(activeHero.path.size()); ++pi) {
                float sx, sy;
                project(activeHero.path[pi], sx, sy);
                float alpha = 1.0f - static_cast<float>(pi - activeHero.pathStep)
                                     / static_cast<float>(activeHero.path.size() - activeHero.pathStep + 1);
                ImU32 dotCol = IM_COL32(255, 230, 80, static_cast<int>(alpha * 180));
                dl->AddCircleFilled({sx, sy}, 3.5f, dotCol);
                // Connector line to previous dot
                if (pi > activeHero.pathStep) {
                    float px, py;
                    project(activeHero.path[pi - 1], px, py);
                    dl->AddLine({px, py}, {sx, sy}, IM_COL32(255, 230, 80, static_cast<int>(alpha * 100)), 1.5f);
                } else {
                    // First dot: connect from hero position
                    float hx, hy;
                    m_hexRenderer.grid().hexToWorld(activeHero.pos, hx, hy);
                    float hsx, hsy;
                    m_camera.worldToScreen(hx, hy, hsx, hsy);
                    dl->AddLine({hsx, hsy}, {sx, sy}, IM_COL32(255, 230, 80, 80), 1.5f);
                }
            }
        }
    }

    // ── Hover tooltip: days to reach or Fight ─────────────────────────────────
    if (m_map.inBounds(m_hovered) && !m_heroes.empty()) {
        const HexTile* ht = m_map.getTile(m_hovered);
        const Hero& activeHero = m_heroes[m_activeHeroIdx];
        if (ht && ht->explored && m_hovered != activeHero.pos) {
            // Check if hovering over an enemy hero
            const Hero* enemyHovered = nullptr;
            if (ht->heroId != 0 && ht->visible) {
                for (const auto& e : m_enemyHeroes)
                    if (e.id == ht->heroId) { enemyHovered = &e; break; }
            }

            bool isFight = (enemyHovered != nullptr);
            if (!isFight && ht->townId != 0)
                for (const auto& t : m_towns)
                    if (t.id == ht->townId && t.ownerId > 1) { isFight = true; break; }

            ImGui::BeginTooltip();
            if (enemyHovered) {
                // Show enemy hero details
                ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", enemyHovered->name.c_str());
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Fight!");
                ImGui::Separator();
                ImGui::Text("Lvl %d  ATK:%d  DEF:%d", enemyHovered->level,
                            enemyHovered->attack, enemyHovered->defense);
                if (!enemyHovered->army.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Army:");
                    const auto& unitDefs = m_registry.units();
                    for (const auto& stack : enemyHovered->army) {
                        if (stack.count <= 0) continue;
                        const char* uname = "?";
                        for (const auto& ud : unitDefs)
                            if (ud.id == stack.defId) { uname = ud.name.c_str(); break; }
                        ImGui::Text("  %-22s x%d", uname, stack.count);
                    }
                }
            } else if (isFight) {
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Fight!");
            } else {
                // Movement cost tooltip (roads halve terrain cost)
                auto costFn = [this, &activeHero](HexCoord c) -> int {
                    const HexTile* t = m_map.getTile(c);
                    if (!t || !activeHero.canEnter(t->terrain) || t->blocked) return 999;
                    int base = activeHero.moveCost(t->terrain);
                    if (m_roadHexes.count(c)) base = std::max(1, base / 2);
                    return base;
                };
                auto path = Pathfinder::find(m_map, activeHero.pos, m_hovered, costFn);
                if (!path.empty()) {
                    int totalCost = 0;
                    for (auto& c : path) {
                        const HexTile* t = m_map.getTile(c);
                        if (t) totalCost += activeHero.moveCost(t->terrain);
                    }
                    int spent     = std::min(totalCost, activeHero.movePool);
                    int remaining = totalCost - spent;
                    int days      = (remaining > 0)
                                    ? (remaining + activeHero.maxMove - 1) / activeHero.maxMove
                                    : 0;
                    if (days == 0) ImGui::Text("Reachable today  (%d MP)", totalCost);
                    else          ImGui::Text("%d day%s  (%d MP)", days, days == 1 ? "" : "s", totalCost);
                } else {
                    const HexTile* bt = m_map.getTile(m_hovered);
                    if (bt && bt->blocked)
                        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Barrier");
                    else if (bt && !activeHero.canEnter(bt->terrain))
                        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Impassable terrain");
                    else
                        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Unreachable");
                }
                // Also show terrain type
                static const char* kTerrainNames[] = {
                    "Plains","Forest","Highland","Corrupted","Toxic","Sacred",
                    "Industrial","Rocky","Swamp","Water","Volcanic","Barren",
                    "Wasteland","Corrupted Forest","Flesh Zone","Mountain"
                };
                int tidx = static_cast<int>(ht->terrain);
                if (tidx >= 0 && tidx < 16)
                    ImGui::TextDisabled("%s", kTerrainNames[tidx]);
            }
            ImGui::EndTooltip();
        }
    }

    // ── Minimap ────────────────────────────────────────────────────────────────
    if (m_showMinimap && m_map.radius() > 0) {
        constexpr float MINI_W = 150.0f, MINI_H = 150.0f;
        constexpr float PAD    = 10.0f;
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float mm_left = PAD;
        const float mm_top  = disp.y - MINI_H - PAD;
        const float mm_cx   = mm_left + MINI_W * 0.5f;
        const float mm_cy   = mm_top  + MINI_H * 0.5f;
        const float R       = static_cast<float>(m_map.radius());
        const float scaleX  = MINI_W * 0.5f / R;
        const float scaleY  = MINI_H * 0.5f / R;

        // Terrain color lookup (dim if explored-only, bright if currently visible)
        auto terrainColor = [](Terrain t, bool vis) -> ImU32 {
            uint8_t a = vis ? 255 : 110;
            switch (t) {
            case Terrain::Water:          return IM_COL32( 35,  65, 145, a);
            case Terrain::Plains:         return IM_COL32(100, 165,  72, a);
            case Terrain::Forest:         return IM_COL32( 38,  95,  44, a);
            case Terrain::Highland:       return IM_COL32(115, 125,  75, a);
            case Terrain::Rocky:          return IM_COL32(135, 125, 105, a);
            case Terrain::Swamp:          return IM_COL32( 85, 105,  55, a);
            case Terrain::Sacred:         return IM_COL32(215, 195, 145, a);
            case Terrain::Industrial:     return IM_COL32( 75,  75,  75, a);
            case Terrain::Corrupted:      return IM_COL32( 75,  35,  95, a);
            case Terrain::Toxic:          return IM_COL32(135, 155,  25, a);
            case Terrain::Volcanic:       return IM_COL32(125,  35,  15, a);
            case Terrain::Barren:         return IM_COL32(175, 150,  95, a);
            case Terrain::Wasteland:      return IM_COL32(125,  95,  55, a);
            case Terrain::CorruptedForest:return IM_COL32( 45,  65,  65, a);
            case Terrain::FleshZone:      return IM_COL32(155,  75,  75, a);
            case Terrain::Mountain:       return IM_COL32(110, 100,  90, a);
            default:                      return IM_COL32( 95,  95,  95, a);
            }
        };

        // Dark backdrop
        dl->AddRectFilled({mm_left-2, mm_top-2},
                          {mm_left+MINI_W+2, mm_top+MINI_H+2},
                          IM_COL32(0, 0, 0, 190));

        // Clip all minimap drawing to its bounds
        dl->PushClipRect({mm_left, mm_top}, {mm_left+MINI_W, mm_top+MINI_H}, true);

        // Draw explored terrain tiles as 2×2 dots
        for (const HexCoord& c : m_map.coords()) {
            const HexTile* t = m_map.getTile(c);
            if (!t || !t->explored) continue;
            float mx = mm_cx + static_cast<float>(c.q) * scaleX;
            float my = mm_cy + (static_cast<float>(c.r) + static_cast<float>(c.q) * 0.5f) * scaleY;
            dl->AddRectFilled({mx-1.f, my-1.f}, {mx+1.f, my+1.f},
                              terrainColor(t->terrain, t->visible));
        }

        // Towns: 4×4 colored square with white outline
        for (const auto& town : m_towns) {
            const HexTile* tt = m_map.getTile(town.pos);
            if (!tt || !tt->explored) continue;
            float mx = mm_cx + static_cast<float>(town.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(town.pos.r) + static_cast<float>(town.pos.q) * 0.5f) * scaleY;
            ImU32 col = town.ownerId == 1 ? IM_COL32( 90, 150, 255, 255)
                      : town.ownerId >  1 ? IM_COL32(255,  70,  70, 255)
                                          : IM_COL32(210, 165,  45, 255);
            dl->AddRectFilled({mx-2.f, my-2.f}, {mx+2.f, my+2.f}, col);
            dl->AddRect({mx-2.f, my-2.f}, {mx+2.f, my+2.f}, IM_COL32(255, 255, 255, 220));
        }

        // Resource mines
        for (const auto& r : m_resources) {
            const HexTile* rt = m_map.getTile(r.pos);
            if (!rt || !rt->explored) continue;
            float mx = mm_cx + static_cast<float>(r.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(r.pos.r) + static_cast<float>(r.pos.q) * 0.5f) * scaleY;
            ImU32 col = r.ownedBy == 1 ? IM_COL32(80, 220, 80, 200)
                      : r.ownedBy  > 1 ? IM_COL32(220, 80, 80, 200)
                                       : IM_COL32(200, 180, 80, 150);
            dl->AddCircleFilled({mx, my}, 1.5f, col);
        }

        // Enemy heroes (only when tile is visible)
        for (const auto& hero : m_enemyHeroes) {
            const HexTile* ht2 = m_map.getTile(hero.pos);
            if (!ht2 || !ht2->visible) continue;
            float mx = mm_cx + static_cast<float>(hero.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(hero.pos.r) + static_cast<float>(hero.pos.q) * 0.5f) * scaleY;
            dl->AddCircleFilled({mx, my}, 2.5f, IM_COL32(255, 60, 60, 255));
        }

        // Player heroes: bright cyan circle
        for (const auto& ph : m_heroes) {
            float mx = mm_cx + static_cast<float>(ph.pos.q) * scaleX;
            float my = mm_cy + (static_cast<float>(ph.pos.r) + static_cast<float>(ph.pos.q) * 0.5f) * scaleY;
            dl->AddCircleFilled({mx, my}, 3.0f, IM_COL32(70, 200, 255, 255));
            dl->AddCircle({mx, my}, 3.5f, IM_COL32(255, 240, 80, 220));
        }

        // Camera viewport rectangle
        {
            constexpr float SQRT3 = 1.7320508f;
            const float hs   = m_hexRenderer.grid().hexSize();
            const float cx_w = m_camera.x();
            const float cy_w = m_camera.y();
            const float z    = m_camera.zoom();
            const float sw   = static_cast<float>(m_width);
            const float sh   = static_cast<float>(m_height);
            // half-extents of visible region in hex-axial units
            const float hx_q  = (sw * 0.5f / z) / (hs * 1.5f);
            const float hy_rq = (sh * 0.5f / z) / (hs * SQRT3);
            // camera center in axial units
            const float cx_q  = cx_w / (hs * 1.5f);
            const float cy_rq = cy_w / (hs * SQRT3);
            float vl = mm_cx + (cx_q - hx_q) * scaleX;
            float vr = mm_cx + (cx_q + hx_q) * scaleX;
            float vt = mm_cy + (cy_rq - hy_rq) * scaleY;
            float vb = mm_cy + (cy_rq + hy_rq) * scaleY;
            dl->AddRect({vl, vt}, {vr, vb}, IM_COL32(255, 255, 255, 210), 0.f, 0, 1.5f);
        }

        dl->PopClipRect();

        // Minimap border
        dl->AddRect({mm_left-2, mm_top-2},
                    {mm_left+MINI_W+2, mm_top+MINI_H+2},
                    IM_COL32(175, 155, 115, 230), 2.0f, 0, 1.5f);
        // "MAP [M]" label above
        dl->AddText({mm_left+2, mm_top-14}, IM_COL32(195, 175, 135, 220), "MAP [M]");
    }
}

// ── Level-up modal ────────────────────────────────────────────────────────────
void Game::renderLevelUpModal()
{
    if (!m_showLevelUpModal) return;
    ImGui::OpenPopup("Level Up!");

    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Level Up!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Hero& hero = m_heroes[m_activeHeroIdx];

        ImGui::Text("Congratulations! %s reached Level %d!", hero.name.c_str(), hero.level);
        ImGui::Separator();
        ImGui::Text("Choose a skill:");
        ImGui::Spacing();

        for (int i = 0; i < static_cast<int>(m_levelUpOffers.size()); ++i) {
            const auto& offer = m_levelUpOffers[i];
            const SkillDef* offerSd = findSkillDef(offer.skillId);
            ImGui::PushID(i);
            if (ImGui::Button(offer.label.c_str(), ImVec2(-1, 36))) {
                // Compute tier before applying offer (for delta calculation)
                int prevTier = 0;
                if (const SkillInstance* existing = hero.skills.getSkill(offer.skillId))
                    prevTier = static_cast<int>(existing->tier);

                LevelUpSystem::applyOffer(offer, hero.skills);

                // Apply immediate passive bonuses for Movement/Vision/Magic skills
                {
                    const SkillDef* sd = findSkillDef(offer.skillId);
                    if (sd) {
                        // v = incremental gain from this skill event
                        // values[] indexed as Basic=0, Advanced=1, Master=2
                        int v;
                        if (offer.isUpgrade) {
                            // prevTier is the index before upgrade (0=Basic, 1=Advanced)
                            v = sd->values[prevTier + 1] - sd->values[prevTier];
                        } else {
                            v = sd->values[0]; // fresh skill at Basic tier
                        }
                        if (sd->effectType == SkillEffectType::MovementBonus) {
                            hero.maxMove += v; hero.movePool = std::min(hero.movePool + v, hero.maxMove);
                        } else if (sd->effectType == SkillEffectType::VisionBonus) {
                            hero.visionRange += v;
                            FogOfWar::updateVision(m_map, hero);
                        } else if (sd->effectType == SkillEffectType::MagicSchoolBonus) {
                            if      (sd->statName == "lightPower")  hero.lightPower  += v;
                            else if (sd->statName == "bloodPower")  hero.bloodPower  += v;
                            else if (sd->statName == "deathPower")  hero.deathPower  += v;
                            else if (sd->statName == "naturePower") hero.naturePower += v;
                            else if (sd->statName == "forgePower")  hero.forgePower  += v;
                            else if (sd->statName == "fleshPower")  hero.fleshPower  += v;
                        }
                    }
                }

                // Apply per-class stat growth
                const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                if (cls) {
                    if (cls->scalesAttack) hero.attack  += 1;
                    else                   hero.defense += 1;
                    if (hero.level % 2 == 0) {
                        if (cls->scalesLightPower)  hero.lightPower  += 1;
                        if (cls->scalesBloodPower)  hero.bloodPower  += 1;
                        if (cls->scalesDeathPower)  hero.deathPower  += 1;
                        if (cls->scalesNaturePower) hero.naturePower += 1;
                        if (cls->scalesForgePower)  hero.forgePower  += 1;
                        if (cls->scalesFleshPower)  hero.fleshPower  += 1;
                    }
                    hero.maxMana += 1;
                    hero.mana = hero.maxMana;
                    // HP grows 10 per level
                    hero.heroMaxHp += 10;
                    hero.heroHp = hero.heroMaxHp;
                }
                m_levelUpOffers.clear();
                // If more level-ups are queued, generate the next set of offers
                if (m_pendingLevelUps > 1) {
                    m_pendingLevelUps--;
                    const HeroClassDef* ncls = m_classRegistry.getClass(hero.classId);
                    if (ncls) {
                        std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                        m_levelUpOffers = LevelUpSystem::generateOffers(
                            *ncls, hero.skills, hero.level, allSkills, hero.faction);
                    }
                    if (m_levelUpOffers.empty())
                        m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                    // Keep m_showLevelUpModal true so the next modal opens immediately
                } else {
                    m_pendingLevelUps = 0;
                    m_showLevelUpModal = false;
                    // Grant Found City: level 5 in campaign (tutorial), level 10 elsewhere
                    int foundCityLevel = (m_state == GameState::Campaign) ? 5 : 10;
                    if (hero.level >= foundCityLevel) {
                        bool hasFC = false;
                        for (int sid : hero.knownSpells)
                            if (sid == SPL::FOUND_CITY) { hasFC = true; break; }
                        if (!hasFC) {
                            hero.knownSpells.push_back(SPL::FOUND_CITY);
                            pushPickupEffect(hero.pos, "Learned: Found City!", IM_COL32(255, 215, 50, 255));
                        }
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            // Show description below button
            if (offerSd) {
                ImGui::SameLine(0, 4);
                ImGui::TextDisabled("  %s", offerSd->description.c_str());
            }
            if (ImGui::IsItemHovered() && offerSd)
                ImGui::SetTooltip("%s", offerSd->description.c_str());
            ImGui::PopID();
            ImGui::Spacing();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("XP: %d / %d", hero.xp, hero.xpToNext);
        ImGui::EndPopup();
    }
}

// ── Hideout screen ────────────────────────────────────────────────────────────
void Game::renderHideoutScreen()
{
    m_hideoutScreen.draw(m_hideout, m_showHideoutScreen);
}

// ── Artifact equip panel [F7] ─────────────────────────────────────────────────
void Game::renderArtifactPanel()
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Artifacts  [F7]", &m_showArtifactPanel)) { ImGui::End(); return; }

    static const char* slotNames[] = {
        "Helm","Armor","Weapon","Shield","Ring","Boots","Cloak","Misc"
    };

    // Helper: format non-zero bonus fields into a short string
    auto bonusStr = [](const ArtifactBonus& b) -> std::string {
        std::string s;
        auto app = [&](const char* label, int v) {
            if (v == 0) return;
            if (!s.empty()) s += "  ";
            if (v > 0) s += '+';
            s += std::to_string(v);
            s += ' ';
            s += label;
        };
        app("ATK",    b.attack);
        app("DEF",    b.defense);
        app("SPD",    b.moveBonus);
        app("HP",     b.hpBonus);
        app("Mana",   b.manaBonus);
        app("Light",  b.lightPower);
        app("Blood",  b.bloodPower);
        app("Death",  b.deathPower);
        app("Nature", b.naturePower);
        app("Forge",  b.forgePower);
        app("Flesh",  b.fleshPower);
        app("Vision", b.visionBonus);
        return s.empty() ? "no bonus" : s;
    };

    // Total equipped bonus summary
    ArtifactBonus total = m_artifactRegistry.totalBonus(hero.artifacts);
    std::string totalStr = bonusStr(total);
    if (!totalStr.empty() && totalStr != "no bonus")
        ImGui::TextColored({0.8f,0.8f,0.3f,1.0f}, "Total: %s", totalStr.c_str());
    ImGui::Separator();

    ImGui::Text("Equipped:");
    ImGui::Separator();
    for (int i = 0; i < HeroArtifacts::SLOT_COUNT; ++i) {
        int aid = hero.artifacts.equippedIds[i];
        const ArtifactDef* def = aid ? m_artifactRegistry.getDef(aid) : nullptr;
        ImGui::PushID(i);
        if (def) {
            std::string bs = bonusStr(def->bonus);
            ImGui::Text("%-8s : %-20s  %s", slotNames[i], def->name.c_str(), bs.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unequip")) {
                hero.artifactInventory.push_back(aid);
                hero.artifacts.unequip(static_cast<ArtifactSlot>(i));
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", def->description.c_str());
        } else {
            ImGui::TextDisabled("%-8s : —", slotNames[i]);
        }
        ImGui::PopID();
    }

    if (!hero.artifactInventory.empty()) {
        ImGui::Spacing();
        ImGui::Text("Inventory:");
        ImGui::Separator();
        for (int j = 0; j < static_cast<int>(hero.artifactInventory.size()); ++j) {
            int aid = hero.artifactInventory[j];
            const ArtifactDef* def = m_artifactRegistry.getDef(aid);
            if (!def) continue;
            ImGui::PushID(j + 1000);
            std::string bs = bonusStr(def->bonus);
            ImGui::Text("%-20s  %s", def->name.c_str(), bs.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", def->description.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Equip")) {
                auto slot    = def->slot;
                int  slotIdx = static_cast<int>(slot);
                int  old     = hero.artifacts.equippedIds[slotIdx];
                if (old) hero.artifactInventory.push_back(old);
                hero.artifacts.equip(aid, slot);
                hero.artifactInventory.erase(hero.artifactInventory.begin() + j);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

// ── Hero inspect panel [F8] ───────────────────────────────────────────────────
void Game::renderHeroInspect()
{
    if (m_heroes.empty()) return;
    const Hero& hero = m_heroes[m_activeHeroIdx];

    ImGui::SetNextWindowSize(ImVec2(340, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Hero  [F8]", &m_showHeroInspect)) { ImGui::End(); return; }

    ImGui::Text("%s", hero.name.c_str());
    const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
    if (cls) {
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), "%s", cls->name.c_str());
        if (!cls->specialtyDesc.empty()) {
            ImGui::TextDisabled("Specialty: %s", cls->specialtyDesc.c_str());
        }
    }
    ImGui::TextDisabled("Level %d  —  XP %d / %d", hero.level, hero.xp, hero.xpToNext);
    {
        float xpFrac = hero.xpToNext > 0 ? static_cast<float>(hero.xp) / hero.xpToNext : 1.0f;
        char xpLabel[32]; std::snprintf(xpLabel, sizeof(xpLabel), "XP %.0f%%", xpFrac * 100.0f);
        ImGui::ProgressBar(xpFrac, ImVec2(-1, 10), xpLabel);
    }
    ImGui::TextDisabled("Battles won: %d", hero.battlesWon);
    if (hero.isGarrisoned)
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "[Garrisoned — +2 DEF in combat]");
    ImGui::Separator();

    ImGui::Text("ATK %d   DEF %d   Vision %d", hero.attack, hero.defense, hero.visionRange);
    ImGui::Text("Mana %d / %d   Move %d / %d",
                hero.mana, hero.maxMana, hero.movePool, hero.maxMove);
    ImGui::Text("HP   %d / %d", hero.heroHp, hero.heroMaxHp);

    // Specialty progression stats
    if (cls) {
        // Veteran / Predator use specialtyAtk as their counter
        bool showGenericAtk = (cls->specialty == SpecialtyType::Veteran ||
                               cls->specialty == SpecialtyType::Predator);
        if (showGenericAtk && hero.specialtyAtk > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                               "Specialty bonus: +%d ATK", hero.specialtyAtk);
        if (cls->specialty == SpecialtyType::Phylactery && hero.phylacteryUsed)
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Phylactery consumed");
        if (cls->specialty == SpecialtyType::Elixir && hero.elixirUsed)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Elixir used this battle");
        if (cls->specialty == SpecialtyType::Recycler && hero.recyclerBonus > 0)
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 0.4f, 1.0f),
                               "Recycler: +%d ATK to all units (%d/5)", hero.recyclerBonus, hero.recyclerBonus);
        if (cls->specialty == SpecialtyType::LivingRune && hero.livingRuneBonus > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                               "Living Rune: +%d ATK/DEF to hero (%d/5)", hero.livingRuneBonus, hero.livingRuneBonus);
        if (cls->specialty == SpecialtyType::BloodScent)
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                               "Blood Scent: Bloodsworn heroes always revealed on map");
    }
    ImGui::Spacing();

    {
        bool anyPower = hero.lightPower || hero.bloodPower || hero.deathPower ||
                        hero.naturePower || hero.forgePower || hero.fleshPower;
        ImGui::Text("Casting Power:");
        if (anyPower) {
            if (hero.lightPower)  ImGui::Text("  Light  +%d", hero.lightPower);
            if (hero.bloodPower)  ImGui::Text("  Blood  +%d", hero.bloodPower);
            if (hero.deathPower)  ImGui::Text("  Death  +%d", hero.deathPower);
            if (hero.naturePower) ImGui::Text("  Nature +%d", hero.naturePower);
            if (hero.forgePower)  ImGui::Text("  Forge  +%d", hero.forgePower);
            if (hero.fleshPower)  ImGui::Text("  Flesh  +%d", hero.fleshPower);
        } else {
            ImGui::TextDisabled("  (no school specialisation)");
        }
    }

    if (!hero.skills.slots.empty()) {
        ImGui::Spacing();
        ImGui::Text("Skills:");
        for (auto& s : hero.skills.slots) {
            if (s.defId == 0) continue;
            const SkillDef* sd = findSkillDef(s.defId);
            const char* tierStr[] = {"Basic","Advanced","Master"};
            int t = static_cast<int>(s.tier);
            ImGui::Text("  %s (%s)", sd ? sd->name.c_str() : "?", (t >= 0 && t <= 2) ? tierStr[t] : "?");
        }

        // Archetype label
        static const int kMight[] = { SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::ARCHERY,
            SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::LOGISTICS,
            SkillID::SCOUTING, SkillID::FIRST_AID, SkillID::LUCK };
        static const int kMagic[] = { SkillID::LIGHT_MAGIC, SkillID::BLOOD_MAGIC,
            SkillID::DEATH_MAGIC, SkillID::NATURE_MAGIC, SkillID::FORGE_MAGIC, SkillID::FLESH_MAGIC };
        int mightN = 0, magicN = 0;
        for (int sid : kMight) if (hero.skills.hasSkill(sid)) ++mightN;
        for (int sid : kMagic) if (hero.skills.hasSkill(sid)) ++magicN;

        const char* archetypeLabel   = nullptr;
        ImVec4      archetypeColour  = {1,1,1,1};
        if      (mightN >= 5 && magicN == 0)         { archetypeLabel = "★ PURE MIGHT";  archetypeColour = {1.0f,0.75f,0.2f,1}; }
        else if (magicN >= 4 && mightN <= 1)         { archetypeLabel = "★ PURE MAGIC";  archetypeColour = {0.6f,0.5f,1.0f,1}; }
        else if (mightN >= 3 && magicN >= 2)         { archetypeLabel = "★ WARLORD";     archetypeColour = {0.8f,0.3f,0.3f,1}; }
        else if (mightN >= 4)                        { archetypeLabel = "Might Build";    archetypeColour = {1.0f,0.85f,0.5f,1}; }
        else if (mightN >= 2)                        { archetypeLabel = "Might Synergy";  archetypeColour = {0.9f,0.8f,0.5f,1}; }
        else if (magicN >= 3)                        { archetypeLabel = "Magic Synergy";  archetypeColour = {0.7f,0.6f,1.0f,1}; }
        else if (magicN >= 2)                        { archetypeLabel = "Dual Magic";     archetypeColour = {0.7f,0.6f,1.0f,1}; }

        if (archetypeLabel) {
            ImGui::Spacing();
            ImGui::TextColored(archetypeColour, "%s", archetypeLabel);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (mightN >= 5 && magicN == 0)
                    ImGui::TextUnformatted("+1 Speed and +10% HP to all units in combat");
                else if (magicN >= 4 && mightN <= 1)
                    ImGui::TextUnformatted("+3 to all casting stats, spells cost -1 mana");
                else if (mightN >= 3 && magicN >= 2)
                    ImGui::TextUnformatted("+1 Morale and +1 Luck to all units in combat");
                else if (mightN >= 4)
                    ImGui::TextUnformatted("+2 ATK/DEF to all units in combat");
                else if (mightN >= 2)
                    ImGui::TextUnformatted("+1 ATK/DEF to all units in combat");
                else if (magicN >= 3)
                    ImGui::TextUnformatted("+2 to all casting stats");
                else if (magicN >= 2)
                    ImGui::TextUnformatted("+1 to all casting stats");
                ImGui::EndTooltip();
            }
        }
    }

    {
        ImGui::Spacing();
        ImGui::Text("Spellbook:");
        ImGui::Separator();
        if (hero.knownSpells.empty()) {
            ImGui::TextDisabled("  -- no spells known --");
        } else {
            for (int sid : hero.knownSpells) {
                const SpellDef* sp = findSpell(sid);
                if (sp) {
                    ImGui::Text("  %s  (%d mana)", sp->name, sp->manaCost);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", sp->desc);
                }
            }
        }
    }

    // Artifacts equipped
    {
        ImGui::Spacing();
        ImGui::Text("Artifacts:");
        ImGui::Separator();
        bool anyEquipped = false;
        static const char* kSlotLabel[] = { "Helm","Armor","Wpn","Shld","Ring","Boots","Cloak","Misc" };
        for (int s = 0; s < static_cast<int>(ArtifactSlot::COUNT); ++s) {
            int artId = hero.artifacts.equippedIds[s];
            if (artId == 0) continue;
            const ArtifactDef* art = m_artifactRegistry.getDef(artId);
            if (!art) continue;
            ImGui::Text("  [%-5s] %s", kSlotLabel[s], art->name.c_str());
            if (ImGui::IsItemHovered() && !art->description.empty())
                ImGui::SetTooltip("%s", art->description.c_str());
            anyEquipped = true;
        }
        if (!anyEquipped)
            ImGui::TextDisabled("  — none equipped —");
        if (!hero.artifactInventory.empty()) {
            ImGui::TextDisabled("  Inventory: %zu artifact(s) unequipped",
                                hero.artifactInventory.size());
        }
    }

    // ── Army portrait row ─────────────────────────────────────────────────────
    {
        ImGui::Spacing();
        ImGui::Text("Army:");
        ImGui::Separator();

        const auto& unitDefs = m_registry.units();
        const float SW = 44.0f, SH = 64.0f, GAP = 3.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int totalUnits = 0, totalHp = 0;
        int slotIdx = 0;

        for (const auto& stack : hero.army) {
            if (slotIdx > 0) ImGui::SameLine(0, GAP);

            const UnitDef* ud = nullptr;
            for (const auto& u : unitDefs) if (u.id == stack.defId) { ud = &u; break; }

            ImTextureID tex = nullptr;
            if (ud) {
                int fid = std::clamp(static_cast<int>(ud->faction), 0, NUM_FACTIONS - 1);
                int tid = std::clamp(ud->tier - 1, 0, NUM_UNIT_TIERS - 1);
                if (m_unitTex[fid][tid].ok())
                    tex = (ImTextureID)(uintptr_t)m_unitTex[fid][tid].id();
            }

            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool hasUnit = (stack.count > 0);

            // Slot bg
            dl->AddRectFilled(pos, {pos.x + SW, pos.y + SH}, IM_COL32(18, 20, 32, 230), 4.0f);
            dl->AddRect(pos, {pos.x + SW, pos.y + SH},
                        hasUnit ? IM_COL32(80, 95, 130, 200) : IM_COL32(35, 40, 58, 140),
                        4.0f, 0, 1.5f);

            if (hasUnit) {
                float sprH2 = SH - 18.0f;
                if (tex) {
                    dl->AddImage(tex, {pos.x + 1, pos.y + 1}, {pos.x + SW - 1, pos.y + 1 + sprH2},
                                 {0.0f, 0.0f}, {0.125f, 1.0f});
                } else if (ud) {
                    char tl[4]; std::snprintf(tl, sizeof(tl), "T%d", ud->tier);
                    dl->AddText({pos.x + SW * 0.5f - 8, pos.y + sprH2 * 0.5f - 7},
                                IM_COL32(130, 140, 165, 200), tl);
                }
                char cnt[12]; std::snprintf(cnt, sizeof(cnt), "x%d", stack.count);
                ImVec2 csz = ImGui::CalcTextSize(cnt);
                float cx = pos.x + (SW - csz.x) * 0.5f, cy = pos.y + SH - 15.0f;
                dl->AddText({cx + 1, cy + 1}, IM_COL32(0, 0, 0, 200), cnt);
                dl->AddText({cx, cy},          IM_COL32(220, 225, 255, 255), cnt);
                totalUnits += stack.count;
                if (ud) totalHp += ud->hp * stack.count;
            }

            char bid[24]; std::snprintf(bid, sizeof(bid), "##hi_army_%d", slotIdx);
            ImGui::InvisibleButton(bid, {SW, SH});

            if (ImGui::IsItemHovered() && hasUnit && ud) {
                ImGui::BeginTooltip();
                ImGui::Text("%s  x%d", ud->name.c_str(), stack.count);
                ImGui::TextDisabled("ATK %d  DEF %d  HP %d  SPD %d",
                                    ud->attack, ud->defense, ud->hp, ud->speed);
                ImGui::TextDisabled("Total HP: %d", ud->hp * stack.count);
                if (ud->range > 0) ImGui::TextDisabled("Ranged  shots %d", ud->shots);
                if (ud->flying)    ImGui::TextDisabled("Flying");
                if (ud->vampiric)  ImGui::TextDisabled("Vampiric");
                ImGui::EndTooltip();
            }
            ++slotIdx;
        }
        if (totalUnits > 0) {
            ImGui::Spacing();
            ImGui::TextDisabled("  Total: %d units  |  %d HP", totalUnits, totalHp);
        } else if (hero.army.empty()) {
            ImGui::TextDisabled("  No units");
        }
    }
    ImGui::End();
}

// ── Combat result popup ───────────────────────────────────────────────────────
void Game::renderCombatResultPopup()
{
    const char* title = m_combatResultWon ? "Battle Won!" : "Battle Lost";
    ImGui::OpenPopup(title);
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);

    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    // ── Header ────────────────────────────────────────────────────────────────
    if (m_combatResultWon)
        ImGui::TextColored({0.3f, 1.0f, 0.4f, 1.0f}, "VICTORY!");
    else
        ImGui::TextColored({1.0f, 0.25f, 0.25f, 1.0f}, "DEFEAT");
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Day %d  Week %d", m_turns.day(), m_turns.week());
    ImGui::Separator();

    // ── XP / Gold row ─────────────────────────────────────────────────────────
    if (m_combatResultXp > 0 || m_combatResultGold > 0) {
        if (m_combatResultXp > 0)
            ImGui::TextColored({0.55f, 0.95f, 0.55f, 1.0f}, " XP  +%d", m_combatResultXp);
        if (m_combatResultXp > 0 && m_combatResultGold > 0) ImGui::SameLine(0, 20);
        if (m_combatResultGold > 0)
            ImGui::TextColored({1.0f, 0.82f, 0.12f, 1.0f}, " Gold  +%d", m_combatResultGold);
        ImGui::Spacing();
        ImGui::Separator();
    }

    // ── Unit card helper: draws a row of unit cards ───────────────────────────
    constexpr float CW = 56.0f, CH = 78.0f, CGAP = 4.0f;
    auto drawUnitRow = [&](const std::vector<BattleUnitRecord>& units,
                           ImU32 borderCol, const char* emptyMsg)
    {
        if (units.empty()) {
            ImGui::TextDisabled("  %s", emptyMsg);
            return;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int idx = 0;
        for (const auto& u : units) {
            if (idx > 0) ImGui::SameLine(0, CGAP);
            ImVec2 p = ImGui::GetCursorScreenPos();

            // Card background + border
            dl->AddRectFilled(p, {p.x + CW, p.y + CH}, IM_COL32(18, 20, 32, 235), 4.0f);
            dl->AddRect(p, {p.x + CW, p.y + CH}, borderCol, 4.0f, 0, 1.8f);

            // Sprite (first frame: uv 0..0.125, 0..1)
            float sprY2 = p.y + CH - 20.0f;
            bool drewSprite = false;
            if (u.faction >= 0 && u.faction < NUM_FACTIONS) {
                int ti = std::clamp(u.tier - 1, 0, NUM_UNIT_TIERS - 1);
                if (m_unitTex[u.faction][ti].ok()) {
                    ImTextureID tid = (ImTextureID)(uintptr_t)m_unitTex[u.faction][ti].id();
                    dl->AddImage(tid, {p.x + 2, p.y + 2}, {p.x + CW - 2, sprY2},
                                 {0.0f, 0.0f}, {0.125f, 1.0f});
                    drewSprite = true;
                }
            }
            if (!drewSprite) {
                // Fallback: colored card with abbreviated unit name
                // Hash name for a unique-per-unit-type color
                uint32_t nh = 0;
                for (unsigned char c : u.name) nh = nh * 31u + c;
                ImU32 bgShade = IM_COL32(
                    35 + (int)(nh & 0x45u),
                    35 + (int)((nh >> 6) & 0x35u),
                    55 + (int)((nh >> 12) & 0x55u), 200);
                dl->AddRectFilled({p.x + 2, p.y + 2}, {p.x + CW - 2, sprY2}, bgShade, 3.0f);
                // First word of name (or first 5 chars)
                std::string abbr = u.name;
                {
                    size_t sp = abbr.find(' ');
                    if (sp != std::string::npos && sp <= 7) abbr = abbr.substr(0, sp);
                    else if (abbr.size() > 6) abbr = abbr.substr(0, 6);
                }
                ImVec2 nsz = ImGui::CalcTextSize(abbr.c_str());
                float nx = p.x + (CW - nsz.x) * 0.5f;
                float ny = p.y + (sprY2 - p.y - nsz.y) * 0.5f + 2.0f;
                dl->AddText({nx + 1, ny + 1}, IM_COL32(0, 0, 0, 180), abbr.c_str());
                dl->AddText({nx, ny},         IM_COL32(215, 220, 255, 255), abbr.c_str());
            }

            // Count badge at bottom
            char cnt[12]; std::snprintf(cnt, sizeof(cnt), "x%d", u.count);
            ImVec2 csz = ImGui::CalcTextSize(cnt);
            float cx = p.x + (CW - csz.x) * 0.5f;
            float cy = p.y + CH - 16.0f;
            dl->AddRectFilled({p.x + 2, cy - 2}, {p.x + CW - 2, p.y + CH - 2},
                              IM_COL32(0, 0, 0, 160), 2.0f);
            dl->AddText({cx + 1, cy + 1}, IM_COL32(0, 0, 0, 200), cnt);
            dl->AddText({cx, cy},         IM_COL32(230, 230, 255, 255), cnt);

            // Invisible button for hover/tooltip
            char bid[24]; std::snprintf(bid, sizeof(bid), "##cr_%d", idx);
            ImGui::InvisibleButton(bid, {CW, CH});
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", u.name.c_str());
                ImGui::TextDisabled("x%d  killed/lost", u.count);
                ImGui::EndTooltip();
            }
            ++idx;
        }
    };

    // ── Enemies defeated ──────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::TextColored({0.85f, 0.55f, 0.2f, 1.0f}, "ENEMIES SLAIN");
    ImGui::Spacing();
    drawUnitRow(m_combatEnemiesDefeated, IM_COL32(180, 60, 40, 220), "none");

    // ── Player losses ─────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored({0.6f, 0.65f, 0.85f, 1.0f}, "YOUR LOSSES");
    ImGui::Spacing();
    drawUnitRow(m_combatUnitsLost, IM_COL32(50, 80, 200, 220), "No losses!");

    // ── Continue button ───────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Continue", ImVec2(-1, 32))) {
        m_showCombatResult = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── Victory modal ─────────────────────────────────────────────────────────────
void Game::renderVictoryModal()
{
    ImGui::OpenPopup("Victory!");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Victory!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f),
                           "All enemy heroes have been defeated!");
        ImGui::Spacing();
        ImGui::TextDisabled("Day %d  Week %d  |  Gold: %d",
                            m_turns.day(), m_turns.week(),
                            m_playerResources.get(ResourceType::Gold));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bw = ImGui::GetWindowWidth() - 32.0f;
        if (ImGui::Button("Continue Exploring", ImVec2(bw * 0.55f, 36))) {
            m_showVictory = false;
            m_audio.playMusic("worldmap_music");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Main Menu", ImVec2(-1, 36))) {
            m_showVictory = false;
            m_state = GameState::MainMenu;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Defeat modal ──────────────────────────────────────────────────────────────
void Game::renderDefeatModal()
{
    ImGui::OpenPopup("Defeat");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Defeat", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_finalDefeat) {
            ImGui::TextColored(ImVec4(1.0f, 0.15f, 0.15f, 1.0f), "Total defeat!");
            ImGui::Spacing();
            ImGui::TextWrapped("You have no heroes with armies and no towns. There is no way to continue.");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Your army was defeated!");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Day %d  Week %d", m_turns.day(), m_turns.week());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bw = ImGui::GetWindowWidth() - 32.0f;
        if (!m_finalDefeat) {
            if (ImGui::Button("Continue (retreat)", ImVec2(bw * 0.55f, 36))) {
                m_showDefeat  = false;
                m_finalDefeat = false;
                m_audio.playMusic("worldmap_music");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(m_finalDefeat ? "Load Last Save" : "Load Last Save", ImVec2(m_finalDefeat ? bw * 0.6f : -1, 36))) {
            m_showDefeat  = false;
            m_finalDefeat = false;
            if (m_state == GameState::Campaign)
                loadGame("saves/campaign" + std::to_string(m_campaignActiveSlot) + ".json");
            else
                loadGame("saves/save" + std::to_string(m_activeSlot) + ".json");
            m_audio.playMusic("worldmap_music");
            ImGui::CloseCurrentPopup();
        }
        if (m_finalDefeat) {
            ImGui::SameLine();
            if (ImGui::Button("Main Menu", ImVec2(-1, 36))) {
                m_showDefeat  = false;
                m_finalDefeat = false;
                m_state = GameState::MainMenu;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

// ── Unit exchange overlay (two player heroes on same tile) ────────────────────
void Game::renderUnitExchange()
{
    if (!m_showUnitExchange) return;
    if (m_heroes.empty() || m_exchangeHeroIdx < 0
        || m_exchangeHeroIdx >= static_cast<int>(m_heroes.size())
        || m_exchangeHeroIdx == m_activeHeroIdx) {
        m_showUnitExchange = false;
        return;
    }
    Hero& heroA = m_heroes[m_activeHeroIdx];
    Hero& heroB = m_heroes[m_exchangeHeroIdx];
    const auto& unitDefs = m_registry.units();

    auto unitName = [&](int defId) -> std::string {
        for (const auto& ud : unitDefs)
            if (ud.id == defId) return ud.name;
        return "Unit";
    };

    ImGuiIO& io = ImGui::GetIO();
    float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;
    ImGui::SetNextWindowPos({cx, cy}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({520, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##exchange", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.2f, 1.0f}, "Unit Exchange");
    ImGui::SameLine(ImGui::GetWindowWidth() - 60);
    if (ImGui::SmallButton("Close")) {
        m_showUnitExchange = false;
        m_exchangeSelSlotA = m_exchangeSelSlotB = -1;
    }
    ImGui::Separator();

    // Helper: draw one hero's army column
    // Returns true if the user selected a slot
    auto drawCol = [&](const char* label, Hero& h, int& selSlot, bool isA) {
        ImGui::BeginGroup();
        ImGui::TextColored({0.7f,0.85f,1.0f,1.0f}, "%s", label);
        ImGui::Text("%s", h.name.c_str());
        ImGui::Spacing();
        constexpr int MAX_SLOTS = 7;
        for (int i = 0; i < MAX_SLOTS; ++i) {
            ImGui::PushID(isA ? (i * 100) : (i * 100 + 50));
            bool occupied = i < static_cast<int>(h.army.size()) && h.army[i].count > 0;
            bool selected = (selSlot == i);
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 0.9f));
            else if (!occupied)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f,0.15f,0.15f,0.6f));
            else
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f,0.35f,0.25f,0.8f));

            char lbl[64];
            if (occupied)
                std::snprintf(lbl, sizeof(lbl), "%-16s x%d",
                    unitName(h.army[i].defId).c_str(), h.army[i].count);
            else
                std::snprintf(lbl, sizeof(lbl), "[ empty ]");

            if (ImGui::Button(lbl, {220, 26})) selSlot = (selSlot == i) ? -1 : i;
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndGroup();
    };

    drawCol("HERO A", heroA, m_exchangeSelSlotA, true);
    ImGui::SameLine(0, 16);

    // Transfer arrow buttons in the middle
    ImGui::BeginGroup();
    ImGui::Dummy({50, 60});
    bool canAtoB = m_exchangeSelSlotA >= 0
        && m_exchangeSelSlotA < static_cast<int>(heroA.army.size())
        && heroA.army[m_exchangeSelSlotA].count > 0;
    bool canBtoA = m_exchangeSelSlotB >= 0
        && m_exchangeSelSlotB < static_cast<int>(heroB.army.size())
        && heroB.army[m_exchangeSelSlotB].count > 0;

    if (!canAtoB) ImGui::BeginDisabled();
    if (ImGui::Button("A>>B", {50, 26})) {
        auto& srcSlot = heroA.army[m_exchangeSelSlotA];
        bool merged = false;
        for (auto& s : heroB.army)
            if (s.defId == srcSlot.defId) { s.count += srcSlot.count; merged = true; break; }
        bool added = merged;
        if (!merged && heroB.army.size() < 7) { heroB.army.push_back(srcSlot); added = true; }
        if (added)
            heroA.army.erase(heroA.army.begin() + m_exchangeSelSlotA);
        m_exchangeSelSlotA = -1;
    }
    if (!canAtoB) ImGui::EndDisabled();

    ImGui::Spacing();

    if (!canBtoA) ImGui::BeginDisabled();
    if (ImGui::Button("B>>A", {50, 26})) {
        auto& srcSlot = heroB.army[m_exchangeSelSlotB];
        bool merged = false;
        for (auto& s : heroA.army)
            if (s.defId == srcSlot.defId) { s.count += srcSlot.count; merged = true; break; }
        bool added = merged;
        if (!merged && heroA.army.size() < 7) { heroA.army.push_back(srcSlot); added = true; }
        if (added)
            heroB.army.erase(heroB.army.begin() + m_exchangeSelSlotB);
        m_exchangeSelSlotB = -1;
    }
    if (!canBtoA) ImGui::EndDisabled();

    ImGui::Spacing();

    // Split half — recheck slots in case A>>B or B>>A just ran and cleared them
    if (canAtoB && m_exchangeSelSlotA >= 0
        && m_exchangeSelSlotA < static_cast<int>(heroA.army.size())
        && heroA.army[m_exchangeSelSlotA].count >= 2) {
        if (ImGui::Button("A/2>B", {50, 26})) {
            auto& src = heroA.army[m_exchangeSelSlotA];
            int half = src.count / 2;
            bool merged = false;
            for (auto& s : heroB.army)
                if (s.defId == src.defId) { s.count += half; merged = true; break; }
            bool added = merged;
            if (!merged && heroB.army.size() < 7) {
                heroB.army.push_back({src.defId, half}); added = true;
            }
            if (added) src.count -= half;
            m_exchangeSelSlotA = -1;
        }
    }
    ImGui::Spacing();
    if (canBtoA && m_exchangeSelSlotB >= 0
        && m_exchangeSelSlotB < static_cast<int>(heroB.army.size())
        && heroB.army[m_exchangeSelSlotB].count >= 2) {
        if (ImGui::Button("B/2>A", {50, 26})) {
            auto& src = heroB.army[m_exchangeSelSlotB];
            int half = src.count / 2;
            bool merged = false;
            for (auto& s : heroA.army)
                if (s.defId == src.defId) { s.count += half; merged = true; break; }
            bool added = merged;
            if (!merged && heroA.army.size() < 7) {
                heroA.army.push_back({src.defId, half}); added = true;
            }
            if (added) src.count -= half;
            m_exchangeSelSlotB = -1;
        }
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 16);
    drawCol("HERO B", heroB, m_exchangeSelSlotB, false);

    ImGui::End();
}

// ── Dwelling recruit popup ────────────────────────────────────────────────────
void Game::renderDwellingPopup()
{
    if (!m_showDwellingPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingObjId) { obj = &o; break; }
    if (!obj) { m_showDwellingPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##dwelling", nullptr, wf)) { ImGui::End(); return; }

    int tier = obj->value;
    int costPerUnit = tier * 50;
    FactionId fac = static_cast<FactionId>(obj->faction);

    // Find unit name for display
    const char* unitName = "Units";
    for (const auto& ud : m_registry.units())
        if (ud.faction == fac && ud.tier == tier && ud.path == UpgradePath::None) {
            unitName = ud.name.c_str(); break;
        }

    ImGui::TextColored({0.9f, 0.7f, 0.2f, 1.0f}, "Unit Dwelling — Tier %d  [%s]", tier, unitName);
    ImGui::Separator();
    ImGui::Text("Available: %d units   |   Cost: %d gold each", obj->available, costPerUnit);
    ImGui::Text("Gold on hand: %d", m_playerResources.get(ResourceType::Gold));
    ImGui::Spacing();

    if (m_heroes.empty()) { m_showDwellingPopup = false; ImGui::End(); return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    int maxAfford = (costPerUnit > 0) ? m_playerResources.get(ResourceType::Gold) / costPerUnit : obj->available;
    int canBuy    = std::min(maxAfford, obj->available);

    // Custom quantity slider
    static int s_dwellingQty = 0;
    if (ImGui::IsWindowAppearing()) s_dwellingQty = canBuy;
    s_dwellingQty = std::clamp(s_dwellingQty, 0, canBuy);

    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderInt("Quantity", &s_dwellingQty, 0, canBuy);
    ImGui::Text("Total cost: %d gold", s_dwellingQty * costPerUnit);
    ImGui::Spacing();

    auto doBuy = [&](int qty) {
        if (qty <= 0) return;
        int total = qty * costPerUnit;
        m_playerResources.add(ResourceType::Gold, -total);
        for (const auto& ud : m_registry.units()) {
            if (ud.faction == fac && ud.tier == tier && ud.path == UpgradePath::None) {
                bool merged = false;
                for (auto& s : hero.army)
                    if (s.defId == ud.id) { s.count += qty; merged = true; break; }
                if (!merged && hero.army.size() < 7)
                    hero.army.push_back({ud.id, qty});
                break;
            }
        }
        obj->available -= qty;
        char pickBuf[40];
        std::snprintf(pickBuf, sizeof(pickBuf), "+%d %s", qty, unitName);
        pushPickupEffect(obj->pos, pickBuf, IM_COL32(120, 220, 120, 255));
        m_audio.playSound("pickup");
    };

    if (s_dwellingQty > 0) {
        if (ImGui::Button("Buy", {100, 28})) { doBuy(s_dwellingQty); m_showDwellingPopup = false; }
        ImGui::SameLine();
        if (ImGui::Button("Buy All", {100, 28})) { doBuy(canBuy); m_showDwellingPopup = false; }
        ImGui::SameLine();
    }
    if (ImGui::Button("Close", {80, 28}))
        m_showDwellingPopup = false;

    ImGui::End();
}

// ── Stat shrine popup ─────────────────────────────────────────────────────────
void Game::renderStatShrinePopup()
{
    if (!m_showStatShrinePopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingObjId) { obj = &o; break; }
    if (!obj) { m_showStatShrinePopup = false; return; }

    // Stat shrine options — faction-aware bonus rotation (6 options)
    static const char* kStatNames[] = { "Attack", "Defense", "Move", "Mana", "Vision", "Hero HP" };
    const char* statName = kStatNames[obj->value % 6];

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##statshrine", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.5f, 0.1f, 1.0f}, "Stat Shrine");
    ImGui::Separator();
    static const char* kStatAmounts[] = { "+1", "+1", "+2", "+5", "+1", "+10" };
    const char* amt = kStatAmounts[obj->value % 6];
    ImGui::Text("Spend 1000 gold for %s %s?", amt, statName);
    ImGui::Text("Gold: %d   Uses remaining: %d", m_playerResources.get(ResourceType::Gold), obj->questState);
    ImGui::Spacing();

    bool canAfford = m_playerResources.get(ResourceType::Gold) >= 1000;

    if (!canAfford) ImGui::BeginDisabled();
    if (ImGui::Button("Yes", {80, 28})) {
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            m_playerResources.add(ResourceType::Gold, -1000);
            switch (obj->value % 6) {
            case 0: hero.attack  += 1; break;
            case 1: hero.defense += 1; break;
            case 2: hero.maxMove += 2; hero.movePool = std::min(hero.movePool + 2, hero.maxMove); break;
            case 3: hero.maxMana += 5; hero.mana = std::min(hero.mana + 5, hero.maxMana); break;
            case 4: hero.visionRange += 1; FogOfWar::updateVision(m_map, hero); break;
            case 5: hero.heroMaxHp += 10; hero.heroHp = std::min(hero.heroHp + 10, hero.heroMaxHp); break;
            }
            obj->questState--;
            gLog("StatShrine: %s %s, uses left: %d\n", amt, statName, obj->questState);
        }
        m_showStatShrinePopup = false;
    }
    if (!canAfford) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("No", {80, 28}))
        m_showStatShrinePopup = false;

    ImGui::End();
}

// ── Treasure chest popup ─────────────────────────────────────────────────────
void Game::renderTreasureChestPopup()
{
    if (!m_showTreasureChestPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingChestId) { obj = &o; break; }
    if (!obj || obj->collected) { m_showTreasureChestPopup = false; return; }

    static const char* kStatNames[] = { "Attack", "Defense", "Movement" };
    int statType = obj->faction % 3;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##tchest", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "Treasure Chest");
    ImGui::Separator();
    ImGui::TextWrapped("You found a weathered chest. Choose your reward:");
    ImGui::Spacing();

    float bw = ImGui::GetWindowWidth() - 32.0f;

    // Option A: Gold
    char goldLbl[64];
    std::snprintf(goldLbl, sizeof(goldLbl), "Gold  (+%d gold coins)", obj->value);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.42f, 0.05f, 1.0f));
    if (ImGui::Button(goldLbl, ImVec2(bw, 34))) {
        m_playerResources.add(ResourceType::Gold, obj->value);
        char buf[32]; std::snprintf(buf, sizeof(buf), "+%d Gold", obj->value);
        pushPickupEffect(obj->pos, buf, IM_COL32(255, 215, 0, 255));
        m_audio.playSound("buy");
        obj->collected = true;
        m_showTreasureChestPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option B: Experience
    char xpLbl[64];
    std::snprintf(xpLbl, sizeof(xpLbl), "Experience  (+%d XP)", obj->questState);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.15f, 1.0f));
    if (ImGui::Button(xpLbl, ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            int oldLevel = hero.level;
            bool leveled = hero.addXp(obj->questState);
            if (leveled) {
                const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                if (cls) {
                    std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                    m_levelUpOffers = LevelUpSystem::generateOffers(*cls, hero.skills, hero.level, allSkills, hero.faction);
                }
                if (m_levelUpOffers.empty())
                    m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                m_pendingLevelUps = hero.level - oldLevel;
                m_showLevelUpModal = true;
            }
            char buf[32]; std::snprintf(buf, sizeof(buf), "+%d XP", obj->questState);
            pushPickupEffect(obj->pos, buf, IM_COL32(120, 220, 120, 255));
        }
        m_audio.playSound("pickup");
        obj->collected = true;
        m_showTreasureChestPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option C: Stat boost
    char statLbl[64];
    std::snprintf(statLbl, sizeof(statLbl), "Stat Boost  (+1 %s, permanent)", kStatNames[statType]);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
    if (ImGui::Button(statLbl, ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            if (statType == 0)      hero.attack++;
            else if (statType == 1) hero.defense++;
            else                  { hero.maxMove += 2; hero.movePool = std::min(hero.movePool + 2, hero.maxMove); }
            char buf[48]; std::snprintf(buf, sizeof(buf), "+1 %s!", kStatNames[statType]);
            pushPickupEffect(obj->pos, buf, IM_COL32(100, 160, 255, 255));
        }
        m_audio.playSound("pickup");
        obj->collected = true;
        m_showTreasureChestPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::Separator();
    if (ImGui::Button("Leave it", ImVec2(bw, 26)))
        m_showTreasureChestPopup = false;

    ImGui::End();
}

// ── Pickup effect helper ──────────────────────────────────────────────────────
void Game::pushPickupEffect(HexCoord pos, const char* text, ImU32 col)
{
    float wx, wy;
    m_hexRenderer.grid().hexToWorld(pos, wx, wy);
    m_pickupEffects.push_back({wx, wy, 2.0f, text, col});

    // Emit matching particles at screen position
    float sx, sy;
    m_camera.worldToScreen(wx, wy, sx, sy);
    m_particles.emit(sx, sy, ParticlePreset::Pickup, 8);
}

// ── Quest popup ───────────────────────────────────────────────────────────────
void Game::renderQuestPopup()
{
    if (!m_showQuestPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingObjId) { obj = &o; break; }
    if (!obj) { m_showQuestPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##quest", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.1f, 1.0f}, "Quest Available!");
    ImGui::Separator();

    // Varied quest descriptions keyed by id
    static const char* kQuestDesc[] = {
        "A hooded traveller speaks in hushed tones: \"There is a relic hidden beyond these hills — I dare not retrieve it myself. Bring it back and I will reward you handsomely.\"",
        "An elder points toward the horizon: \"Something stirs in that forsaken place. Venture forth and confirm our fears. I shall compensate your bravery well.\"",
        "A wounded scout gasps: \"My companions fell near that cursed site. Find what drove them off and return with proof — gold awaits you.\"",
        "A merchant clutches his pack nervously: \"I lost my ledger at a strange landmark east of here. Retrieve it and five-hundred gold pieces are yours.\"",
        "A hermit emerges from shadows: \"The spirits show me a sign at a place I cannot name. You, traveller — find it and return to me. Your effort will not go unrewarded.\"",
        "A garrison captain frowns: \"We've had reports of unusual activity at a location I've marked. Scout it and report back; there's coin in it for you.\"",
        "A scholar waves a parchment: \"Ancient writings speak of an artifact at these coordinates. Recover it for study and I'll pay you fairly.\"",
        "A cloaked figure steps forward: \"Call it fate that brings you here. Travel to the marked spot, survive what you find, and claim your gold.\"",
    };
    int descIdx = static_cast<int>(obj->id) % (int)(sizeof(kQuestDesc) / sizeof(kQuestDesc[0]));
    ImGui::TextWrapped("%s\n\nReward: 500 gold.", kQuestDesc[descIdx]);
    ImGui::Spacing();

    if (ImGui::Button("Accept", {100, 28})) {
        obj->questState = 1;
        m_showQuestPopup = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Decline", {100, 28}))
        m_showQuestPopup = false;

    ImGui::End();
}

void Game::renderCryptPopup()
{
    if (!m_showCryptPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingCryptId) { obj = &o; break; }
    if (!obj) { m_showCryptPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##crypt", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.7f, 0.4f, 1.0f, 1.0f}, "Crypt Cleared!");
    ImGui::Separator();
    int goldReward = 200 + obj->value * 150;
    int spellId    = obj->questState; // set when combat wins
    char msg[128];
    std::snprintf(msg, sizeof(msg),
        "The crypt's defenders have fallen. You claim the spoils:\n+%d Gold", goldReward);
    ImGui::TextWrapped("%s", msg);
    if (spellId > 0) {
        const SpellDef* sp = findSpell(spellId);
        if (sp) {
            ImGui::TextColored({0.4f,0.8f,1.0f,1.0f}, "Spell: %s", sp->name);
        }
    }
    ImGui::Spacing();
    float bw = ImGui::GetWindowWidth() - 32.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.2f, 0.55f, 1.0f));
    if (ImGui::Button("Claim Reward", ImVec2(bw, 34))) {
        m_playerResources.add(ResourceType::Gold, goldReward);
        char buf[32]; std::snprintf(buf, sizeof(buf), "+%d Gold (Crypt)", goldReward);
        pushPickupEffect(obj->pos, buf, IM_COL32(200, 140, 255, 255));
        if (spellId > 0 && !m_heroes.empty()) {
            Hero& h = m_heroes[m_activeHeroIdx];
            bool known = false;
            for (int k : h.knownSpells) if (k == spellId) { known = true; break; }
            if (!known) h.knownSpells.push_back(spellId);
        }
        obj->collected = true;
        m_showCryptPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

void Game::renderUtopiaPopup()
{
    if (!m_showUtopiaPopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingUtopiaId) { obj = &o; break; }
    if (!obj || obj->collected) { m_showUtopiaPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##utopia", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.1f, 1.0f}, "Utopia Conquered!");
    ImGui::Separator();
    ImGui::TextWrapped("Ancient guardians have been vanquished. Choose your reward:");
    ImGui::Spacing();

    float bw = ImGui::GetWindowWidth() - 32.0f;

    // Option A: Rare artifact (give a known artifact id if any left)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.35f, 0.05f, 1.0f));
    if (ImGui::Button("Artifact  (Rare equip from the vault)", ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& h = m_heroes[m_activeHeroIdx];
            // Give a random artifact based on reward seed
            int artId = 1 + static_cast<int>(obj->value % 8);
            if (!m_artifactRegistry.getDef(artId)) artId = 1;
            h.artifactInventory.push_back(artId);
            char buf[48]; std::snprintf(buf, sizeof(buf), "Artifact acquired! (Utopia)");
            pushPickupEffect(obj->pos, buf, IM_COL32(255, 200, 50, 255));
        }
        obj->collected = true; m_showUtopiaPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option B: Gold + faction primary resource
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.35f, 0.04f, 1.0f));
    if (ImGui::Button("2000 Gold  +  10x Faction Resource", ImVec2(bw, 34))) {
        m_playerResources.add(ResourceType::Gold, 2000);
        // Give faction-appropriate primary resource
        static const ResourceType kFacRes[] = {
            ResourceType::FaithStones, ResourceType::BloodEssence,
            ResourceType::VerdantSap,  ResourceType::Mercury,
            ResourceType::BloodEssence,ResourceType::Mercury,
            ResourceType::Iron,        ResourceType::BloodEssence,
            ResourceType::FaithStones,
        };
        int fac = obj->faction % 9;
        m_playerResources.add(kFacRes[fac], 10);
        char buf[56];
        std::snprintf(buf, sizeof(buf), "+2000 Gold +10 Resource (Utopia)");
        pushPickupEffect(obj->pos, buf, IM_COL32(255, 215, 0, 255));
        obj->collected = true; m_showUtopiaPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Option C: Stack of T5 creatures from the faction
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.38f, 0.15f, 1.0f));
    if (ImGui::Button("Army  (12x T5 units join your force)", ImVec2(bw, 34))) {
        if (!m_heroes.empty()) {
            Hero& h = m_heroes[m_activeHeroIdx];
            // Find a T5 unit for this faction from registry
            int t5DefId = 0;
            for (const auto& ud : m_registry.units()) {
                if (ud.tier == 5 && static_cast<int>(ud.faction) == (obj->faction % 9)) {
                    t5DefId = ud.id; break;
                }
            }
            if (t5DefId == 0) {
                // Fallback: pick any T5 unit
                for (const auto& ud : m_registry.units())
                    if (ud.tier == 5) { t5DefId = ud.id; break; }
            }
            if (t5DefId > 0) {
                bool merged = false;
                for (auto& s : h.army)
                    if (s.defId == t5DefId) { s.count += 12; merged = true; break; }
                if (!merged && h.army.size() < 7)
                    h.army.push_back({t5DefId, 12});
                char buf[56]; std::snprintf(buf, sizeof(buf), "+12 T5 Units (Utopia)");
                pushPickupEffect(obj->pos, buf, IM_COL32(100, 220, 100, 255));
            }
        }
        obj->collected = true; m_showUtopiaPopup = false;
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

// ── Mine info popup (right-click on mine) ────────────────────────────────────
void Game::renderMineInfoPopup()
{
    if (!m_showMineInfoPopup) return;
    const ResourceNode* r = nullptr;
    for (const auto& rn : m_resources)
        if (rn.id == m_mineInfoId) { r = &rn; break; }
    if (!r) { m_showMineInfoPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##mine_info", nullptr, wf)) { ImGui::End(); return; }

    const char* status = r->ownedBy == 1 ? " [Owned]"
                       : r->guardBeaten  ? " [Captured]"
                       : " [Guarded]";
    ImGui::TextColored({0.9f, 0.75f, 0.2f, 1.0f}, "%s Mine%s  +%d/wk",
                       resourceName(r->type), status, r->amount);
    ImGui::Separator();

    if (r->guardBeaten || r->ownedBy == 1) {
        ImGui::TextDisabled("No defenders — mine is unguarded.");
    } else {
        ImGui::TextColored({0.95f, 0.45f, 0.45f, 1.0f}, "Defenders:");
        auto guards = makeMineGuardUnits(*r);
        int guardPower = 0;
        for (const auto& g : guards) {
            ImGui::Text("  %-20s x%-4d  ATK %d  DEF %d  HP %d  SPD %d",
                        g.name.c_str(), g.count, g.attack, g.defense, g.maxHp, g.speed);
            guardPower += g.count * g.maxHp * (g.attack + g.defense / 2);
        }

        int playerPower = 0;
        if (!m_heroes.empty()) {
            const Hero& h = m_heroes[m_activeHeroIdx];
            for (const auto& stack : h.army) {
                const UnitDef* ud = m_registry.getUnitDef(stack.defId);
                if (ud) playerPower += stack.count * ud->hp * (ud->attack + ud->defense / 2);
            }
        }

        ImGui::Separator();
        if (playerPower > 0) {
            float ratio = static_cast<float>(guardPower) / static_cast<float>(playerPower);
            const char* rating;
            ImVec4 col;
            if      (ratio < 0.30f) { rating = "Trivial";      col = {0.5f, 1.0f, 0.5f, 1.0f}; }
            else if (ratio < 0.60f) { rating = "Weak";         col = {0.7f, 1.0f, 0.5f, 1.0f}; }
            else if (ratio < 0.90f) { rating = "Moderate";     col = {1.0f, 0.9f, 0.4f, 1.0f}; }
            else if (ratio < 1.10f) { rating = "Even Match";   col = {1.0f, 0.7f, 0.2f, 1.0f}; }
            else if (ratio < 1.50f) { rating = "Strong";       col = {1.0f, 0.4f, 0.3f, 1.0f}; }
            else                    { rating = "Overwhelming"; col = {0.9f, 0.1f, 0.1f, 1.0f}; }
            ImGui::Text("Threat vs your army: ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(col, " %s", rating);
        } else {
            ImGui::TextDisabled("(no hero selected to compare)");
        }
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 80.0f) * 0.5f);
    if (ImGui::Button("Close", {80, 0}))
        m_showMineInfoPopup = false;
    ImGui::End();
}

// ── Tree of Knowledge popup ───────────────────────────────────────────────────
void Game::renderTreeOfKnowledgePopup()
{
    if (!m_showTreeKnowledgePopup) return;
    WorldObject* obj = nullptr;
    for (auto& o : m_worldObjects)
        if (o.id == m_pendingTreeId) { obj = &o; break; }
    if (!obj || obj->collected) { m_showTreeKnowledgePopup = false; return; }
    if (m_heroes.empty()) { m_showTreeKnowledgePopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    int goldCost  = 2000;
    int freeXp    = 200 * hero.level;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##treknowledge", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.3f, 0.9f, 0.4f, 1.0f}, "Tree of Knowledge");
    ImGui::Separator();
    ImGui::TextWrapped("An ancient tree pulses with accumulated wisdom. It offers two paths:");
    ImGui::Spacing();

    ImGui::BulletText("Pay %d Gold -> gain one full level", goldCost);
    ImGui::BulletText("Accept freely -> gain %d XP", freeXp);
    ImGui::Spacing();
    ImGui::Text("Hero: %s  (Level %d)  Gold: %d",
        hero.name.c_str(), hero.level, m_playerResources.get(ResourceType::Gold));
    ImGui::Spacing();

    bool canAfford = m_playerResources.get(ResourceType::Gold) >= goldCost;

    if (!canAfford) ImGui::BeginDisabled();
    if (ImGui::Button("Pay Gold (+1 Level)", {160, 28})) {
        m_playerResources.add(ResourceType::Gold, -goldCost);
        obj->collected = true;
        m_showTreeKnowledgePopup = false;
        // Force-level: give enough XP to trigger level-up
        int xpNeeded = hero.xpToNext - hero.xp;
        int oldLvl = hero.level;
        hero.addXp(xpNeeded);
        pushPickupEffect(obj->pos, "Level Up!", IM_COL32(80, 220, 100, 255));
        const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
        if (cls) {
            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
            m_levelUpOffers = LevelUpSystem::generateOffers(
                *cls, hero.skills, hero.level, allSkills, hero.faction);
        }
        if (m_levelUpOffers.empty())
            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
        m_pendingLevelUps = hero.level - oldLvl;
        m_showLevelUpModal = true;
        m_audio.playSound("levelup");
        ScriptContext lvCtx; lvCtx.heroId = hero.id;
        m_triggers.fire(TriggerType::HeroLevel, lvCtx);
    }
    if (!canAfford) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Take XP", {80, 28})) {
        obj->collected = true;
        m_showTreeKnowledgePopup = false;
        int oldLvl = hero.level;
        char buf[32]; std::snprintf(buf, sizeof(buf), "+%d XP", freeXp);
        pushPickupEffect(obj->pos, buf, IM_COL32(100, 220, 130, 255));
        m_audio.playSound("pickup");
        if (hero.addXp(freeXp)) {
            const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
            if (cls) {
                std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                m_levelUpOffers = LevelUpSystem::generateOffers(
                    *cls, hero.skills, hero.level, allSkills, hero.faction);
            }
            if (m_levelUpOffers.empty())
                m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
            m_pendingLevelUps = hero.level - oldLvl;
            m_showLevelUpModal = true;
            m_audio.playSound("levelup");
            ScriptContext lvCtx; lvCtx.heroId = hero.id;
            m_triggers.fire(TriggerType::HeroLevel, lvCtx);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Pass", {60, 28}))
        m_showTreeKnowledgePopup = false;

    ImGui::End();
}

// ── Pre-combat encounter prompt ───────────────────────────────────────────────
void Game::renderEncounterPrompt()
{
    if (!m_showEncounterPrompt) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({420, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##encounter_prompt", nullptr, wf)) { ImGui::End(); return; }

    // Title
    ImGui::TextColored({1.0f, 0.55f, 0.15f, 1.0f}, "%s", m_encounterTitle.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    // Enemy unit list
    ImGui::TextColored({0.95f, 0.45f, 0.45f, 1.0f}, "Defenders:");
    int guardPower = 0;
    for (const auto& u : m_pendingEncounterUnits) {
        ImGui::Text("  %-22s x%-4d  ATK %d  DEF %d  HP %d",
                    u.name.c_str(), u.count, u.attack, u.defense, u.maxHp);
        guardPower += u.count * u.maxHp * (u.attack + u.defense / 2);
    }

    // Threat rating vs player army
    ImGui::Spacing();
    ImGui::Separator();
    int playerPower = 0;
    if (!m_heroes.empty()) {
        const Hero& h = m_heroes[m_activeHeroIdx];
        for (const auto& stack : h.army) {
            const UnitDef* ud = m_registry.getUnitDef(stack.defId);
            if (ud) playerPower += stack.count * ud->hp * (ud->attack + ud->defense / 2);
        }
    }
    if (playerPower > 0 && guardPower > 0) {
        float ratio = static_cast<float>(guardPower) / static_cast<float>(playerPower);
        const char* rating;
        ImVec4 col;
        if      (ratio < 0.30f) { rating = "Trivial";      col = {0.5f, 1.0f, 0.5f, 1.0f}; }
        else if (ratio < 0.60f) { rating = "Weak";         col = {0.7f, 1.0f, 0.5f, 1.0f}; }
        else if (ratio < 0.90f) { rating = "Moderate";     col = {1.0f, 0.9f, 0.4f, 1.0f}; }
        else if (ratio < 1.10f) { rating = "Even Match";   col = {1.0f, 0.7f, 0.2f, 1.0f}; }
        else if (ratio < 1.50f) { rating = "Strong";       col = {1.0f, 0.4f, 0.3f, 1.0f}; }
        else                    { rating = "Overwhelming"; col = {0.9f, 0.1f, 0.1f, 1.0f}; }
        ImGui::Text("Threat: ");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(col, " %s", rating);
    } else {
        ImGui::TextDisabled("(no army to compare)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Fight button (green)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.55f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.70f, 0.20f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.40f, 0.10f, 1.0f});
    if (ImGui::Button("Fight!", {140, 32})) {
        m_showEncounterPrompt = false;
        if (m_encounterOnAccept) m_encounterOnAccept();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 20);

    // Retreat button (dark red)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.50f, 0.12f, 0.12f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.65f, 0.18f, 0.18f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.35f, 0.08f, 0.08f, 1.0f});
    if (ImGui::Button("Retreat", {140, 32})) {
        m_showEncounterPrompt = false;
        if (m_encounterOnDecline) m_encounterOnDecline();
    }
    ImGui::PopStyleColor(3);

    ImGui::End();
}

// ── Kingdom Overview panel ────────────────────────────────────────────────────
void Game::renderKingdomPanel()
{
    if (!m_showKingdomPanel) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Once, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({560, 480}, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.97f);

    if (!ImGui::Begin("Kingdom Overview", &m_showKingdomPanel,
        ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }

    // ── HEROES ────────────────────────────────────────────────────────────────
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "HEROES");
    ImGui::Separator();

    for (const auto& h : m_heroes) {
        char hdr[120];
        std::snprintf(hdr, sizeof(hdr), "%s  (Level %d  ATK %d  DEF %d  MP %d/%d)",
                      h.name.c_str(), h.level, h.attack, h.defense, h.mana, h.maxMana);
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Move bar
            float mv = h.maxMove > 0 ? static_cast<float>(h.movePool) / h.maxMove : 0.0f;
            ImGui::ProgressBar(mv, ImVec2(-1, 8), "");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Movement: %d / %d", h.movePool, h.maxMove);

            // Army
            ImGui::TextColored({0.7f, 1.0f, 0.7f, 1.0f}, "Army:");
            ImGui::SameLine();
            bool first = true;
            for (const auto& s : h.army) {
                if (s.count <= 0) continue;
                // Look up unit name
                const char* uname = "Unit";
                for (const auto& ud : m_registry.units())
                    if (ud.id == s.defId) { uname = ud.name.c_str(); break; }
                if (!first) ImGui::SameLine();
                ImGui::TextColored({0.85f, 0.9f, 0.85f, 1.0f}, "[%s x%d]", uname, s.count);
                first = false;
            }
            if (first) ImGui::TextDisabled("  (no army)");

            // Known world-map spells
            bool hasWS = false;
            for (int sid : h.knownSpells) {
                const SpellDef* sp = findSpell(sid);
                if (sp && sp->target == SpellTarget::WorldMap) {
                    if (!hasWS) { ImGui::TextColored({0.7f, 0.7f, 1.0f, 1.0f}, "Spells:"); ImGui::SameLine(); hasWS = true; }
                    ImGui::TextColored({0.8f, 0.8f, 1.0f, 1.0f}, "[%s]", sp->name);
                    ImGui::SameLine();
                }
            }
            if (hasWS) ImGui::NewLine();

            ImGui::TreePop();
        }
    }

    ImGui::Spacing();

    // ── TOWNS ─────────────────────────────────────────────────────────────────
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "TOWNS");
    ImGui::Separator();

    static const char* kFacShort[] = {
        "HO","BS","TK","EE","CW","VK","IA","AM","CV"
    };
    bool anyTown = false;
    for (const auto& t : m_towns) {
        if (t.ownerId != 1) continue;
        anyTown = true;
        int fi = static_cast<int>(t.faction);
        char thdr[100];
        std::snprintf(thdr, sizeof(thdr), "%s  [%s]  %d buildings",
                      t.name.c_str(),
                      (fi >= 0 && fi < 9) ? kFacShort[fi] : "?",
                      static_cast<int>(t.builtBuildings.size()));
        if (ImGui::TreeNodeEx(thdr, ImGuiTreeNodeFlags_Leaf)) {
            // Garrison
            if (!t.garrison.empty()) {
                ImGui::TextColored({0.9f, 0.8f, 0.5f, 1.0f}, "Garrison:");
                for (const auto& s : t.garrison) {
                    const char* uname = "Unit";
                    for (const auto& ud : m_registry.units())
                        if (ud.id == s.defId) { uname = ud.name.c_str(); break; }
                    ImGui::SameLine();
                    ImGui::Text("[%s x%d]", uname, s.count);
                }
            }
            ImGui::TreePop();
        }
    }
    if (!anyTown) ImGui::TextDisabled("No towns owned.");

    ImGui::Spacing();

    // ── MINES & INCOME ────────────────────────────────────────────────────────
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "MINES & INCOME");
    ImGui::Separator();

    // Aggregate mines by resource type
    int mineCount[RESOURCE_COUNT] = {};
    int mineIncome[RESOURCE_COUNT] = {};
    for (const auto& r : m_resources) {
        if (r.ownedBy != 1) continue;
        int ri = static_cast<int>(r.type);
        mineCount[ri]++;
        mineIncome[ri] += r.amount;
    }

    static constexpr ImVec4 kResCol[] = {
        {1.0f, 0.82f, 0.2f, 1.0f},  // Gold
        {0.65f,0.72f,0.80f, 1.0f},  // Iron
        {0.91f,0.89f,1.00f, 1.0f},  // FaithStones
        {0.85f,0.25f,0.25f, 1.0f},  // BloodEssence
        {0.30f,0.75f,0.35f, 1.0f},  // VerdantSap
        {0.20f,0.72f,0.65f, 1.0f},  // Mercury
    };

    ImGui::Columns(4, "mines_cols", true);
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Resource");     ImGui::NextColumn();
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Mines owned");  ImGui::NextColumn();
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Income / wk");  ImGui::NextColumn();
    ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "Current");      ImGui::NextColumn();
    ImGui::Separator();
    for (int i = 0; i < RESOURCE_COUNT; ++i) {
        auto rt = static_cast<ResourceType>(i);
        ImVec4 col = (i < 6) ? kResCol[i] : ImVec4(1,1,1,1);
        ImGui::TextColored(col, "%s", resourceName(rt));     ImGui::NextColumn();
        ImGui::Text("%d", mineCount[i]);                      ImGui::NextColumn();
        if (mineIncome[i] > 0)
            ImGui::TextColored({0.4f,1.f,0.4f,1.f}, "+%d", mineIncome[i]);
        else
            ImGui::TextDisabled("0");
        ImGui::NextColumn();
        ImGui::Text("%d", m_playerResources.get(rt));         ImGui::NextColumn();
    }
    ImGui::Columns(1);

    ImGui::Spacing();
    float bw = ImGui::GetWindowWidth() - 32.0f;
    if (ImGui::Button("Close", ImVec2(bw, 28)))
        m_showKingdomPanel = false;

    ImGui::End();
}

// ── Mini-map ──────────────────────────────────────────────────────────────────
// Minimap rendering is handled inside renderWorldOverlay(), toggled with M key.
void Game::renderMinimap() {}

// ── World-map spell panel ─────────────────────────────────────────────────────
void Game::renderWorldSpellPanel()
{
    if (m_heroes.empty()) { m_showWorldSpellPanel = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({8.0f, io.DisplaySize.y - 340.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({220, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    if (!ImGui::Begin("World Spells", &m_showWorldSpellPanel,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }

    ImGui::Text("Mana: %d / %d", hero.mana, hero.maxMana);
    ImGui::Separator();

    bool any = false;
    for (int sid : hero.knownSpells) {
        const SpellDef* spl = findSpell(sid);
        if (!spl || spl->target != SpellTarget::WorldMap) continue;
        any = true;
        bool canAfford = hero.mana >= spl->manaCost;
        if (!canAfford) ImGui::BeginDisabled();
        char label[128];
        std::snprintf(label, sizeof(label), "%dmana  %s", spl->manaCost, spl->name);
        if (ImGui::Button(label, ImVec2(-1, 0)))
            castWorldSpell(sid);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", spl->desc);
        if (!canAfford) ImGui::EndDisabled();
    }
    if (!any) ImGui::TextDisabled("No world-map spells known.");
    ImGui::End();
}

// ── castWorldSpell ────────────────────────────────────────────────────────────
void Game::castWorldSpell(int spellId)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    const SpellDef* spl = findSpell(spellId);
    if (!spl || hero.mana < spl->manaCost) return;

    if (spellId == SPL::VISIONS) {
        hero.mana -= spl->manaCost;
        m_audio.playSound("spell");
        // Temporarily expand vision to reveal radius around hero
        int saved = hero.visionRange;
        hero.visionRange = spl->power;
        FogOfWar::updateVision(m_map, hero);
        hero.visionRange = saved;
        // Count revealed entities for feedback
        int found = 0;
        for (const auto& eh : m_enemyHeroes)
            if (HexGrid::distance(hero.pos, eh.pos) <= spl->power) ++found;
        for (const auto& obj : m_worldObjects)
            if (!obj.collected && HexGrid::distance(hero.pos, obj.pos) <= spl->power) ++found;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Visions: %d things revealed", found);
        pushPickupEffect(hero.pos, buf, IM_COL32(160, 160, 255, 255));
        m_showWorldSpellPanel = false;
    }
    else if (spellId == SPL::TOWN_PORTAL) {
        if (hero.movePool < hero.maxMove) {
            pushPickupEffect(hero.pos,
                "Town Portal needs full movement — hero already moved!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        hero.mana -= spl->manaCost;
        m_audio.playSound("spell");
        m_showWorldSpellPanel = false;
        m_showTownPortalPopup = true;
    }
    else if (spellId == SPL::FOUND_CITY) {
        if (hero.level < 10) {
            pushPickupEffect(hero.pos, "Found City requires hero level 10!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        // Must stand on a cleared Utopia
        WorldObject* utopia = nullptr;
        for (auto& obj : m_worldObjects)
            if (obj.type == WorldObjectType::Utopia && obj.pos == hero.pos && obj.collected)
                { utopia = &obj; break; }
        if (!utopia) {
            pushPickupEffect(hero.pos, "Found City: must stand on a cleared Utopia!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        // Check cost: 10 000 gold + 10 each other resource
        Resources cost;
        cost.set(ResourceType::Gold,         10000);
        cost.set(ResourceType::Iron,            10);
        cost.set(ResourceType::FaithStones,     10);
        cost.set(ResourceType::BloodEssence,    10);
        cost.set(ResourceType::VerdantSap,      10);
        cost.set(ResourceType::Mercury,         10);
        if (!m_playerResources.canAfford(cost)) {
            pushPickupEffect(hero.pos, "Found City: insufficient resources!",
                IM_COL32(255, 80, 80, 255));
            return;
        }
        m_foundCityUtopiaId = utopia->id;
        hero.mana -= spl->manaCost;
        m_audio.playSound("spell");
        m_showWorldSpellPanel = false;
        m_showFoundCityPopup  = true;
    }
}

// ── Town Portal popup ─────────────────────────────────────────────────────────
void Game::renderTownPortalPopup()
{
    if (!m_showTownPortalPopup) return;
    if (m_heroes.empty()) { m_showTownPortalPopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({320, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##townportal", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "Town Portal");
    ImGui::Separator();
    ImGui::TextWrapped("Choose a destination (all movement spent on arrival):");
    ImGui::Spacing();

    // Collect player towns sorted by distance
    std::vector<std::pair<int,Town*>> options;
    for (auto& t : m_towns)
        if (t.ownerId == 1)
            options.push_back({HexGrid::distance(hero.pos, t.pos), &t});
    std::sort(options.begin(), options.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    if (options.empty()) {
        ImGui::TextDisabled("No friendly towns.");
    }
    for (auto& [dist, t] : options) {
        char label[80];
        std::snprintf(label, sizeof(label), "%s  (%d tiles away)", t->name.c_str(), dist);
        if (ImGui::Button(label, ImVec2(-1, 30))) {
            // Remove hero from old tile
            if (HexTile* ot = m_map.getTile(hero.pos)) ot->heroId = 0;
            hero.pos = t->pos;
            hero.movePool = 0;
            if (HexTile* nt = m_map.getTile(hero.pos)) nt->heroId = hero.id;
            FogOfWar::updateVision(m_map, hero);
            // Snap camera to destination
            float wx, wy;
            m_hexRenderer.grid().hexToWorld(hero.pos, wx, wy);
            m_camera.setPosition(wx, wy);
            pushPickupEffect(hero.pos, "Town Portal!", IM_COL32(100, 180, 255, 255));
            m_showTownPortalPopup = false;
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(-1, 28))) {
        // Refund mana — spell was already deducted before opening popup
        const SpellDef* spl = findSpell(SPL::TOWN_PORTAL);
        if (spl) hero.mana = std::min(hero.maxMana, hero.mana + spl->manaCost);
        m_showTownPortalPopup = false;
    }
    ImGui::End();
}

// ── Found City popup ──────────────────────────────────────────────────────────
void Game::renderFoundCityPopup()
{
    if (!m_showFoundCityPopup) return;
    if (m_heroes.empty()) { m_showFoundCityPopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    // Find the Utopia object by id
    WorldObject* utopia = nullptr;
    for (auto& obj : m_worldObjects)
        if (obj.id == m_foundCityUtopiaId) { utopia = &obj; break; }
    if (!utopia) { m_showFoundCityPopup = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##foundcity", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.85f, 0.3f, 1.0f}, "Found City");
    ImGui::Separator();
    ImGui::TextWrapped("Choose the faction for your new settlement.");
    ImGui::TextWrapped("Cost: 10000 Gold + 10 Iron + 10 Faith Stones + 10 Blood Essence + 10 Verdant Sap + 10 Mercury");
    ImGui::Spacing();

    static const char* kFacNames[] = {
        "Holy Order","Bloodsworn","Thornkin","Eternal Empire",
        "Crimson Wardens","Voidkin","Iron Assembly","Amalgamate","Convergence"
    };
    float bw = ImGui::GetWindowWidth() - 32.0f;
    HexCoord cityPos = utopia->pos;

    for (int i = 0; i < 9; ++i) {
        if (ImGui::Button(kFacNames[i], ImVec2(bw, 30))) {
            Resources cost;
            cost.set(ResourceType::Gold,         10000);
            cost.set(ResourceType::Iron,            10);
            cost.set(ResourceType::FaithStones,     10);
            cost.set(ResourceType::BloodEssence,    10);
            cost.set(ResourceType::VerdantSap,      10);
            cost.set(ResourceType::Mercury,         10);
            m_playerResources.spend(cost);

            // Build unique town id
            uint32_t newId = 1;
            for (const auto& t : m_towns) newId = std::max(newId, t.id + 1);

            Town newTown;
            newTown.id      = newId;
            newTown.name    = std::string(kFacNames[i]) + " Settlement";
            newTown.faction = static_cast<FactionId>(i);
            newTown.pos     = cityPos;
            newTown.ownerId = 1;
            if (HexTile* ht = m_map.getTile(newTown.pos)) ht->townId = newTown.id;
            m_towns.push_back(newTown);

            // Remove the Utopia world object
            m_worldObjects.erase(
                std::remove_if(m_worldObjects.begin(), m_worldObjects.end(),
                    [this](const WorldObject& o){ return o.id == m_foundCityUtopiaId; }),
                m_worldObjects.end());

            char buf[64];
            std::snprintf(buf, sizeof(buf), "Founded: %s Settlement!", kFacNames[i]);
            pushPickupEffect(hero.pos, buf, IM_COL32(255, 215, 50, 255));
            m_showFoundCityPopup = false;
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(bw, 28))) {
        // Refund mana
        const SpellDef* spl = findSpell(SPL::FOUND_CITY);
        if (spl) hero.mana = std::min(hero.maxMana, hero.mana + spl->manaCost);
        m_showFoundCityPopup = false;
    }
    ImGui::End();
}

// ── Shipyard popup (build a boat) ─────────────────────────────────────────────
void Game::renderShipyardPopup()
{
    if (!m_showShipyardPopup) return;
    if (m_heroes.empty()) { m_showShipyardPopup = false; return; }
    Hero& hero = m_heroes[m_activeHeroIdx];

    int goldCost = 2000 + hero.boatCount * 1000;
    int ironCost = 10;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##shipyard", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({0.3f, 0.6f, 1.0f, 1.0f}, "Shipyard");
    ImGui::Separator();
    ImGui::TextWrapped("Build a boat and set sail across the waters.");
    ImGui::Spacing();
    ImGui::Text("Cost: %d Gold + %d Iron", goldCost, ironCost);
    if (hero.boatCount > 0)
        ImGui::TextDisabled("(Boats built: %d — each costs 1000g more)", hero.boatCount);
    ImGui::Spacing();

    int gold = m_playerResources.get(ResourceType::Gold);
    int iron = m_playerResources.get(ResourceType::Iron);
    ImGui::Text("Your resources: %d Gold, %d Iron", gold, iron);
    ImGui::Spacing();

    bool canBuild = (gold >= goldCost && iron >= ironCost);

    if (!canBuild) ImGui::BeginDisabled();
    if (ImGui::Button("Build Boat", {120, 28})) {
        m_playerResources.add(ResourceType::Gold, -goldCost);
        m_playerResources.add(ResourceType::Iron, -ironCost);
        hero.onBoat    = true;
        hero.boatCount += 1;
        m_showShipyardPopup = false;
        pushPickupEffect(hero.pos, "Set Sail!", IM_COL32(80, 160, 255, 255));
        m_audio.playSound("pickup");
        // Recalculate reachable tiles with boat movement
        auto costFn = [this, &hero](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            if (!t || !hero.canEnter(t->terrain) || t->blocked) return 999;
            int base = hero.moveCost(t->terrain);
            if (m_roadHexes.count(c)) base = std::max(1, base / 2);
            return base;
        };
        m_reachable = Pathfinder::reachable(m_map, hero.pos, costFn, hero.movePool);
    }
    if (!canBuild) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", {70, 28}))
        m_showShipyardPopup = false;

    ImGui::Spacing();
    if (hero.onBoat) {
        ImGui::Separator();
        ImGui::TextColored({0.4f, 0.9f, 0.5f, 1.0f}, "You are already on a boat.");
        ImGui::TextWrapped("Disembark onto land first before building another.");
    }

    ImGui::End();
}

// ── Siege camp prompt ─────────────────────────────────────────────────────────
void Game::renderSiegeCampPrompt()
{
    if (!m_showSiegeCampPrompt) return;

    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == m_siegePromptTownId) { town = &t; break; }
    if (!town) { m_showSiegeCampPrompt = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##siegeprompt", nullptr, wf)) { ImGui::End(); return; }

    ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "Besiege %s?", town->name.c_str());
    ImGui::Separator(); ImGui::Spacing();

    // Garrison summary
    ImGui::TextColored({0.9f, 0.4f, 0.4f, 1.0f}, "Garrison:");
    for (const auto& s : town->garrison) {
        const UnitDef* ud = m_registry.getUnitDef(s.defId);
        if (ud) ImGui::Text("  %-22s x%d", ud->name.c_str(), s.count);
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextWrapped("Attack now or lay siege and wait for allies. "
                       "While sieged, defenders cannot leave the town, "
                       "but they may Fortify (+4 DEF, stronger walls, +3 tower dmg).");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    Hero& hero = m_heroes[m_activeHeroIdx];
    float bw = (ImGui::GetWindowWidth() - 40.0f) / 3.0f;

    // Attack now
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.55f, 0.15f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.20f, 0.20f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.40f, 0.10f, 0.10f, 1.0f});
    if (ImGui::Button("Attack Now!", {bw, 34})) {
        m_showSiegeCampPrompt = false;
        Hero garrisonHero;
        garrisonHero.id      = 0;
        garrisonHero.name    = town->name + " Garrison";
        garrisonHero.faction = town->faction;
        garrisonHero.army    = town->garrison;
        m_lastCombatEnemyId  = 0;
        m_pendingTownCaptureId = town->id;
        auto pUnits = makeHeroUnits(hero, m_registry.units(), true);
        auto gUnits = makeHeroUnits(garrisonHero, m_registry.units(), false);
        enterCombat(hero, pUnits, garrisonHero, gUnits);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Storm the town immediately. No waiting for allies.\nYou face the full garrison alone.");
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 6);

    // Lay siege
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.35f, 0.60f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.45f, 0.75f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.25f, 0.45f, 1.0f});
    if (ImGui::Button("Lay Siege", {bw, 34})) {
        m_showSiegeCampPrompt = false;
        hero.isSiegeCamping    = true;
        hero.siegeTargetTownId = town->id;
        town->underSiege       = true;
        // Spend 25% of remaining movement
        int cost = std::max(1, hero.movePool / 4);
        hero.movePool = std::max(0, hero.movePool - cost);
        pushPickupEffect(hero.pos, "Siege Camp!", IM_COL32(100, 160, 255, 255));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Park your army outside the town.\n"
                          "Defenders can't leave. Allied heroes can join before\n"
                          "the siege assault fires automatically at end of turn.\n"
                          "Costs 25%% of remaining movement.");
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 6);

    // Cancel
    if (ImGui::Button("Retreat", {bw, 34}))
        m_showSiegeCampPrompt = false;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Back away. Do nothing this action.");

    ImGui::End();
}

// ── Siege indicator overlay ───────────────────────────────────────────────────
void Game::renderSiegeIndicator()
{
    // Draw a pulsing ring around any besieged town visible on screen
    auto* dl = ImGui::GetBackgroundDrawList();
    for (const auto& t : m_towns) {
        if (!t.underSiege) continue;
        float wx, wy;
        m_hexRenderer.grid().hexToWorld(t.pos, wx, wy);
        float sx, sy;
        m_camera.worldToScreen(wx, wy, sx, sy);
        // Pulsing alpha
        float pulse = 0.55f + 0.35f * sinf(m_mapTime * 3.0f);
        ImU32 col = IM_COL32(255, 200, 60, static_cast<int>(pulse * 255));
        dl->AddCircle({sx, sy}, 28.0f, col, 24, 3.0f);
        dl->AddText({sx - 20.0f, sy - 32.0f}, IM_COL32(255,200,60,230), "SIEGE");
    }
    // Highlight camped heroes with a blue tent icon
    for (const auto& h : m_heroes) {
        if (!h.isSiegeCamping) continue;
        float wx, wy;
        m_hexRenderer.grid().hexToWorld(h.pos, wx, wy);
        float sx, sy;
        m_camera.worldToScreen(wx, wy, sx, sy);
        dl->AddCircleFilled({sx, sy - 24.0f}, 6.0f, IM_COL32(100, 180, 255, 200));
        dl->AddText({sx - 12.0f, sy - 38.0f}, IM_COL32(100,180,255,230), "Camp");
    }
}

// ── Siege combat trigger ──────────────────────────────────────────────────────
void Game::triggerSiegeCombat(uint32_t townId)
{
    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == townId) { town = &t; break; }
    if (!town || town->garrison.empty()) return;

    // Collect all camping heroes for this town (combine their armies)
    std::vector<Hero*> campers;
    for (auto& h : m_heroes)
        if (h.isSiegeCamping && h.siegeTargetTownId == townId) campers.push_back(&h);
    if (campers.empty()) return;

    // Lift siege camp flags
    for (auto* h : campers) {
        h->isSiegeCamping    = false;
        h->siegeTargetTownId = 0;
    }

    // Lead attacker = first camper; merge other campers' armies into them
    Hero& lead = *campers[0];
    for (size_t i = 1; i < campers.size(); ++i) {
        for (auto& s : campers[i]->army) {
            bool merged = false;
            for (auto& ls : lead.army)
                if (ls.defId == s.defId) { ls.count += s.count; merged = true; break; }
            if (!merged && lead.army.size() < 7) lead.army.push_back(s);
        }
        campers[i]->army.clear();
    }

    // Build garrison defender hero
    Hero garrisonHero;
    garrisonHero.id      = 0;
    garrisonHero.name    = town->name + " Garrison";
    garrisonHero.faction = town->faction;
    garrisonHero.army    = town->garrison;

    // Apply fortify bonuses to garrison hero stats
    if (town->siegeFortified || town->fortifyDefBonus > 0) {
        garrisonHero.defense += town->fortifyDefBonus;
    }

    m_lastCombatEnemyId    = 0;
    m_pendingTownCaptureId = town->id;

    auto pUnits = makeHeroUnits(lead, m_registry.units(), true);
    auto gUnits = makeHeroUnits(garrisonHero, m_registry.units(), false);

    // Boost garrison units' defense from fortify
    if (town->fortifyDefBonus > 0) {
        for (auto& u : gUnits) u.defense += town->fortifyDefBonus;
    }

    // Reset fortify state after combat is entered
    town->underSiege       = false;
    town->siegeFortified   = false;
    town->fortifyDefBonus  = 0;
    town->fortifyWallBonus = 0;
    town->fortifyTowerBonus= 0;

    enterCombat(lead, pUnits, garrisonHero, gUnits);
}

// ── March button — visible when hero is selected and no siege target nearby ──
void Game::renderMarchButton()
{
    // Only show when player controls active hero and game is in world map
    if (m_hotSeatP2Turn) return;
    if (m_heroes.empty() || m_activeHeroIdx < 0 ||
        m_activeHeroIdx >= static_cast<int>(m_heroes.size())) return;

    Hero& hero = m_heroes[m_activeHeroIdx];
    if (hero.isSiegeCamping) return;  // already camped

    // Check if adjacent to any enemy town (siege option is already shown instead)
    bool nearEnemyTown = false;
    for (const auto& t : m_towns) {
        if (t.ownerId != 0 && t.ownerId != 1 &&
            HexGrid::distance(hero.pos, t.pos) <= 1) {
            nearEnemyTown = true;
            break;
        }
    }
    if (nearEnemyTown) return;

    int curWeek = m_turns.week();
    bool onCooldown = (hero.marchCooldownWeek > curWeek);

    ImGuiIO& io = ImGui::GetIO();
    float panelW = 200.0f;
    float panelH = 90.0f;
    float px = io.DisplaySize.x - panelW - 8.0f;
    float py = io.DisplaySize.y - panelH - 60.0f;  // above End Turn button area

    ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("##march_btn", nullptr, wf)) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "MARCH");
        ImGui::Separator();

        if (onCooldown) {
            ImGui::TextDisabled("Cooldown: Week %d", hero.marchCooldownWeek);
            ImGui::BeginDisabled();
            ImGui::Button("March!", ImVec2(-1, 0));
            ImGui::EndDisabled();
        } else {
            int cost = std::max(1, hero.movePool / 4);  // 25% of current movePool
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                               "Cost: %d MP  Bonus: +%d MP", cost, hero.maxMove / 10);
            if (ImGui::Button("March!", ImVec2(-1, 0))) {
                hero.movePool -= cost;
                if (hero.movePool < 0) hero.movePool = 0;
                // +10% maxMove added at start of next week
                hero.marchCooldownWeek = curWeek + 1;
                hero.marchBonusActive  = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "March Order\n\n"
                    "Push your troops to move faster.\n"
                    "Costs 25%% of current movement.\n"
                    "Grants +10%% movement next week.\n"
                    "Cooldown: 1 week."
                );
            }
        }
    }
    ImGui::End();
}
