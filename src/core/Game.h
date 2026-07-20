#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <imgui.h>

#include "DevLog.h"
#include "GameState.h"
#include "InputState.h"
#include "TurnManager.h"
#include "../renderer/SpriteBatch.h"
#include "../renderer/Camera2D.h"
#include "../renderer/SpriteAnim.h"
#include "../world/HexMap.h"
#include "../world/HexMapRenderer.h"
#include "../world/FogOfWar.h"
#include "../hero/Hero.h"
#include "../ai/Pathfinder.h"
#include "../town/Town.h"
#include "../town/BuildingRegistry.h"
#include "../combat/CombatEngine.h"
#include "../data/Resources.h"
#include "../data/ResourceNode.h"
#include "../data/SaveLoad.h"
#include "../data/SaveDB.h"
#include "../data/MapFormat.h"
#include "../ui/UIRenderer.h"
#include "../ui/WorldMapHUD.h"
#include "../ui/CombatHUD.h"
#include "../ui/TownScreen.h"
#include "../meta/HideoutDB.h"
#include "../meta/ConquestMode.h"
#include "../scripting/LuaEngine.h"
#include "../scripting/TriggerSystem.h"
#include "../editor/MapEditor.h"
#include "../editor/SimulatorWindow.h"
#include "../campaign/CampaignManager.h"
#include "../ui/CampaignHUD.h"
#include "../ui/HideoutScreen.h"
#include "../hero/LevelUpSystem.h"
#include "../hero/HeroClass.h"
#include "../hero/Artifacts.h"
#include "../world/WorldObject.h"
#include "../audio/AudioManager.h"
#include "../renderer/ParticleSystem.h"

class Game
{
public:
    Game() = default;
    ~Game() = default;

    // hidden=true creates the SDL window with SDL_WINDOW_HIDDEN instead of
    // maximized — used by the --watch-ai-test CLI path so a background dev
    // smoke test never appears on screen or steals focus from anything else
    // running.
    bool init(const std::string& title, int width, int height, bool hidden = false);
    void run();
    void shutdown();

    // Dev/test hook: skip the menu entirely and drop straight into a
    // Watch-AI game with the given player count/map shape/size so AI-vs-AI
    // behaviour can be verified from the gLog output alone (no UI
    // interaction needed). Slots 0 and 1 are put on the same team so
    // alliance behaviour gets exercised too. shape: 0=Hexagon, 1=JebusCross,
    // 2=JebusCross3, 3=Ring. size: 0=Small..3=XLarge — see --watch-ai-test
    // in main.cpp.
    void autoStartWatchAI(int playerCount = 6, int shape = 0, int size = 0);

    // --seed CLI: force the next startNewGame()'s world seed (worldgen,
    // faction rolls, AI personalities, combat RNGs all derive from it), making
    // runs reproducible — the missing piece for A/B-testing AI changes and the
    // Phase 3 determinism differential (see THREADING.md).
    void setForcedSeed(uint32_t s) { m_forcedSeed = s; m_forcedSeedSet = true; }

private:
    uint32_t m_forcedSeed    = 0;
    bool     m_forcedSeedSet = false;
    // ── Core loop ──────────────────────────────────────────────────────────────
    void processEvents();
    void update(float dt);
    void render();

    // ── State dispatch ─────────────────────────────────────────────────────────
    void updateWorldMap(float dt);
    void renderWorldMap();
    void updateCombat(float dt);
    void renderCombat();
    void updateTown(float dt);
    void renderTown();
    void updateEditor(float dt);
    void renderEditor();
    void updateCampaign(float dt);
    void renderCampaign();
    void updateMainMenu(float dt);
    void renderMainMenu();
    void updateConquest(float dt);
    void renderConquest();
    void startConquestBattle(int nodeIndex);
    void onConquestBattleEnd(bool victory);
    void startArenaBattle();
    void onArenaBattleEnd(bool victory);
    void conquestUnitIcon(int defId, float size = 32.f);
    // Absolute, rebuild-proof path to the shared meta DB (hideout + conquest).
    std::string metaDbPath() const;
    // Draw a full-screen loading frame (backdrop + progress bar) during init().
    void renderLoadingScreen(float progress, const char* label);
    // Draw the menu backdrop aspect-correct "cover" (fills any resolution without
    // distortion, cropping overflow) onto the current ImGui background draw list.
    void drawMenuBackdrop(float W, float H, int scrimAlpha);
    void enterCampaign();
    void exitCampaign();

    // ── State transitions ─────────────────────────────────────────────────────
    void enterWorldMap();
    void doEndTurn();       // shared end-of-turn logic (SPACE + HUD button)
    void enterCombat(Hero& playerHero,
                     const std::vector<CombatUnit>& playerUnits,
                     const Hero& enemyHero,
                     const std::vector<CombatUnit>& enemyUnits);
    void enterTown(Town* town);
    void enterEditor();
    void exitCombat(bool playerWon);
    void exitTown();
    void exitEditor();

    // ── New game / settings ───────────────────────────────────────────────────
    void startNewGame();       // reset all state and generate a fresh world
    void saveSettings();       // persist settings.json
    void loadSettings();       // read settings.json and apply to audio/display

    // ── World map helpers ──────────────────────────────────────────────────────
    void updateHeroMovement(float dt);
    void drawHero(const Hero& hero);
    void renderWorldOverlay();      // ImGui DrawList markers for all map entities
    void renderWorldMapImGui();     // ImGui-only portion shared with campaign render
    void onTileClicked(HexCoord h);
    void checkTileEvents();

    // ── ImGui integration ──────────────────────────────────────────────────────
    bool initImGui();
    void shutdownImGui();
    void beginImGuiFrame();
    void endImGuiFrame();

    // ── Save / Load (DB-backed) ────────────────────────────────────────────────
    // Save to DB. If m_activeSaveId==0 creates a new row; otherwise overwrites.
    void saveGame(const std::string& customName = "");
    // Load from DB row id. Returns false on failure.
    bool loadGame(int64_t saveId);
    // Legacy file-path load (for campaign autosave compatibility)
    bool loadGameFile(const std::string& path);

