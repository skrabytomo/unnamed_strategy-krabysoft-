#include "Game.h"
#include "../magic/SpellRegistry.h"
#include "../hero/SkillRegistry.h"
#include "../hero/LevelUpSystem.h"
#include "../world/WorldGen.h"
#include "../world/HexGrid.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#  include <direct.h>
#else
#  include <unistd.h>
#endif
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

static constexpr const char* HIDEOUT_PATH = "saves/hideout.db";

static std::string slotPath(int slot)
{
    return "saves/save" + std::to_string(slot) + ".json";
}

static std::string campaignSlotPath(int slot)
{
    return "saves/campaign" + std::to_string(slot) + ".json";
}

// ── Init ──────────────────────────────────────────────────────────────────────
bool Game::init(const std::string& title, int width, int height)
{
    m_width  = width;
    m_height = height;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    // Resolve executable directory for asset loading
    {
        char* base = SDL_GetBasePath();
        if (base) {
            m_basePath = base;
            SDL_free(base);
        }
        // Also try to chdir there so saves/ scripts/ etc. resolve correctly
        if (!m_basePath.empty()) {
#ifdef _WIN32
            _chdir(m_basePath.c_str());
#else
            chdir(m_basePath.c_str());
#endif
        }
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    m_window = SDL_CreateWindow(title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!m_window) { fprintf(stderr, "Window: %s\n", SDL_GetError()); return false; }

    m_glCtx = SDL_GL_CreateContext(m_window);
    if (!m_glCtx) { fprintf(stderr, "GL context: %s\n", SDL_GetError()); return false; }
#ifdef _WIN32
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "GLEW init failed\n"); return false; }
#endif

    SDL_GL_SetSwapInterval(1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_camera.setViewport(width, height);
    m_camera.setPosition(0.0f, 0.0f);

    if (!m_batch.init())                          { fprintf(stderr, "SpriteBatch failed\n"); return false; }
    if (!m_hexRenderer.init(40.0f, m_basePath))   { fprintf(stderr, "HexRenderer failed\n"); return false; }
    if (!m_ui.init(width, height))                { fprintf(stderr, "UIRenderer failed\n"); return false; }
    m_iconTex.load(m_basePath + "assets/icons.png", true, false);
    m_spellIconTex.load(m_basePath + "assets/icons_spells.png", true, false);

    // Per-unit sprite sheets (optional — falls back to circles if missing)
    // File: assets/sprites/faction_F_tT.png  (F=faction 0-8, T=tier 1-6)
    for (int i = 0; i < NUM_FACTIONS; ++i)
        for (int t = 0; t < NUM_UNIT_TIERS; ++t) {
            char rel[80];
            std::snprintf(rel, sizeof(rel), "assets/sprites/faction_%d_t%d.png", i, t + 1);
            m_unitTex[i][t].load(m_basePath + rel, false, false);
        }

    // SDL cursors
    m_cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    m_cursorFight = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

    // Building registry
    m_registry.init();

    // Hero class registry
    m_classRegistry.init();

    // Artifact registry
    m_artifactRegistry.init();

    startNewGame();
    m_state = GameState::MainMenu;

    // Load hero faction portraits (first idle frame of each faction's t1 sprite)
    for (int i = 0; i < NUM_FACTIONS; ++i) {
        char rel[80];
        std::snprintf(rel, sizeof(rel), "assets/portraits/faction_%d.png", i);
        m_portraitTex[i].load(m_basePath + rel, false, false);
    }

    // Load faction town art (user-provided or placeholder)
    for (int i = 0; i < NUM_FACTIONS; ++i) {
        char rel[80];
        std::snprintf(rel, sizeof(rel), "assets/towns/faction_%d.png", i);
        m_townTex[i].load(m_basePath + rel, false, false);
    }

    // Load building category icon atlas
    m_buildingIconTex.load(m_basePath + "assets/buildings/icons_buildings.png", true, false);

    // Load per-building shared art (fort/market/warehouse/townhall/cityhall/mageguild)
    static const char* kSharedBuildingFiles[NUM_SHARED_BUILDING_ART] = {
        "assets/buildings/fort.png",
        "assets/buildings/market.png",
        "assets/buildings/warehouse.png",
        "assets/buildings/town_hall.png",
        "assets/buildings/city_hall.png",
        "assets/buildings/mage_guild.png",
    };
    for (int i = 0; i < NUM_SHARED_BUILDING_ART; ++i)
        m_sharedBuildingTex[i].load(m_basePath + kSharedBuildingFiles[i], false, false);

    // Load combat board terrain backgrounds (assets/terrain/combat/NAME.png)
    static const char* kTerrainBgName[NUM_TERRAIN_TYPES] = {
        "plains", "forest", "highland", "corrupted", "toxic",
        "sacred", "industrial", "rocky", "swamp", "plains",
        "volcanic", "barren", "wasteland", "corrupted_forest", "flesh_zone",
    };
    for (int i = 0; i < NUM_TERRAIN_TYPES; ++i) {
        char rel[96];
        std::snprintf(rel, sizeof(rel), "assets/terrain/combat/%s.png", kTerrainBgName[i]);
        m_combatBgTex[i].load(m_basePath + rel, false, false);
    }

    // Wire WorldMapHUD callbacks
    m_worldHUD.init(width, height);
    if (m_iconTex.ok())
        m_worldHUD.setIconTex((ImTextureID)(uintptr_t)m_iconTex.id());
    for (int i = 0; i < NUM_FACTIONS; ++i)
        if (m_portraitTex[i].ok())
            m_worldHUD.setPortraitTex(i, (ImTextureID)(uintptr_t)m_portraitTex[i].id());
    m_worldHUD.onEndTurn     = [this]() { doEndTurn(); };
    m_worldHUD.onWorldSpells = [this]() { m_showWorldSpellPanel = !m_showWorldSpellPanel; };
    m_worldHUD.onKingdom     = [this]() { m_showKingdomPanel    = !m_showKingdomPanel; };
    m_worldHUD.onOptions     = [this]() { m_showPauseMenu       = !m_showPauseMenu; };
    m_worldHUD.onHeroClicked = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(m_heroes.size())) {
            if (idx == m_activeHeroIdx) {
                // Second click on same hero: open/close the inspect panel
                m_showHeroInspect = !m_showHeroInspect;
                return;
            }
            m_activeHeroIdx = idx;
            const Hero& h = m_heroes[idx];
            float hx2, hy2;
            m_hexRenderer.grid().hexToWorld(h.pos, hx2, hy2);
            m_camera.setPosition(hx2, hy2);
            m_selected = {-999, -999};
            auto costFn = [this, &h](HexCoord c) -> int {
                const HexTile* t = m_map.getTile(c);
                if (!t || !h.canEnter(t->terrain)) return 999;
                return h.moveCost(t->terrain);
            };
            m_reachable = Pathfinder::reachable(m_map, h.pos, costFn, h.movePool);
        }
    };

    m_worldHUD.onTownClicked = [this](int idx) {
        // Find the idx-th player-owned town and jump to it
        int count = 0;
        for (auto& t : m_towns) {
            if (t.ownerId != 1) continue;
            if (count == idx) {
                float tx, ty;
                m_hexRenderer.grid().hexToWorld(t.pos, tx, ty);
                m_camera.setPosition(tx, ty);
                enterTown(&t);
                return;
            }
            ++count;
        }
    };

    // Wire TownScreen callbacks
    m_townScreen.init(width, height);
    m_townScreen.onClose = [this]() { exitTown(); };

    // Wire CombatHUD callbacks
    m_combatHUD.init(width, height);
    m_combatHUD.onWait      = [this]() { m_combat.wait(); };
    m_combatHUD.onDefend    = [this]() { m_combat.skipUnit(); };
    m_combatHUD.onEndCombat = [this]() { exitCombat(false); };
    m_combatHUD.onSpells    = [this]() { m_showSpellPanel = !m_showSpellPanel; };

    // Open hideout DB (non-fatal if it fails)
    m_hideout.open(HIDEOUT_PATH);

    // Scripting
    if (m_lua.init()) {
        m_triggers.setEngine(&m_lua);
        bindLuaAPI();
        m_lua.execFile("scripts/autoload.lua");
    }

    // ImGui (non-fatal if fails)
    if (initImGui())
        m_editor.init(width, height);

    // Audio
    if (m_audio.init()) {
        m_audio.loadWav("click",          "assets/sounds/click.wav");
        m_audio.loadWav("pickup",         "assets/sounds/pickup.wav");
        m_audio.loadWav("levelup",        "assets/sounds/levelup.wav");
        m_audio.loadWav("hit",            "assets/sounds/hit.wav");
        m_audio.loadWav("spell",          "assets/sounds/spell.wav");
        m_audio.loadWav("buy",            "assets/sounds/buy.wav");
        m_audio.loadWav("worldmap_music",  "assets/sounds/worldmap_music.wav");
        m_audio.loadWav("combat_music_1", "assets/sounds/combat_music.wav");
        m_audio.loadWav("combat_music_2", "assets/sounds/combat_music_2.wav");
        m_audio.loadWav("combat_music_3", "assets/sounds/combat_music_3.wav");
        m_audio.loadWav("combat_music_4", "assets/sounds/combat_music_4.wav");
        m_audio.loadWav("town_music",     "assets/sounds/town_music.wav");
        for (int fi = 0; fi < 9; ++fi) {
            char key[32], path[64];
            std::snprintf(key,  sizeof(key),  "faction_music_%d", fi);
            std::snprintf(path, sizeof(path), "assets/sounds/faction_music_%d.wav", fi);
            m_audio.loadWav(key, path);
        }
        m_audio.playMusic("worldmap_music");
    }

    loadSettings();   // apply persisted volume / fullscreen settings

    m_running = true;
    gLog("Game initialized: %dx%d\n", width, height);
    return true;
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void Game::run()
{
    Uint64 prev = SDL_GetPerformanceCounter();
    const Uint64 freq = SDL_GetPerformanceFrequency();
    while (m_running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - prev) / static_cast<float>(freq);
        if (dt > 0.1f) dt = 0.1f;
        prev = now;
        m_input.beginFrame();
        processEvents();
        update(dt);
        render();
    }
}

