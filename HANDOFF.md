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
| World-map AI (combat) | src/combat/CombatEngine.cpp — Passive / Standard / Tactical | ✅ |
| World-map AI (movement) | src/core/Game_WorldMap.cpp — strength-aware pathing, mine/object capture | ✅ |
| World-map AI (town builds) | src/core/Game_WorldMap.cpp — faction-priority build orders + PathA upgrades | ✅ |

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

## Editor (F2)
- Terrain painting, Town/Resource/HeroStart/Trigger/Erase tools
- ProGen panel: seed, players, map size, resource density → Generate
- Save/Load: File menu or Ctrl+S/O → maps/*.map
- Resource editor hardcodes Gold/amount=3 on place (type/amount not yet editable in UI)

## Build
```bash
cmake --build build -j4
./build/bin/unnamed_strategy
```
Requires: SDL2, OpenGL 3.3, SDL_mixer. Dependencies (ImGui, nlohmann/json) fetched via CMake FetchContent.