    bool loadGameApply(GameSaveData& data);

    // Returns the resource pool for the currently active player.
    // The N-player handoff swaps m_playerResources in/out of m_players[], so this
    // is always the current player's pool — no hot-seat special case needed.
    Resources& currentResources() { return m_playerResources; }
    // Returns the active hero for the current player (m_heroes is swapped per player).
    Hero* currentActiveHero();
    const Hero* currentActiveHero() const;

    // ── Hotseat multiplayer ───────────────────────────────────────────────────
    void renderPlayerTurnBanner();
    int  currentPlayerId() const { return m_currentPlayerIdx + 1; }

    // ── Level-up modal ─────────────────────────────────────────────────────────
    void renderLevelUpModal();

    // ── Combat spell panel ─────────────────────────────────────────────────────
    void renderSpellPanel();

    // ── World-map spell panel and effects ─────────────────────────────────────
    void renderWorldSpellPanel();
    void renderTownPortalPopup();
    void renderFoundCityPopup();
    void castWorldSpell(int spellId);
    // Re-apply the Lighthouse sea-speed bonus to every hero, based on which
    // side currently owns each Lighthouse. Call after a beacon changes hands.
    void refreshLighthouseBoosts();

    // ── Land connectivity (pathfinding fast-reject) ──────────────────────────
    // A FAILING 400-hex A* is the single most expensive thing the AI does: it
    // explores the whole horizon before concluding "unreachable". Measured at
    // ~76 ms per call, dominating the entire turn.
    //
    // Land passability is uniform across heroes (Mountain and Water are the only
    // blockers, plus Barrier tiles), so one flood fill labels every land tile
    // with a component id. Two tiles in different components CANNOT be connected
    // by land — an O(1) lookup replaces the doomed search entirely.
    // Rebuilt once per turn; terrain does not change mid-turn.
    //
    // m_seaComp is the amphibious version for heroes already ON a boat: they
    // sail Water and disembark onto any land, so only Mountain and Barrier
    // tiles separate components. Without it, onBoat heroes skipped the
    // fast-reject entirely and naval planning paid full price for dead ends.
    std::unordered_map<HexCoord, int, HexCoordHash> m_landComp;
    std::unordered_map<HexCoord, int, HexCoordHash> m_seaComp;
    int  m_landCompTurn = -1;
    void rebuildLandComponents();
    // true when a route is provably impossible (different components) for a
    // hero in the given movement mode. onBoat picks the amphibious map.
    bool routeImpossible(bool onBoat, HexCoord a, HexCoord b) const
    {
        const auto& comp = onBoat ? m_seaComp : m_landComp;
        auto ia = comp.find(a);
        auto ib = comp.find(b);
        if (ia == comp.end() || ib == comp.end()) return false; // unknown: let A* decide
        return ia->second != ib->second;
    }

    // ── Combat board (hex grid with units) ────────────────────────────────────
    void renderCombatBoard();

    // ── Artifact equip panel (F7) ──────────────────────────────────────────────
    void renderArtifactPanel();

    // ── Hero inspect panel (F8) ───────────────────────────────────────────────
    void renderHeroInspect();

    // ── Mage guild overlay in town (ImGui) ────────────────────────────────────
    void renderMageGuild();
    void renderCapturePopup();
    void renderTownLostPopup();
    void renderWeekSummary();
    void renderTavern();
    void renderArtifactForge();   // craftable artifact shop in town
    void renderMarketplace();     // resource trading (4:1 exchange)

    // ── Unit exchange overlay ─────────────────────────────────────────────────
    void renderUnitExchange();

    // ── World object interaction popups ───────────────────────────────────────
    void renderDwellingPopup();
    void renderStatShrinePopup();
    void renderQuestPopup();

    // ── Victory / defeat modals ────────────────────────────────────────────────
    void renderCombatResultPopup();
    void renderVictoryModal();
    void renderDefeatModal();

    // ── Lua scripting API (called from Lua, thin wrappers) ────────────────────
    void bindLuaAPI();
    int  luaGetDay()       const { return m_turns.day(); }
    int  luaGetWeek()      const { return m_turns.week(); }
    int  luaGetGold()      const { return m_playerResources.get(ResourceType::Gold); }
    int  luaGetHeroLevel() const { return m_heroes.empty() ? 1 : m_heroes[m_activeHeroIdx].level; }
    void luaAddGold(int n)       { m_playerResources.add(ResourceType::Gold, n); }
    void luaAddXP(int n);
    void luaAddSpell(int spellId);

    // ── Hideout screen ─────────────────────────────────────────────────────────
    void renderHideoutScreen();

    // ── SDL / GL ───────────────────────────────────────────────────────────────
    SDL_Window*   m_window   = nullptr;
    SDL_GLContext m_glCtx    = nullptr;
    bool          m_running  = false;
    int           m_width    = 0;
    int           m_height   = 0;
    std::string   m_basePath;   // SDL_GetBasePath() — prefix for asset paths

    // ── State machine ──────────────────────────────────────────────────────────
    GameState m_state = GameState::WorldMap;

    // ── Core systems ──────────────────────────────────────────────────────────
    InputState     m_input;
    SpriteBatch    m_batch;
    Camera2D       m_camera;
    UIRenderer     m_ui;

    // ── World map ─────────────────────────────────────────────────────────────
    HexMap         m_map;
    HexMapRenderer m_hexRenderer;
    MapSize        m_mapSize = MapSize::Small;

    // ── Heroes ────────────────────────────────────────────────────────────────
    std::vector<Hero> m_heroes;
    std::vector<Hero> m_enemyHeroes;
    int               m_activeHeroIdx = 0;
    uint32_t          m_nextHeroId    = 300; // global counter for tavern-hired hero IDs

    HexCoord       m_hovered  {-999, -999};
    HexCoord       m_selected {-999, -999};

    float m_moveT    = 1.0f;
    float m_moveSrcX = 0.0f, m_moveSrcY = 0.0f;
    float m_moveDstX = 0.0f, m_moveDstY = 0.0f;

