# Claude Code — Project Instructions

## Project overview
HoMM3-style hex-grid strategy game. C++20 / SDL2 / OpenGL 3.3 Core / ImGui 1.90.8.
See `GAME_PROJECT.md` for full design document, `HANDOFF.md` for architecture.

## Branch — STRICT, NO EXCEPTIONS
**Work ONLY on `main`. NEVER create any branch. NEVER create a pull request.**
- Do NOT create a `claude/*` working branch or any other branch — commit directly to `main` and push to `main`.
- Do NOT open, suggest, or switch to a feature/topic branch, even a "temporary" one.
- If a tool or default wants to create a branch, refuse and commit to `main` instead.
- Stray branches have repeatedly caused divergence/merge hell in this repo. Only `main` may ever exist on the remote.
- If you find any non-`main` branch, delete it:
  `git push <remote> --delete <branch>`

## Build
```bash
cmake --build build -j4



Only interact with skrabytomo/unnamed_strategy-krabysoft-.
