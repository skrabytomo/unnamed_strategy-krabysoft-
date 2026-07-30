# CONQUEST MODE — persistent hideout progression game mode

Status: design LOCKED. All 5 phases IMPLEMENTED (2026-07).

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
| Gems (mode-only) | daily/weekly quests, arena, milestones, **gold exchange (25g:1gem)** | hero respec, buying chests |
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

**Balance fix (2026-07):** gems were the only chest currency and had a thin
income stream (quest rewards only, ~70-120/week), while victory gold
(100-400+/battle) had no sink besides the cheap recruit shop — Golden (150
gems) and Grand (400 gems) chests were a multi-week grind even for a player
winning constantly. Added a Gold→Gems exchange (25 gold = 1 gem) in the Gem
Shop, so banked battle winnings convert into real chest-buying power.

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

## 5b. Per-unit leveling (2026-07) — "level up your favorites"

Every unit TYPE in your collection tracks its own XP/level, separate from the
hero. **XP scales with usage**: deploying N units of a type into a Conquest
battle grants that type N XP (win or lose — using it is what counts, not
winning with it). Capped at level 20, +3%/level to attack and HP (max +60% at
20). Applied at deploy time in `Game_Conquest.cpp` (kept out of the shared
`ArmyBuilder` since regular skirmish units don't persist a level). Level and
XP-to-next shown in the Collection pool (with a tooltip) and the Battle Team
list.

## 5c. Infinite Conquest Level (2026-07) — the never-capped meta-track

Separate from both the hero's own level and per-unit levels. Fed by **battles
won** (`grantVictoryRewards`, 10×nodeTier XP) **and quest claims**
(`claimQuest`, 15 daily / 40 weekly XP) combined — genuinely uncapped, no top
level. Every level-up grants **1 key to a random faction**, giving unlocking
every unit-upgrade path a real, ever-growing income source instead of being
gated purely by chest-opening luck. Higher Conquest Level also scales chest
drop *sizes* (+4%/level, capped +150%) — a committed player's chests get
meaningfully bigger over time, not just a cosmetic level number. Shown in the
Conquest top bar next to gold/gems, with a tooltip.

## 5d. Town (2026-07) — the persistent hideout town, finally built

The "Town" row from the original mode-structure table (line 17) was designed
from day one but never implemented until now. Three gold-upgradable tracks,
each capped at level 10, accessed via the "Town" button in Conquest's bottom
bar:

| Track | Effect |
|---|---|
| **Dwellings** | Passive gold (40/level/day, collected on Conquest entry, capped at 7 days so idle time doesn't stack forever) **+** a free weekly chest (tier jumps at milestones: Wooden→Iron at lvl 3, →Golden at lvl 9, →Grand at lvl 10) **+** extra weekly quest slots (4 at lvl 5, 5 at lvl 10 — base is 3). |
| **Walls** | 1 perk point per level (10 max), spent on 6 permanent perks (5 ranks each, cost = rank in points): Unit Attack/HP/Defense (+4%/rank, applies to ALL your Conquest units, stacks with per-unit leveling) and Player Gold/XP/Chest-Luck (+5%/+5%/+3% per rank). Points never refunded. |
| **Mage Guild** | +8% hero spell power per level (max +80% at level 10), applied as a combat-stat boost to your hero in both map battles and Arena — a proxy until the game has a dedicated casting-power stat. |

Upgrade cost ramps: `500 + level² × 60 + level × 240` gold (500 → 4160 at
level 9→10). This is the long-term gold sink — previously gold only fed the
cheap recruit shop (§ above) and the gold→gems exchange.

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
| 6 | Per-unit leveling (usage XP) + infinite Conquest Level (keys/chest scaling) | DONE |
| 7 | Town: Dwellings (passive gold/chest/quest slots), Walls (perks), Mage Guild (spell power) | DONE |

All seven phases implemented. Possible future polish: leaderboard cosmetics,
real ghost-army snapshots of other players (currently generated power-matched
opponents), deeper Mage Guild integration with the shared spell/skill system.

## DB schema (added to the existing hideout sqlite file)

```
conquest_hero(id, name, faction, classId, level, xp, attack, defense,
              skillsBlob, spellsBlob)
conquest_collection(defId INTEGER PRIMARY KEY, count INTEGER, pathChoice INTEGER,
                     xp INTEGER, level INTEGER)   -- xp/level added 2026-07 (§5b)
conquest_currencies(gold, gems)          -- single row
conquest_state(key TEXT PRIMARY KEY, value INTEGER)   -- generic; also stores
                                          -- "conquest_level"/"conquest_xp" (§5c),
                                          -- "town_dwellings"/"town_walls"/
                                          -- "town_mageguild", "perk_*" (6 keys),
                                          -- "dwelling_gold_last_collect",
                                          -- "dwelling_chest_week" (§5d)
conquest_keys(faction INTEGER PRIMARY KEY, count INTEGER)
conquest_quests(id, type, param, progress, target, expiry, claimed)
conquest_arena(week, points, entriesToday, lastEntryDay)
conquest_map(week, nodeStateBlob)        -- cleared/available per node
```
