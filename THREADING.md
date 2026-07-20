# THREADING — getting the AI off one core

Status (2026-07-20): **Phase 2 landed. The two pre-Phase-3 spike items are DONE
— Phase 3 (plan/apply split) is now the front of the queue.**

- **Prerequisite — DevLog race: FIXED.** `DevLog::lines()` handed out a reference
  to the shared vector while `gLog()` appended under a mutex. Replaced with
  `DevLog::snapshot()`, which copies under the lock; `s_silent` is now atomic.
  This had to land before any worker thread could call `gLog()`.
- **Phase 2 — parallel candidate A\* fan-out: LANDED.** Persistent pool in
  `src/core/WorkerPool.h` (singleton, threads created once, blocking
  `parallelFor`; the dispatching thread works too). One deviation from the design
  below, deliberate: the top candidate is still searched **serially first**. A
  blind fan-out of all 10 would be a *pessimism* — the serial loop short-circuits
  on the first hit, so the common case costs one A\* and fanning out costs ten.
  Only the failure path (a rejected 400-hex march search, then retries) is
  parallelised. Result is still the lowest reachable index, i.e. identical.
- **Phase 0/1 — gone, and not needed.** They were built on `fullgame_sim` and
  removed with it (2026-07-17): that simulator ran a separate, simpler
  reimplementation of the AI, not the real `doEndTurn`, so it tested the wrong
  thing.
- **Phase 4's real item, done first (it was worth far more than threading):**
  measurement showed pathfinding was ~99% of turn cost, so two race-free fixes
  landed ahead of Phase 3:
  1. **Per-turn path reuse** (`Hero::stepPath`) — A* was re-running on every
     move step to the same destination, ~85–100×/turn. **~2.3× faster turns.**
  2. **O(1) land-connectivity fast-reject** (`Game::rebuildLandComponents`) —
     a *failing* 400-hex A* explored the whole horizon before giving up (~76 ms
     a call). One BFS per turn labels land components; a different-component
     target is rejected by lookup. **Typical turn: ~1700 ms → ~50 ms.**

  Combined, a typical AI turn's pathfinding went from ~3900 ms to ~50 ms.

- **Spike items 1 & 2: DONE (2026-07-20).** Measured on 8-player XL watch-AI
  runs (~95 turns): **avg ~100 ms, typical turn 10–100 ms, max ~560 ms** of
  per-turn pathfinding — down from ~700 ms avg / ~2 s max. Four changes, and
  the biggest win was a *behaviour bug*, not a cache:
  1. **`bestReachable` was never set on the march-cache hit** — so the lock's
     forced 1e9 candidate score made `wantBoatForBestTarget` fire on every
     step, `aiTryBoat` swapped in a dock path, the hero stepped off its march
     path, and the divergence check wiped the cache. Result: the "cached"
     400-hex march search actually re-ran EVERY turn for EVERY marching hero
     (~25 long searches/turn late-game) while heroes yo-yoed toward docks.
     One line (`bestReachable = true` on the cache hit) took divergence wipes
     from 449 to 0 in a 4-minute run.
  2. **Per-turn unreachable-target memo** (`failedTargets`, local to a hero's
     turn): a failing search is memoised (keyed by target, valued by horizon)
     and skipped for the rest of the turn; the step-path reuse now keys on the
     *effective* top candidate (first not-known-unreachable), so a doomed
     leader no longer forces a re-search per step. Cleared on boat state
     change (reachability regime changed).
  3. **Cross-turn march-fail throttle** (`Hero::marchFailGoal/marchFailTurn`):
     a locked march goal whose 400-hex search failed is retried every 4th turn
     instead of every turn (a failing 400-hex A* explores the whole landmass —
     ~450-530 ms on XL). The hero works fallback targets between retries.
  4. **`m_seaComp` amphibious component map**: onBoat heroes now get the O(1)
     fast-reject too (Water traversable, disembark anywhere, so only
     Mountain/Barrier split components). Also: `marchGoalTurns` was
     incremented per STEP, so the "60-turn" lock timeout really expired after
     ~6-8 turns and every cross-map march re-locked + re-searched mid-march;
     it now counts once per hero-turn.

- **Phase 3 — now the front of the queue.** `doEndTurn()` still runs
  synchronously on the render thread. The workload left to move is ~100 ms
  typical / ~560 ms worst-case; the candidate rescan (~50 ms/turn) is now a
  comparable share of the remainder. The plan/apply split fixes the residual
  hitch *and* produces the pure `AiPlanner` kernel Phase 0's test harness
  needs.

