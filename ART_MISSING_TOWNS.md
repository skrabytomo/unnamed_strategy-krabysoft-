# Missing-Art Manifest #2 — Town Screens & Upgrade-Path Sprites

Companion to `ART_MISSING.md` (icons/summons/heroes) and `ART_SIEGE.md` (siege).
This covers two gaps found during the sprite_brief.md audit:

1. **Unit upgrade-path (A/B) sprites don't exist** — every PathA/PathB unit
   currently renders using its faction's base tier-1..6 sprite (`faction_F_tT.png`).
   No visual distinction between the base unit and its two upgrades.
2. **No town-screen art at all** — building lists render as text/UI only, no
   HoMM3-style painted town screen with buildings placed in a scene.

Shared house style (same as sprite_brief.md):
> dark-fantasy strategy game art, painterly, high contrast, clean silhouette,
> no text, no watermark, centered.

---

## 1. Upgrade-path unit sprites (108 files)

Naming: `faction_F_tT_a.png` (Path A) / `faction_F_tT_b.png` (Path B), same
512×64 / 8-frame format as the base sheet. **Engine does not load these yet**
— `Game_Core.cpp:132` only requests `faction_F_tT.png`; wiring path-aware
loading is a separate code task once files exist.

For every faction/tier, take the base unit's prompt from `sprite_brief.md` and
append the Path A or Path B cue below (applies uniformly across all 6 tiers
of that faction):

| Faction | Path A cue (append to base prompt) | Path B cue (append to base prompt) |
|---|---|---|
| Holy Order | brighter gilded trim, ornate zealous detailing, faint gold light aura | heavier grey plate, battered/penitent look, no glow |
| Crimson Wardens | leaner build, sharper blades, red cloak more tattered/wind-swept | bulkier build, riveted heavier armor, banner/cloth added |
| Thornkin | paired/twin small companion motif (extra vine or leaf cluster) | larger, more gnarled bark mass, mossier and heavier |
| Eternal Empire | teal spectral glow intensified, ornate imperial filigree | bone-white and cracked, rawer/more feral undead look |
| Bloodsworn | brighter blood-rune glow, more elaborate ritual markings | thicker obsidian plating, blunter weapon, tankier stance |
| Voidkin | more translucent/phasing edges, brighter purple glow | more solid/anchored look, muted grey-purple, less glow |
| Iron Assembly | polished brass/copper accents, cleaner rivets | scrap-metal patchwork look, mismatched plates |
| Amalgamate | faster/leaner silhouette, more visible veins pulsing | bulkier fused mass, more bone plate coverage |
| Convergence | brighter chrome, more mirror-reflective panels | more organic tissue visible fused with chrome, bulkier |

Sheet-format line to append (same as existing summon-unit convention):
> sprite sheet 512×64, exactly 8 frames of 64×64 side by side, consistent
> character across frames: frames 1-4 idle sway, 5-6 attack lunge, 7 damaged
> flinch, 8 collapse/dissipate.

---

## 2. Town screen backgrounds + building art (9 backgrounds + ~99 building sprites)

Two-layer approach (true HoMM3 style — a flat single image can't be clicked
per-building, so buildings are separate transparent cutouts placed on a
background):

- **`town_bg_F.png`** — base scene background only (sky, ground, faction
  terrain), 1600×900, no buildings, this is what's visible before anything
  is built.
- **`town_full_F.png`** — reference/preview composite showing ALL buildings
  placed together at final layout, same 1600×900 canvas. Used as the layout
  reference when positioning the individual building cutouts below (art
  reference only, not loaded directly by the engine).
- **`building_F_<id>.png`** — one transparent cutout per building (Town Hall
  + 6 unit dwellings + faction support buildings), sized to its footprint,
  positioned at fixed (x,y) on `town_bg_F.png` when built. This is what
  actually gets clicked in-engine (recruit dialog on dwellings, hero
  recruitment on Town Hall/Tavern, etc.).

| F | Faction | Background palette (from sprite_brief.md) | Buildings needing cutout art |
|---|---|---|---|
| 0 | Holy Order | gold, white, silver | Town Hall, Squire Barracks, Paladin Hall, Crusader Chapel, Battle Cleric Sanctum, Holy Champion Spire, Archangel Cathedral |
| 1 | Crimson Wardens | deep red, black, dark leather | Warden's Hold, Scout Camp, Ranger Lodge, Hunter's Lodge, Berserker Hall, Warden's Tower, Warlord's Bastion |
| 2 | Thornkin | forest green, brown bark | Sprout Hollow (Hall), + 5 more dwellings (Thornkin Warrior/Forest Guardian/Treant/Elder Thornkin/Ancient Colossus) |
| 3 | Eternal Empire | bone white, teal glow | Imperial Throne (Hall), + 5 dwellings (Armoured Skeleton/Zombie Warrior/Death Knight/Lich/Eternal Emperor), Phantom Keep |
| 4 | Bloodsworn | dark red, obsidian, blood runes | War Hall, + 5 dwellings (Blood Warrior/Berserker/Blood Champion/Oracle/Bloodsworn Avatar) |
| 5 | Voidkin | deep purple, black, void wisps | Void Nexus (Hall), Wisp Hollow, + 4 more dwellings, Wraith Spire |
| 6 | Iron Assembly | steel grey, copper, bronze | Forge Hall, + 5 dwellings, Titan Assembly |
| 7 | Amalgamate | flesh pink, dark veins, bone | Grafting Hall, + 5 dwellings, Fleshwork Forge |
| 8 | Convergence | silver chrome, mirror-reflective | Harmony Hall, Resonance Spire, Unity Forge, + 3 more dwellings |

(Exact remaining building names per faction are in `src/town/BuildingRegistry.cpp`
— search each faction's `// ── FACTION NAME ──` block. Table above lists the
distinctive ones; T1–T3 base dwellings follow the same naming pattern.)

### Prompt template — background
> Game background illustration, 1600×900, painterly dark-fantasy strategy
> game art, high contrast, no text, no watermark. A <faction palette> themed
> town plot — open ground/sky only, no buildings yet, ready for structures
> to be placed on top. Faction: <name>.

### Prompt template — building cutout
> Game building sprite, transparent background, painterly dark-fantasy
> strategy game art, high contrast, clean silhouette, no text, no watermark.
> Subject: <building name/description>, <faction palette> style, sized to
> read clearly at town-screen scale.

---

## Wiring notes (code work, separate from art generation)
- Load `faction_F_tT_a.png`/`_b.png` when unit's `UpgradePath` is PathA/PathB,
  fall back to `faction_F_tT.png` if the path-specific file is missing.
- New `TownSceneRenderer` (or extend `TownScreen`) needed: load `town_bg_F.png`,
  stamp each built building's cutout at its fixed layout coordinate, hit-test
  clicks against each cutout's bounding box → open that building's existing
  recruit/build UI.
- Layout coordinates (x,y per building id per faction) need to be defined
  once `town_full_F.png` references exist to eyeball placement from.
