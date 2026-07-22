# Missing terrain variants — after the low-quality cleanup (2026-07)

The renderer (`src/world/HexMapRenderer.cpp`) loads terrain as **contiguous
variants from `_0`**: `TYPE_0.png`, `TYPE_1.png`, … and **stops at the first
gap**. If `TYPE_0.png` is missing it ignores every higher-numbered file and
falls back to the single legacy `TYPE.png`. So a kept tile at, say, `_23` with
no `_0` is **dead weight — the engine never loads it.** Up to `MAX_VARIANTS=24`
per type; a healthy type today has ~3.

**Good news:** nothing renders untextured — every type still has either usable
variants or a legacy `TYPE.png` fallback.

## ⚠ The catch: 7 types kept a good tile at the wrong index

You deleted the bad variants but kept a survivor at a **high index** (`_23`, and
`industrial` also `_10`). Because `_0` is gone, the loader skips them and uses
the flat legacy texture instead — you lost all variety on these.

**Quick win (no regeneration):** rename each survivor to `_0` and the engine
uses it immediately.

| Type | usable from `_0` | orphaned (ignored) | legacy `TYPE.png` | action |
|---|---|---|---|---|
| plains     | 0 | `_23`        | yes | rename `_23`→`_0`, then add `_1 _2` |
| forest     | 0 | `_23`        | yes | rename `_23`→`_0`, then add `_1 _2` |
| highland   | 0 | `_23`        | yes | rename `_23`→`_0`, then add `_1 _2` |
| corrupted  | 0 | `_23`        | yes | rename `_23`→`_0`, then add `_1 _2` |
| toxic      | 0 | `_23`        | yes | rename `_23`→`_0`, then add `_1 _2` |
| rocky      | 0 | `_23`        | yes | rename `_23`→`_0`, then add `_1 _2` |
| industrial | 0 | `_10`, `_23` | yes | rename one →`_0`, other →`_1`, add `_2` |

## Types that are fine (contiguous from `_0`)

| Type | variants | note |
|---|---|---|
| sacred | 1 | has `_0`; add `_1 _2` for variety |
| water  | 2 | mirrored-repeat scroll; add `_2` optional |
| mountain | 2 | add `_2` optional |
| swamp, volcanic, barren, wasteland | 3 | healthy |
| corrupted_forest, flesh_zone | 3 | healthy (no legacy fallback, but 3 variants) |

## To bring every type to 3 contiguous variants

After the renames above, the tiles still to **generate** (each 1:1, painterly,
edge-tileable, same style as the survivors):

- **2 each** for: plains, forest, highland, corrupted, toxic, rocky (`_1`, `_2`)
- **1** for industrial (`_2`)
- **2** for sacred (`_1`, `_2`)
- **1 each** (optional) for water, mountain (`_2`)

= **16 required** + 2 optional. These are square terrain tiles, not the
seamless-per-edge kind, so Gemini can do them — say the word and I'll add a
`kind:"terrain"` section to `tools/asset_gen/manifest.json`.