    std::vector<HexCoord> m_reachable;
    std::unordered_set<HexCoord, HexCoordHash> m_roadHexes;

    // ── Towns & resources ─────────────────────────────────────────────────────
    std::vector<Town>         m_towns;
    std::vector<ResourceNode> m_resources;
    std::vector<HexCoord>     m_heroStarts;
    BuildingRegistry          m_registry;

    // ── Economy / turn ────────────────────────────────────────────────────────
    Resources    m_playerResources;
    // Per-AI-player economy: index i = ownerId (m_numHumanPlayers + 1 + i).
    // Each bot earns income from and pays for its own towns/mines/units/heroes
    // independently now — previously every AI player pooled into one shared
    // "AI team" fund, which is also why they never fought each other (nothing
    // about a shared pool respects individual ownership). AI's only allowed
    // advantage over a human is information (no fog of war).
    std::vector<Resources> m_aiResources;
    Resources& aiResources(uint32_t ownerId) {
        static Resources dummy;
        int idx = static_cast<int>(ownerId) - m_numHumanPlayers - 1;
        if (idx < 0 || idx >= (int)m_aiResources.size()) { dummy = Resources{}; return dummy; }
        return m_aiResources[idx];
    }
    TurnManager  m_turns;

    // AI-owned ids: towns/mines store enemy HERO ids, which are always above
    // the human player id range (humans are 1..m_numHumanPlayers).
    bool isAiOwner(uint32_t id) const { return id > static_cast<uint32_t>(m_numHumanPlayers); }

    // ── Combat ────────────────────────────────────────────────────────────────
    CombatEngine m_combat;
    bool         m_showSpellPanel  = false;
    uint32_t     m_spellTargetId   = 0;    // unit ID pre-selected for spell

    // Combat board rendering/click transform
    float        m_combatBoardScale = 1.0f;
    float        m_combatBoardOffX  = 0.0f;
    float        m_combatBoardOffY  = 0.0f;

    // ── UI ────────────────────────────────────────────────────────────────────
    WorldMapHUD  m_worldHUD;
    CombatHUD    m_combatHUD;
    TownScreen   m_townScreen;

    // ── Icon texture atlas (256x96, 8x3 cells of 32x32) ──────────────────────
    Texture           m_iconTex;
    Texture           m_spellIconTex;  // 5×5 atlas, 32×32 cells — spell icons
    Texture           m_menuBgTex;     // main-menu / loading-screen backdrop

    // ── Ornate main-menu chrome (mode 0 only) ─────────────────────────────────
    Texture           m_menuHeaderEmblemTex;   // assets/ui/menu_header_emblem.png
    Texture           m_menuButtonFrameTex;    // assets/ui/menu_button_frame.png (wide bar, reused per button)
    // Index order matches the mode-0 button list: New Game, Load Game, Campaign,
    // Battle Sim, Watch AI vs AI, Settings, Map Editor, Quit.
    static constexpr int NUM_MENU_ICONS = 8;
    Texture           m_menuIconTex[NUM_MENU_ICONS];

    // ── Per-unit sprite textures: m_unitTex[faction][tier-1] ─────────────────
    // File: assets/sprites/faction_F_tT.png  (F=0-8, T=1-6)
    // Each is a single-row sprite sheet with TOTAL_COLS animation frames.
    static constexpr int NUM_FACTIONS  = 9;
    static constexpr int NUM_UNIT_TIERS = 6;
    Texture           m_unitTex[NUM_FACTIONS][NUM_UNIT_TIERS];
    int               m_unitTexCols[NUM_FACTIONS][NUM_UNIT_TIERS] = {};  // frame count per sheet
    Texture           m_portraitTex[NUM_FACTIONS];

    // Summoned-unit sprite sheets (Necromancy skeletons, WildGrowth ghosts) —
    // combat units with no faction/tier mapping. Same 8-frame row format.
    Texture           m_summonSkelTex,  m_summonGhostTex;
    int               m_summonSkelCols = 8, m_summonGhostCols = 8;
    // Per-faction world-map hero figures (assets/sprites/hero_F.png). Where a
    // faction has none yet, the map falls back to its tier-1 unit sprite.
    Texture           m_heroTex[NUM_FACTIONS];
    int               m_heroTexCols[NUM_FACTIONS] = {};

    // ── Siege art: defensive towers (per faction) + attacker engines ─────────
    // File: assets/sprites/tower_F.png (F=0-8), assets/sprites/engine_<key>.png
    // See ART_SIEGE.md for the key list. Same 8-frame sheet format as units.
    Texture           m_towerTex[NUM_FACTIONS];
    int               m_towerTexCols[NUM_FACTIONS] = {};
    static constexpr int NUM_ENGINE_KEYS = 14;
    Texture           m_engineTex[NUM_ENGINE_KEYS];
    int               m_engineTexCols[NUM_ENGINE_KEYS] = {};

    // Siege fortification art: assets/siege/{wall,wall_damaged,gate,moat}_F.png
    Texture           m_wallTex[NUM_FACTIONS];
    Texture           m_wallDamagedTex[NUM_FACTIONS];
    Texture           m_gateTex[NUM_FACTIONS];
    Texture           m_moatTex[NUM_FACTIONS];
    // Faction whose fortification art is in play for the current siege battle.
    int               m_siegeTownFaction = 0;

    // Resolve which sheet a combat animator draws from (faction unit or summon).
    const Texture* combatSpriteTexture(const SpriteAnimator& a) const {
        if (a.kind == 1) return &m_summonSkelTex;
        if (a.kind == 2) return &m_summonGhostTex;
        if (a.kind == 3) {
            int fi = a.faction;
            return (fi >= 0 && fi < NUM_FACTIONS) ? &m_towerTex[fi] : nullptr;
        }
        if (a.kind == 4) {
            int ei = a.engineIdx;
            return (ei >= 0 && ei < NUM_ENGINE_KEYS) ? &m_engineTex[ei] : nullptr;
        }
        int fi = a.faction;
        int ti = std::max(0, std::min(NUM_UNIT_TIERS - 1, a.tier - 1));
        if (fi >= 0 && fi < NUM_FACTIONS) return &m_unitTex[fi][ti];
        return nullptr;
    }

