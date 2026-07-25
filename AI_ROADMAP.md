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

1. [PLANNED] **Remove richRes** from the sim tool (trivial).
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
4. [FITS] **Strategic town abandonment** — AI evaluates each town's
   defend-cost vs income; pulls troops from a remote low-value town instead of
   dying to defend it, redeploying to the real fight.
5. [FITS] **Toward-optimal pathfinding** — we already cap the search horizon;
   tighten so the AI wastes no movement points on sub-optimal tiles.

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