// ── Events ────────────────────────────────────────────────────────────────────
void Game::processEvents()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (m_imguiReady) ImGui_ImplSDL2_ProcessEvent(&e);
        m_input.handleEvent(e);

        if (e.type == SDL_QUIT) m_running = false;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            if (m_state == GameState::Town)
                exitTown();
            else if (m_state == GameState::Combat)
                exitCombat(false);
            else if (m_state == GameState::WorldMap || m_state == GameState::Campaign)
                m_showPauseMenu = !m_showPauseMenu;
            else
                m_running = false;
        }

        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_RESIZED) {
            m_width  = e.window.data1;
            m_height = e.window.data2;
            m_camera.setViewport(m_width, m_height);
            m_ui.resize(m_width, m_height);
            m_worldHUD.resize(m_width, m_height);
            m_combatHUD.resize(m_width, m_height);
            glViewport(0, 0, m_width, m_height);
        }
    }
}

// ── Update dispatch ───────────────────────────────────────────────────────────
void Game::update(float dt)
{
    m_audio.update();
    if (m_input.keyDown(SDLK_F5)) {
        if (m_state == GameState::Campaign)
            saveGame(campaignSlotPath(m_campaignActiveSlot));
        else
            saveGame(slotPath(m_activeSlot));
    }
    if (m_input.keyDown(SDLK_F9)) {
        if (m_state == GameState::Campaign)
            loadGame(campaignSlotPath(m_campaignActiveSlot));
        else
            loadGame(slotPath(m_activeSlot));
    }
    if (m_input.keyDown(SDLK_F2)) {
        if (m_state == GameState::Editor) exitEditor();
        else enterEditor();
    }


    switch (m_state) {
        case GameState::MainMenu: updateMainMenu(dt);  break;
        case GameState::WorldMap: updateWorldMap(dt);  break;
        case GameState::Combat:   updateCombat(dt);    break;
        case GameState::Town:     updateTown(dt);      break;
        case GameState::Editor:   updateEditor(dt);    break;
        case GameState::Campaign: updateCampaign(dt);  break;
        default: break;
    }
}

// ── Render dispatch ───────────────────────────────────────────────────────────
void Game::render()
{
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.04f, 0.03f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    switch (m_state) {
        case GameState::MainMenu: renderMainMenu();  break;
        case GameState::WorldMap: renderWorldMap();  break;
        case GameState::Combat:   renderCombat();    break;
        case GameState::Town:     renderTown();      break;
        case GameState::Editor:   renderEditor();    break;
        case GameState::Campaign: renderCampaign();  break;
        default: break;
    }

    SDL_GL_SwapWindow(m_window);
}

// ── Save / Load ───────────────────────────────────────────────────────────────
void Game::saveGame(const std::string& path)
{
    SDL_RWops* f = SDL_RWFromFile("saves/.keep", "w");
    if (f) SDL_RWclose(f);

    GameSaveData data = SaveLoad::packState(
        m_map, m_heroes, m_enemyHeroes,
        m_towns, m_worldObjects, m_resources, m_nextObjId,
        m_playerResources,
        m_turns.day(), m_turns.week(),
        m_mapSize,
        m_newGameDifficulty, m_activeHeroIdx);

    data.campaign = m_campaign.toSaveState();

    if (SaveLoad::saveGame(path, data))
        gLog("Game saved to %s\n", path.c_str());
    else
        fprintf(stderr, "Save failed: %s\n", path.c_str());
}

