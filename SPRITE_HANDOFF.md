# SPRITE HANDOFF — reframe + unit visibility (read this first next session)

## Context
Ongoing task: reframe every animated unit sprite sheet (2928×352 = 8 frames of 366×352;
engine frame layout cols 0-3 idle, 4-5 attack, 6 hurt, 7 dead) into 8 clean semantic
frames, for all 9 factions. Then a **visibility pass** because many units read as
see-through / faded / dark against terrain in-game. The visibility pass is where we are
STUCK — several blind attempts made it worse.

**PROCESS RULES (important):**
- Do NOT generate preview images — the media API rejects them as "too large" and the user
  cannot see them, and it wastes turns. Work from the USER'S screenshots + in-game testing.
- When the user references a screenshot, **look at it directly and analyze it** before
  touching pixels. Last session a screenshot (Thornkin = all outlines, Iron Assembly = only
  bottom 2 tiers solid) never arrived readable and I kept guessing — ask them to re-send.
- All work goes to `main`. Never open a PR unless asked.

## Reframe status by faction (all pushed to `main`)
- **0 Holy Order** — reframed (earlier session). Visibility OK.
- **1 Crimson Wardens** — reframed. Visibility OK.
- **2 Thornkin** — reframed. **VISIBILITY BAD**: branchy creature reads as "just outlines,
  not filled." Outline commit `0efba0b` did NOT fix it.
- **3 Eternal Empire** — reframed. **t1 still needs user re-export** (baked checker on shield
  + "Skeleton Soldier" text). Visibility: user says "too see through."
- **4 Bloodsworn** — reframed. t3/t4 brightened (gamma lift); t4 has a pale silhouette
  outline + frame6 swapped to brightened-no-outline. Visibility: "several units see through."
