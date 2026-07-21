# Missing art — hero portraits & faction icons

Generated from `manifest.json`. This is the human-readable list the
`gemini_gen.py` generator works through. **Scope: hero art + icons only —
unit sprites and their upgrade variants are intentionally excluded.**

Every prompt below is sent to Gemini wrapped as:

```
Generate a single high-detail image, 1:1 square aspect ratio. <prompt> Art style: dark-fantasy strategy game art, painterly, high contrast, clean readable silhouette, dramatic rim lighting, plain dark background, no text, no words, no watermark, no border, centered composition.
```

## Hero portraits — `assets/portraits/faction_<F>_<N>.png`

The engine currently loads one face per faction (`faction_<F>.png`); these add
3 more each so heroes look different. Existing `faction_<F>.png` stays as the fallback.


### 0 — Holy Order

- **`assets/portraits/faction_0_1.png`** — Fantasy character portrait bust, facing forward: a radiant Holy Order paladin commander in white-and-gold plate armor, faint golden halo, noble stern face.
- **`assets/portraits/faction_0_2.png`** — Fantasy character portrait bust, facing forward: a Holy Order high priestess of light, white and gold robes, glowing holy staff, serene glowing eyes.
- **`assets/portraits/faction_0_3.png`** — Fantasy character portrait bust, facing forward: a zealous Holy Order crusader knight in a winged helm, sunlit white-gold armor, resolute expression.

### 1 — Crimson Wardens

- **`assets/portraits/faction_1_1.png`** — Fantasy character portrait bust, facing forward: a grim Crimson Wardens blood-knight in polished silver armor and a deep crimson cloak, disciplined cold stare.
- **`assets/portraits/faction_1_2.png`** — Fantasy character portrait bust, facing forward: a Crimson Wardens templar with faint self-inflicted ritual scars, silver-and-red armor, solemn devout expression.
- **`assets/portraits/faction_1_3.png`** — Fantasy character portrait bust, facing forward: a highland fortress captain of the Crimson Wardens, silver breastplate, crimson tabard, weathered scarred face.

### 2 — Thornkin

- **`assets/portraits/faction_2_1.png`** — Fantasy character portrait bust, facing forward: a Thornkin druid beastmaster wearing living-wood and leaf armor, moss and bark, calm wild green eyes.
- **`assets/portraits/faction_2_2.png`** — Fantasy character portrait bust, facing forward: a thorn-crowned Thornkin forest warden with branching antlers, vines and bark skin, fierce protective look.
- **`assets/portraits/faction_2_3.png`** — Fantasy character portrait bust, facing forward: a Thornkin symbiotic ranger bonded to a small beast on the shoulder, leaf-cloak, sharp attentive gaze.

### 3 — Eternal Empire

- **`assets/portraits/faction_3_1.png`** — Fantasy character portrait bust, facing forward: an Eternal Empire undead death-lord wearing a jagged bone crown, black-and-silver armor, hollow glowing eyes.
- **`assets/portraits/faction_3_2.png`** — Fantasy character portrait bust, facing forward: a pale Eternal Empire necromancer in flowing black robes with violet death-magic wisps, gaunt sinister face.
- **`assets/portraits/faction_3_3.png`** — Fantasy character portrait bust, facing forward: a skeletal Eternal Empire knight-commander with a ghostly second-soul aura, tarnished dark armor.

### 4 — Bloodsworn

- **`assets/portraits/faction_4_1.png`** — Fantasy character portrait bust, facing forward: a Bloodsworn cultist ascendant in dark-red robes covered in ritual scars, fanatical burning eyes.
- **`assets/portraits/faction_4_2.png`** — Fantasy character portrait bust, facing forward: a demonic Bloodsworn blood-priest mid-ascension, growing horns, crimson energy, cruel ecstatic grin.
- **`assets/portraits/faction_4_3.png`** — Fantasy character portrait bust, facing forward: a swamp-corrupted Bloodsworn blood-knight in blackened red plate, dripping crimson runes, menacing stare.

### 5 — Voidkin

- **`assets/portraits/faction_5_1.png`** — Fantasy character portrait bust, facing forward: a corrupted Voidkin void-elf warlock wreathed in purple void energy, pale skin, unsettling glowing eyes.
- **`assets/portraits/faction_5_2.png`** — Fantasy character portrait bust, facing forward: a Voidkin possession-sorcerer with swirling void-purple eyes and floating dark runes, sly threatening look.
- **`assets/portraits/faction_5_3.png`** — Fantasy character portrait bust, facing forward: a Voidkin corrupted nature shaman with twisted blackened antlers, decaying leaves, eerie violet glow.

