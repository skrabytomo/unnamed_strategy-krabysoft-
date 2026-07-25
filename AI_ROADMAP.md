# AI Roadmap — "Project Omniscient" triage

Source: the *Information-Unfair AI Overhaul* design doc. Goal per that doc:
an AI that wins through **perfect information / omniscience**, never through
economic cheating ("cheats with God, not with gold").

This file triages that doc against what the engine actually is, so we build the
parts that fit and skip the parts that don't. Status legend:
`DONE` · `PLANNED` · `FITS` (worth doing, not started) · `NO-FIT` (doesn't
match the engine) · `LATER` (real but out of current scope).

---

## Reality check: the "economic cheat" is (mostly) already gone

The doc's sacred line — delete the `richRes` free-resource hack — targets
`FullGameSim.cpp`. That file is the **headless offline balance-testing binary**
(`fullgame_sim`), NOT the live game. The real game (`Game_WorldMap.cpp`) already
gives each AI its own honest resource pool (`m_aiResources`) and builds from it;
the mine economy was fixed so AI earns income legitimately. So no live player is
losing to a "corrupt banker" today.

- ~~[PLANNED] Remove `richRes` from `FullGameSim.cpp`~~ — moot: `fullgame_sim`
  was **deleted** 2026-07-17 (see below).

---

## ⚠ `fullgame_sim` was deleted, and its numbers were never trustworthy

The full AI-vs-AI simulator was removed 2026-07-17. Two reasons, both damning
for anything ever concluded from it:

1. **It ran a different AI than the game.** `FullGameSim.cpp` had its own
   `simHero`/`aiHeroMoveToward` — a separate, simpler reimplementation, not the
   real `doEndTurn`. Its balance numbers measured a bot that never shipped.
2. **It was never reproducible.** It called neither `DamageCalc::seedRng` nor
   `CombatEngine::seedTurnRng`, and `DamageCalc`'s generator defaults to
   `std::random_device{}()` — so `--seed` controlled worldgen but **not a single
   damage roll**. Every win-rate it printed (including the `*** IMBALANCED` >65%
   flags) carried unmeasured run-to-run variance.

So: treat any past `fullgame_sim` balance conclusion as unfounded. The
combat-only `sim_test` binary is untouched and still valid for its narrower job
(headless CombatEngine matchups). See `THREADING.md` for how AI-vs-AI testing is
meant to work going forward (a harness around the *real* planner).

---

## Already implemented (doc features we shipped earlier)