bool Game::loadGame(const std::string& path)
{
    GameSaveData data;
    if (!SaveLoad::loadGame(path, data)) {
        fprintf(stderr, "Load failed: %s\n", path.c_str());
        return false;
    }

    m_mapSize = static_cast<MapSize>(data.mapSizeEnum);
    m_map.create(m_mapSize);

    int day = 1, week = 1;
    SaveLoad::unpackState(data, m_map, m_heroes, m_enemyHeroes,
                          m_towns, m_worldObjects, m_resources, m_nextObjId,
                          m_playerResources, day, week);
    for (const auto& wo : m_worldObjects)
        if (wo.type == WorldObjectType::Barrier && !wo.collected)
            if (HexTile* t = m_map.getTile(wo.pos)) t->blocked = true;

    m_newGameDifficulty = data.difficulty;
    m_activeHeroIdx = (!m_heroes.empty())
        ? std::min(data.activeHeroIdx, (int)m_heroes.size() - 1)
        : 0;
    if (!m_heroes.empty()) {
        float hx, hy;
        m_hexRenderer.grid().hexToWorld(m_heroes[m_activeHeroIdx].pos, hx, hy);
        m_camera.setPosition(hx, hy);
    }

    for (auto& t : m_towns)
        if (HexTile* tile = m_map.getTile(t.pos)) tile->townId = t.id;

    m_moveT    = 1.0f;
    m_state    = GameState::WorldMap;
    m_selected = {-999,-999};
    m_reachable.clear();

    if (data.campaign.active) {
        m_campaign.init();
        m_campaign.fromSaveState(data.campaign);
        m_state = GameState::Campaign;
    }

    gLog("Game loaded from %s (day %d week %d)\n", path.c_str(), day, week);
    return true;
}