- **5 Voidkin** — reframed. Visibility "almost okay." Residual checker specks in some
  crescent centers + behind t6 wings (baked into VFX glow; can't fully remove).
- **6 Iron Assembly** — reframed. Visibility: t1–t4 (dark steel) "faded", t5/t6 (gold) solid.
  Outline commit `0efba0b` did NOT fix. NOTE minigun-barrel-leak fixed (`c5f5b35`) by
  cutting at TRUE occ≈0 gaps, not the thin-barrel valleys.
- **7 Amalgamate** — NOT reframed (still original). Alpha-clean, looks OK. Earlier note: t5
  has frame-drift to watch for.
- **8 Convergence** — NOT reframed (still original). Alpha-clean, looks OK.

## Visibility problem — measured facts (the crux)
"See-through" is really THREE different problems; one treatment won't fix all:
1. **Genuine partial-alpha** — Thornkin tiers 53–90% solid (t6=53%); Bloodsworn dark tiers
   partial. Literally translucent.
2. **Sparse/branchy silhouette** — Thornkin frames 72–89% filled of silhouette bbox with
   thousands of enclosed transparent holes (thorn lattice). Terrain shows through gaps →
   "not filled."
3. **Dark low-contrast colour** — Iron Assembly t1–t4 measure 96–99% SOLID yet read
   "faded": dark steel blends with terrain (NOT alpha/fill). Same for Eternal Empire and
   Bloodsworn dark tiers.

## Failed attempts (do not repeat blindly)
- **Attempt A** (reverted, uncommitted): solidify = alpha≥60→255 but ERASE alpha<60, then
  outline. BUG: erased the alpha 24–59 colour fill → Thornkin "just outlines, colour gone",
  Iron Assembly "faded". Restored via `git checkout`.
- **Attempt B** (committed `0efba0b`): dark 2px OUTER-silhouette outline only, no alpha
  change. User says Thornkin STILL "not filled", Iron Assembly STILL "faded". Outer outline
  alone is insufficient. **Consider reverting `0efba0b`** or keeping as a base under the real fix.

## DONE this session — renderer-side outline (the real fix, needs in-game tuning)
- **Implemented a uniform dark silhouette outline in the combat renderer**
  (`src/core/Game_Combat.cpp`, the alive-unit draw near the `dl->AddImage(tid, p0, p1, ...tint)`
  call, ~line 492). Technique: draw the same frame 8× at small offsets with a dark tint
  underneath the real sprite. Offset copies also darken lattice gaps so branchy bodies
  (Thornkin) read filled. **Builds clean.** Touches NO art. Reversible.
  - Tuning knobs: `const float o = sprW*0.022f + 1.0f;` (outline thickness ~2-3px) and
    `IM_COL32(12,10,16,205)` (colour/alpha). Skipped for ghosts.
- **Reverted the per-sprite outline** (`0efba0b`) via commit `6db1617` so Thornkin/Iron
  Assembly aren't double-outlined — visibility is now handled uniformly by the renderer.
- **NEXT SESSION:** user tests in-game. If outline too strong/weak → tune `o`/alpha. If the
  dark tiers (Iron Assembly t1-4, EE, Bloodsworn dark) still read faint, add a brightness/
  contrast lift to those sprites (reuse Bloodsworn t3/t4 gamma-lift). Consider adding the
  same outline to the WORLD-MAP unit draw if stacks look faint there too.

## Fallback fix if renderer outline is rejected (per problem — NEVER erase colour)
1. **Thornkin (branchy + partial-alpha):** (a) solidify by BOOSTING alpha (alpha>~20 → 255,
   never erase); (b) morphological CLOSE / dilate body 1–2px (grow colour from nearest body
   pixel) to thicken thin branches and shrink gaps; (c) optionally flood-fill enclosed holes
   with nearby body colour. Keep the dark outline. Goal: solid coloured creature.
2. **Iron Assembly t1–t4, Eternal Empire, Bloodsworn dark tiers:** brightness/contrast/
   saturation boost so bodies pop (reuse the gamma-lift used on Bloodsworn t3/t4).
3. **STRONGLY CONSIDER a renderer-side fix instead of editing 54 sheets:** a uniform outline
   shader / drop-shadow / ground-disc / rim-light, or a small unit size bump, where units are
   blitted. Grep `m_unitTex` in `src/core/Game_Combat.cpp` (combat draw) + the world-map unit
   draw. One reversible renderer change beats 54 destructive sprite edits.

## Combat question — ALREADY ANSWERED (no code change needed)
"Can ranged units melee after ammo runs out?" — YES on all four paths:
- Player `src/combat/CombatEngine.cpp:829-838` (`if range>0 && shotsLeft>0` shoot, else melee
  if adjacent). AI Passive `:1324/1338`, Standard `:1394`, Tactical `:1487/1540` all gate
  shooting on `shotsLeft>0` and fall through to melee when empty.
- No melee penalty (`DamageCalc.cpp` — no ranged-in-melee halving); retaliation correct
  (`DamageCalc.cpp:335`, only on melee). Keys off `range>0 && shots>0`.
- **Data caveat:** a unit redesigned ranged→melee that still has `range>0` but `shots=0` in
  `UnitDef` behaves as "always melee" (works, but zero its `range` for cleanliness).

## Pending user request (not started)
**Unit design-match check:** user built units from a sprite sheet; some intended-ranged are
now melee, etc. Diff `UnitDef` data (attack/range/shots roles) against the user's intended
design. Ask the user for the reference sheet / intended per-unit roles first.

## Reframe method + tooling (scratchpad is EPHEMERAL — recreate if gone)
Scratchpad (last session): `/tmp/claude-0/-home-user-unnamed-strategy-krabysoft-/e804b016-59b7-5214-b8e5-09ea0e5b9913/scratchpad/`
- `ccpack4.py`: `pack_map(SP, srcfile, sel, big)` — `sel` = list of `(x0,x1,cell)` mapping a
  source x-range to frame cell 0–7. Also `pack_map2(...deleak)`, `clean()`, `comps_of()`.
  `clean(pts,big)` keeps main mass + large comps (VFX crescents), drops far small embers.
- `visfix.py`: `outline_only(path, outline=(10,8,12), rad=2)` — current committed approach.
- `strip3.py`: `strip_checker3(path, seg_dist, endpoint_tol)` → `(im,A,B)`.
- Files: `f{F}t{T}_orig.png` (pulled asset), `_gentle.png` (working), `_packed.png` (output).

Key rules learned:
- **Sheets 4–8 are ALPHA-CLEAN (no checker).** The old checker-strip DAMAGED dark bodies —
  always reframe from the ORIGINAL; only strip where real baked checker exists (Bloodsworn
  t6 gray+pink behind flail; Voidkin t5/t6 behind poses/wings).
- **Cut at TRUE gaps (occ≈0), not thin-weapon valleys.** Weapons (minigun barrel, EE
  dragon-orb staff, Voidkin scythe, dagger) extend right of the body; the valley detector
  finds the thin-weapon gap and splits the weapon into the next frame (the Iron Assembly
  "barrel leaks into next frame" bug). Always cut AFTER the weapon.
- **Semantic 8 selection:** 2–4 idle + 2 attack + 1 crescent VFX + dead; drop duplicate
  idles / 2nd crescent / far-right extra idle.
- **Corner sparkle:** small white ✦ at ~x2850-2895, y273-348 of every sheet — erase before packing.

## Verification
- After sprite edits: measure solid-alpha % and interior-fill % — but the user must confirm
  IN-GAME (`cmake --build build -j4`, run, watch/AI battle). Do NOT rely on generated previews.
- Assets: `assets/sprites/faction_{0..8}_t{1..6}.png`. Current `main` tip when handoff written: `0efba0b`.
