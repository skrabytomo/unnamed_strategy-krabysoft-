# Missing-Art Manifest — placeholder renders to replace

Everything currently drawn with a procedural placeholder instead of real art,
with the exact file/slot the code reads and a Gemini prompt for each. Formats
are copied from the existing assets so new art drops in without code changes.

Faction index F: 0 HolyOrder · 1 CrimsonWardens · 2 Thornkin · 3 EternalEmpire ·
4 Bloodsworn · 5 Voidkin · 6 IronAssembly · 7 Amalgamate · 8 Convergence.

Shared house style for every prompt below:
> dark-fantasy strategy game art, painterly, high contrast, clean silhouette,
> transparent background, no text, no watermark, centered.

---

## 1. World-map object icons  →  `assets/icons.png`  (256×192 atlas, 8×6 grid of 32×32)

The atlas is one image; each object reads cell `(col = idx%8, row = idx/6-row)`.
**Generate each icon standalone at 256×256 on transparent background, then downscale
to 32×32 and paste into the listed cell.** These slots are currently blank, so the
object shows only a glow ring on the map.

| Slot | Cell (col,row) | Object | Icon prompt (append the shared style line) |
|------|----------------|--------|--------------------------------------------|
| 32 | 0,4 | Arena | crossed gladiator swords over a round sand pit, bronze |
| 33 | 1,4 | Artifact Merchant | a merchant's coin purse and a gem on a market stall awning |
| 34 | 2,4 | Choke Guard | a spiked iron portcullis blocking a mountain pass |
| 35 | 3,4 | Shipyard | a wooden boat hull on stocks with an anchor |
| 36 | 4,4 | Fishing House | a fishing hut on stilts with a hanging net and fish |
| 37 | 5,4 | Experience Well | a glowing blue stone well radiating starlight |
| 38 | 6,4 | Landmark | a weathered stone monument obelisk with runes |
| 39 | 7,4 | Cursed Ground | cracked black earth with green skull-wisps rising |
| 40 | 0,5 | Neutral Outpost | a small stone watchtower with a neutral grey banner |
| 41 | 1,5 | Witch Hut | a crooked swamp hut on chicken legs, green cauldron glow |
| 42 | 2,5 | Stables | a horseshoe and a rearing horse silhouette, wooden stable |
| 43 | 3,5 | Tree of Knowledge | a golden-leaved great tree with a glowing book at its roots |
| 44 | 4,5 | Pandora's Box | an ornate dark chest cracked open with chaotic multicolor light |

(Slots 0–31 and 45–47 already have art — do not regenerate.)

---

## 2. Summoned / neutral combat units  →  `assets/sprites/` (2928×352, 8 frames of 366×352)

Same animated-sheet format as `faction_F_tT.png`: frames 1-4 idle, 5-6 attack,
7 hurt, 8 dead. Transparent background, character faces RIGHT.

| File | Unit | Prompt (append the shared style line + sheet format line) |
|------|------|-----------------------------------------------------------|
| `summon_skeleton.png` | Skeleton (Necromancy raise) | a tattered undead skeleton warrior with a rusty sword and cracked shield |
| `summon_ghost.png` | Ghost (Wild Growth / Overgrowth) | a translucent pale-green forest wraith, wispy trailing form, glowing eyes |

Sheet-format line to append:
> sprite sheet 2928×352, exactly 8 frames of 366×352 side by side, consistent
> character across frames: frames 1-4 idle sway, 5-6 attack lunge, 7 damaged
> flinch, 8 collapse/dissipate.

---

## 3. World-map hero figures  →  `assets/sprites/hero_F.png` (2928×352, 8 frames of 366×352)

Optional but high-impact: heroes currently borrow their faction's tier-1 unit
sprite. A dedicated mounted/leader figure per faction reads much better. Same
sheet format as §2. One per faction (F = 0..8):

| File | Look |
|------|------|
| hero_0.png | HolyOrder: armored paladin-commander on a white steed, gold trim |
| hero_1.png | CrimsonWardens: bone-armored death knight on a skeletal horse |
| hero_2.png | Thornkin: antlered druid-warden riding a great stag |
| hero_3.png | EternalEmpire: spectral marshal on a ghostly warhorse |
| hero_4.png | Bloodsworn: red-armored warlord on an armored dire-boar |
| hero_5.png | Voidkin: hooded voidcaster floating on a rift-disc |
| hero_6.png | IronAssembly: steam-knight in a walking mech-frame |
| hero_7.png | Amalgamate: flesh-grafted overseer on a many-legged mount |
| hero_8.png | Convergence: crystalline synthesist on a hovering prism platform |

(Wiring note: hero sheets aren't loaded yet — ping "wire hero sprites" once the
files exist and the map renderer will prefer `hero_F.png` over the tier-1 stand-in.)

---

## 4. Siege art  →  see `ART_SIEGE.md`

Per-faction towers, walls, gates, moats and the named siege engines + upgrade
variants are specified there. Same 2928×352 sheet format for the engines/towers,
single 366×352 images for walls/gates/moat.

---

## Gemini prompt template (paste, then fill `<subject>` from the tables)

For an animated unit/hero/engine sheet:
> Game asset sprite sheet, 2928x352 pixels, exactly 8 frames of 366x352 side by
> side, transparent background, one consistent character across all frames facing
> right. Frames 1-4 idle sway, 5-6 attack action, 7 damaged flinch, 8 defeated.
> Subject: <subject>. Dark-fantasy strategy game art, painterly, high contrast,
> clean silhouette, no text, no watermark.

For a map/object icon:
> Single game icon, 256x256 pixels, transparent background, centered, bold
> readable silhouette for downscaling to 32x32. Subject: <subject>. Dark-fantasy
> strategy game art, painterly, high contrast, no text, no watermark.
