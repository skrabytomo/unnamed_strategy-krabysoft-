# Gemini asset generator

Drives **your logged-in Gemini** (running inside **Brave**) to generate the
game's missing **hero portraits** and **faction crest icons**, one at a time,
until you run out of Gemini image quota. It attaches to a Brave you launch — it
**never sees your password** and downloads nothing but the finished PNGs.

**No dependencies.** It talks to Brave over the Chrome DevTools Protocol using
only the Python **standard library** (a tiny built-in WebSocket + `Runtime.evaluate`),
so it runs on the MSYS2 Python you already have — no pip, no venv, no Playwright.

Scope is deliberately **hero art + icons only**. Unit sprites and their upgrade
variants are **excluded** (you handle those separately). See
[`MISSING_ART.md`](MISSING_ART.md) for the full list + prompts;
[`manifest.json`](manifest.json) is the machine-readable version the script reads.

## Setup (once)

Nothing to install. Optional preflight:
```bash
cd tools/asset_gen
./setup.sh          # just checks python + brave and prints next steps
```

## Each run

1. **Fully quit Brave** — check Task Manager, no `brave.exe` left. **This is the
   #1 gotcha:** if any Brave is already running, `--remote-debugging-port` is
   silently ignored and the port never opens (you'll get "connection refused").
2. Launch Brave with the debug port on your normal profile:
   ```bash
   ./launch_brave.sh            # bash
   # or, in PowerShell:  .\launch_brave.ps1
   ```
   Make sure you're signed in at <https://gemini.google.com>.
3. Generate:
   ```bash
   python gemini_gen.py --dry-run      # preview what's pending
   python gemini_gen.py                # generate everything pending
   python gemini_gen.py --only hero    # portraits only
   python gemini_gen.py --only icon    # crests only
   python gemini_gen.py --only terrain # map terrain tiles only
   python gemini_gen.py --limit 5      # cap this run to 5
   ```

It **skips any file that already exists**, so when Gemini cuts you off just
re-run later — it continues where it stopped. `--force` regenerates existing
files.

## What it writes

| Kind | Files | Drops into the game? |
|---|---|---|
| Hero portraits | `assets/portraits/faction_<F>_<N>.png` (F 0–8, N 1–3) | Needs a tiny loader change (see below) |
| Faction crests | `assets/towns/crest_<F>.png` (F 0–8) | Needs a one-line lobby wire-up |
| Terrain tiles | `assets/terrain/<biome>[_<N>].png` | **Yes, drop-in** — same paths the engine already loads |

### Terrain tiles (`--only terrain`)

Regenerates the map ground tiles that were drawn as a *circular vignette on a
dark background* — the map hex samples the bright centre plus a dark ring, so
neighbours don't visually touch. These prompts force a **seamless, full-bleed,
edge-to-edge** texture (like `water.png`, which was always correct) so the tile
fills the whole hex. Nine biomes are covered: barren, corrupted,
corrupted_forest, flesh_zone, mountain, swamp, toxic, volcanic, wasteland. The
old vignetted PNGs were deleted, so a plain `--only terrain` run recreates them
in place — no engine change needed. Eyeball the results: if Gemini still frames
one with a border/vignette, `--force` that single file after tightening its
prompt in `manifest.json`.

Per `ART_DROPIN_MANIFEST.md`, the engine currently loads only one portrait per
faction (`faction_<F>.png`). Once these exist, the loader in
`src/core/Game_Core.cpp` (around the `m_portraitTex` load) and the lobby picker
need a small change to pick `faction_<F>_<N>.png` by hero index — ping me and
I'll wire it. The crests are the `crest_<F>.png` option from the same doc.

## Post-processing notes

- Gemini returns ~1024×1024 images on a solid background (not transparent). The
  game's portrait slots accept a framed bust, so that's fine as-is. If you want
  transparency for the crests, chroma-key them afterward.
- Bytes are saved to the `.png` path even if Gemini hands back JPEG — the game's
  image loader sniffs format by content, not extension.

## When Gemini changes its web UI

Every selector/text-match lives in the `CONFIG` block at the top of
[`gemini_gen.py`](gemini_gen.py). If a step stops working, the script drops a
screenshot + HTML into `_debug/` — open it, find the new selector, update
`CONFIG`. Nothing else needs to change.

## A note on the approach

This automates the Gemini **web UI**, which may run against Google's terms of
service. The sanctioned, more robust route is the **Gemini / Imagen API** (an
API key + a short HTTP script, no browser). If you'd rather go that way, say so
and I'll swap `gemini_gen.py` for an API client that reads the same
`manifest.json`.