The goal: the game freezes to 0 FPS on XL maps with 8 players while watching AI.
The endpoint: an AI with the CPU headroom to be genuinely smarter (see
`AI_ROADMAP.md`, "Project Omniscient").

---

## The premise correction that shapes everything

Parallelism alone **does not fix a freeze**. If the AI turn takes 4s and 8 cores
cut it to 0.6s, you still get a 0.6s hitch — and Watch-AI fires `doEndTurn()`
every turn, so 0.6s hitches back to back still read as "unresponsive."

Three different problems, three different fixes:

| Symptom | Fix | Needs cores? |
|---|---|---|
| 0 FPS / unresponsive | Get AI work **off the render thread** | No |
| AI turn takes too long | **Parallelize** the AI's internal search | Yes |
| AI not smart enough | Deeper search (live MCTS) | Yes — what cores are *for* |

## Why the AI can't just be moved to a thread

`doEndTurn()` (`Game_WorldMap.cpp`) *decides and mutates in the same breath*.
Inside the per-hero loop it writes tile ownership (`m_map.getTile()->heroId`),
resolves rival combat and **eliminates other heroes mid-iteration**, spawns new
heroes into `m_enemyHeroes`, and touches `m_camera`, `m_audio` and `gLog`.
There is no "think" phase to lift out — one has to be *created*.

The boundary is **not** "double-buffer the whole `Game` object". It is:

> **Pure parallel search kernel.** Workers read a frozen snapshot and return
> *plans* (goal + path + score). The main thread applies plans serially, in a
> fixed hero order. All mutation stays exactly where it is today.

Design principle, given no TSan on the shipping toolchain (below): **make races
impossible by construction, not detectable after the fact.** Workers take
`const&` only — the compiler is the enforcement mechanism. No mutexes in the hot
path means nothing to deadlock and no lock ordering to get wrong.

---

## Testing a bug class that hides

A race only appears when two threads collide at a precise timing overlap, which
differs every run. It shows up rarely and often vanishes when observed. The
normal loop (reason about it, build, watch it happen) structurally does not work.

**ThreadSanitizer does not exist on this toolchain.** MinGW UCRT GCC 16.1.0;
`/c/msys64/ucrt64/lib/` has ~1147 entries and **zero** sanitizer runtimes;
`-fsanitize=thread` fails to link. Upstream TSan supports Linux/macOS/BSD only
and never has supported Windows. Porting it is a compiler-runtime project — not
the move.

The original plan leaned on `fullgame_sim` as the test vehicle: it was
SDL/ImGui/OpenGL-free, so it could be built under WSL/Linux with real TSan, and
it carried a `--state-hash` determinism differential (same seed, any thread
count → identical digest). **That sim has been removed** (see status above), so
both nets are gone. This is a real hole in the plan and has to be filled before
Phase 3 lands threading — the two viable replacements:

1. **Extract the planner into its own SDL-free test target.** Phase 3 already
   pulls the AI planning out of `doEndTurn` into a pure `AiPlanner` kernel. Give
   that kernel a small headless `main` (real AI, unlike the deleted sim), build
   *it* under WSL/Linux with `-fsanitize=thread`, and drive many turns. This is
   the honest version of the old idea — it TSan-tests the code actually being
   threaded, not a reimplementation.
2. **A determinism differential on the real game.** Add a seeded
   `--watch-ai-test` run that fingerprints end-of-turn state; the digest for a
   seed must be identical at 1 vs N planning threads. Same principle as the old
   `--state-hash`, but measuring the shipping AI.

Design principle stands regardless: **make races impossible by construction**
(workers take `const&` only) so the detector is a backstop, not the primary
defence.

---

## Phases

### Phase 0 — test harness
**Not yet built.** The first attempt (a threaded `fullgame_sim` with
`--state-hash`) was discarded with that sim because it tested the wrong AI. The
replacement is option 1 above — a headless target around the real `AiPlanner`
kernel — and it can only be built once Phase 3 has extracted that kernel. Until
then, verification is the real game (`--watch-ai-test`, watch frames render) plus
option 2's determinism check.

