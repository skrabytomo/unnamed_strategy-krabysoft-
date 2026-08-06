# Art Drop-In Manifest — playtest gaps (2026-07)

Precise targets for the art gaps found in the latest Watch-AI session, so
Gemini output drops in with **zero renaming**. Every path is relative to the
repo root. The loader reads these exact names; match them and it just works.

House style (keep consistent with sprite_brief.md):
> dark-fantasy strategy game art, painterly, high contrast, clean silhouette,
> no text, no watermark, centered, transparent background (PNG) where noted.

---

## 1. Hero portraits — only 1 per faction today (highest priority)

**Problem:** every hero of a faction shows the same face because only one
portrait exists per faction. The setup lobby, world-map hero list, and the
(planned) right-click hero sheet all read the same file.

**Current (loaded):** `assets/portraits/faction_<F>.png`  (F = 0..8)
- Square, ~256×256 or larger, transparent or framed bust. One per faction.

**Wanted:** several portraits per faction so different heroes look different.
Two options — pick one and tell me, I wire the loader to match:

- **Option A (simplest):** `assets/portraits/faction_<F>_<N>.png`
  (F = 0..8 faction, N = 0..k hero index). e.g. `faction_4_0.png`,
  `faction_4_1.png`, … The engine picks by hero index. Keep `faction_<F>.png`
  as the fallback.
- **Option B (named heroes):** `assets/portraits/<hero_name>.png` with a small
  name→file table. More work, only if you want specific named heroes.

Suggested count: 3–4 per faction = 27–36 files. Same square format as the
existing `faction_<F>.png`.

| Faction | F | Folder prefix |
|---|---|---|
| Holy Order      | 0 | faction_0_* |
| Crimson Wardens | 1 | faction_1_* |
| Thornkin        | 2 | faction_2_* |
| Eternal Empire  | 3 | faction_3_* |
| Bloodsworn      | 4 | faction_4_* |
| Voidkin         | 5 | faction_5_* |
| Iron Assembly   | 6 | faction_6_* |
| Amalgamate      | 7 | faction_7_* |
| Convergence     | 8 | faction_8_* |

---

## 2. Faction town crest — now uses town art (good), confirm dimensions

**Current (loaded, used as the setup-lobby crest):**
`assets/towns/faction_<F>.png`  (F = 0..8)

These already exist. The setup lobby now shows them as the faction crest
(instead of a unit sprite). If you want a **larger / dedicated crest** distinct
from the full town scene, add:
`assets/towns/crest_<F>.png` (square, ~128×128, emblem-style) and tell me —
I'll point the lobby at the crest and keep `faction_<F>.png` for the town scene.

---

## 3. Resource icons — confirm the atlas slots

Resource icons are read from a single atlas: `assets/icons.png`, sliced as an
**8-column × 6-row** grid. The six resources live at atlas indices **32–37**:

| Index | Resource |
|---|---|
| 32 | Gold |
| 33 | Iron |
| 34 | Faith Stones |
| 35 | Blood Essence |
| 36 | Verdant Sap |
| 37 | Mercury |

(index → col = idx%8, row = idx/8). If you regenerate `icons.png`, keep the
grid layout and those six slots, or tell me the new layout and I'll update the
slicing constants.

---

## 4. Multiple hero portraits also feed the right-click hero sheet (planned)

When the "right-click hero → artifact/stats sheet (classic-strategy-style)" UI is built,
it will reuse the same `assets/portraits/faction_<F>_<N>.png` files as a large
portrait. No extra art needed beyond #1 — just flagging so the portraits are
generated at a size that still looks good enlarged (≥256×256 recommended).

---

## Not art — engine work queued separately (for reference)

These came up in the same session but are code, not art (tracked for when you're
back from asset generation):

- Siege: units rendering on top of castle walls; walls taking too much damage
  from normal units (siege engines should breach).
- AI: idle players, no gold-trading / creature-buying at 56k gold, no AI-vs-AI
  attacks, per-owner map colors still reading red.
- Naval: shipyards built but ships unused / no sailing; make water travel on a
  ship faster than land.
- Town UI: no "already built" indicator.
- Kingdom overview: make it visual (mines/towns/pictures), not a text list.
- Town/hero pickers: show icons, right-click unit overview.