    // ── Combat board terrain backgrounds: one per Terrain enum value ──────────
    // File: assets/terrain/combat/TERRAIN_NAME.png
    static constexpr int NUM_TERRAIN_TYPES = 15;
    Texture           m_combatBgTex[NUM_TERRAIN_TYPES];

    // ── Faction town art (world map + town screen banner) ─────────────────────
    // File: assets/towns/faction_N.png  (N=0-8)
    Texture           m_townTex[NUM_FACTIONS];
    // Fortification-stage town art (HoMM3-style: town grows walls as you build
    // Fort->Citadel->Castle[+Bastion]). Stage 0 = basic (no fort), 1 = Fort,
    // 2 = Citadel, 3 = Castle, 4 = Castle+Bastion. Loaded from
    // assets/towns/faction_<F>_<stage>.png; any missing stage falls back to the
    // nearest lower stage, ultimately to m_townTex[faction].
    static constexpr int NUM_TOWN_STAGES = 5;
    Texture           m_townTexStage[NUM_FACTIONS][NUM_TOWN_STAGES];
    // Returns the fort stage (0-4) for a town from its built fort buildings.
    int townFortStage(const Town& t) const;
    // Returns the best available town texture id for a faction+stage (with
    // fallback), or 0 if none. Defined in Game_Core.cpp.
    unsigned int townStageTexId(int faction, int stage) const;

    // ── Building category icon atlas ───────────────────────────────────────────
    // File: assets/buildings/icons_buildings.png  (384×64, 6 cols × 1 row)
    Texture           m_buildingIconTex;

    // ── Per-faction single-tier buildings [faction] ───────────────────────────
    // fort, market, town_hall, city_hall — each has 9 faction variants
    Texture m_fortTex[NUM_FACTIONS];
    Texture m_marketTex[NUM_FACTIONS];
    Texture m_townHallTex[NUM_FACTIONS];
    Texture m_cityHallTex[NUM_FACTIONS];

    // ── Per-faction mage guild art [faction][tier-1] ──────────────────────────
    // Files: assets/buildings/mage_guild/mage_guild_f{0-8}_t{1-4}.png
    static constexpr int MAGE_GUILD_TIERS  = 4;
    Texture m_mageGuildTex[NUM_FACTIONS][MAGE_GUILD_TIERS];

    // ── Per-faction warehouse art [faction][tier-1] ───────────────────────────
    // Files: assets/buildings/warehouse/warehouse_f{0-8}_t{1-3}.png
    static constexpr int WAREHOUSE_TIERS   = 3;
    Texture m_warehouseTex[NUM_FACTIONS][WAREHOUSE_TIERS];

    // ── HolyOrder dwelling art (base + A + B per tier) ───────────────────────
    // Files: assets/units/holy_order/<DwellingName>.png
    // Indexed [tier-1][variant]: 0=base, 1=PathA, 2=PathB
    static constexpr int HO_DWELLING_TIERS    = 6;
    static constexpr int HO_DWELLING_VARIANTS = 3;
    Texture m_hoDwellingTex[HO_DWELLING_TIERS][HO_DWELLING_VARIANTS];

    static constexpr int CW_DWELLING_TIERS    = 6;
    static constexpr int CW_DWELLING_VARIANTS = 3;
    Texture m_cwDwellingTex[CW_DWELLING_TIERS][CW_DWELLING_VARIANTS];

    // Files: assets/units/eternal_empire/<UnitName>.png
    static constexpr int EE_DWELLING_TIERS    = 6;
    static constexpr int EE_DWELLING_VARIANTS = 3;
    Texture m_eeDwellingTex[EE_DWELLING_TIERS][EE_DWELLING_VARIANTS];

    // Files: assets/units/thornkin/<DwellingName>.png
    static constexpr int TK_DWELLING_TIERS    = 6;
    static constexpr int TK_DWELLING_VARIANTS = 3;
    Texture m_tkDwellingTex[TK_DWELLING_TIERS][TK_DWELLING_VARIANTS];

    // Files: assets/units/bloodsworn/<DwellingName>.png
    static constexpr int BS_DWELLING_TIERS    = 6;
    static constexpr int BS_DWELLING_VARIANTS = 3;
    Texture m_bsDwellingTex[BS_DWELLING_TIERS][BS_DWELLING_VARIANTS];

    // Files: assets/units/voidkin/<DwellingName>.png
    static constexpr int VK_DWELLING_TIERS    = 6;
    static constexpr int VK_DWELLING_VARIANTS = 3;
    Texture m_vkDwellingTex[VK_DWELLING_TIERS][VK_DWELLING_VARIANTS];

    // Files: assets/units/iron_assembly/<DwellingName>.png
    static constexpr int IA_DWELLING_TIERS    = 6;
    static constexpr int IA_DWELLING_VARIANTS = 3;
    Texture m_iaDwellingTex[IA_DWELLING_TIERS][IA_DWELLING_VARIANTS];

    // Files: assets/units/amalgamate/<DwellingName>.png
    static constexpr int AM_DWELLING_TIERS    = 6;
    static constexpr int AM_DWELLING_VARIANTS = 3;
    Texture m_amDwellingTex[AM_DWELLING_TIERS][AM_DWELLING_VARIANTS];

    // Files: assets/units/convergence/<DwellingName>.png
    static constexpr int CV_DWELLING_TIERS    = 6;
    static constexpr int CV_DWELLING_VARIANTS = 3;
    Texture m_cvDwellingTex[CV_DWELLING_TIERS][CV_DWELLING_VARIANTS];

    // Capitol buildings (one per faction): assets/buildings/capitol/<faction>.png
    Texture m_capitolTex[NUM_FACTIONS];

    // Faction special/support buildings (2 per faction): assets/buildings/special/<Name>.png
    Texture m_powerSpecialTex[NUM_FACTIONS];
    Texture m_supportSpecialTex[NUM_FACTIONS];