### Phase 1 — purity (was landed on the sim, reverted with it)
Two fixes were made and then deleted along with `fullgame_sim`. Both are still
**true and worth re-doing** wherever the equivalent code lives, so they are
recorded here, not lost:
- **A rollout must not mutate the shared map.** `MCTSHero::selectGoal`/`rollout`
  took a non-const `HexMap&` and wrote `heroId` into the real map, never
  restoring it — while the header claimed "read-only during rollout". Those
  writes were never read back by anything; they only corrupted the caller's map.
  Any future rollout/lookahead evaluator must take `const HexMap&`. (The file is
  gone, so there is nothing to fix today — this is a rule for whatever replaces
  it.)
- **A `--seed` must reach *every* RNG.** `fullgame_sim` seeded no combat RNG at
  all: `DamageCalc`'s generator defaults to `std::random_device{}()`, so the
  seed governed worldgen but not one damage roll, and the tool was never
  reproducible. Whatever headless harness Phase 0 grows must seed
  `DamageCalc::seedRng` and `CombatEngine::seedTurnRng` from its seed. Those
  generators are `thread_local`, which is correct only while a whole game runs on
  one thread; **Phase 3 splits one turn across threads and must pass the RNG in
  explicitly.**

### Phase 2 — parallel candidate A* fan-out (behaviour-identical)
The AI tries up to 10 candidates serially, taking the first reachable. `costFn`
only *reads* `m_map`/`m_towns`/`m_roadHexes`, and nothing mutates during the
fan-out — so running those A* searches in parallel and taking the lowest
reachable index is **provably identical** to today's result. `Pathfinder::find`
is already pure (all state function-local) — verified, no change needed.

Use a **persistent** worker pool, created once. Spawning threads per turn would
erase the win. Mirror the existing pattern in `SimulatorWindow.h`.

### Phase 3 — AI off the render thread ← fixes the 0 FPS
Split the AI block into `plan → apply`:
- **Plan** (worker pool, `const` snapshot): per-hero goal + path + score.
- **Apply** (main thread, fixed order): every existing mutation, untouched.

`updateWorldMap()` dispatches the job and returns immediately; the frame loop
keeps rendering; apply happens on the frame the job completes.

**Known race to fix first:** `DevLog::lines()` returns a reference to the shared
vector with **no lock**, while `gLog()` appends under one. An AI thread logging
while the UI renders the log = reallocation under a reader = crash.

#### Conflict detection: disjoint reach (the scheduling rule)

Within one turn a hero can only touch hexes within its `movePool` of its start
(roads halve cost but never below 1/hex, so `movePool` steps is a safe **upper
bound** — never compute the radius from average cost). Inflate that disc by **+1**:
the AI picks up garrisons and recruits from towns at `distance <= 1`, so its
*influence* radius is `movePool + 1`, not `movePool`.

If two heroes' inflated discs are **disjoint**, nothing either does this turn can
touch the other: they cannot share a tile, cannot fight (combat here requires
same-tile), cannot contest the same mine or town. Their planning is independent
and can run concurrently.

**But disjoint discs are not sufficient on their own, and this is the trap:**
heroes of the *same owner* are coupled no matter how far apart they are —

- they spend from **one shared treasury** (`aiResources(ownerId)`), so two heroes
  300 hexes apart still both draw on the same gold in `aiPaidRecruit`;
- the supply-chain shuttle makes non-raider heroes target **their own raider's
  position**, so same-owner heroes read each other regardless of distance.

So the scheduling rule is:

