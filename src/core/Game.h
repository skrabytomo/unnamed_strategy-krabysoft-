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
#include "../data/MapFormat.h"
#include "../ui/UIRenderer.h"
#include "../ui/WorldMapHUD.h"
#include "../ui/CombatHUD.h"
#include "../ui/TownScreen.h"
#include "../meta/HideoutDB.h"
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

class Game
{
public:
    Game() = default;
    ~Game() = default;

    bool init(const std::string& title, int width, int height);
    void run();
    void shutdown();

private:
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

    // ── Save / Load ────────────────────────────────────────────────────────────
    void saveGame(const std::string& path);
    bool loadGame(const std::string& path);

    // ── Level-up modal ─────────────────────────────────────────────────────────
    void renderLevelUpModal();

    // ── Combat spell panel ─────────────────────────────────────────────────────
    void renderSpellPanel();

    // ── World-map spell panel and effects ─────────────────────────────────────
    void renderWorldSpellPanel();
    void renderTownPortalPopup();
    void renderFoundCityPopup();
    void castWorldSpell(int spellId);

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
    std::vector<Hero> m_defeatedHeroPool; // heroes removed from map after defeat/retreat; hireable in tavern
    int               m_activeHeroIdx = 0;

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
    TurnManager  m_turns;

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

    // ── Per-unit sprite textures: m_unitTex[faction][tier-1] ─────────────────
    // File: assets/sprites/faction_F_tT.png  (F=0-8, T=1-6)
    // Each is a single-row sprite sheet with TOTAL_COLS animation frames.
    static constexpr int NUM_FACTIONS  = 9;
    static constexpr int NUM_UNIT_TIERS = 6;
    Texture           m_unitTex[NUM_FACTIONS][NUM_UNIT_TIERS];
    Texture           m_portraitTex[NUM_FACTIONS];

    // ── Combat board terrain backgrounds: one per Terrain enum value ──────────
    // File: assets/terrain/combat/TERRAIN_NAME.png
    static constexpr int NUM_TERRAIN_TYPES = 15;
    Texture           m_combatBgTex[NUM_TERRAIN_TYPES];

    // ── Faction town art (world map + town screen banner) ─────────────────────
    // File: assets/towns/faction_N.png  (N=0-8)
    Texture           m_townTex[NUM_FACTIONS];

    // ── Per-unit combat animators (keyed by CombatUnit id) ───────────────────
    std::unordered_map<uint32_t, SpriteAnimator> m_combatAnimators;

    // ── Floating damage text effects in combat ────────────────────────────────
    struct CombatDmgEffect { float bx, by, t; int dmg; bool isHeal; };
    std::vector<CombatDmgEffect> m_combatDmgEffects;

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

    // ── Persistent meta layer ──────────────────────────────────────────────────
    HideoutDB    m_hideout;
    HideoutScreen m_hideoutScreen;
    bool          m_showHideoutScreen = false;

    // ── Level-up flow ──────────────────────────────────────────────────────────
    HeroClassRegistry          m_classRegistry;
    std::vector<LevelUpOffer>  m_levelUpOffers;
    bool                       m_showLevelUpModal = false;
    int                        m_pendingLevelUps  = 0;  // queued level-ups awaiting skill pick

    // State to return to after visiting a town or combat (handles Campaign → Town → Campaign)
    GameState m_prevState = GameState::WorldMap;

    // ── Victory / defeat ──────────────────────────────────────────────────────
    bool m_showVictory  = false;
    bool m_showDefeat   = false;
    bool m_finalDefeat  = false;  // no heroes with armies and no player towns

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
    void renderGarrisonPanel();

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

    // ── Town-lost notification (enemy captured player town) ───────────────────
    bool        m_showTownLostPopup = false;
    std::string m_lostTownName;

    // ── Unit exchange between player heroes ────────────────────────────────────
    bool        m_showUnitExchange  = false;
    int         m_exchangeHeroIdx   = -1;   // index of the OTHER hero
    int         m_exchangeSelSlotA  = -1;   // selected slot in hero A's army
    int         m_exchangeSelSlotB  = -1;   // selected slot in hero B's army

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
    uint32_t m_pendingCryptId   = 0;
    uint32_t m_pendingUtopiaId  = 0;
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
    AudioManager m_audio;

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

    // ── Main menu sub-state & save slots ─────────────────────────────────────
    int  m_menuMode            = 0;   // 0=main, 1=newgame, 2=loadgame, 3=settings, 4=campaign, 5=battlesim
    int  m_activeSlot          = 0;   // which general save slot (0-4) is in use
    int  m_campaignActiveSlot  = 0;   // which campaign save slot (0-2) is in use
    int  m_newGameMapSize    = 0;   // 0=Small, 1=Medium, 2=Large, 3=XLarge
    int  m_newGameFaction    = 0;   // 0=HolyOrder ... 8=Convergence
    int  m_newGameDifficulty = 1;   // 0=Easy, 1=Normal, 2=Hard
    int  m_newGameClassId    = 0;   // classId of chosen hero class (0=auto)

    // ── Battle Simulator ─────────────────────────────────────────────────────
    bool  m_fromBattleSim     = false;
    bool  m_simAutoPlay       = false;
    float m_simAutoPlayTimer  = 0.f;   // seconds until next AI action in watch mode
    int   m_simWeek           = 5;
    int   m_simFaction1       = 0;
    int   m_simFaction2       = 1;

    // ── Persisted display / audio settings ───────────────────────────────────
    float m_settingsSfxVol       = 0.7f;
    float m_settingsMasVol       = 0.35f;
    bool  m_settingsFullscreen   = false;
    bool  m_settingsAutoSave     = true;    // save at each week-end automatically
    float m_settingsAnimSpeed    = 1.0f;   // combat animation speed multiplier (0.5–2.0)
    bool  m_settingsShowDmgNums  = true;   // floating damage numbers in combat
};