    // Shared: Bastion + Shipyard: assets/buildings/special/<Name>.png
    Texture m_bastionTex;
    Texture m_shipyardTex;

    // ── Per-unit combat animators (keyed by CombatUnit id) ───────────────────
    std::unordered_map<uint32_t, SpriteAnimator> m_combatAnimators;

    // ── Floating damage text effects in combat ────────────────────────────────
    struct CombatDmgEffect { float bx, by, t; int dmg; bool isHeal; };
    std::vector<CombatDmgEffect> m_combatDmgEffects;

    // ── Ranged-attack projectiles (arrow/bolt travels attacker → target) ──────
    struct CombatProjectile { float x0, y0, x1, y1, t, duration; ImU32 color; };
    std::vector<CombatProjectile> m_combatProjectiles;

    // ── Editor ────────────────────────────────────────────────────────────────
    MapEditor         m_editor;
    SimulatorWindow   m_simWindow;
    bool              m_imguiReady = false;

    // ── Scripting ─────────────────────────────────────────────────────────────
    LuaEngine     m_lua;
    TriggerSystem m_triggers;

    // ── Campaign ───────────────────────────────────────────────────────────────
    CampaignManager m_campaign;
    CampaignHUD     m_campaignHUD;

    // ── World objects (scrolls, chests, shrines) ──────────────────────────────
    std::vector<WorldObject> m_worldObjects;
    uint32_t                 m_nextObjId = 1;

    // ── Artifact registry ──────────────────────────────────────────────────────
    ArtifactRegistry m_artifactRegistry;

    // ── Artifact / Hero inspect overlay flags ─────────────────────────────────
    bool m_showArtifactPanel = false;
    bool m_showHeroInspect   = false;

    // ── Combat tracking ───────────────────────────────────────────────────────
    uint32_t                m_lastCombatEnemyId    = 0;
    std::vector<UnitStack>  m_battleStartArmy;     // hero army snapshot before combat (for FIRST_AID)
    Terrain                 m_pendingCombatTerrain = Terrain::Plains;
    Terrain                 m_combatTerrain        = Terrain::Plains;

    // ── AI bot personalities (per owner) ──────────────────────────────────────
    // Explorer: roams for mines/objects, timid in fights, wide map grab.
    // Builder:  turtles, pours gold into town buildings, defends.
    // Warrior:  aggressive, hunts rival heroes/towns early, army over economy.
    // Mage:     calm economy + defense early, escalates to dominance late.
    enum class AiPersonality : uint8_t { Explorer = 0, Builder, Warrior, Mage };
    // Indexed by ownerId (0 unused); assigned at game start, ~8 slots + margin.
    AiPersonality m_aiPersonality[10] = {};
    const char*   aiPersonalityName(AiPersonality p) const {
        switch (p) {
            case AiPersonality::Explorer: return "Explorer";
            case AiPersonality::Builder:  return "Builder";
            case AiPersonality::Warrior:  return "Warrior";
            case AiPersonality::Mage:     return "Mage";
        }
        return "?";
    }
    // ── Persistent meta layer ──────────────────────────────────────────────────
    HideoutDB    m_hideout;
    HideoutScreen m_hideoutScreen;
    bool          m_showHideoutScreen = false;

    // ── Conquest mode (see CONQUEST_MODE.md) ──────────────────────────────────
    ConquestMode m_conquest;
    int          m_conquestActiveNode = -1;   // node currently being fought
    bool         m_conquestHeroSetup  = false; // hero-creation sub-screen open
    int          m_conquestSetupFaction = 0;
    int          m_conquestSetupClassId = 0;
    char         m_conquestSetupName[48] = "Wanderer";
    // Phase 2: army screen + chest popup + casualty tracking
    bool         m_conquestShowArmy = false;
    std::vector<std::pair<int,int>> m_conquestDeployed; // defId→count sent into battle
    ConquestMode::ChestResult m_conquestChestResult;
    bool         m_conquestShowChestResult = false;
    // Phase 3: quests + gem shop
    bool         m_conquestShowQuests  = false;
    bool         m_conquestShowGemShop = false;
    // Phase 4: unit path upgrades
    bool         m_conquestShowUpgrades  = false;
    int          m_conquestUpgradeFaction = 0;
    // Phase 5: arena
    bool         m_conquestShowArena = false;
    // Cached per-player watch summary (rebuilt once per week, not per frame —
    // the per-frame version looped 8 players × 4158 resources ≈ 2M ops/sec,
    // which pegged low-end CPUs).
    struct WatchPlayerRow { uint32_t owner; long long str; int heroes, towns, mines, gold; };
    std::vector<WatchPlayerRow> m_watchSummary;
    int          m_watchSummaryWeek = -1;
    // Weeks each owner (1-9) has gone without any town. A hero can't be
    // sustained forever with no town — after a grace period the owner's heroes
    // disband and the player is eliminated. This is what actually removes a
    // player who lost their last town but still has a wandering army.
    int          m_ownerTownlessWeeks[10] = {0};
    bool         m_conquestInArena   = false;   // true while an arena fight is active
    // Phase 2 revised: cheap recruit shop
    bool         m_conquestShowRecruit    = false;
    int          m_conquestRecruitFaction = 0;

    // ── Level-up flow ──────────────────────────────────────────────────────────
    HeroClassRegistry          m_classRegistry;
    std::vector<LevelUpOffer>  m_levelUpOffers;
    bool                       m_showLevelUpModal = false;
    int                        m_pendingLevelUps  = 0;  // queued level-ups awaiting skill pick

    // State to return to after visiting a town or combat (handles Campaign → Town → Campaign)
    GameState m_prevState = GameState::WorldMap;

    // ── Victory / defeat ──────────────────────────────────────────────────────
    bool        m_showVictory    = false;
    bool        m_showDefeat     = false;
    bool        m_finalDefeat    = false;  // no heroes with armies and no player towns
    std::string m_victoryMessage;          // set before raising m_showVictory