> **Partition by owner.** Each AI player is a lane: its own treasury, its own
> heroes, its own shuttle logic. Within a lane, heroes are planned **serially**
> (preserving today's semantics exactly). Across lanes, build a graph with an
> edge between two players when any of their heroes' inflated discs overlap;
> **connected components run in parallel.**

This fits the actual problem shape: 8 players who start far apart give ~8
independent lanes — one per core — and lanes only merge late, when armies
converge, which is exactly when hero counts have thinned. It is deterministic:
the partition is computed from the frozen snapshot and lanes are applied in
fixed player order.

**What it does and does not buy.** It gives *safety* (no two heroes ever contend
for a tile) and *determinism*. It does **not** give bit-identical-to-serial
results, because scoring reads mutable global state at any distance: a hero
scores an enemy hero's tile as `value / distance`, so in the serial version the
second hero sees the first hero's **post-move** position, while in the parallel
version it sees the **start-of-turn** position. Targets can therefore differ
slightly from today's. That is the real behaviour change in Phase 3, it is
inherent to snapshot planning rather than to this scheduling rule, and it is
acceptable — but it must be stated, not discovered later.

### Phase 4 — spend the headroom
- **Live MCTS.** `AI_ROADMAP.md` marked it NO-FIT *specifically because* it
  "would freeze the real-time-rendered game for minutes." Phases 0–3 are exactly
  what retires that objection: rollouts are pure, off the render thread, and
  embarrassingly parallel.
- **Deeper horizons.** `kAiPathHorizon = 60` is purely a CPU concession.
- ~~**Cache the per-step candidate rescan.**~~ **MEASURED AND WRONG (2026-07-19).**
  This claimed the rescan was "the dominant cost" and "likely a bigger raw win
  than threading." Instrumenting one turn on an 8-player XL map says otherwise:

  ```
  cand-rebuilds=100  cand=34.8ms  path=3977.1ms   (cand 1%)
  cand-rebuilds=88   cand=31.4ms  path=4712.6ms   (cand 1%)
  ```

  The candidate rescan is **~30 ms/turn — about 1%**. **Pathfinding is ~99%, at
  3.3–4.7 SECONDS per turn.** Caching the rescan would have bought ~1%.

  **The real target is pathfinding volume.** A* runs per *move step*, not per
  turn: ~85–100 steps per turn, each recomputing a full path to the same target.
  The `marchPath`/`marchPathIdx` cache already avoids this for locked town
  goals; extending that reuse to *all* targets (recompute only when the target
  changes or the hero strays off the path) attacks the actual 99%. That is
  race-free and orthogonal to threading — it is the item that deserves the
  "worth doing regardless" label this bullet used to carry.

  Note this also reframes Phase 3: moving a 3–4 s/turn workload off the render
  thread stops it *blocking the frame*, but the turn still takes 3–4 s. Cutting
  the pathfinding first makes Phase 3 cheaper and the sim genuinely faster.

---

## Off-ramp

If Phase 3 proves hairy: **frame-spreading** (process K heroes per frame, resume
next frame, single-threaded) fixes the 0 FPS symptom with *zero* race risk. It
makes the AI neither faster nor smarter, so it is a fallback rather than the
plan — but Phase 1's plan/apply split is what makes it cheap to reach.

---

## Picking this up cold

Everything needed is in the repo — no prior session context required.

**Build note:** `cmake` and `ninja` live in `/c/msys64/ucrt64/bin`, which is
**not** on the default MSYS2 bash PATH. Prepend it or nothing builds:

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake -B build -G Ninja                            # reconfigure after file changes
cmake --build build --target unnamed_strategy -j4  # the game (the only target)
```

(The `sim_test` combat-balance target was removed along with `sim_main.cpp`;
`src/sim/` survives only as the in-editor SimulatorWindow's backend. Verify AI
work with `--watch-ai-test` + `--seed=N` for reproducible worlds.)

**Files for the phases still to come:**

| File | Change |
|---|---|
| `src/ai/WorkerPool.h` *(new)* | Persistent pool, created **once** — per-turn thread creation erases the win. Mirror the `std::thread` + `std::atomic` + `std::mutex` pattern already in `src/editor/SimulatorWindow.h:47-52`. |
| `src/ai/AiPlanner.h/.cpp` *(new)* | The pure kernel: `const` snapshot in, plans out. Workers take `const&` only — that is the whole safety argument. |
| `src/core/Game_WorldMap.cpp` | Split the AI block of `doEndTurn()` (starts ~line 1480) into plan/apply; dispatch async from `updateWorldMap()` (~line 920). The per-hero loop and its candidate fan-out are ~line 1862-2340. |
| `src/core/DevLog.cpp` | Fix `lines()` handing out an unlocked reference before any AI thread calls `gLog()`. |
| `src/ai/Pathfinder.cpp` | **No change needed** — already pure, all state function-local. Verified. |

**Do not re-derive these — they are settled:**
- `Pathfinder::find` is pure. Checked.
- TSan cannot exist on MinGW. Checked the lib dir directly.
- `DamageCalc`/`CombatEngine` RNGs are `thread_local` — fine for whole-game-per-
  thread, must be passed explicitly once a single turn spans threads (Phase 3).
- `fullgame_sim` was deleted on purpose (2026-07-17): it tested a fake AI. Don't
  resurrect it as the harness — build the Phase 0 target around the real
  `AiPlanner` instead.

**Start with:** Phase 3's `AiPlanner` extraction is the linchpin — it fixes the
0 FPS freeze *and* produces the pure kernel that Phase 0's real test harness
needs. Phase 2 (parallel A* fan-out) is a safe, behaviour-identical warm-up that
proves the worker pool first.
