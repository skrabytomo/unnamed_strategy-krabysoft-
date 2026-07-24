# Balance data — tweak the numbers without recompiling

The game ships with all unit and building stats compiled in as defaults. At
startup it then reads the two files in this folder and **overrides matching
entries by `id`**, so you can rebalance the game by editing JSON — no build,
no code.

## Editable files (loaded at startup)

| File             | What it controls                                            |
|------------------|-------------------------------------------------------------|
| `units.json`     | Per-unit `hp`, `attack`, `defense`, `damageMin`/`damageMax`, `speed`, `range`, `shots`, `flying`, `vampiric`, `regenerates`, and `cost`. |
| `buildings.json` | Per-building `cost`, `weeklyIncome`, growth fields (`weeklyGrowth`, `growthA/B`, `growthBonus`, `growthMultPct`), and `minWeek`. |

Each entry carries its `name` next to the numbers so you can find what you want
by eye. Match is by `id` — **do not change an `id`**, or the override silently
stops matching and the compiled default is used instead.

## How to edit

1. Open `units.json` or `buildings.json`.
2. Change any number you like (e.g. make Squires cheaper, or a dwelling grow
   faster).
3. Relaunch the game. On success the log prints:
   `[BALANCE] applied overrides: N units, M buildings (from assets/data/)`

Safety: anything missing — a file, an `id`, a single field — falls back to the
compiled default. A partial or malformed file can only change what it explicitly
names; it can never brick the game. Only the fields listed above are read; the
rest of each entry (abilities, descriptions, prerequisites) is informational.

## Regenerating from current defaults

To reset these files to the game's current compiled values, run:

```
unnamed_strategy --export-content=assets/data
```

That re-dumps `units.json`, `buildings.json` (plus read-only reference dumps of
factions, resources, terrain and the asset manifest). Edit the two files above
afterwards.