    // ── Combat result summary popup ───────────────────────────────────────────
    bool        m_showCombatResult  = false;
    bool        m_combatResultWon   = false;
    int         m_combatResultXp    = 0;
    int         m_combatResultGold  = 0;
    int         m_combatResultKills = 0;
    int         m_combatResultLost  = 0;

    struct BattleUnitRecord {
        std::string name;
        int defId   = 0;
        int faction = -1;
        int tier    = 1;
        int count   = 0;
    };
    std::vector<BattleUnitRecord> m_combatUnitsLost;
    std::vector<BattleUnitRecord> m_combatEnemiesDefeated;

    // ── Garrison management overlay (in town) ─────────────────────────────────
    bool        m_showGarrisonPanel   = false;
    int         m_garrisonSelSlot     = -1;
    int         m_garrisonSelSide     = -1;   // 0=hero army, 1=town garrison
    bool        m_garrisonSplitMode   = false; // true if selection was ctrl+clicked (split-in-half on drop)
    void renderGarrisonPanel();

    // Shared 7-slot unit-stack row UI (icon + count, click-to-select then
    // click-target to move/merge/swap, ctrl+click to split in half) — used by
    // both the garrison panel and the hero<->hero unit exchange so they read
    // and behave identically. Only draws + tracks selection; it cannot perform
    // the transfer itself since it only sees one army at a time. When the user
    // clicks a target slot, it stores the click in m_slotTransferTarget* and
    // leaves (selSide,selSlot) as the source — the caller resolves both arrays
    // and calls resolveSlotTransfer() once after drawing every row for the frame.
    int  m_slotTransferTargetSide = -1;
    int  m_slotTransferTargetSlot = -1;
    void drawUnitSlotRow(int side, std::vector<UnitStack>& army,
                          int& selSide, int& selSlot, bool& splitMode);
    // Call once after all rows for a screen are drawn. srcArmy/dstArmy are
    // resolved by the caller from (selSide) and (m_slotTransferTargetSide).
    // Performs move/merge/swap (or split-in-half if splitMode was set) and
    // clears the shared selection/target state.
    void resolveSlotTransfer(std::vector<UnitStack>& srcArmy, std::vector<UnitStack>& dstArmy,
                              int& selSlot, bool& splitMode);

    std::unordered_set<int> m_mageGuildT4BonusGiven;  // hero IDs that already got the T4 mana bonus this game

    // ── Town service overlay flags (opened via "Town Services" bar) ───────────
    bool        m_showMageGuildPanel    = false;
    bool        m_showTavernPanel       = false;
    bool        m_showArtifactForgePanel = false;
    bool        m_showMarketPanel       = false;
    int         m_marketSellType        = 0;   // ResourceType index to sell
    int         m_marketBuyType         = 1;   // ResourceType index to buy
    int         m_marketSellQty         = 4;   // multiples of trade ratio

    // ── Town capture notification ─────────────────────────────────────────────
    // ── World-map spell panel ─────────────────────────────────────────────────
    bool        m_showWorldSpellPanel = false;
    bool        m_showTownPortalPopup = false;
    bool        m_showFoundCityPopup  = false;
    uint32_t    m_foundCityUtopiaId   = 0;

    // ── Kingdom overview panel ────────────────────────────────────────────────
    bool        m_showKingdomPanel    = false;
    void renderKingdomPanel();

    bool        m_showCapturePopup  = false;
    std::string m_capturedTownName;

    // ── Pending town capture after garrison combat ────────────────────────────
    uint32_t    m_pendingTownCaptureId = 0;

    // ── Town DEFENSE siege: AI attacks a human town → real playable battle ────
    uint32_t    m_pendingTownDefenseId = 0;   // town being defended (player side = garrison)
    uint32_t    m_defenseAttackerId    = 0;   // enemy hero assaulting it
    bool        m_showDefensePrepPopup = false;
    int         m_siegePrepChoice      = -1;  // -1 none, 0 spikes, 1 nets, 2 shield wall, 3 plating
    void        startTownDefenseBattle(int prepChoice);
    void        renderDefensePrepPopup();
    // >=0: the current combat is vs ANOTHER HUMAN's hero — index into m_players.
    // On victory the defeated hero is removed from m_players[idx].heroes instead
    // of m_enemyHeroes.
    int         m_lastCombatHumanIdx   = -1;
    // Pandora's Box being fought over (0 = none); reward rolls on victory.
    uint32_t    m_pendingPandoraId     = 0;

    // ── Town-lost notification (enemy captured player town) ───────────────────
    bool        m_showTownLostPopup   = false;
    std::string m_lostTownName;

    // ── Unit exchange between player heroes ────────────────────────────────────
    // Same slot-grid / click-select-then-click-target model as the garrison
    // panel (0=hero A, 1=hero B), including ctrl+click-to-split.
    bool        m_showUnitExchange  = false;
    int         m_exchangeHeroIdx   = -1;   // index of the OTHER hero (in m_heroes)
    int         m_exchangeSelSide   = -1;   // 0=hero A, 1=hero B
    int         m_exchangeSelSlot   = -1;
    bool        m_exchangeSplitMode = false;
    int         m_exchangeSelArtifactSide = -1; // artifact trade: 0=hero A inventory, 1=hero B
    int         m_exchangeSelArtifactIdx  = -1;

    // ── World object interactions ─────────────────────────────────────────────
    uint32_t m_pendingObjId          = 0;
    bool     m_showDwellingPopup     = false;
    bool     m_showStatShrinePopup   = false;
    bool     m_showQuestPopup        = false;
    uint32_t m_lastBanditCampId      = 0;
    bool     m_showTreasureChestPopup = false;
    uint32_t m_pendingChestId         = 0;
    void renderTreasureChestPopup();
    bool     m_showCryptPopup   = false;
    bool     m_showUtopiaPopup  = false;
    uint32_t m_pendingCryptId            = 0;
    uint32_t m_pendingUtopiaId           = 0;
    uint32_t m_pendingMineId             = 0;  // resource node whose guard was accepted (set on win)
    uint32_t m_pendingNeutralOutpostId   = 0;  // outpost whose fight was accepted (set collected on win)
    void renderCryptPopup();
    void renderUtopiaPopup();

