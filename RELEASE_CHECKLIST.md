# Road to Steam — release checklist

One place that ties together everything needed to ship. Grouped by who does it.
Detailed how-tos live in `packaging/steam/`.

## Status at a glance

**Done (code/tooling):** engine, combat, towns, heroes, campaign, conquest,
save/load, AI (idle fix + economy: build cadence, income priority, watched
parity; decisive conquest: elimination priority, beatable garrison cap,
eased AI-vs-AI storm threshold so games resolve), all HUD/visual polish,
classic-strategy-style score + highscores + results screens,
achievements (7, wired), data-driven balance loading (editable JSON ships
in `assets/data/`), in-game Credits (with AI disclosure) + How-to-Play,
Steamworks SDK integration, SteamPipe packaging, full art-generation
pipeline (every category), store-asset spec.

**Blocked (needs a file from the dev, then code finishes it):**
- [x] ~~Push one regenerated terrain tile → fix the full-bleed artifact~~
      DONE 2026-07-25: 23-tile terrain batch landed; the black-gap artifact
      was renderer-side (missing mipmaps), fixed in `a65556f`. See
      `ART_MISSING_TERRAIN.md`.
- [x] ~~Push a fresh `session.log` from a current build → verify AI economy~~
      DONE 2026-07-25: verified directly with a headless seeded run
      (`--watch-ai-test=6 --seed=42` under Xvfb, current `main` build).
      16 weeks: towns kept building every single week (all 6 towns, 14–22
      builds each, still advancing trees at week 16), recruitment grew
      18→~30 units/week, 9 town storms, heroes to level 5, AI turn avg
      111 ms / max 547 ms. Economy confirmed healthy on a current build.

## 1. Dev tasks (no account/money needed — do these first)

- [ ] **Finish the art.** Regenerate/complete assets at home. Whole set is
      regenerable: `python tools/asset_gen/gemini_api_gen.py --only <kind>`
      (kinds: terrain, dwelling, building, siege, hero, icon, town, collage,
      capsule). Use the **paid API** for clean commercial licensing.
- [ ] **Tune balance** by editing `assets/data/units.json` / `buildings.json`
      — these now ship in the repo, pre-seeded from the compiled defaults and
      ready to edit. Loads at startup, no recompile. See `assets/data/README.md`
      and the `[BALANCE]` log line. (Re-seed anytime with
      `--export-content=assets/data`.)
- [ ] **Playtest** a few full games start→finish: crashes, softlocks, balance,
      that towns finish their trees and armies grow (the AI economy fix).
      *Headless evidence so far (2026-07-25): a full 6-player AI game (seed
      123) ran start→resolution (dominance win, week 8) with zero crashes,
      asserts or softlocks; a 16-week seed-42 run confirmed towns keep
      building and armies grow. Still needs YOUR hands-on runs for feel,
      balance and UI issues — headless can't judge those.*
- [ ] **Store art**: generate the two capsule masters
      (`--only capsule`), crop to the sizes in `packaging/steam/STORE_ASSETS.md`,
      add your title/logo. Capture 5–10 **screenshots** at 1920×1080 and cut a
      short **trailer** from the running game.

## 2. Steamworks setup (account + $100)

- [ ] Create the Steamworks partner account, pay Steam Direct ($100/app),
      complete identity/bank/tax verification.
- [ ] Create the app → note **App ID** + **Depot ID**.
- [ ] Build with Steam on:
      `-DUSE_STEAMWORKS=ON -DSTEAMWORKS_SDK_ROOT=... -DSTEAM_APP_ID=...`
      (redistributable auto-bundles; add `steam_appid.txt` for dev). See
      `packaging/steam/STEAMWORKS.md`.
- [ ] Create the **achievements** in the admin using the API names in
      `packaging/steam/ACHIEVEMENTS.md` (upload earned + locked icons).
- [ ] Point **Steam Auto-Cloud** at the save dir (`SDL_GetPrefPath` folder) —
      no code needed.

## 3. Store page

- [ ] Fill the store page: capsules, screenshots, trailer, short + full
      description, ~5–10 tags, system requirements (64-bit Windows 10+, OpenGL
      3.3 GPU).
- [ ] **Tick the AI-content disclosure** (already in the in-game Credits too).
- [ ] Remember Steam's **~30-day** minimum between page-live and release.

## 4. Ship

- [ ] `packaging/steam/upload_steam.sh` (SteamPipe) to upload a build; verify in
      Steamworks → Builds; `PREVIEW=1` first for a dry run.
- [ ] Set the build live on a **test branch**, play it end-to-end on a clean
      machine.
- [ ] Promote to `default`, pass Steam's review, release.

## Doc index
- `packaging/steam/README.md` — SteamPipe upload
- `packaging/steam/STEAMWORKS.md` — SDK on/off, achievements, cloud, overlay
- `packaging/steam/STORE_ASSETS.md` — exact asset sizes + text
- `packaging/steam/ACHIEVEMENTS.md` — achievement API names
- `tools/asset_gen/README.md` — art generation pipeline
