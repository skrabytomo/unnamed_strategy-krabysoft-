# Missing terrain variants — after the low-quality cleanup (2026-07)

The renderer (`src/world/HexMapRenderer.cpp`) loads terrain as **contiguous
variants from `_0`**: `TYPE_0.png`, `TYPE_1.png`, … and **stops at the first
gap**. Up to `MAX_VARIANTS=24` per type. A missing `_0` (with no legacy
`TYPE.png`) would render untextured — but every type here has a fallback, so
nothing is broken.

## ✅ Rename quick-win — DONE

The cleanup left 7 types with their only good tile at a high index (`_23`,
`industrial` also `_10`), which the loader ignored. Those survivors were renamed
to `_0`/`_1` so the engine now uses them:

`plains_23→_0`, `forest_23→_0`, `highland_23→_0`, `corrupted_23→_0`,
`toxic_23→_0`, `rocky_23→_0`, `industrial_10→_0`, `industrial_23→_1`.

## Current variant count per type

| Type | variants (`_0`…) | to reach 3 |
|---|---|---|
| plains     | 1 | `_1 _2` |
| forest     | 1 | `_1 _2` |
| highland   | 1 | `_1 _2` |
| corrupted  | 1 | `_1 _2` |
| toxic      | 1 | `_1 _2` |
| rocky      | 1 | `_1 _2` |
| sacred     | 1 | `_1 _2` |
| industrial | 2 | `_2` |
| water      | 2 | `_2` (optional; mirrored-repeat scroll) |
| mountain   | 2 | `_2` (optional) |
| swamp, volcanic, barren, wasteland | 3 | — healthy |
| corrupted_forest, flesh_zone | 3 | — healthy (no legacy, 3 variants) |

## To generate

**15 required** (`_1`,`_2` for the seven 1-variant types + `_2` for industrial)
plus 2 optional (`water_2`, `mountain_2`). Square terrain tiles, painterly, same
style as the surviving tiles — Gemini-friendly. Say the word and I'll add a
`kind:"terrain"` batch to `tools/asset_gen/manifest.json`.
