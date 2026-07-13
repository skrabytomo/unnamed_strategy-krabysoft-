# CONQUEST MODE — persistent hideout progression game mode

Status: design LOCKED (2026-07). Phase 1 in progress.

Core loop: pick hero → fight through a generated near-linear map → win battles →
earn XP/gold/gems/chests → build a persistent unit collection → upgrade units via
faction keys (Path A/B choice) → push Arena for rank → weekly ladder reset.

## 1. Mode structure

| Piece | Design | Builds on |
|---|---|---|
| Entry | Main menu → "Conquest" | Game_MainMenu (menuMode 7) |
| Hero | any unlocked hero; level/skills/spells persist in DB forever | HideoutDB (same sqlite file) |
| Map | 1 generated map per week (seed = ISO week number → identical for everyone that week); ~15-20 encounter nodes in a mostly linear chain + 2-3 side branches guarding bonus chests | WorldGen |
| Combat | standard engine; enemy strength scales with node depth × collection power | CombatEngine + ArmyBuilder |
| Town | one hideout town, gold-upgradable: dwellings ↑ weekly unit drip, walls ↑ arena def bonus, mage guild ↑ spell unlocks | town systems |

## 2. Currencies

| Currency | Earned | Spent |
|---|---|---|
| XP | victory: 50 × nodeTier × (1 + streak×0.1) | hero levels → skill/spell picks |
| Gold | battles, quests, nodes | city upgrades, direct unit purchase |
| Gems (mode-only) | daily/weekly quests, arena, milestones | hero respec, buying chests |
| Keys (9 faction kinds) | boss nodes, weekly quest, arena ranks | unit path upgrades (§5) |

## 3. Chests & collection

| Chest | Source/cost | Contents |
|---|---|---|
| Wooden | daily quest | 5-15 units T1-T2, 1 faction |
| Iron | weekly quest / 50 gems | 10-20 units T1-T4, 2 factions |
| Golden | arena win streak / 150 gems | 15-30 units T1-T5 + 1 random key |
| Grand | weekly arena top rank | 30+ units, T6 chance, 3 keys, gems |

Units land in a persistent collection pool (defId → count). Team assembly: pick
any 6 stacks from the pool, cross-faction freely. **Map battles cost real
casualties** (units leave the pool). Chests refill it.

## 4. Quests

| Type | Examples | Reward |
|---|---|---|
| Daily ×3 (random) | win 2 battles / win with 3+ factions in team / clear a side node | Wooden chest + 10 gems + gold |
| Weekly ×3 (fixed pool) | clear 10 nodes / 5 arena wins / open 4 chests | Iron chest + 1 key + 50 gems |

## 5. Unit upgrades (keys)

9 factions × T1-T5 = 45 upgrade slots. Pay faction keys → permanently choose
Path A or Path B for that tier; all owned/future units of that tier become that
variant. T6 has no paths (already ultimate). Respec costs gems.
Key cost per tier: T1=1, T2=2, T3=3, T4=5, T5=8 → 19/faction, 171 total.

## 6. Arena — NO CASUALTIES

Arena is exhibition: full unit restore after every fight, win or lose.
Progression is never lost in arena (matchmaker guarantees a close fight; losing
units there would kill the mode). Real attrition exists only on the map.

| Rule | Value |
|---|---|
| Opponent | ArmyBuilder army targeting yourPower × rand(0.95-1.15) |
| Entry | free 3×/day; extra tries cost gems |
| Win | rank points + gold; streak → Golden chest |
| Ladder | weekly point reset; top rank = Grand chest + banner cosmetic |
| Opponent flavor | seeded "ghost" snapshots of collection armies (offline, no netcode v1) |
| Power formula | Σ(count × tier² × pathBonus) + heroLevel×10 |

## 7. Build phases

| Phase | Content | Status |
|---|---|---|
| 1 | Mode entry, persistent hero, weekly linear map, XP/gold, DB schema | DONE |
| 2 | Chests + collection pool + team assembly UI | DONE |
| 3 | Quests + gems | DONE |
| 4 | Keys + path-upgrade screen | DONE |
| 5 | Arena + weekly ghost ladder | DONE |

All five phases implemented. Possible future polish: town gold-upgrade screen
(dwelling drip / walls / mage guild), leaderboard cosmetics, real ghost-army
snapshots of other players (currently generated power-matched opponents).

## DB schema (added to the existing hideout sqlite file)

```
conquest_hero(id, name, faction, classId, level, xp, attack, defense,
              skillsBlob, spellsBlob)
conquest_collection(defId INTEGER PRIMARY KEY, count INTEGER, pathChoice INTEGER)
conquest_currencies(gold, gems)          -- single row
conquest_keys(faction INTEGER PRIMARY KEY, count INTEGER)
conquest_quests(id, type, param, progress, target, expiry, claimed)
conquest_arena(week, points, entriesToday, lastEntryDay)
conquest_map(week, nodeStateBlob)        -- cleared/available per node
```