    // ── Mine inspection popup (right-click on mine) ───────────────────────────
    bool     m_showMineInfoPopup = false;
    uint32_t m_mineInfoId        = 0;
    void renderMineInfoPopup();

    // ── Tree of Knowledge choice popup ────────────────────────────────────────
    bool     m_showTreeKnowledgePopup = false;
    uint32_t m_pendingTreeId          = 0;
    void renderTreeOfKnowledgePopup();

    // ── Shipyard popup (build a boat) ─────────────────────────────────────────
    bool     m_showShipyardPopup = false;
    void     renderShipyardPopup();

    // ── Artifact Merchant popup (world-map merchant; buy Special artifacts) ───
    bool     m_showMerchantPopup  = false;
    int      m_merchantSeed       = 0;
    void     renderArtifactMerchantPopup();

    // ── Arena (fight for +ATK or +DEF) ───────────────────────────────────────
    bool     m_showArenaPopup    = false;
    int      m_arenaBonusChoice  = 0;   // 0=ATK, 1=DEF
    void     renderArenaPopup();
    void     startArenaCombat();

    // ── Fishing House (passive income; no popup needed) ───────────────────────
    // Income applied in doEndTurn() each day

    // ── Pre-combat encounter prompt (decline / fight choice) ─────────────────
    bool                    m_showEncounterPrompt   = false;
    std::string             m_encounterTitle;
    std::vector<CombatUnit> m_pendingEncounterUnits; // enemy units for the prompt
    Hero                    m_pendingEncounterHero;  // enemy hero for the prompt
    std::function<void()>   m_encounterOnAccept;    // called when player clicks Fight
    std::function<void()>   m_encounterOnDecline;   // called when player clicks Retreat
    void renderEncounterPrompt();

    // ── Siege camp ────────────────────────────────────────────────────────────
    bool     m_showSiegeCampPrompt  = false;
    uint32_t m_siegePromptTownId    = 0;
    void renderSiegeCampPrompt();
    void renderSiegeIndicator();   // world-map overlay showing camped heroes
    void triggerSiegeCombat(uint32_t townId);   // resolve all camped heroes vs a town
    void renderMarchButton();      // March ability button for selected hero

    // ── Upgrade Path A/B choice popup ────────────────────────────────────────
    bool m_showUpgradePathPopup = false;
    int  m_upgradePathA         = 0;
    int  m_upgradePathB         = 0;
    void renderUpgradePathPopup();

    // ── Right-click combat unit stat popup ────────────────────────────────────
    uint32_t m_combatRightClickUnitId = 0;

    // ── Hero click tracking (world map — single click centers, double shows inspect) ──
    int m_heroClickTarget = -1;

    // ── Pause menu (Escape on world map) ─────────────────────────────────────
    bool m_showPauseMenu = false;
    void renderPauseMenu();

    // ── Mini-map overlay ──────────────────────────────────────────────────────
    void renderMinimap();
    bool m_showMinimap = true;

    // ── Debug / cheat options ─────────────────────────────────────────────────
    bool m_fogDisabled = false;

    // ── Cached weekly income (updated each turn end) ─────────────────────────
    Resources m_cachedWeeklyIncome;

    // ── Week summary popup ────────────────────────────────────────────────────
    bool        m_showWeekSummary   = false;
    int         m_weekSummaryWeek   = 0;
    Resources   m_weekSummaryIncome;
    std::string m_weeklyEventHeadline;   // empty = no event this week
    std::string m_weeklyEventBody;


    // Choice events -- when non-empty the week summary shows option buttons
    struct WeekChoiceOption {
        std::string label;
        std::string effectText;
        std::function<void()> onSelect;
    };
    std::vector<WeekChoiceOption> m_weekChoiceOptions;

    // ── Audio ─────────────────────────────────────────────────────────────────
    AudioManager    m_audio;
    ParticleSystem  m_particles;

    // ── World map time (for object idle animations) ───────────────────────────
    float m_mapTime = 0.0f;

    // ── Floating pickup text effects ──────────────────────────────────────────
    struct PickupEffect { float wx, wy, t; std::string text; ImU32 col; };
    std::vector<PickupEffect> m_pickupEffects;
    void pushPickupEffect(HexCoord pos, const char* text, ImU32 col);

    // ── World-map hero animators ──────────────────────────────────────────────
    std::unordered_map<uint32_t, SpriteAnimator> m_heroMapAnimators;

    // ── SDL cursors ───────────────────────────────────────────────────────────
    SDL_Cursor* m_cursorArrow = nullptr;
    SDL_Cursor* m_cursorFight = nullptr;

    // ── Campaign tutorial ─────────────────────────────────────────────────────
    bool m_campaignTutorialSeen = false;
    int  m_tutorialStep         = 0;
    void renderCampaignTutorial();

    // ── Save DB ───────────────────────────────────────────────────────────────
    SaveDB  m_saveDB;
    int64_t m_activeSaveId = 0;  // 0 = no current save row (new game, not yet saved)

    // ── Main menu sub-state ───────────────────────────────────────────────────
    int  m_menuMode            = 0;   // 0=main, 1=newgame, 2=loadgame, 3=settings, 4=campaign, 5=battlesim
    int  m_newGameMapSize    = 0;   // 0=Small, 1=Medium, 2=Large, 3=XLarge
    int  m_newGameMapShape   = 0;   // 0=Hexagon, 1=JebusCross, 2=JebusCross3, 3=Ring
    int  m_newGameFaction    = 0;   // 0=HolyOrder ... 8=Convergence
    int  m_newGameDifficulty = 1;   // 0=Easy, 1=Normal, 2=Hard
    int  m_newGameClassId    = 0;   // classId of chosen hero class (0=auto)