// ── New game (reset + world gen) ──────────────────────────────────────────────
void Game::startNewGame()
{
    // Clear all runtime state
    m_heroes.clear();
    m_enemyHeroes.clear();
    m_towns.clear();
    m_resources.clear();
    m_worldObjects.clear();
    m_heroMapAnimators.clear();
    m_pickupEffects.clear();
    m_reachable.clear();
    m_activeHeroIdx   = 0;
    m_moveT           = 1.0f;
    m_selected        = {-999, -999};
    m_hovered         = {-999, -999};
    m_nextObjId       = 1;
    m_turns           = TurnManager{};
    m_playerResources = Resources{};
    m_showVictory     = false;
    m_showDefeat      = false;
    m_finalDefeat     = false;
    m_showCapturePopup = false;
    m_showTownLostPopup = false;
    m_showCombatResult = false;

    // Generate world procedurally using selected settings
    static constexpr MapSize kMapSizes[] = {
        MapSize::Small, MapSize::Medium, MapSize::Large, MapSize::XLarge
    };
    m_mapSize = kMapSizes[std::clamp(m_newGameMapSize, 0, 3)];
    m_map.create(m_mapSize);

    WorldGenParams wgp;
    wgp.seed        = static_cast<uint32_t>(SDL_GetTicks()) ^ 0x5A5A5A5Au;
    wgp.size        = m_mapSize;
    wgp.playerCount = 4;
    wgp.waterRatio  = 0.18f;
    auto wgResult   = WorldGen::generate(m_map, wgp);

    m_resources = std::move(wgResult.resources);
    m_nextObjId = static_cast<uint32_t>(m_resources.size()) + 1;

    m_playerResources.set(ResourceType::Gold, 5000);
    m_playerResources.set(ResourceType::Iron, 20);

    static constexpr FactionId kFactions[] = {
        FactionId::HolyOrder, FactionId::CrimsonWardens, FactionId::Thornkin,
        FactionId::EternalEmpire, FactionId::Bloodsworn, FactionId::Voidkin,
        FactionId::IronAssembly, FactionId::Amalgamate, FactionId::Convergence
    };
    static constexpr int kFactionStartSpell[] = {
        SPL::BLESS, SPL::BLOOD_FRENZY, SPL::ENTANGLE,
        SPL::CURSE, SPL::BLOOD_FRENZY, SPL::ENTANGLE,
        SPL::REINFORCE, SPL::MEND_FLESH, SPL::BLESS
    };
    static const char* kFactionHeroNames[] = {
        "Alara", "Dren", "Korvas", "Mira", "Seld",
        "Thayne", "Vex", "Lyra", "Cael"
    };
    int fi = std::clamp(m_newGameFaction, 0, 8);

    Hero hero;
    hero.id       = 1;
    hero.name     = kFactionHeroNames[fi];
    hero.faction  = kFactions[fi];
    hero.pos      = wgResult.startPositions.empty() ? HexCoord{0,0}
                                                    : wgResult.startPositions[0];
    hero.movePool = hero.maxMove;
    hero.lightPower = (fi == 0 || fi == 8) ? 3 : 0;
    hero.knownSpells = { kFactionStartSpell[fi] };

    // Assign chosen hero class (or first available for faction)
    {
        const HeroClassDef* chosenCls = nullptr;
        if (m_newGameClassId != 0)
            chosenCls = m_classRegistry.getClass(m_newGameClassId);
        if (!chosenCls) {
            auto cls4fac = m_classRegistry.getClassesForFaction(hero.faction);
            if (!cls4fac.empty()) chosenCls = cls4fac[0];
        }
        if (chosenCls) {
            hero.classId = chosenCls->id;
            hero.efficientSpecialty  = (chosenCls->specialty == SpecialtyType::Efficient);
            hero.bloodScentSpecialty = (chosenCls->specialty == SpecialtyType::BloodScent);
            hero.infestationSpecialty = (chosenCls->specialty == SpecialtyType::Infestation);
            hero.ghostWalkSpecialty   = (chosenCls->specialty == SpecialtyType::GhostWalk);
            hero.blightAuraSpecialty  = (chosenCls->specialty == SpecialtyType::BlightAura);
            // Grant first skill from class pool at Basic tier
            if (!chosenCls->skillPool.empty()) {
                int startSkillId = chosenCls->skillPool[0];
                hero.skills.learn(startSkillId);
                // Apply immediate world-map bonuses for passive skills
                if (const SkillDef* sd = findSkillDef(startSkillId)) {
                    int v = sd->values[0]; // Basic tier value
                    if (sd->effectType == SkillEffectType::MovementBonus) {
                        hero.maxMove += v;
                        hero.movePool = hero.maxMove;
                    } else if (sd->effectType == SkillEffectType::VisionBonus) {
                        hero.visionRange += v;
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
        }
    }

    // Apply Hideout permanent upgrades to the starting hero and resources
    if (m_hideout.isOpen()) {
        // Castle: bonus starting gold (+200 / +400 / +700 per tier)
        static constexpr int CASTLE_GOLD[] = { 0, 200, 600, 1300 }; // cumulative per tier
        int castleTier = m_hideout.getUpgradeLevel(HideoutBranch::CASTLE);
        if (castleTier > 0)
            m_playerResources.add(ResourceType::Gold, CASTLE_GOLD[std::min(castleTier, 3)]);

        // Barracks: bonus hero ATK (T1: +1 ATK, T2: +1 ATK +1 DEF)
        int barracksTier = m_hideout.getUpgradeLevel(HideoutBranch::BARRACKS);
        if (barracksTier >= 1) hero.attack++;
        if (barracksTier >= 2) hero.defense++;

        // Vault: bonus rare resources
        int vaultTier = m_hideout.getUpgradeLevel(HideoutBranch::VAULT);
        if (vaultTier >= 1) {
            m_playerResources.add(ResourceType::Iron, 1);
            m_playerResources.add(ResourceType::Mercury, 1);
        }
        if (vaultTier >= 2) {
            m_playerResources.add(ResourceType::VerdantSap, 1);
            m_playerResources.add(ResourceType::BloodEssence, 1);
            m_playerResources.add(ResourceType::FaithStones, 1);
        }

        // Shrine: second starting spell (faction-appropriate)
        if (m_hideout.getUpgradeLevel(HideoutBranch::SHRINE) >= 1) {
            static constexpr int kShrineSpell[] = {
                SPL::DIVINE_SHIELD, SPL::DRAIN_LIFE, SPL::SERPENT_VENOM,
                SPL::WITHER,        SPL::ENERVATE,   SPL::CURSE,
                SPL::SHRAPNEL,      SPL::FESTER,     SPL::REINFORCE
            };
            int shrineSpell = kShrineSpell[fi];
            bool alreadyKnown = false;
            for (int s : hero.knownSpells) if (s == shrineSpell) { alreadyKnown = true; break; }
            if (!alreadyKnown) hero.knownSpells.push_back(shrineSpell);
        }

        // Sanctum: +10 max mana
        if (m_hideout.getUpgradeLevel(HideoutBranch::SANCTUM) >= 1) {
            hero.maxMana += 10;
            hero.mana = hero.maxMana;
        }
    }

    // Difficulty bonuses for the player hero
    int diff = std::clamp(m_newGameDifficulty, 0, 2);
    if (diff == 0) { hero.attack += 2; hero.defense += 2; } // Easy: +2 ATK/DEF

    m_heroes.push_back(hero);
    if (HexTile* ht = m_map.getTile(hero.pos)) ht->heroId = hero.id;

    // Army sizes: Easy=larger player army, Normal/Hard=base
    static const int kT1Count[] = {24, 20, 20};
    static const int kT2Count[] = {10,  8,  8};

    auto giveStartingArmy = [&](Hero& h, int t1, int t2) {
        for (int tier : {1, 2}) {
            int cnt = (tier == 1) ? t1 : t2;
            for (const auto& ud : m_registry.units()) {
                if (ud.faction == h.faction && ud.tier == tier
                    && ud.path == UpgradePath::None) {
                    h.army.push_back({ud.id, cnt});
                    break;
                }
            }
        }
    };
    giveStartingArmy(m_heroes[0], kT1Count[diff], kT2Count[diff]);

    // Faction-appropriate enemy hero names (9 factions × 3 names each)
    static const char* kEnemyHeroNames[9][3] = {
        {"Seraphiel", "Ardent Inquisitor", "Blessed Blade"},       // HolyOrder
        {"Vael Bonechant", "Mortis Raider", "Crypt Sovereign"},    // CrimsonWardens
        {"Root-Elder", "Thornweave", "Briar Sovereign"},           // Thornkin
        {"Shade Marshal", "Revenant Warden", "Iron Phantom"},      // EternalEmpire
        {"Kael Bloodfang", "Ravager Lord", "Warlord Gruk"},        // Bloodsworn
        {"Vex Nullform", "Phase Stalker", "Rift Caller"},          // Voidkin
        {"Cogmaster Rex", "Iron Overseer", "Steam Baron"},         // IronAssembly
        {"Flesh-Weave", "Graft Sovereign", "Marrow Sculptor"},     // Amalgamate
        {"Synth-One", "Accord Delegate", "Unity Seeker"},          // Convergence
    };
    uint32_t nameRng = wgp.seed ^ 0xABCD1234u;
    for (int i = 1; i < static_cast<int>(wgResult.startPositions.size()) && i <= 3; ++i) {
        FactionId ef = (i < static_cast<int>(wgResult.towns.size()))
                       ? wgResult.towns[i].faction : FactionId::EternalEmpire;
        int efi = std::clamp(static_cast<int>(ef), 0, 8);
        nameRng = nameRng * 1664525u + 1013904223u;
        const char* eName = kEnemyHeroNames[efi][nameRng % 3];
        Hero eHero;
        eHero.id       = 99u + static_cast<uint32_t>(i);
        eHero.name     = eName;
        eHero.faction  = ef;
        eHero.pos      = wgResult.startPositions[i];
        eHero.movePool = eHero.maxMove;
        // Stats scale with difficulty: Easy +0, Normal +1, Hard +2/+2
        static const int kDiffAtkBonus[] = {0, 1, 2};
        static const int kDiffDefBonus[] = {0, 1, 2};
        eHero.attack  += kDiffAtkBonus[diff];
        eHero.defense += kDiffDefBonus[diff];
        // Faction-specific spells and school power for the enemy hero
        // Two spells: one offensive/debuff + one DoT or heavy hitter
        static const int kEnemySpells[9][2] = {
            {SPL::SMITE,         SPL::RADIANCE},         // HolyOrder
            {SPL::WITHER,        SPL::VENOMOUS_CLOUD},   // CrimsonWardens
            {SPL::ENTANGLE,      SPL::SERPENT_VENOM},    // Thornkin
            {SPL::CURSE,         SPL::WITHER},            // EternalEmpire
            {SPL::DRAIN_LIFE,    SPL::ENERVATE},          // Bloodsworn
            {SPL::CURSE,         SPL::PLAGUE},            // Voidkin
            {SPL::SHRAPNEL,      SPL::NAPALM},            // IronAssembly
            {SPL::FESTER,        SPL::ACID_SPRAY},        // Amalgamate
            {SPL::SMITE,         SPL::SHRAPNEL},          // Convergence
        };
        eHero.knownSpells = { kEnemySpells[efi][0], kEnemySpells[efi][1] };
        eHero.mana    = 20;
        eHero.maxMana = 20;
        // Assign a random class from the enemy's faction pool and set persistent specialty flags
        {
            auto eCls = m_classRegistry.getClassesForFaction(ef);
            if (!eCls.empty()) {
                int pick = static_cast<int>(i % eCls.size());
                const HeroClassDef* ecls = eCls[pick];
                eHero.classId = ecls->id;
                eHero.ghostWalkSpecialty  = (ecls->specialty == SpecialtyType::GhostWalk);
                eHero.blightAuraSpecialty = (ecls->specialty == SpecialtyType::BlightAura);
                eHero.infestationSpecialty = (ecls->specialty == SpecialtyType::Infestation);
            }
        }
        // School power scales enemy hero spells (roughly half player's starting tier)
        switch (ef) {
            case FactionId::HolyOrder:      eHero.lightPower  = 2; break;
            case FactionId::CrimsonWardens: eHero.deathPower  = 2; break;
            case FactionId::Thornkin:       eHero.naturePower = 2; break;
            case FactionId::EternalEmpire:  eHero.deathPower  = 2; break;
            case FactionId::Bloodsworn:     eHero.bloodPower  = 2; break;
            case FactionId::Voidkin:        eHero.deathPower  = 2; break;
            case FactionId::IronAssembly:   eHero.forgePower  = 2; break;
            case FactionId::Amalgamate:     eHero.fleshPower  = 2; break;
            case FactionId::Convergence:    eHero.lightPower  = 1; eHero.forgePower = 1; break;
            default: break;
        }
        // Enemy army: Easy=smaller, Normal=base, Hard=larger
        static const int kEnemyT1[] = {14, 20, 25};
        static const int kEnemyT2[] = { 5,  8, 10};
        giveStartingArmy(eHero, kEnemyT1[diff], kEnemyT2[diff]);
        m_enemyHeroes.push_back(eHero);
        if (HexTile* ht = m_map.getTile(eHero.pos)) ht->heroId = eHero.id;
    }

    for (int i = 0; i < static_cast<int>(wgResult.towns.size()); ++i) {
        Town& wt = wgResult.towns[i];
        if (i == 0) {
            wt.ownerId = 1;
            // Force the player's starting town to match their chosen faction
            wt.faction = static_cast<FactionId>(std::clamp(m_newGameFaction, 0, 8));
            // Pre-build Mage Guild and faction town hall so income starts immediately
            int hallId = (static_cast<int>(wt.faction) + 1) * 100;
            wt.builtBuildings.push_back(BID::MAGE_GUILD);
            wt.builtBuildings.push_back(hallId);
            // Rebuild weeklyIncome from pre-built buildings
            for (int bid : wt.builtBuildings) {
                const BuildingDef* def = m_registry.getBuildingDef(bid);
                if (def) wt.weeklyIncome.addAll(def->weeklyIncome);
            }
        } else {
            // Assign enemy town to the corresponding AI hero (heroes are 99+i)
            bool assignedToAI = (i >= 1 && i <= static_cast<int>(m_enemyHeroes.size()));
            if (assignedToAI) {
                wt.ownerId = 99u + static_cast<uint32_t>(i);
                // Pre-build same starting buildings as the player
                int hallId = (static_cast<int>(wt.faction) + 1) * 100;
                wt.builtBuildings.push_back(BID::MAGE_GUILD);
                wt.builtBuildings.push_back(hallId);
                for (int bid : wt.builtBuildings) {
                    const BuildingDef* def = m_registry.getBuildingDef(bid);
                    if (def) wt.weeklyIncome.addAll(def->weeklyIncome);
                }
            } else {
                wt.ownerId = 0;
                for (const auto& ud : m_registry.units()) {
                    if (ud.faction == wt.faction && ud.tier == 1
                        && ud.path == UpgradePath::None) {
                        wt.garrison.push_back({ud.id, 15});
                        break;
                    }
                }
            }
        }
        if (HexTile* ht = m_map.getTile(wt.pos)) ht->townId = wt.id;
        m_towns.push_back(wt);

        // Paint faction-appropriate terrain around the town (radius 3)
        auto factionTerrain = [](FactionId f) -> Terrain {
            switch (f) {
            case FactionId::HolyOrder:     return Terrain::Sacred;
            case FactionId::CrimsonWardens:return Terrain::Highland;
            case FactionId::Thornkin:      return Terrain::Forest;
            case FactionId::EternalEmpire: return Terrain::Toxic;
            case FactionId::Bloodsworn:    return Terrain::Corrupted;
            case FactionId::Voidkin:       return Terrain::CorruptedForest;
            case FactionId::IronAssembly:  return Terrain::Industrial;
            case FactionId::Amalgamate:    return Terrain::Wasteland;
            case FactionId::Convergence:   return Terrain::Plains;
            default:                       return Terrain::Plains;
            }
        };
        Terrain ft = factionTerrain(wt.faction);
        for (auto& nc : HexGrid::range(wt.pos, 3)) {
            HexTile* nt = m_map.getTile(nc);
            if (nt && nt->terrain != Terrain::Water)
                nt->terrain = ft;
        }
    }

    m_worldObjects.clear();
    {
        uint32_t rng = wgp.seed ^ 0xF00DBABE;
        auto lcg = [&]() { return (rng = rng * 1664525u + 1013904223u); };

        auto allCoords = m_map.coords();
        for (size_t ci = allCoords.size() - 1; ci > 0; --ci)
            std::swap(allCoords[ci], allCoords[lcg() % (ci + 1)]);

        HexCoord startPos = m_heroes.empty() ? HexCoord{0,0} : m_heroes[0].pos;
        // Pick a tile — prefer tiles within minDist..maxDist of start (for first N objects)
        int nearPickCount = 0;
        auto pickTile = [&](int minDist = 0, int maxDist = 999) -> HexCoord {
            for (auto& c : allCoords) {
                const HexTile* t = m_map.getTile(c);
                if (!t || t->terrain == Terrain::Water) continue;
                if (t->heroId || t->townId || t->resourceId) continue;
                bool used = false;
                for (auto& o : m_worldObjects) if (o.pos == c) { used = true; break; }
                if (used) continue;
                int d = HexGrid::distance(c, startPos);
                if (d < minDist || d > maxDist) continue;
                return c;
            }
            // Fallback: any valid tile
            for (auto& c : allCoords) {
                const HexTile* t = m_map.getTile(c);
                if (!t || t->terrain == Terrain::Water) continue;
                if (t->heroId || t->townId || t->resourceId) continue;
                bool used = false;
                for (auto& o : m_worldObjects) if (o.pos == c) { used = true; break; }
                if (!used) return c;
            }
            return {0, 0};
        };

        // Scale object count to map size
        int mapR  = static_cast<int>(m_map.radius());
        int scale = std::max(1, mapR / 16); // Small=1, Medium=1, Large=2, XL=3

        static const int kScrollSpells[] = {
            SPL::SMITE, SPL::REGROWTH, SPL::CURSE, SPL::BLESS, SPL::CALL_LIGHTNING,
            SPL::REINFORCE, SPL::OVERCLOCK, SPL::WITHER, SPL::BARKSKIN
        };
        // First scroll + cache guaranteed near start (visible from turn 1)
        {
            HexCoord p = pickTile(6, 10);
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::SpellScroll, p,
                kScrollSpells[lcg() % 9], ResourceType::Gold, false});
        }
        {
            ResourceType rtype = ResourceType::Gold;
            HexCoord p = pickTile(6, 10);
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::ResourceCache, p,
                500 + static_cast<int>(lcg() % 1500), rtype, false});
        }

        // Guarantee 2 resource mines visible from the start (within 5-9 hexes)
        {
            static const ResourceType kNearRes[] = { ResourceType::Gold, ResourceType::Iron };
            for (int ri = 0; ri < 2; ++ri) {
                for (auto& c : allCoords) {
                    HexTile* t = m_map.getTile(c);
                    if (!t || t->terrain == Terrain::Water) continue;
                    if (t->heroId || t->townId || t->resourceId) continue;
                    bool usedByObj = false;
                    for (auto& o : m_worldObjects) if (o.pos == c) { usedByObj = true; break; }
                    if (usedByObj) continue;
                    int d = HexGrid::distance(c, startPos);
                    if (d < 5 || d > 9) continue;
                    bool tooClose = false;
                    for (auto& r : m_resources)
                        if (HexGrid::distance(c, r.pos) < 4) { tooClose = true; break; }
                    if (tooClose) continue;
                    ResourceNode node;
                    node.id     = m_nextObjId++;
                    node.pos    = c;
                    node.type   = kNearRes[ri];
                    node.amount = (node.type == ResourceType::Gold)
                                 ? 250
                                 : 3 + static_cast<int>(lcg() % 3);
                    t->resourceId = node.id;
                    m_resources.push_back(node);
                    break;
                }
            }
        }

        // Guarantee 1 faction-specific resource mine within 10 hexes of start
        {
            FactionId playerFaction = FactionId::None;
            for (const auto& t : m_towns)
                if (t.ownerId == 1) { playerFaction = t.faction; break; }

            auto factionPrimaryRes = [](FactionId f) -> ResourceType {
                switch (f) {
                case FactionId::HolyOrder:      return ResourceType::FaithStones;
                case FactionId::CrimsonWardens: return ResourceType::FaithStones;
                case FactionId::Thornkin:       return ResourceType::VerdantSap;
                case FactionId::EternalEmpire:  return ResourceType::Mercury;
                case FactionId::Bloodsworn:     return ResourceType::BloodEssence;
                case FactionId::Voidkin:        return ResourceType::VerdantSap;
                case FactionId::IronAssembly:   return ResourceType::Iron;
                case FactionId::Amalgamate:     return ResourceType::BloodEssence;
                case FactionId::Convergence:    return ResourceType::Mercury;
                default:                        return ResourceType::Gold;
                }
            };

            ResourceType fres = factionPrimaryRes(playerFaction);
            for (auto& c : allCoords) {
                HexTile* t = m_map.getTile(c);
                if (!t || t->terrain == Terrain::Water) continue;
                if (t->heroId || t->townId || t->resourceId) continue;
                bool usedByObj = false;
                for (auto& o : m_worldObjects) if (o.pos == c) { usedByObj = true; break; }
                if (usedByObj) continue;
                int d = HexGrid::distance(c, startPos);
                if (d < 4 || d > 10) continue;
                bool tooClose = false;
                for (auto& r : m_resources)
                    if (HexGrid::distance(c, r.pos) < 3) { tooClose = true; break; }
                if (tooClose) continue;
                ResourceNode node;
                node.id     = m_nextObjId++;
                node.pos    = c;
                node.type   = fres;
                node.amount = (fres == ResourceType::Gold) ? 250 : 3 + static_cast<int>(lcg() % 3);
                t->resourceId = node.id;
                m_resources.push_back(node);
                gLog("Placed faction mine (%s) at (%d,%d) dist=%d\n",
                       resourceName(fres), c.q, c.r, d);
                break;
            }
        }
        for (int s = 0; s < 4 * scale; ++s) {
            HexCoord p = pickTile();
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::SpellScroll, p,
                kScrollSpells[lcg() % 9], ResourceType::Gold, false});
        }
        for (int a = 0; a < 3 * scale; ++a) {
            HexCoord p = pickTile();
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::ArtifactChest, p,
                1 + static_cast<int>(lcg() % 8), ResourceType::Gold, false});
        }
        for (int x = 0; x < 3 * scale; ++x) {
            HexCoord p = pickTile();
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::XPShrine, p,
                200 + static_cast<int>(lcg() % 400), ResourceType::Gold, false});
        }
        for (int rc = 0; rc < 4 * scale; ++rc) {
            HexCoord p = pickTile();
            static const ResourceType kRTypes[] = {
                ResourceType::Gold, ResourceType::Iron,
                ResourceType::FaithStones, ResourceType::BloodEssence,
                ResourceType::VerdantSap, ResourceType::Mercury
            };
            ResourceType rtype = kRTypes[lcg() % 6];
            int rval = (rtype == ResourceType::Gold) ? 500 + static_cast<int>(lcg() % 2000)
                                                     : 5   + static_cast<int>(lcg() % 10);
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::ResourceCache, p, rval, rtype, false});
        }

        // Landmarks — named historical sites; permanent XP on first visit
        for (int lm = 0; lm < 3 * scale; ++lm) {
            HexCoord p = pickTile();
            WorldObject wo;
            wo.id    = m_nextObjId++;
            wo.type  = WorldObjectType::Landmark;
            wo.pos   = p;
            wo.value = 300 + static_cast<int>(lcg() % 500); // XP amount
            m_worldObjects.push_back(wo);
        }

        // Cursed Ground — damages army each crossing; questState = charges (3-5)
        for (int cg = 0; cg < 2 * scale; ++cg) {
            HexCoord p = pickTile();
            WorldObject wo;
            wo.id         = m_nextObjId++;
            wo.type       = WorldObjectType::CursedGround;
            wo.pos        = p;
            wo.value      = 10 + static_cast<int>(lcg() % 20); // dmg per trigger
            wo.questState = 3 + static_cast<int>(lcg() % 3);   // charges
            m_worldObjects.push_back(wo);
        }

        // Neutral Outposts — guarded; capture gives weekly T1 production
        for (int no = 0; no < 2 * scale; ++no) {
            HexCoord p = pickTile();
            WorldObject wo;
            wo.id      = m_nextObjId++;
            wo.type    = WorldObjectType::NeutralOutpost;
            wo.pos     = p;
            wo.faction = static_cast<uint8_t>(lcg() % 9);
            wo.value   = 1; // T1 dwellings
            m_worldObjects.push_back(wo);
        }
    }

    for (auto& wo : wgResult.worldObjects) m_worldObjects.push_back(wo);
    for (const auto& wo : m_worldObjects)
        if (wo.id >= m_nextObjId) m_nextObjId = wo.id + 1;

    // Build road network connecting all towns via shortest land paths
    m_roadHexes.clear();
    if (m_towns.size() >= 2) {
        auto roadCost = [this](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            return (t && t->terrain != Terrain::Water) ? 1 : 999;
        };
        // Connect each town to every other town
        for (size_t ti = 0; ti < m_towns.size(); ++ti) {
            for (size_t tj = ti + 1; tj < m_towns.size(); ++tj) {
                auto path = Pathfinder::find(m_map, m_towns[ti].pos, m_towns[tj].pos, roadCost);
                for (auto& h : path) m_roadHexes.insert(h);
                // Also insert the town tile itself
                m_roadHexes.insert(m_towns[ti].pos);
                m_roadHexes.insert(m_towns[tj].pos);
            }
        }
    }

    FogOfWar::hideAll(m_map);
    FogOfWar::updateVision(m_map, m_heroes[0]);

    float hx, hy;
    m_hexRenderer.grid().hexToWorld(m_heroes[0].pos, hx, hy);
    m_camera.setPosition(hx, hy);
}