| Doc feature | Status | Where |
|---|---|---|
| Zero idle production (build + recruit every AI town each turn) | DONE | Game_WorldMap AI build tick |
| Trade calculator / retreat when losing | DONE | heroStrength ratios, retreat thresholds, 1.5x mine margin |
| Multi-hero coordination | DONE | army-consolidation shuttles, per-player raider/economic/defender roles |
| Neutral "fear" (skip fights it can't win) | DONE | mine/site strength-margin gating |
| Bot personalities | DONE | Explorer / Builder / Warrior / Mage |
| AI reads true state (no fog gate on AI) | DONE (implicit) | AI already reads m_enemyHeroes/m_towns/m_resources directly |

---

## The "Psychic Bundle" — fits the engine, bounded, high payoff

Delivers the doc's actual fantasy (AI that feels omniscient) without the
lag/refactor traps. Recommended build order:

1. [OBSOLETE] **Remove richRes** from the sim tool — moot since `fullgame_sim`
   (the tool that had it) was deleted 2026-07-17.
2. [SHIPPED 2026-07-25] **Tech scouting** — the AI reads rival built tech and
   reacts strategically (`Game_WorldMap.cpp`, grep "Tech scouting"):
   - *Pre-wall strike window*: rival towns with no Fort are boosted attack
     targets (×1.35 — open-field capture before the walls go up); Castle
     walls slightly deprioritized (×0.85). Kill shots/desperation untouched.
     March commits log `[SCOUT: pre-wall window]` — verified on seed 42:
     multiple AIs independently converge on the unwalled town.
   - *Caster-wary hunting*: if the hunted rival's owner has a T3/T4 mage
     guild, their hero counts as +15%/+25% effectively stronger, so the AI
     needs a real edge to engage a caster and disengages sooner (flows into
     aggressive/veryWeak/strRatio/dominant via nearHumanStr).
   - NOT included (engine mismatch): combat-side "focus the enemy caster" —
     spells are hero-cast here, the caster isn't a unit on the board.
   There is no literal build *queue* (builds are instant), so scouting reads
   built tech — same payoff.
3. [SHIPPED 2026-07-25] **Resource-hoarding prediction** — `aiTurnSetup`
   reads every owner's treasury against the big purchases still open to them
   (City Hall → faction Capitol, or Castle). At 50–99% of the cheapest such
   cost the owner is "hoarding" (`S.hoardingOwners`, logged weekly as
   `[SCOUT] P<n> is hoarding`), and rivals' mine scoring boosts that owner's
   GOLD mines to 200 (above key-resource denial, below own-build-blocker) —
   the raid lands in the window where it forces emergency spending. Below
   50% a raid barely matters; at 100% they buy next tick, window gone.
4. [SHIPPED 2026-07-25] **Strategic town abandonment** — `aiTownIsWriteOff()`
   (Game_WorldMap.cpp): a town is a write-off when a non-allied hero within
   8 hexes fields ≥3× the strength that would defend it (garrison + any
   rescuing hero) AND the owner has a better-developed town elsewhere. The
   last town and the most-built town are never written off — those fights
   are existential. Wired into two decisions:
   - weekly garrison recruiting skips a write-off town (gold flows to towns
     that will still exist next week) — logs `[SCOUT] P<n> writes off …`;
   - Town Portal defense skips a town that's hopeless even with the jumping
     hero's strength added — logs `[SCOUT] … lets <town> fall`.
   Scope note: this is the bounded version — no proactive garrison
   *evacuation* by passing heroes yet (heroes already pick up garrisons when
   standing on own towns, which covers most of that value).
5. [FITS] **Toward-optimal pathfinding** — we already cap the search horizon;
   tighten so the AI wastes no movement points on sub-optimal tiles.

---

## ⚠ MEASURED 2026-07-25 — games still don't resolve on water-separated maps

Three full headless games (`scripts/verify_ai.sh` / `--watch-ai-test`, current
`main`) exposed a real shipping issue: **map shape decides whether a game ever
resolves.**

| Seed | Shape / size | Outcome |
|---|---|---|
| 123 | Hexagon, Small | Resolved **week 8** by dominance — healthy |
| 7 | JebusCross, Medium | Week-80 **backstop**, 3 players still alive; 9 storms, 34 combats |
| 999 | **Ring**, Small | Week-80 **backstop**, **5 of 6 players still alive**, "winner" held 2 towns; only **6 combats and 11 march commits in 553 turns** |

Root cause on Ring (evidence in the seed-999 log): players are separated by
water, and **the AI never put a single hero on a boat in 80 weeks** — zero
naval lines, and 4 of 16 long-path calls failed outright. Land pathing
rejects the unreachable rivals, so the AI falls back to farming its own
island forever. The week-80 backstop then hands the win to whoever has the
biggest pile — which is why the game "ends" with almost everyone alive.

This is not the old "AI never targets towns" bug (targeting works — the AI
commits to marches at 40-66 hexes and the SCOUT tags fire). It is
specifically that **naval invasion is not wired into the AI's conquest
path**. Highest-value AI work remaining; it directly blocks the
"games resolve" requirement in `RELEASE_CHECKLIST.md`.

### Naval investigation (in progress 2026-07-25)

Instrumented the silent failure paths — that is what made these findable.
`[NAVAL]` now logs, once per hero per week, exactly why passage failed.

**Fixed so far:**
1. **Boat-score gate.** Candidate scores are distance-diluted (`add()` divides
   by hex distance), so an overseas capital 50 hexes out scores
   `600/sqrt(50) ≈ 85` — under the flat `score >= 150` gate that authorised
   buying passage. Naval conquest was impossible beyond ~16 hexes at any army
   strength. An enemy town now always justifies passage.
2. **Single-dock dead end.** `aiTryBoat` committed to the nearest dock by hex
   distance and gave up if unroutable. Now walks all docks nearest-first and
   takes the first reachable one (max 4 A* per turn).

3. **Unreachable docks were eating every attempt.** The map scatters coastal
   Shipyard *objects* across every island; sorted by hex distance they filled
   all attempts while the hero's own reachable town dock was never tried.
   Docks are now filtered by the O(1) land-connectivity check before any A*.

**STILL BROKEN — zero boats after four attempts.** Do not assume the naval
chain works; it does not. What is actually verified is only that heroes *want*
passage and *enumerate* docks. No `launched a boat` line has ever appeared.

Two dead ends, so nobody repeats them:
- **Not a search-budget limit.** `maxNodes` 120k and `maxCost` 4000 were both
  tried; neither produced a boat. Limits are back to 1200/30000.
- **A "5-6x perf regression" from those attempts was a measurement error** —
  Ring turns compared against Hexagon turns. Ring was always ~610-740ms.

**Latest measurement (dock-walk logging).** Heroes DO walk to docks and close
distance — Seraphiel 40→27 hexes, Unity Seeker 71→59, Briar Sovereign 61→39
across weeks 6→7. Then the walk logs **stop entirely at week 7** and no boat is
ever bought. So the hero abandons the dock walk partway: `wantBoatForBestTarget`
stops being true once something else outbids the overseas town (or the march
lock drops), and the hero turns back to local targets having wasted the trip.
The remaining fix is to make a committed dock walk STICKY — latch it like
`marchGoal` already latches an overseas target after boarding — rather than
re-deciding from scratch every turn. Ruled out by this measurement: oscillation
(distance genuinely shrinks) and the boat branch never being entered.

**Earlier step — stop guessing, instrument the decision.** The `[NAVAL]` log
proved insufficient because it reported `sameLandmass` for `docks[0]` (an
allied town) while the actual failure was against a different, nearer,
unreachable Shipyard *object* — so it read as "reachable but no route", which
is what sent two attempts chasing search limits. Log the *specific dock
coordinate that was tried and rejected*, and `Pathfinder::find`'s exit reason
(goal-unreachable vs cost-exceeded vs node-exceeded), before touching any
more constants.

Also note the AI only builds Shipyards from week ~6, so early-game
`0 dock(s)` complaints are correct behaviour, not a bug.

Repro: `./build/bin/unnamed_strategy --watch-ai-test=6:3:0 --seed=999`
(shape 3 = Ring). Self-terminates and logs `[WATCH-AI]`.

---

## Formalize what we already have (optional, low priority)

- [FITS/LOW] **Explicit "God View" accessor** — a named `AIGodView` wrapper over
  the game state. We already bypass fog for AI; this is just making the implicit
  explicit for clarity. Not urgent — no behavior change.

---

## Does NOT fit the engine (skip unless the engine changes)

| Doc feature | Why it doesn't fit |
|---|---|
| **MCTS in the live game, 500 sims/hero/turn** | ~~NO-FIT~~ → **RECONSIDERED, see below.** |
| **PublicState / AbsoluteState client-server split** | NO-FIT. Engine has one shared game state; AI already sees everything. Formal split is a large refactor for a property we already have by default. |
| **Height / high-ground vision system** | NO-FIT (for now). Hex model has no elevation. Large terrain-system addition. |

### Live MCTS — the NO-FIT is being retired (2026-07-17)

The original verdict was *"500 full-game rollouts per hero per turn would freeze
the real-time-rendered game for minutes."* That reasoning was sound but it
assumed the AI runs synchronously on the render thread, one core. The threading
plan removes that assumption — off the render thread, embarrassingly parallel
rollouts, near-linear scaling. So MCTS becomes viable at Phase 4.

Caveat: the old `MCTSHero` implementation is **gone** (deleted with
`fullgame_sim`, 2026-07-17). It was also a cautionary tale — its rollouts wrote
into the real shared map (state nothing read back), which any re-implementation
must not repeat: a lookahead evaluator takes `const HexMap&`. See `THREADING.md`.

MCTS stays NO-FIT *today* and becomes viable at Phase 4, once the planner is
extracted and parallel.

**Note the split in this roadmap's own goals:** the Psychic Bundle above
(tech scouting, hoarding prediction, town abandonment) is *informational* and
costs ~no CPU — it needs no threading at all and delivers the "how did it know?"
payoff on its own. Cores buy **search depth**, which is a different axis. Both
are wanted; conflating them is the main scoping risk.

---

## Real but out of current scope (LATER — these are features, not AI work)

| Doc feature | Note |
|---|---|
| Edge-bait / fog-radius ambush | LATER. Needs AI to model the player's exact vision radius and predict the chase path. ~1-2 weeks for one gimmick. |
| Backstab treaties / diplomacy AI | LATER. No diplomacy subsystem exists at all — net-new system. |
| New victory conditions (economic / artifact / cataclysm) | LATER. Each is a game-mode feature, not AI work. Reasonable someday; unrelated to "smarter AI." |
| Auto-artifact synthesis (instant set detection) | LATER. Small, but depends on artifact-set mechanics we haven't built. |

---

## One-line summary

Build the **Psychic Bundle** (tech scouting, hoarding prediction, town
abandonment, optimal pathing) + delete the sim-only richRes. Skip live-game
MCTS and the state-split refactor. Defer diplomacy / new victory conditions /
elevation to their own future feature work.
