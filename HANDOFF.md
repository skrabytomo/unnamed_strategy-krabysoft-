# HANDOFF — Unnamed Strategy Game
*For Claude Code — read this + GAME_PROJECT.md before doing anything*

---

## What this is
HoMM3-style turn-based strategy game. C++20 / SDL2 / OpenGL 3.3 Core / ImGui 1.90.8.
9 factions, hex-grid world map, turn-based combat, town building, campaign system.
See GAME_PROJECT.md for full design document.

## What is built (all phases complete)

| System | Files | Status |
|---|---|---|
| Engine / core loop | src/main.cpp, src/core/Game.h/cpp, src/renderer/* | ✅ |
| Game state machine | src/core/GameState.h, Game_WorldMap/Combat/Town/Campaign/Editor/MainMenu.cpp | ✅ |
| World map | src/world/HexGrid, HexMap, HexMapRenderer, FogOfWar, WorldGen | ✅ |
| Pathfinding | src/ai/Pathfinder.cpp | ✅ |
| Town system | src/town/BuildingRegistry, Town, BuildingDef, UnitDef | ✅ |
| Combat | src/combat/CombatEngine, CombatGrid, CombatUnit, DamageCalc | ✅ |
| Hero system | src/hero/Hero, HeroClass, LevelUpSystem, Artifacts, Skills, SkillRegistry | ✅ |
| UI | src/ui/WorldMapHUD, CombatHUD, TownScreen, CampaignHUD, HideoutScreen, UIRenderer | ✅ |
| Save / Load | src/data/SaveLoad.cpp, MapFormat.cpp | ✅ |
| Map editor | src/editor/MapEditor.cpp, SimulatorWindow.cpp | ✅ |
| Procedural world gen | src/world/WorldGen.cpp | ✅ |
| Lua scripting | src/scripting/LuaEngine.cpp, TriggerSystem.cpp, scripts/*.lua | ✅ |
| Campaign system | src/campaign/CampaignManager.cpp, AlignmentSystem.h, CampaignDef.h | ✅ |
| Hideout meta-layer | src/meta/HideoutDB.cpp (SQLite) | ✅ |
| Audio | src/audio/AudioManager.cpp (SDL_mixer) | ✅ |
| Combat simulator | src/sim/Simulator.cpp, ArmyBuilder.cpp (standalone sim_test binary) | ✅ |
| Turn manager | src/core/TurnManager.cpp — 7-day week, income, end turn | ✅ |
| Skill archetype system | src/combat/CombatEngine.cpp (`applyArchetype` in `initCombat`) | ✅ |
| World-map AI (combat) | src/combat/CombatEngine.cpp — Passive (Easy) / Tactical (Normal+Hard); aiTargetScore() kill/danger-aware focus-fire targeting; enemy hero casts one spell per round incl. watch/auto mode (processOneAIAction) | ✅ |
| World-map AI (movement) | src/core/Game_WorldMap.cpp — strength-based roles (strongest=raider, 2nd=economic, rest=defenders that intercept threats to owned towns); hunts NEAREST human hero across all players; GhostWalk = half target score (not exempt); beats mine guards off-screen at ≥1.3x strength; runs after last human's turn in hot-seat | ✅ |
| World-map AI (growth) | Field-upgrades base→built path at towns; tiered weekly reinforcements from best town's dwellings (difficulty-scaled 0.75x/1x/1.5x); wiped armies restart with fresh T1 stack; aiHeroAwardXp() = XP+levels+skills from combat wins; aiEquipOrStashArtifact() auto-equips pickups | ✅ |
| World-map AI (town builds) | src/core/Game_WorldMap.cpp — faction-priority build orders + PathA upgrades, infinite richRes budget | ✅ |
| AI difficulty scaling | Easy/Normal/Hard: reinforcement 0.75/1/1.5x, raider aggression at 60/50/40% strength ratio, retreat below 50/40/30%, hero cap 5/6/8, combat Passive/Tactical/Tactical | ✅ |
| **2-Player Hot-Seat** | Runs entirely on the N-player system (m_players/m_currentPlayerIdx; doEndTurn handoff swaps m_heroes/m_playerResources). P2 = m_players[1] hero id=2 (menu faction/class overlaid in startNewGame). m_enemyHeroes is PURE AI in all modes. Legacy m_hotSeatP2Turn/m_selectedEnemyHero/m_player2Resources system deleted. Walking onto another human's hero = combat (m_lastCombatHumanIdx routes victory removal to m_players[idx].heroes). m_hotSeatMode is only the menu flag + handoff privacy screen; re-derived on load from numHumanPlayers>=2 | ✅ |
| **Siege Camp mechanic** | Hero::isSiegeCamping/siegeTargetTownId, Town::underSiege/siegeFortified/fortifyBonuses, Game_WorldMap.cpp renderSiegeCampPrompt/renderSiegeIndicator/triggerSiegeCombat | ✅ |
| **Fortify button** | Town screen service bar (Game_Town.cpp), one use per siege turn, +4 DEF/+2 wall HP/+3 tower dmg | ✅ |
| **March ability** | Hero::marchCooldownWeek/marchBonusActive, renderMarchButton() in Game_WorldMap.cpp — costs 25% move, gives +10% next week, 1-week CD | ✅ |
| **Balance sim** | src/sim/FullGameSim.cpp + SimDB.cpp + fullgame_main.cpp — headless AI vs AI, SQLite results, all-vs-all balance report | ✅ |
| **Artifact shops** | ArtifactDef::shopPrice; Tavern wares (3 rotating Specials/week by town+week seed); ArtifactMerchant map object (permanent 3-artifact shop, seed stored in obj.value); Game_Town.cpp renderArtifactMerchantPopup() | ✅ |
| **AI emergency hire** | exitCombat(): when last enemy hero dies and AI has a town, immediately spawns a replacement hero (T1 army scaled to week) so AI isn't passive until next weekly phase | ✅ |
| **Arena map object** | WorldObjectType::Arena; hero chooses +1 ATK or +1 DEF, fights scaled Arena Champion; stat applied in exitCombat() on win; per-hero visit lock via obj.questState; 2-3 per map | ✅ |
| **Experience Well** | WorldObjectType::ExperienceWell; one-time XP grant (500 + week×100) on step; fires level-up modal if threshold crossed; 3-4 per map | ✅ |

## Architecture overview

```
Game (core loop — Game.cpp)
├── GameState enum → dispatches to Game_WorldMap / Game_Combat / Game_Town / Game_Campaign / Game_Editor / Game_MainMenu
├── InputState          — keyboard + mouse each frame
├── Camera2D            — orthographic, pan/zoom
├── SpriteBatch         — batched sprite rendering
├── HexMap              — tile data, terrain, resource nodes, towns, heroes
├── HexMapRenderer      — renders hex grid via OpenGL
├── Hero                — world map entity, movement, pathfinding, inventory
├── Pathfinder          — A* + reachable flood fill
├── FogOfWar            — explored (permanent) + visible (current turn)
├── TurnManager         — 7-day week, end turn, mine income
├── BuildingRegistry    — static building + unit defs for all 9 factions
├── Town                — instance state, build queue, recruit
├── CombatEngine        — hex combat, speed turn order, Wait, retaliation, AI
├── UIRenderer          — immediate mode quads + text
├── WorldMapHUD         — resource bar, hero list, minimap, end turn
├── CombatHUD           — turn order strip, unit info, action buttons
├── TownScreen          — building tree, recruit panel
├── CampaignHUD         — campaign objectives, alignment display
├── HideoutScreen       — meta-progression, faction unlock
├── MapEditor           — terrain paint, place towns/resources/triggers, ProGen panel
├── LuaEngine           — script host, trigger callbacks
├── TriggerSystem       — map event triggers → Lua callbacks
├── CampaignManager     — chapter progression, alignment scoring, branching
├── HideoutDB           — SQLite persistence for meta state
└── AudioManager        — SDL_mixer, per-state music tracks
```

## Resource economy
- Gold mines: `node.amount = 250`
- Non-gold mines: `node.amount = 2-5`
- Income added each new week in `Game_WorldMap.cpp` after `m_turns.endTurn()`
- Faction primary resources: HO/CW=FaithStones, TK/VK=VerdantSap, EE/CV=Mercury, BS/AM=BloodEssence, IA=Iron
- Warehouse chain: T1(BID=3) → T2(BID=7) → T3(BID=8); each tier adds 2 Iron/wk
- Mage Guild chain: T1(BID=5) → T2(BID=6) → T3(BID=9, 30% off) → T4(BID=10, 50% off + mana)

## Key architecture rules
- `glClear` must use `GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT`
- `UIRenderer::endFrame()` must NOT call `m_textQueue.clear()` — `flushText()` does it
- World-map overlay labels/icons must be clipped against `HUD_TOP=68`, `HUD_BOTTOM=sh-52`, `HUD_RIGHT=sw-185`
- `CombatEngine::wait()` and board clicks must guard `WantCaptureMouse`
- ImGui popups: only one `BeginPopupModal` per frame — chain with `else if`
- Camera clamp: `limX = max(0, mapExtX - screenW/(2*zoom))` — viewport-compensated
- Default ImGui font has no Unicode — use ASCII only in all strings
- `m_heroes`/`m_playerResources` always belong to the CURRENT player (the N-player
  handoff in doEndTurn swaps them through m_players[]); never special-case "whose
  turn" — use `currentPlayerId()`. `m_enemyHeroes` is pure AI in every mode.
- Mode-entry buttons that call startNewGame() must reset `m_newGameHotSeat` first
  (Watch AI and Campaign do) or a stale menu toggle forces numHumanPlayers=2 and
  gates the AI block off.
- The enemy AI block in doEndTurn runs only when `m_numHumanPlayers <= 1 ||
  lastPlayerEndedTurn` — i.e. once per full round.

## Editor (F2)
- Terrain painting, Town/Resource/HeroStart/Trigger/Erase tools
- ProGen panel: seed, players, map size, resource density → Generate
- Save/Load: File menu or Ctrl+S/O → maps/*.map
- Resource editor hardcodes Gold/amount=3 on place (type/amount not yet editable in UI)

## Pending — Town building art & interactivity (not started)
Building **names** are now fixed to match the roster (Crimson Wardens renamed from
Undead-leftover Ossuary/Crypt/Lich Spire naming to Scout Camp/Ranger Lodge/Hunter's
Lodge/Berserker Hall/Warden's Tower/Warlord's Bastion).

Full missing-art catalog for this (backgrounds, building cutouts, and the 108
missing upgrade-path unit sprites) is in **`ART_MISSING_TOWNS.md`** — generate
externally (same Gemini/DALL-E workflow as sprite_brief.md), then ping to wire
into the engine (path-aware sprite loading + town scene renderer, both noted
as separate code tasks in that file).

## Build
```bash
cmake --build build -j4
./build/bin/unnamed_strategy
```
Requires: SDL2, OpenGL 3.3, SDL_mixer. Dependencies (ImGui, nlohmann/json) fetched via CMake FetchContent.
