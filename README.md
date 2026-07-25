# Unnamed Strategy *(working title)*

A classic-style turn-based strategy game: hex world map, separate tactical
combat screen, hero progression, 9 asymmetric factions, procedural world
generation, campaign, map editor, and a persistent Conquest meta-mode.
Single-player. C++20 / SDL2 / OpenGL 3.3 core / Dear ImGui.

> Solo project, built from scratch. Road to Steam: see
> [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md).

## Build

```bash
cmake -B build
cmake --build build -j4
./build/bin/unnamed_strategy
```

Requires a C++20 compiler, CMake, and SDL2/SDL2_mixer development libraries.
Everything else (ImGui, GLM, stb, nlohmann-json, SQLite, Lua) is vendored in
`third_party/`. Windows builds use MSYS2/UCRT64; a one-click installer is made
with `./packaging/build_installer.sh` (see [`packaging/README.md`](packaging/README.md)).

## Play

- **New Game** — pick faction, difficulty, map shape/size; conquer every rival.
- **Conquest** — persistent progression mode: permanent hero, weekly seeded
  maps, collection pool, quests, arena ladder (see [`CONQUEST_MODE.md`](CONQUEST_MODE.md)).
- **Watch AI** — sit back and watch 6 bots fight it out.
- **Editor** — build and share custom maps.

Balance is data-driven: edit `assets/data/units.json` / `buildings.json` — no
recompile needed (see `assets/data/README.md`).

## Development

| Doc | What's in it |
|---|---|
| [`GAME_PROJECT.md`](GAME_PROJECT.md) | Full design document |
| [`HANDOFF.md`](HANDOFF.md) | Architecture + current state |
| [`AI_ROADMAP.md`](AI_ROADMAP.md) | AI plans ("Project Omniscient") |
| [`THREADING.md`](THREADING.md) | Performance / threading plan |
| [`DOCS_INDEX.md`](DOCS_INDEX.md) | Index of everything else |

Useful dev commands:

```bash
./build/bin/unnamed_strategy --seed=42            # reproducible run (logs [SEED])
./build/bin/unnamed_strategy --watch-ai-test=6 --seed=42 --max-weeks=12
                                                  # hidden self-terminating AI game
scripts/verify_ai.sh 12 42                        # one-command AI economy check
python tools/asset_gen/gemini_gen.py              # regenerate missing art
```

Game art is AI-generated (disclosed in-game in Credits).
