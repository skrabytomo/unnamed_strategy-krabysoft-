# Docs Index — what's current (2026-07)

Quick map of every doc in this repo and whether it's live reference or reflects
completed work. Cleaned up 2026-07 (removed stale ART_MISSING.md; annotated the
rest with real state).

## Live / current reference

| Doc | What it is |
|---|---|
| `GAME_PROJECT.md` | Master design document (factions, systems, lore). Timeless. |
| `HANDOFF.md` | Architecture + **current-state addendum at top** (read first). |
| `CLAUDE.md` | Claude Code project instructions (build, branch rules). |
| `CONQUEST_MODE.md` | Conquest mode design — all 5 phases IMPLEMENTED. |
| `AI_ROADMAP.md` | Triaged "Project Omniscient" AI plan (DONE/FITS/NO-FIT tiers). |
| `THREADING.md` | Plan for getting the AI off the render thread and onto many cores (fixes the XL/8-player 0 FPS freeze). Phases 0–1 landed; 2–4 designed. Read before touching `doEndTurn()` or adding threads. |
| `packaging/README.md` | One-click installer / packaging instructions. |

## Art references (art gaps, for Gemini generation)

| Doc | What it is |
|---|---|
| `ART_DROPIN_MANIFEST.md` | Precise drop-in targets for the current art gaps (multiple hero portraits per faction, crest/icon specs). |
| `ART_MISSING_TOWNS.md` | Upgrade-path A/B sprites (0/108, real gap). Dwelling art now DONE. |
| `ART_SIEGE.md` | Siege art spec (siege art files exist; reference for any redo). |
| `BUILDING_ART_BRIEF.md` | Town building art brief (reference). |
| `SPRITE_HANDOFF.md` | Combat readability pass (landed) + reframe tooling notes. |

## Removed

- `ART_MISSING.md` — deleted; all its pending items (hero_1..8 figures, siege
  art, object icons) now exist in `assets/`.

## Known open engine work (not in a dedicated doc)

Tracked in `HANDOFF.md` addendum: siege walls, AI idle/no-trade/no-attack,
naval (ships unused), town "already built" indicator, visual kingdom overview,
hero right-click artifact sheet, icon-based town/hero pickers.

Performance / threading work is tracked in `THREADING.md` (XL 8-player freeze).
