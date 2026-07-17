# Siege Art Manifest — generation checklist

Everything sieges need art for, with file naming the code will look for once
sprites are wired in. All units use the standard sheet format
(**2928×352 px, 8 frames of 366×352**, same as `assets/sprites/faction_F_tT.png`).
Static structures are single images (**366×352**).

Faction index F: 0 HolyOrder, 1 CrimsonWardens, 2 Thornkin, 3 EternalEmpire,
4 Bloodsworn, 5 Voidkin, 6 IronAssembly, 7 Amalgamate, 8 Convergence.

## 1. Defensive towers (one per faction, in the game already as units)
`assets/sprites/tower_F.png` — animated sheet (idle 0-3, attack 4-5, hurt 6, dead 7)

| F | Name (in game) | Look |
|---|----------------|------|
| 0 | Seraphic Spire | white-gold spire, radiant glyphs, light beams from the tip |
| 1 | Bone Watchtower | stacked bone and grave-iron, green soulfire brazier |
| 2 | Bramble Tower | living tree-tower, thorned vines, glowing amber sap |
| 3 | Wraith Turret | ghostly black iron, spectral cannon, teal wisps |
| 4 | Blood Altar Tower | red stone altar-tower, blood channels, crimson glow |
| 5 | Void Lens | floating obsidian shards around a purple lens |
| 6 | Iron Flak Tower | riveted steel flak platform, smoke stacks, muzzle flash |
| 7 | Flesh Spitter | fleshy organic turret, sinew, bone spike launcher |
| 8 | Prism Array | crystalline prism cluster, refracted rainbow beam |

## 2. Fortifications (static, one set per faction)
- `assets/siege/wall_F.png` — wall segment (matches faction architecture)
- `assets/siege/wall_F_damaged.png` — cracked/burning variant (HP < 50%)
- `assets/siege/gate_F.png` — the gate tile
- `assets/siege/moat_F.png` — moat/ditch strip (water, thorns, lava, bone pit
  ... faction-flavored; default is water)

Wall flavor by faction: 0 white stone + gold trim · 1 bone-set masonry ·
2 living wood + bramble · 3 black iron ghost-lit · 4 red stone blood-veined ·
5 floating obsidian slabs · 6 riveted steel plate · 7 flesh-and-bone ridge ·
8 crystal-latticed alloy.

## 3. Siege engines (attacker side; in the game already as units)
`assets/sprites/engine_<key>.png` — animated sheet, same format.

Base archetypes (fallback art if a faction key is missing):
- `engine_catapult` · `engine_ram` · `engine_trebuchet` · `engine_tower`

Faction specials (exact in-game names):
| Key | Name | Faction |
|-----|------|---------|
| engine_divine_trebuchet | Divine Trebuchet | HolyOrder |
| engine_silver_trebuchet | Silver Trebuchet | CrimsonWardens |
| engine_living_tower | Living Tower | Thornkin |
| engine_bone_crusher | Bone Crusher | EternalEmpire |
| engine_blood_catapult | Blood Catapult | Bloodsworn |
| engine_void_caster | Void Caster | Voidkin |
| engine_iron_ram / engine_iron_catapult / engine_iron_trebuchet | Iron Ram / Catapult / Trebuchet | IronAssembly |
| engine_flesh_drill | Flesh Drill | Amalgamate |
| (base set) | Ram/Catapult/Trebuchet | Convergence |

### Upgrade variants (planned — art now, mechanics later)
Each engine gets one upgrade tier: `engine_<key>_u.png`. Visual language:
reinforced frame, glowing faction accents, banners. (Upgrade mechanics
will be a Bastion-tier building unlock.)

## 4. Generation prompt template (Gemini)
> Game asset sprite sheet, 2928x352, exactly 8 frames of 366x352 side by side,
> transparent background, consistent character across frames.
> Frames 1-4 idle sway, 5-6 attack action, 7 damaged flinch, 8 destroyed.
> Subject: <look from the tables above>, dark fantasy strategy game style,
> painterly, high contrast, no text, no watermark.

For static structures (walls/gates/moat): single 366x352 image, transparent
background, same style line.

## 5. Wiring status
- Towers, walls, gate, moat exist as GAMEPLAY today (per-faction stats).
- **Base siege art IS wired and rendering** now: `Game_Combat.cpp`'s `drawRun`
  lambda draws `m_wallTex[sf]` / `m_wallDamagedTex[sf]` / `m_moatTex[sf]` and
  the gate overlays `m_gateTex[sf]`. (This §5 previously said "uses generic
  placeholders" — stale; the base fort art landed.)

### 5b. ⚠️ Tall wall/moat variant — ART EXISTS, WIRING REGRESSED
`assets/siege/wall_0..8_tall.png` and `moat_0..8_tall.png` (18 files) are the
"puzzle-fit" taller fortification art. They are present in `assets/` but the
engine does **not** load or use them — the wiring (`m_wallTallTex` /
`m_moatTallTex` texture arrays + a `tallTex` param on `drawRun` preferring the
tall art for the undamaged wall state) was written in a prior session but that
change was **uncommitted and got discarded**. Grep confirms `m_wallTallTex` = 0
files in the tree.

To restore (a code task, verify with a live siege so alignment/size is right):
1. `Game.h`: `Texture m_wallTallTex[NUM_FACTIONS]; Texture m_moatTallTex[NUM_FACTIONS];`
2. `Game_Core.cpp`: load `wall_%d_tall.png` / `moat_%d_tall.png` into them.
3. `Game_Combat.cpp` `drawRun`: add a `tallTex` param; use it for the healthy
   (non-damaged) wall/moat state when `.ok()`, else fall back to the base tex.
Deferred deliberately: it can't be visually verified headlessly, so it should be
done when a siege battle can be watched on screen.