    // ── HoMM-style game setup slots ───────────────────────────────────────────
    // Slot 0 is always you. Each additional slot is Human (hot-seat) or Bot,
    // with its own faction (9 = Random) and starting bonus.
    static constexpr int MAX_SETUP_SLOTS = 8;
    int m_setupPlayerCount = 2;                             // total players, 2..8 (map zones)
    int m_slotType[MAX_SETUP_SLOTS]    = {0, 1, 1, 1, 1, 1, 1, 1};  // 0=Human, 1=Bot
    int m_slotFaction[MAX_SETUP_SLOTS] = {0, 9, 9, 9, 9, 9, 9, 9};  // 0..8 faction, 9=Random
    int m_slotBonus[MAX_SETUP_SLOTS]   = {2, 2, 2, 2, 2, 2, 2, 2};  // 0=Artifact, 1=+5 faction resource, 2=+1500 gold
    // 0 = free agent (default, everyone is their own team — current FFA
    // behaviour). >0 = team number; slots sharing a team number are allies.
    int m_slotTeam[MAX_SETUP_SLOTS]    = {0, 0, 0, 0, 0, 0, 0, 0};
    // Per-slot hero class id (0 = first/default class for that faction).
    int m_slotClassId[MAX_SETUP_SLOTS] = {0, 0, 0, 0, 0, 0, 0, 0};

    // ── Alliances ──────────────────────────────────────────────────────────
    // Live-game team per player slot, index = ownerId - 1 (slot s -> ownerId
    // s+1 for both human and AI slots — matches Town::ownerId/Hero::ownerId).
    // Populated from m_slotTeam at game start; persisted across save/load.
    std::vector<int> m_playerTeam;
    bool isAllied(uint32_t a, uint32_t b) const {
        if (a == b) return true;
        int ia = static_cast<int>(a) - 1, ib = static_cast<int>(b) - 1;
        if (ia < 0 || ia >= (int)m_playerTeam.size() || ib < 0 || ib >= (int)m_playerTeam.size())
            return false;
        int ta = m_playerTeam[ia], tb = m_playerTeam[ib];
        return ta != 0 && ta == tb; // team 0 = free agent, allied with no one
    }

    // ── Hotseat multiplayer state ─────────────────────────────────────────────
    int  m_numHumanPlayers      = 1;   // 1=singleplayer, N=N-player hotseat
    int  m_currentPlayerIdx     = 0;   // 0-based
    int  m_newGameNumPlayers    = 1;   // new-game menu selection

    struct PlayerState {
        std::vector<Hero>  heroes;
        Resources          resources;
        int                activeHeroIdx = 0;
        std::vector<Hero>  defeatedPool;
    };
    struct PlayerNotifs {
        bool        townLost    = false;
        std::string townName;
        bool        defeated    = false;
        bool        weekSummary = false;
        int         weekNum     = 0;
        Resources   weekIncome;
    };
    std::vector<PlayerState>  m_players;     // one entry per human player
    std::vector<PlayerNotifs> m_playerNotifs;

    bool  m_showPlayerTurnBanner = false;
    float m_playerTurnBannerT    = 0.0f;

    // ── Battle Simulator ─────────────────────────────────────────────────────
    bool  m_fromBattleSim     = false;
    bool  m_simAutoPlay       = false;
    float m_simAutoPlayTimer  = 0.f;   // seconds until next AI action in watch mode
    int   m_simWeek           = 5;
    int   m_simFaction1       = 0;
    int   m_simFaction2       = 1;
    int   m_simSiegeMode      = 0;     // 0 open field, 1 side 2 defends a town, 2 side 1 defends
    bool  m_simBastion        = false; // defender has a Bastion (+25% walls, auto defense prep)

    // ── Watch AI vs AI mode ────────────────────────────────────────────────────
    bool  m_watchingAI      = false;   // both sides controlled by AI
    // Watched-side heroes that already took their move this game-day (by id) —
    // survives combat interruption so no hero double-moves after a fight.
    std::vector<uint32_t> m_watchMovedThisDay;
    float m_watchAITimer    = 0.f;     // countdown to next auto end-turn
    float m_watchAISpeed    = 1.0f;    // delay multiplier (0.25 – 8.0, same range everywhere)
    bool  m_watchAIPaused   = false;   // hard pause — turns stop, UI stays live
    void  watchAiMovePlayerHero();     // runs player hero through same AI logic as enemies
    void  watchAiMoveSupportHero(Hero& hero, bool isCourier); // scouts/courier: no combat
    // Award XP to an AI hero and auto-apply level-ups (stats + class skills, no UI).
    void  aiHeroAwardXp(Hero& hero, int xp);
    // Weekly yield of a mine, scaled by game week so mines stay relevant late game:
    // gold mines +5/week, special resources +1 per 10 weeks.
    int   mineYield(const ResourceNode& r) const {
        int week = m_turns.week();
        return r.amount + (r.type == ResourceType::Gold ? week * 5 : week / 10);
    }
    // Equip a picked-up artifact into a free slot (or better-stat swap), else stash it.
    void  aiEquipOrStashArtifact(Hero& hero, int artifactId);

    // ── Hot-seat 2-player mode ─────────────────────────────────────────────────
    // Hot-seat runs entirely on the N-player system (m_players / m_currentPlayerIdx);
    // these are just the menu flag and the pass-the-device privacy screen.
    bool      m_hotSeatMode      = false;  // two humans share one screen
    bool      m_hotSeatHandoff   = false;  // show handoff screen
    bool      m_newGameHotSeat   = false;  // set in new game menu
    void      renderHotSeatHandoff();
    int       m_p2Faction        = 1;      // faction index chosen by P2 in menu
    int       m_p2ClassId        = 0;

    // ── Persisted display / audio settings ───────────────────────────────────
    float m_settingsSfxVol       = 0.7f;
    float m_settingsMasVol       = 0.35f;
    bool  m_settingsFullscreen   = false;
    bool  m_settingsAutoSave     = true;    // save at each week-end automatically
    float m_settingsAnimSpeed    = 1.0f;   // combat animation speed multiplier (0.5–2.0)
    bool  m_settingsShowDmgNums  = true;   // floating damage numbers in combat
};
