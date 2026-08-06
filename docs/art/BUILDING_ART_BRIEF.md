# Building Art Regeneration Brief — ALL 9 FACTIONS

Full regeneration list. Every building name below is now final (matches
sprite_brief.md-driven unit renames, verified against `BuildingRegistry.cpp`).
Same format spec as `sprite_brief.md` for anything animated; buildings are
**static single images**, not sprite sheets.

## Format spec (paste into every request)

```
Generate a single static building illustration for a 2D strategy game town screen.

CANVAS: 512×512 pixels, transparent background (PNG)
STYLE: 16-bit pixel art, classic-strategy / Final Fantasy Tactics aesthetic,
       clean dark outlines, limited palette (~24 colours), no anti-aliasing,
       dithering only, isometric or 3/4 front view, centered, no text, no watermark
GROUNDING: base of the building touches the bottom edge of the canvas
```

Path A / Path B upgrade buildings = same base building, visually upgraded
(bigger, more ornate, added detail signaling the upgrade) — not a different
structure. Reuse the base prompt + append the faction's Path A/B cue from the
table below.

Faction Path A/B visual cue (append to every building prompt in that faction):

| Faction | Path A cue | Path B cue |
|---|---|---|
| Holy Order | brighter gold trim added, small banners/glow | heavier grey stone added, more fortified look |
| Crimson Wardens | sharper roofline, red banners added | bulkier stonework, reinforced look |
| Thornkin | extra vine/leaf growth, twin sapling motif | thicker gnarled bark, mossier |
| Eternal Empire | teal spectral glow added to windows/carvings | bone-white cracked stone, rawer |
| Bloodsworn | more blood-rune carvings glowing red | thicker obsidian plating |
| Voidkin | translucent/glowing purple energy added | more solid/anchored dark stone |
| Iron Assembly | polished brass/copper trim | scrap-metal patchwork additions |
| Amalgamate | more visible pulsing veins on walls | bulkier fused bone-plate additions |
| Convergence | brighter chrome panels | more organic tissue fused into structure |

---

## Faction 0 — Holy Order (gold, white, silver)

| Building | Produces | Prompt |
|---|---|---|
| Cathedral Hall | (Town Hall) | grand white-and-gold cathedral with tall spire, stained glass |
| Squire Barracks | Squire | small stone barracks with practice yard, silver banners |
| Paladin Hall | Paladin | golden-roofed hall with knight statues flanking the door |
| Crusader Chapel | Crusader | white stone chapel with a large cross carved above the entrance |
| Battle Cleric Sanctum | Battle Cleric | hooded-monk sanctum, cloisters, soft golden light from within |
| Holy Champion Spire | Holy Champion | tall white tower with a glowing halo-ring near the top |
| Archangel Cathedral | Archangel | massive cathedral with feathered-wing motifs carved into the facade, radiant light |
| Light Shrine | (support) | small open-air shrine, glowing light pillar, gold trim |
| Reliquary | (support) | ornate vault building housing a glowing relic behind glass |
| Sacred Sanctum | (capitol) | grand basilica, largest structure in the faction, gold dome |

## Faction 1 — Crimson Wardens (deep red, black, dark leather)

| Building | Produces | Prompt |
|---|---|---|
| Warden's Hold | (Town Hall) | fortified red-and-black keep, banners, watchtowers |
| Scout Camp | Scout | small leather-tent camp with a campfire and lookout post |
| Ranger Lodge | Ranger | wooden lodge with red cloaks hanging outside, bows racked on the wall |
| Hunter's Lodge | Hunter | rustic hunting lodge, animal pelts, trophy antlers over the door |
| Berserker Hall | Berserker | rough timber hall with crossed axes mounted above the entrance |
| Warden's Tower | Warden Commander | stone watchtower flying a crimson banner |
| Warlord's Bastion | Warlord | dark fortified bastion, spiked battlements, war banners |
| Death Altar | (support) | small dark stone altar, faint red glow |
| Warden's Brand Chamber | (support) | branding chamber, iron sigil over a forge |
| Warden's Citadel | (capitol) | large red-black citadel, tallest structure, war banners on every tower |

