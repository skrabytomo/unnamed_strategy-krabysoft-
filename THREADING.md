# THREADING — getting the AI off one core

Status: **Phase 0 + Phase 1 landed** (2026-07-17). Phases 2–4 designed, not built.

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

Two nets instead:

**1. Run the real TSan under WSL.** `fullgame_sim` is deliberately free of
SDL/ImGui/OpenGL (see `CMakeLists.txt`) and contains the whole AI kernel —
`Pathfinder`, `CombatEngine`, `HexMap`, `MCTSHero` — needing only SQLite. So it
builds and runs under Linux, where TSan does exist:

```bash
wsl --install                      # not installed yet
cmake -B build-tsan -DCMAKE_CXX_COMPILER=clang++ -DSIM_TSAN=ON
cmake --build build-tsan --target fullgame_sim
./build-tsan/bin/fullgame_sim --games 20 --threads 8 --state-hash
```

`SIM_TSAN=ON` hard-errors on Windows rather than producing a link failure.

**2. A determinism differential (the Windows net).** `--state-hash` prints an
FNV-1a digest of the final state of every game plus a combined digest folded in
**job order, never completion order**. The rule:

> Same `--seed`, any `--threads` → **identical digest**. If it differs, threads
> are sharing state they should not.

Any race that matters perturbs state, so it trips this. It converts "reproduces
once an hour" into a deterministic, runnable check. TSan only catches races on
paths it actually executes; the digest catches *any* divergence. They are
complementary — neither replaces the other.

**Verified 2026-07-17** (`--factions 0 1 --games 2 --seed 42 --max-weeks 1`):

| Run | Threads | Digest |
|---|---|---|
| A | 1 | `0e7467861b014d57` |
| B (repeat of A) | 1 | `0e7467861b014d57` |
| C | 2 | `0e7467861b014d57` |

Per-game digests matched too (`de259e392d3a8a4a`, `9d510fbe9aa52281`). A vs B is
the check that would have **failed** before the RNG fix below — `random_device`
reseeded every run. A vs C is thread-independence.

**Caveat — the harness is expensive, for a fixable reason.** Measured
2026-07-17: **~38s for a single game of a single week**, one core pegged, and
cost is roughly linear in weeks (~40-55s per simulated week, ≈3s per hero-day).
A broad "50 seeds × 1-vs-8-threads" sweep is therefore impractical today.
`--threads N` claws back a factor of N since games are independent, but the real
cause is worth fixing:

> `FullGameSim.cpp` and `MCTSHero.cpp` both call `Pathfinder::find(...)` with
> the **default `maxCost = 999`**. On the sim's hardcoded Medium map (radius
> 114, ~39k tiles) that budget reaches essentially the whole grid, so every
> unreachable or distant goal costs a full-map A*. The live game caps this at
> 60 / 400 (`kAiPathHorizon`) precisely because it froze the main thread; the
> sim never got the same treatment.

This is the same pathology as the in-game freeze, in the offline tool. Capping
it is orthogonal to threading and would make the determinism sweep cheap enough
to run in CI. Note it will change sim results (and therefore hashes), so it is a
deliberate behaviour change to make on its own, not folded into a threading
commit.

---

## Phases

### Phase 0 — harness first ✅ LANDED
`--threads N` (runs whole games concurrently) and `--state-hash` on
`fullgame_sim`. Games are scheduled as jobs up front in a fixed order; each
worker writes only the result slot it claimed into a pre-sized vector, so no two
threads touch the same element and no reallocation occurs. Aggregation and all
DB writes happen after the join, on the main thread.

### Phase 1 — purity ✅ LANDED
- **`MCTSHero` takes `const HexMap&`.** It previously took a non-const `HexMap&`
  and its rollouts wrote `heroId` into the **real** map, never restoring it —
  while the header claimed the map was "read-only during rollout". `heroId`
  appeared exactly twice in that file, both **writes, never read**: nothing in a
  rollout consumed it. The lines did nothing but corrupt the caller's map. They
  are gone, and `const` now propagates through the whole call chain, which is
  what lets rollouts share one map across threads without locking.
- **The sim now seeds its RNGs.** `FullGameSim` called neither
  `DamageCalc::seedRng` nor `CombatEngine::seedTurnRng`. `DamageCalc`'s
  generator defaults to `std::random_device{}()`, so `--seed` governed worldgen
  but **not one damage roll** — the balance tool was never reproducible, and any
  win-rate it printed (including `*** IMBALANCED` flags) carried unmeasured
  run-to-run variance. Both streams are now seeded from `cfg.seed`.

  The generators stay `thread_local`, which is *correct* while a whole game runs
  start-to-finish on one worker: it seeds the stream it is about to use and no
  other game on that thread can interleave. **Phase 3 breaks that assumption**
  (one game's work split across threads) and will need the RNG passed in
  explicitly.

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
- **Cache the per-step candidate rescan.** Every step of every hero currently
  rebuilds the whole candidate list (scan of all towns, resources, objects,
  heroes) and re-sorts it. This is now the dominant cost and is **orthogonal and
  race-free** — likely a bigger raw win than threading. Worth doing regardless.

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
cmake --build build --target fullgame_sim -j4     # ~20s
cmake --build build --target unnamed_strategy -j4 # the game
```

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
- `MCTSHero`'s map writes were dead (`heroId` written, never read). Removed.
- TSan cannot exist on MinGW. Checked the lib dir directly.
- The RNGs are `thread_local` **on purpose** and that is correct until Phase 3.

**Start with:** Phase 2 (behaviour-identical, proves the pool safely), or the
uncapped-A* fix above, which is orthogonal, race-free, and probably the larger
raw win.
