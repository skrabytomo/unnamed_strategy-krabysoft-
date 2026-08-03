# Terrain art status (2026-08-04 — REGRESSION FOUND: wrong art style)

The renderer (`src/world/HexMapRenderer.cpp`) loads terrain as **contiguous
variants from `_0`**: `TYPE_0.png`, `TYPE_1.png`, … and **stops at the first
gap**. Up to `MAX_VARIANTS=24` per type. A missing `_0` falls back to legacy
`TYPE.png`; every type has at least one texture, so nothing renders untextured.

## ❌ 2026-08-04 — the "23-tile Gemini regen batch" below is WRONG FORMAT

User report: "everything apart water fucked" (tiles don't match/blend). Traced
the full render pipeline (enum order, `s_terrainBase[]` array order, load loop,
UV sampling, `glBindTexture` call) — all internally correct, no code bug.
**Opened the actual PNG files and found the real cause**: every land-terrain
`_0` file from the batch below (`plains_0.png`, `forest_0.png`, and by
inspection almost certainly the rest of that batch) was generated as a
**standalone hex-icon/card**: a dark hex-shaped border baked into the pixels,
grey card-padding filling the square canvas outside the hex, and a decorative
sparkle in the corner. That's a UI-palette-icon style, NOT a seamless tileable
ground texture — the renderer expects a full-bleed square that blends
edge-to-edge with its neighbors (it clips a hex shape out of it via UV
sampling, it does not want the hex shape pre-baked into the art).

**`water_0.png` is the one correct example** — full-bleed, edge-to-edge,
continuous wave pattern with zero border/padding/icon elements. That's why
water is the only terrain that renders correctly: it's the only file actually
generated in the right format.

**Fix: regenerate every land-terrain file using water's style, not the
hex-card style.** Prompt correction:

> Seamless, full-bleed, edge-to-edge tileable ground texture, painterly style,
> filling the ENTIRE square canvas corner to corner. NO hex border, NO card
> shape, NO vignette, NO background padding, NO decorative icons/sparkles/UI
> elements of any kind — just continuous [grass/forest/etc.] texture that
> blends seamlessly with itself when tiled. This is a repeating ground texture
> for a game map, not a standalone icon.

Do NOT reuse the old prompt template that produced the hex-card versions —
whatever produced `water_0.png` correctly is the template to copy.

## ✅ The "black gaps between tiles" artifact — root cause was NOT the art

Diagnosed 2026-07-25: hex UVs only sample the inscribed r=0.5 circle of the
texture (corners never sampled), and measured edge-rim brightness matches tile
centers (no vignette) — the art was innocent. The real cause: terrain textures
were loaded **without mipmaps**, so ~40 px map-zoom hexes minified 1024² art
~25× with plain `GL_LINEAR`, speckling near-black texels along hex edges.
Fixed by enabling the mip chain in `HexMapRenderer.cpp` (commit `a65556f`).
**Don't regenerate art to fix seams/gaps — that class of bug is renderer-side.**
This diagnosis is still correct; it's a SEPARATE issue from the hex-card
format problem found above.

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
