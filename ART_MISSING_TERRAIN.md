# Terrain art status (2026-07-25 — regen batch landed)

The renderer (`src/world/HexMapRenderer.cpp`) loads terrain as **contiguous
variants from `_0`**: `TYPE_0.png`, `TYPE_1.png`, … and **stops at the first
gap**. Up to `MAX_VARIANTS=24` per type. A missing `_0` falls back to legacy
`TYPE.png`; every type has at least one texture, so nothing renders untextured.

## ✅ 2026-07-25 — the 23-tile Gemini regen batch is on `main`

All 23 manifest terrain targets exist on disk (swamp/volcanic/barren/
wasteland/corrupted_forest/flesh_zone ×3, mountain ×2, corrupted+corrupted_0,
toxic_0). Generated at 1024², via `tools/asset_gen/gemini_gen.py` (logged-in
Brave path).

## ✅ The "black gaps between tiles" artifact — root cause was NOT the art

Diagnosed 2026-07-25: hex UVs only sample the inscribed r=0.5 circle of the
texture (corners never sampled), and measured edge-rim brightness matches tile
centers (no vignette) — the art was innocent. The real cause: terrain textures
were loaded **without mipmaps**, so ~40 px map-zoom hexes minified 1024² art
~25× with plain `GL_LINEAR`, speckling near-black texels along hex edges.
Fixed by enabling the mip chain in `HexMapRenderer.cpp` (commit `a65556f`).
**Don't regenerate art to fix seams/gaps — that class of bug is renderer-side.**

## Current variant count per type

| Type | variants (`_0`…) | to reach 3 |
|---|---|---|
| plains     | 1 | `_1 _2` (in manifest) |
| forest     | 1 | `_1 _2` (in manifest) |
| highland   | 1 | `_1 _2` (in manifest) |
| corrupted  | 1 | `_1 _2` (in manifest) |
| toxic      | 1 | `_1 _2` (in manifest) |
| rocky      | 1 | `_1 _2` (in manifest) |
| sacred     | 1 | `_1 _2` (in manifest) |
| industrial | 2 | `_2` (in manifest) |
| mountain   | 2 | `_2` (in manifest) |
| water      | 2 | `_2` optional — mirrored-repeat scroll hides repetition |
| swamp, volcanic, barren, wasteland | 3 | — healthy |
| corrupted_forest, flesh_zone | 3 | — healthy |

## To generate — manifest is ready, just run it

The 16 remaining variants above were **added to
`tools/asset_gen/manifest.json`** (2026-07-25), same full-bleed style block as
the landed batch. Next run picks them up automatically:

```bash
cd tools/asset_gen
python gemini_gen.py --only terrain      # skips everything that exists
```

When they land, every land type has 3 variants and this file is done.
