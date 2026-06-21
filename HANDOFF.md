# HANDOFF — Unnamed Strategy Game
*For Claude Code — read this + GAME_PROJECT.md before doing anything*

---

## What this is
HoMM3-style turn-based strategy game. C++ / SDL2 / OpenGL 3.3. Solo project. See GAME_PROJECT.md for full design document.

## What is built (Phases 0-5)

| Phase | Files | Status |
|---|---|---|
| 0 — Engine | CMakeLists.txt, src/main.cpp, src/core/Game.h/cpp, src/renderer/* | ✅ Complete |
| 1 — World Map | src/world/HexGrid, HexMap, HexMapRenderer, FogOfWar, src/ai/Pathfinder, src/core/InputState | ✅ Complete |
| 2 — Town System | src/town/*, src/data/Resources.h, src/core/TurnManager | ✅ Complete |
| 3 — Combat | src/combat/* | ✅ Complete |
| 4 — Hero System | src/hero/* | ✅ Complete |
| 5 — UI | src/ui/* | ✅ Complete |

## What is NOT built yet (Phases 6-10)

| Phase | What |
|---|---|
| 6 | Save/Load — JSON map serialization, game state save/load, SQLite hideout |
| 7 | Procedural world generator, ImGui map editor, Lua scripting |
| 8 | Campaign state machine, alignment scoring, faction unlock branching |
| 9 | Hideout meta-layer, SQLite persistence, 9th faction unlock |
| 10 | SDL_mixer audio, font renderer, particle effects, balance pass |

## Known issues / TODOs

| Issue | Location | Notes |
|---|---|---|
| Text rendering is placeholder | src/ui/UIRenderer.cpp drawText() | Draws colored blocks per char — needs real font (stb_truetype or FreeType) |
| Hero draw is stub | src/core/Game.cpp drawHero() | Position tracked, visual placeholder — needs sprite |
| HexMapRenderer raw GL id hack | src/world/HexMapRenderer.h | m_rawWhiteId — acceptable for now, replace when tileset exists |
| BuildingRegistry only has Holy Order | src/town/BuildingRegistry.cpp | Other 8 factions need buildings added — follow same pattern |
| UnitDef circular include | src/town/UnitDef.h | Includes BuildingDef.h at bottom for UpgradePath — clean up |
| Combat not wired to world map | src/core/Game.cpp | CombatEngine exists but Game doesn't trigger it on hero collision yet |
| UI not wired to Game | src/core/Game.h | WorldMapHUD, TownScreen, CombatHUD instantiated but not in Game yet |
| No game state machine | src/core/ | Need a GameState enum (WorldMap, Combat, Town, Campaign) to switch screens |

## Architecture overview

```
Game (core loop)
├── InputState          — keyboard + mouse each frame
├── Camera2D            — orthographic, pan/zoom
├── SpriteBatch         — batched sprite rendering
├── HexMap              — tile data, terrain, fog of war
├── HexMapRenderer      — renders hex grid via OpenGL
├── Hero                — world map entity, movement, pathfinding
├── Pathfinder          — A* + reachable flood fill
├── FogOfWar            — vision reveal/hide
├── TurnManager         — 7-day week, end turn, income
├── BuildingRegistry    — static building + unit definitions
├── Town                — instance, build, recruit
├── CombatEngine        — hex combat, turn order, AI, damage
├── UIRenderer          — immediate mode colored quads
├── WorldMapHUD         — resource bar, hero list, end turn
├── CombatHUD           — turn order, unit info, action buttons
└── TownScreen          — building tree, recruit panel
```

## Next task for Claude Code
**Phase 6 — Save/Load**
1. JSON serialization for HexMap (nlohmann/json)
2. Game state save/load (hero pos, resources, towns, fog of war)
3. SQLite for hideout persistent state
4. Also wire up the GameState machine so WorldMap/Combat/Town screens actually switch

## Build instructions
```bash
mkdir build && cd build
cmake ..
make -j4
./unnamed_strategy
```
Requires: SDL2, OpenGL. stb_image.h already in third_party/.

## Key design decisions already made
- Flat-top hex grid, axial coordinates
- HoMM3-style movement (pool spent per terrain cost)
- FOW = explored (permanent) + visible (current turn only)
- 7-day week, weekly recruitment growth
- Combat: speed-based turn order, Wait queue, retaliation, morale bonus action
- Damage formula: HoMM3-style attack vs defense modifier
- 9 factions, all classes defined — see GAME_PROJECT.md
