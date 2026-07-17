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

- [PLANNED] Remove `richRes.add(...9999)` from `FullGameSim.cpp` (cleanliness /
  honesty; affects only the offline sim, not gameplay).

---

## ⚠ The sim was never reproducible (fixed 2026-07-17)

Anything this roadmap or past balance passes concluded from `fullgame_sim`
numbers deserves a re-run. `FullGameSim` called neither `DamageCalc::seedRng`
nor `CombatEngine::seedTurnRng`, and `DamageCalc`'s generator defaults to
`std::random_device{}()` — so `--seed` controlled worldgen but **not a single
damage roll**. The same seed produced different results every run, and every
win-rate it printed (including the `*** IMBALANCED` >65% flags) carried
unmeasured run-to-run variance.

Both streams are now seeded from `cfg.seed`. Re-run any balance conclusion you
still rely on. See `THREADING.md`.

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
2. [FITS] **Tech scouting** — AI reads the player's town build queue and reacts.
   e.g. player builds a Mage Guild → AI prioritizes anti-magic / focuses the
   enemy caster in combat. Bounded, concrete, strong "how did it know" payoff.
3. [FITS] **Resource-hoarding prediction** — AI tracks the player's gold/turn and
   treasury. When the player is ~1 purchase away from a big building (e.g.
   Capitol), the AI times a mine raid to force emergency spending. High payoff.
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
assumed the AI runs synchronously on the render thread, one core, with rollouts
that scribble on the live map. All three assumptions are being removed:

- rollouts are now **pure** (`MCTSHero` takes `const HexMap&`) — they were
  silently corrupting the caller's map, and nothing ever read what they wrote;
- the AI is moving **off the render thread**, so its cost stops being frame time;
- rollouts are **embarrassingly parallel** — near-linear scaling across cores.

MCTS stays NO-FIT *today* and becomes viable at Phase 4. See `THREADING.md` for
the phases, the scheduling rule, and what still has to be true first.

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