// ── Settings persistence ──────────────────────────────────────────────────────
void Game::saveSettings()
{
    nlohmann::json j;
    j["sfxVol"]        = m_settingsSfxVol;
    j["musVol"]        = m_settingsMasVol;
    j["fullscreen"]    = m_settingsFullscreen;
    j["autoSave"]      = m_settingsAutoSave;
    j["animSpeed"]     = m_settingsAnimSpeed;
    j["showDmgNums"]   = m_settingsShowDmgNums;
    std::ofstream f("settings.json");
    if (f) f << j.dump(2);
}

void Game::loadSettings()
{
    std::ifstream f("settings.json");
    if (!f) return;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        m_settingsSfxVol        = j.value("sfxVol",      0.7f);
        m_settingsMasVol        = j.value("musVol",       0.35f);
        m_settingsFullscreen    = j.value("fullscreen",   false);
        m_settingsAutoSave      = j.value("autoSave",     true);
        m_settingsAnimSpeed     = j.value("animSpeed",    1.0f);
        m_settingsShowDmgNums   = j.value("showDmgNums",  true);
        m_audio.setSfxVolume(m_settingsSfxVol);
        m_audio.setMusicVolume(m_settingsMasVol);
        if (m_settingsFullscreen)
            SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } catch (...) {}
}