## Faction 2 — Thornkin (forest green, brown bark)

| Building | Produces | Prompt |
|---|---|---|
| Grove Heart | (Town Hall) | massive living tree with a hollow doorway, glowing moss |
| Sprout Hollow | Vine Sprite | small mossy burrow at the base of a tree, glowing vines |
| Briar Thicket | Thornkin Warrior | dense thorn-bush enclosure with a bark archway |
| Vine Den | Forest Guardian | vine-wrapped den built into a hillside, wooden shield motif |
| Guardian Grove | Treant | small grove of young trees with claw-branch silhouettes |
| Elder Circle | Elder Thornkin | ring of standing bark-covered stones, druidic |
| World Tree Root | Ancient Colossus | massive exposed root structure, mossy and ancient |
| Ancient Circle | (support) | stone circle, glowing green runes |
| Symbiosis Web | (support) | web of vines connecting several small trees |
| Ancient Heartwood | (capitol) | the largest living tree in the grove, glowing heart-shaped hollow |

## Faction 3 — Eternal Empire (bone white, teal glow)

| Building | Produces | Prompt |
|---|---|---|
| Imperial Throne | (Town Hall) | grand bone-white palace with teal-glowing banners |
| Skeleton Pen | Skeleton Soldier | fenced bone-yard with rusted weapon racks |
| Armoured Crypt | Armoured Skeleton | stone crypt with teal-glowing armor displayed outside |
| Zombie Gallery | Zombie Warrior | crumbling gallery/hall, tattered imperial banners |
| Death Knight Foundry | Death Knight | dark foundry with spectral teal forge-glow |
| Lich Keep | Lich | tall spired keep, hovering spectral lights around it |
| Emperor's Vault | Eternal Emperor | grand mausoleum-vault, crown motif above the door |
| Necropolis Gate | (support) | ornate gate into a bone-white necropolis |
| Monument of Eternity | (support) | tall teal-glowing obelisk monument |
| Eternal Citadel | (capitol) | largest structure, bone-white fortress with teal banners |

## Faction 4 — Bloodsworn (dark red, obsidian, blood runes)

| Building | Produces | Prompt |
|---|---|---|
| War Hall | (Town Hall) | obsidian-and-red war hall, blood-rune carvings on the facade |
| Cultist Pen | Cultist | tattered ritual tent camp, dark red banners |
| Blood Warrior Pits | Blood Warrior | sunken stone pit arena, obsidian weapon racks |
| Berserker Hut | Berserker | rough hut with blood-rune totems outside |
| Blood Champion Corral | Blood Champion | fortified corral, dark obsidian gate |
| Oracle Pavilion | Oracle | open pavilion with hanging blood-filled vials, ritual bones |
| Bloodsworn Avatar Shrine | Bloodsworn Avatar | massive dark shrine, demonic statue motif |
| Blood Altar | (support) | small stone altar with a pool of glowing red liquid |
| War Shrine | (support) | small war-totem shrine, red banners |
| Bloodspire Fortress | (capitol) | largest structure, spiked obsidian tower dripping with rune-glow |

## Faction 5 — Voidkin (deep purple, black, void wisps)

| Building | Produces | Prompt |
|---|---|---|
| Void Nexus | (Town Hall) | floating dark structure with a swirling purple void-portal core |
| Sprite Hollow | Void Sprite | small dark hollow with faint purple wisps floating around it |
| Scout Den | Void Scout | shadowy den half-phased into the ground, purple glow at the entrance |
| Stalker Arch | Void Stalker | jagged dark stone archway, crackling void energy |
| Mage Gate | Void Mage | ornate dark gate with a floating purple crystal above it |
| Wraith Spire | Void Wraith | tall translucent-looking spire, ghostly purple haze |
| Herald Rift | Void Herald | a tear in reality with dark crystalline structure around it |
| Rift Gate | (support) | small unstable purple portal framed in dark stone |
| Void Lens | (support) | floating dark lens/orb structure, purple light refracting through it |
| Void Core Nexus | (capitol) | largest structure, massive void-portal core with orbiting fragments |