### 6 — Iron Assembly

- **`assets/portraits/faction_6_1.png`** — Fantasy character portrait bust, facing forward: an Iron Assembly dwarven runesmith artificer with a mechanical arm, glowing runes, soot-streaked determined face.
- **`assets/portraits/faction_6_2.png`** — Fantasy character portrait bust, facing forward: an Iron Assembly engineer-commander in runic steam power-armor, brass and iron, glowing rune goggles.
- **`assets/portraits/faction_6_3.png`** — Fantasy character portrait bust, facing forward: an Iron Assembly forge-master holding a glowing blueprint, gears and pistons, gruff confident expression.

### 7 — Amalgamate

- **`assets/portraits/faction_7_1.png`** — Fantasy character portrait bust, facing forward: an Amalgamate flesh-metal hybrid warlord, biomechanical fusion of muscle and steel plating, cold artificial eye.
- **`assets/portraits/faction_7_2.png`** — Fantasy character portrait bust, facing forward: a grotesque Amalgamate organic-machine surgeon, exposed sinew and chrome tubing, unnerving stitched face.
- **`assets/portraits/faction_7_3.png`** — Fantasy character portrait bust, facing forward: an adaptive Amalgamate amalgam leader with shifting armored plates over living flesh, many glinting eyes.

### 8 — Convergence

- **`assets/portraits/faction_8_1.png`** — Fantasy character portrait bust, facing forward: an enigmatic Convergence hooded arcanist wreathed in shifting prismatic mirror-energy, face half in shadow.
- **`assets/portraits/faction_8_2.png`** — Fantasy character portrait bust, facing forward: a masked Convergence mirror-mage in reflective robes echoing many magic schools, calm inscrutable pose.
- **`assets/portraits/faction_8_3.png`** — Fantasy character portrait bust, facing forward: a Convergence void-hideout sorcerer surrounded by fractured mirror shards reflecting arcane runes.

## Faction crest icons — `assets/towns/crest_<F>.png`

Clean emblem/insignia per faction (the `crest_<F>.png` option from `ART_DROPIN_MANIFEST.md` §2). Square, emblem-style.

- **`assets/towns/crest_0.png`** (Holy Order) — Heraldic faction crest emblem for the Holy Order: a radiant sun-and-winged-shield insignia, gold and white, symmetrical.
- **`assets/towns/crest_1.png`** (Crimson Wardens) — Heraldic faction crest emblem for the Crimson Wardens: a silver tower flanked by a crimson drop of blood, symmetrical.
- **`assets/towns/crest_2.png`** (Thornkin) — Heraldic faction crest emblem for the Thornkin: an antlered tree-and-thorn sigil, deep green and bark-brown, symmetrical.
- **`assets/towns/crest_3.png`** (Eternal Empire) — Heraldic faction crest emblem for the Eternal Empire: a crowned skull over crossed bone scepters, black and bone-white, symmetrical.
- **`assets/towns/crest_4.png`** (Bloodsworn) — Heraldic faction crest emblem for the Bloodsworn: a chalice overflowing with blood beneath jagged horns, dark red and black, symmetrical.
- **`assets/towns/crest_5.png`** (Voidkin) — Heraldic faction crest emblem for the Voidkin: a corrupted eye within twisted antlers, void-purple and black, symmetrical.
- **`assets/towns/crest_6.png`** (Iron Assembly) — Heraldic faction crest emblem for the Iron Assembly: a runic cog-and-hammer sigil, brass and iron grey with glowing runes, symmetrical.
- **`assets/towns/crest_7.png`** (Amalgamate) — Heraldic faction crest emblem for the Amalgamate: a fused flesh-and-gear ouroboros, sickly grey-green and chrome, symmetrical.
- **`assets/towns/crest_8.png`** (Convergence) — Heraldic faction crest emblem for the Convergence: a prismatic mirrored hexagon radiating many-colored arcane rays, symmetrical.

## Explicitly NOT generated here

- **Unit sprites** (`assets/units/**`, `assets/sprites/faction_*_t*.png`) and their
  `_u` upgrade variants — excluded by request.
- **The `icons.png` resource atlas** (cells 32–37) is fine as-is. The odd-looking
  cells past 37 are unused/placeholder atlas slots, not referenced by the game, so
  regenerating them individually has no drop-in target — that's why 'icons' here
  means the standalone faction **crests**, which the loader can actually use.