// ── ImGui integration ─────────────────────────────────────────────────────────
bool Game::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // ── Medieval dark-stone / gold / crimson theme ────────────────────────────────────────
    ImGuiStyle& sty = ImGui::GetStyle();
    sty.WindowRounding    = 2.0f;
    sty.ChildRounding     = 2.0f;
    sty.PopupRounding     = 2.0f;
    sty.FrameRounding     = 2.0f;
    sty.ScrollbarRounding = 2.0f;
    sty.GrabRounding      = 2.0f;
    sty.TabRounding       = 2.0f;
    sty.WindowBorderSize  = 1.0f;
    sty.FrameBorderSize   = 0.0f;
    sty.FramePadding      = {6.0f, 4.0f};
    sty.ItemSpacing       = {8.0f, 6.0f};
    sty.WindowPadding     = {10.0f, 10.0f};
    sty.ScrollbarSize     = 12.0f;

    ImVec4* C = sty.Colors;
    C[ImGuiCol_Text]                  = {0.92f, 0.86f, 0.70f, 1.00f}; // parchment
    C[ImGuiCol_TextDisabled]          = {0.52f, 0.46f, 0.34f, 1.00f};
    C[ImGuiCol_WindowBg]              = {0.10f, 0.08f, 0.06f, 0.96f}; // dark stone
    C[ImGuiCol_ChildBg]               = {0.08f, 0.06f, 0.05f, 0.90f};
    C[ImGuiCol_PopupBg]               = {0.10f, 0.08f, 0.06f, 0.97f};
    C[ImGuiCol_Border]                = {0.42f, 0.32f, 0.10f, 0.72f}; // dark gold
    C[ImGuiCol_BorderShadow]          = {0.00f, 0.00f, 0.00f, 0.50f};
    C[ImGuiCol_FrameBg]               = {0.18f, 0.14f, 0.10f, 0.90f}; // dark iron
    C[ImGuiCol_FrameBgHovered]        = {0.28f, 0.22f, 0.12f, 0.90f};
    C[ImGuiCol_FrameBgActive]         = {0.38f, 0.28f, 0.12f, 0.90f};
    C[ImGuiCol_TitleBg]               = {0.20f, 0.08f, 0.06f, 1.00f}; // deep burgundy
    C[ImGuiCol_TitleBgActive]         = {0.38f, 0.14f, 0.08f, 1.00f};
    C[ImGuiCol_TitleBgCollapsed]      = {0.12f, 0.05f, 0.04f, 1.00f};
    C[ImGuiCol_MenuBarBg]             = {0.14f, 0.11f, 0.08f, 1.00f};
    C[ImGuiCol_ScrollbarBg]           = {0.06f, 0.05f, 0.04f, 0.90f};
    C[ImGuiCol_ScrollbarGrab]         = {0.38f, 0.28f, 0.10f, 0.80f};
    C[ImGuiCol_ScrollbarGrabHovered]  = {0.52f, 0.40f, 0.14f, 0.90f};
    C[ImGuiCol_ScrollbarGrabActive]   = {0.65f, 0.50f, 0.18f, 1.00f};
    C[ImGuiCol_CheckMark]             = {0.85f, 0.65f, 0.20f, 1.00f}; // gold
    C[ImGuiCol_SliderGrab]            = {0.58f, 0.44f, 0.14f, 0.90f};
    C[ImGuiCol_SliderGrabActive]      = {0.78f, 0.58f, 0.18f, 1.00f};
    C[ImGuiCol_Button]                = {0.22f, 0.18f, 0.12f, 0.90f}; // dark iron
    C[ImGuiCol_ButtonHovered]         = {0.48f, 0.35f, 0.12f, 1.00f}; // burnished bronze
    C[ImGuiCol_ButtonActive]          = {0.68f, 0.50f, 0.16f, 1.00f}; // bright gold
    C[ImGuiCol_Header]                = {0.38f, 0.13f, 0.08f, 0.85f}; // deep crimson
    C[ImGuiCol_HeaderHovered]         = {0.52f, 0.19f, 0.10f, 0.90f};
    C[ImGuiCol_HeaderActive]          = {0.64f, 0.24f, 0.12f, 1.00f};
    C[ImGuiCol_Separator]             = {0.38f, 0.28f, 0.08f, 0.80f};
    C[ImGuiCol_SeparatorHovered]      = {0.55f, 0.40f, 0.12f, 0.90f};
    C[ImGuiCol_SeparatorActive]       = {0.70f, 0.52f, 0.15f, 1.00f};
    C[ImGuiCol_ResizeGrip]            = {0.35f, 0.25f, 0.08f, 0.40f};
    C[ImGuiCol_ResizeGripHovered]     = {0.52f, 0.38f, 0.12f, 0.70f};
    C[ImGuiCol_ResizeGripActive]      = {0.70f, 0.52f, 0.16f, 0.90f};
    C[ImGuiCol_Tab]                   = {0.18f, 0.14f, 0.10f, 0.85f};
    C[ImGuiCol_TabHovered]            = {0.48f, 0.35f, 0.12f, 0.90f};
    C[ImGuiCol_TabActive]             = {0.36f, 0.25f, 0.10f, 1.00f};
    C[ImGuiCol_TabUnfocused]          = {0.14f, 0.10f, 0.07f, 0.85f};
    C[ImGuiCol_TabUnfocusedActive]    = {0.28f, 0.20f, 0.10f, 1.00f};
    C[ImGuiCol_PlotLines]             = {0.80f, 0.60f, 0.20f, 1.00f};
    C[ImGuiCol_PlotLinesHovered]      = {1.00f, 0.80f, 0.30f, 1.00f};
    C[ImGuiCol_PlotHistogram]         = {0.65f, 0.48f, 0.15f, 1.00f};
    C[ImGuiCol_PlotHistogramHovered]  = {0.85f, 0.65f, 0.20f, 1.00f};
    C[ImGuiCol_TableHeaderBg]         = {0.22f, 0.10f, 0.06f, 1.00f};
    C[ImGuiCol_TableBorderStrong]     = {0.38f, 0.28f, 0.10f, 1.00f};
    C[ImGuiCol_TableBorderLight]      = {0.28f, 0.20f, 0.08f, 0.70f};
    C[ImGuiCol_TableRowBg]            = {0.00f, 0.00f, 0.00f, 0.00f};
    C[ImGuiCol_TableRowBgAlt]         = {0.12f, 0.10f, 0.07f, 0.30f};
    C[ImGuiCol_TextSelectedBg]        = {0.55f, 0.40f, 0.10f, 0.40f};
    C[ImGuiCol_DragDropTarget]        = {0.85f, 0.65f, 0.20f, 0.90f};
    C[ImGuiCol_NavHighlight]          = {0.80f, 0.60f, 0.20f, 0.90f};
    C[ImGuiCol_NavWindowingHighlight] = {0.85f, 0.65f, 0.20f, 0.70f};
    C[ImGuiCol_NavWindowingDimBg]     = {0.00f, 0.00f, 0.00f, 0.50f};
    C[ImGuiCol_ModalWindowDimBg]      = {0.00f, 0.00f, 0.00f, 0.72f};

    if (!ImGui_ImplSDL2_InitForOpenGL(m_window, m_glCtx)) {
        fprintf(stderr, "ImGui SDL2 backend init failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        fprintf(stderr, "ImGui OpenGL3 backend init failed\n");
        return false;
    }
    m_imguiReady = true;
    gLog("ImGui %s ready\n", ImGui::GetVersion());
    return true;
}