## Faction 6 — Iron Assembly (steel grey, copper, bronze)

| Building | Produces | Prompt |
|---|---|---|
| Forge Hall | (Town Hall) | industrial steel-and-copper factory hall, smokestacks |
| Automaton Works | Automaton | small workshop with gear-arm parts stacked outside |
| Infantry Bay | Infantry Unit | steel hangar bay, riveted plating, shield racks |
| Clockwork Depot | Clockwork Warrior | gear-covered depot, piston machinery visible |
| Gunner Foundry | Gunner | foundry with brass cannon-barrel parts stacked outside |
| Steam Colossus Assembly | Steam Colossus | massive assembly hall, boiler stacks and steam vents |
| Iron Titan Dock | Iron Titan | huge industrial dock built for a quadrupedal war machine |
| Blueprint Vault | (support) | secure vault, blueprint schematics on the walls |
| Overclock Chamber | (support) | small chamber crackling with energy coils |
| Grand Megaforge | (capitol) | largest structure, towering multi-chimney forge complex |

## Faction 7 — Amalgamate (flesh pink, dark veins, bone)

| Building | Produces | Prompt |
|---|---|---|
| Grafting Hall | (Town Hall) | grotesque organic-architecture hall, fused flesh and bone walls |
| Crawler Vat | Crawler | small pulsating vat structure, fleshy exterior |
| Flesh Warrior Bay | Flesh Warrior | bay with bone-plate scaffolding, veins running along the walls |
| Brute Works | Brute | hulking organic structure, exposed rib-like beams |
| Behemoth Forge | Behemoth | large fused organic-metal forge, bone plating |
| Flesh Colossus Pit | Flesh Colossus | deep sunken pit, cannon-arm scaffolding visible |
| Apex Chamber | Apex | central chamber with tendrils and bone spires radiating outward |
| Flesh Vault | (support) | small vault structure, pulsing veins |
| Merge Chamber | (support) | chamber with fusing/merging organic-machine apparatus |
| Grand Fleshpit | (capitol) | largest structure, massive pulsating organic complex |

## Faction 8 — Convergence (silver chrome, mirror-reflective)

| Building | Produces | Prompt |
|---|---|---|
| Synthesis Nexus | (Town Hall) | sleek chrome-and-organic fusion structure, mirrored panels |
| Initiate Chamber | Initiate | small chrome chamber, seams of exposed circuitry |
| Soldier Lab | Soldier | clean lab structure, half-organic half-chrome paneling |
| Mirror Warrior Hall | Mirror Warrior | hall with fully mirrored reflective walls |
| Champion Spire | Champion | tall chrome spire, energy conduits running up its sides |
| Elite Gate | Elite | ornate chrome gate, sleek geometric design |
| Convergence Prime Forge | Convergence Prime | grand fusion forge, perfect chrome-organic symmetry |
| Resonance Well | (support) | small circular well radiating soft chrome light |
| Mirror Chamber | (support) | chamber lined with reflective mirror panels |
| Synthesis Hub | (capitol) | largest structure, central hub with radiating chrome-organic spokes |

---

## Shared/generic buildings (already have art — do not regenerate)
Fort, Market, Town Hall, City Hall, Mage Guild (4 tiers), Warehouse (3 tiers)
already have per-faction art at `assets/buildings/<name>/<name>_f{0-8}[_t{n}].png`.

## File naming for new art
`assets/units/<faction_folder>/<Building Name>.png` (base), `<Building Name> (A).png` / `(B).png`
— matches the existing HO/CW/EE convention wired in `Game_Core.cpp`. New factions
(Thornkin, Eternal Empire, Bloodsworn, Voidkin, Iron Assembly, Amalgamate,
Convergence) need the same loader pattern added (`kTKDwellings`, `kEEDwellings`
already exists, `kBSDwellings`, `kVKDwellings`, `kIADwellings`, `kAMDwellings`,
`kCVDwellings`) — code task, separate from art generation.
