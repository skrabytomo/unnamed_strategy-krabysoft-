# Docs Index — what's current (2026-07-25)

Quick map of every doc in this repo and whether it's live reference or reflects
completed work. Cleaned up 2026-07 (removed stale ART_MISSING.md; annotated the
rest with real state).

## Live / current reference

| Doc | What it is |
|---|---|
| `README.md` | Repo front page: what the game is, build/play/dev quickstart. |
| `RELEASE_CHECKLIST.md` | Road to Steam — the one place tying together everything needed to ship. Both former blocked items resolved 2026-07-25. |
| `GAME_PROJECT.md` | Master design document (factions, systems, lore). Timeless. |
| `HANDOFF.md` | Architecture + **current-state addendum at top** (read first). |
| `CLAUDE.md` | Claude Code project instructions (build, branch rules). |
| `CONQUEST_MODE.md` | Conquest mode design — all 5 phases IMPLEMENTED. |
| `HIDEOUT_PROGRESSION.md` | The between-runs meta-layer: every XP source, what it buys, pacing maths, and the tuning knobs. Read before changing hideout numbers. |
| `AI_ROADMAP.md` | Triaged "Project Omniscient" AI plan (DONE/FITS/NO-FIT tiers). |
| `THREADING.md` | AI off the render thread. **The 0 FPS Watch freeze is FIXED (2026-07-20)**: resumable AI round, frame-spread in Watch mode, verified by a `--seed` determinism differential. Remaining: worker-thread planning inside the new seams. Read before touching `doEndTurn()` or adding threads. |
| `packaging/README.md` | One-click installer / packaging instructions. |

## Art references (art gaps, for Gemini generation)

| Doc | What it is |
|---|---|
| `ART_DROPIN_MANIFEST.md` | Precise drop-in targets for the current art gaps (multiple hero portraits per faction, crest/icon specs). |
| `ART_MISSING_TERRAIN.md` | Terrain variant status. **Records that the "black gaps between tiles" artifact was renderer-side (missing mipmaps), NOT the art — read before regenerating tiles to fix seams.** |
| `UNIT_ROLES.md` | Every unit's intended role (ranged/melee/fly), generated from `units.json`. Data half of the sprite design-match check; flags that 4 of 9 factions field zero shooters. |
| `ART_MISSING_TOWNS.md` | Upgrade-path A/B sprites (0/108, real gap). Dwelling art now DONE. |
| `ART_SIEGE.md` | Siege art spec (siege art files exist; reference for any redo). |
| `BUILDING_ART_BRIEF.md` | Town building art brief (reference). |
| `SPRITE_HANDOFF.md` | Combat readability pass (landed) + reframe tooling notes. |

## Removed

- `ART_MISSING.md` — deleted; all its pending items (hero_1..8 figures, siege
  art, object icons) now exist in `assets/`.

## Known open engine work (not in a dedicated doc)

Tracked in `HANDOFF.md` addendum (statuses updated 2026-07-25 — siege
units-on-walls and the AI dock yo-yo "idle" bug are FIXED; naval hulls all do
their jobs now; AI gold-hoarding is no longer just "verified" but actively
*exploited* by rivals via hoarding prediction): still open are siege
wall-damage *tuning*, town "already built" indicator, visual kingdom overview,
hero right-click artifact sheet, icon-based town/hero pickers.

Verification tooling: `scripts/verify_ai.sh [WEEKS] [SEED]` runs a headless
seeded game and prints the AI economy summary (one command, auto-xvfb).

Performance / threading work is tracked in `THREADING.md` (freeze fixed;
worker-thread planning optional next).