void Game::shutdownImGui()
{
    if (!m_imguiReady) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_imguiReady = false;
}

void Game::beginImGuiFrame()
{
    if (!m_imguiReady) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void Game::endImGuiFrame()
{
    if (!m_imguiReady) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Lua scripting API ─────────────────────────────────────────────────────────
void Game::luaAddSpell(int spellId)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    for (int s : hero.knownSpells) if (s == spellId) return;
    hero.knownSpells.push_back(spellId);
}

void Game::luaAddXP(int amount)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    int oldLvl = hero.level;
    if (hero.addXp(amount)) {
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
    }
}

void Game::bindLuaAPI()
{
    lua_State* L = m_lua.state();
    if (!L) return;

    // Store this in registry so non-capturing callbacks can reach Game state
    lua_pushlightuserdata(L, static_cast<void*>(this));
    lua_setfield(L, LUA_REGISTRYINDEX, "Game");

    m_lua.registerGameFunc("getDay", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetDay() : 0);
        return 1;
    });

    m_lua.registerGameFunc("getWeek", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetWeek() : 0);
        return 1;
    });

    m_lua.registerGameFunc("getGold", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetGold() : 0);
        return 1;
    });

    m_lua.registerGameFunc("getHeroLevel", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetHeroLevel() : 1);
        return 1;
    });

    m_lua.registerGameFunc("addGold", [](lua_State* L) -> int {
        int n = static_cast<int>(luaL_checkinteger(L, 1));
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        if (g) g->luaAddGold(n);
        return 0;
    });

    m_lua.registerGameFunc("addSpell", [](lua_State* L) -> int {
        int id = static_cast<int>(luaL_checkinteger(L, 1));
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        if (g) g->luaAddSpell(id);
        return 0;
    });

    m_lua.registerGameFunc("addXP", [](lua_State* L) -> int {
        int n = static_cast<int>(luaL_checkinteger(L, 1));
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        if (g) g->luaAddXP(n);
        return 0;
    });
}

// ── Shutdown ──────────────────────────────────────────────────────────────────
void Game::shutdown()
{
    SDL_FreeCursor(m_cursorArrow);
    SDL_FreeCursor(m_cursorFight);
    m_audio.shutdown();
    m_lua.shutdown();
    shutdownImGui();
    m_editor.shutdown();
    m_hideout.close();
    if (m_glCtx) SDL_GL_DeleteContext(m_glCtx);
    if (m_window) SDL_DestroyWindow(m_window);
    SDL_Quit();
    gLog("Shutdown\n");
}
