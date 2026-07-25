# Hideout / out-of-game progression

The hideout is the persistent meta-layer between runs: you earn **XP** by
playing, and spend it on permanent upgrades that apply to every future new
game. State lives in SQLite (`hideout.db` in the OS per-user data dir, so it
survives rebuilds and reinstalls). Screen: **F6** in game.

## Where XP comes from

| Source | XP | Repeatable |
|---|---|---|
| Win a battle | 50 | yes, every battle |
| Win a game (Easy / Normal / Hard) | 150 / 250 / 400 | yes, every game |
| Milestone: first battle won | 25 | once |
| Milestone: first town captured | 75 | once |
| Milestone: hero reached level 5 | 100 | once |
| Milestone: hero reached level 10 | 250 | once |
| Milestone: survived to week 10 | 100 | once |
| Milestone: won a game | 200 | once |
| Milestone: campaign completed | 300 | once |
| Campaign completion (direct award) | 200 | once |

One-time milestone total: **1050 XP**. Upgrade-tier milestones (Castle T1-3,
Barracks T1-2, Vault T1-2) pay **0** on purpose — they are *bought* with XP, so
refunding XP for them would be circular.

## What XP buys

| Branch | Tiers | Costs | Effect (cumulative) |
|---|---|---|---|
| Castle | 3 | 100 / 300 / 600 | +200 / +600 / +1300 starting Gold |
| Barracks | 2 | 150 / 400 | T1 hero +1 ATK; T2 also +1 DEF |
| Vault | 2 | 200 / 500 | T1 +1 Iron +1 Mercury; T2 +1 of each rare resource |
| Shrine | 1 | 250 | Hero starts knowing one extra (faction) spell |
| Sanctum | 1 | 400 | Hero starts with +10 max mana |

**Everything unlocked = 2900 XP.**

Unlocking Castle T2 + Barracks T1 + Vault T1 unlocks **Convergence**, the
secret 9th faction (`isConvergenceUnlocked()`).

## Pacing

A first full game on Normal — say ten battles won, a town captured, hero to
level 5, past week 10, then victory — pays roughly:

`10x50 (battles) + 250 (win) + 25+75+100+100+200 (milestones)` ≈ **1250 XP**

So the first game buys several tiers, and full unlock lands somewhere around
the third or fourth completed game. That is the intended shape: visibly
rewarding immediately, not finished in one sitting.

**Before 2026-07-25 the numbers were very different.** XP existed only as +50
per battle and +200 for the campaign, so maxing the tree took ~58 battles and
winning an entire game paid *nothing at all*. If you want to retune, these are
the knobs:

- `HideoutDB::milestoneReward()` — one-time milestone payouts
- `kWinXP[]` in `Game_Combat.cpp` (search "game won on difficulty") — per-win XP
- `m_hideout.addXP(50)` in `Game_Combat.cpp` — per-battle XP
- The `*_COSTS[]` arrays at the top of `src/ui/HideoutScreen.cpp` — tier prices

## Gotcha for anyone editing this

`completeMilestone()` is called **unconditionally** from gameplay — every
battle won re-fires `FIRST_BATTLE_WON`, every capture re-fires
`FIRST_TOWN_CAPTURED`. The XP bonus is therefore guarded by an
`isMilestoneComplete()` check so it pays exactly once. `tests/test_hideout.cpp`
asserts that ("fire the same milestone 10x, get paid once"). Run it with:

```bash
cmake -B build -DBUILD_TESTS=ON && ctest --test-dir build --output-on-failure
```
