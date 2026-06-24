# PROJECT: [UNNAMED] — Master Game Design Document
*From scratch · C++ · Long-term solo project · Last updated 2026-06-10*

---

## Core Vision
Turn-based strategy in HoMM3 vein. Hex world map, separate combat screen, hero progression, campaign + custom maps, procedural world gen, map editor, castle/hideout meta-layer. Single player only for now. Surface map only (no underground).

---

## STACK

| Layer | Choice | Notes |
|---|---|---|
| Language | C++ | Performance, what HoMM3 used |
| Window/Input | SDL2 | Cross-platform, handles input, audio baseline |
| Renderer | OpenGL 3.3 core | Mature, well documented |
| Audio | SDL_mixer | Music + SFX, simple |
| Networking | ENet | Reserved for later, reliable UDP |
| Save/Config | JSON (nlohmann) + SQLite | JSON for maps/config, SQLite for hideout meta-state |
| Scripting | Lua | Campaign triggers, AI behavior, moddable |
| Math | GLM | Vectors/matrices, header-only |
| Asset loading | stb_image | PNG/JPG, single header |
| UI (game) | Custom | No good embeddable C++ game UI exists |
| UI (editor) | Dear ImGui | Editor and debug tooling only |
| Build | CMake | Standard, cross-platform |

---

## FACTIONS (9 total)

| # | Name | Align | Magic School | Home Terrain | Penalty Terrain | Unique Mechanic |
|---|---|---|---|---|---|---|
| 1 | Holy Order | Good | Light | Plains/Sacred | Corrupted/Toxic | Dual Meter — Desperation + Inspiration |
| 2 | Crimson Wardens | Good | Blood (self-sacrifice) | Highland/Fortress | Corrupted/Swamp | Warden's Mark — focus fire burst |
| 3 | Thornkin | Good | Nature (pure) | Forest | Volcanic/Barren | Symbiosis — bonded pairs, berserk on companion death |
| 4 | Eternal Empire | Evil | Death/Darkness | Toxic/Corrupted | Sacred/Holy ground | Eternal Command — every unit has two lives |
| 5 | Bloodsworn | Evil | Blood (enemy sacrifice) | Corrupted/Swamp | Sacred/Plains | Blood Pool + Ascension |
| 6 | Voidkin | Evil | Nature (corrupted) | Corrupted Forest | Holy/Sacred | Possession — turns enemy units against their own army |
| 7 | Iron Assembly | Neutral | Runic/Mechanical | Industrial/Rocky | Swamp/Water | Crafting + Blueprint System + Salvage |
| 8 | Amalgamate | True Neutral | Organic-Machine hybrid | Wasteland/Flesh zones | Sacred/Clean terrain | Adaptation — gains resistances mid-battle, persists across campaign |
| 9 | Convergence | Locked (hideout) | All schools (reduced) | Adaptive — no bonus/penalty | None | Mirroring — copies enemy faction mechanic for 5 rounds |

**Faction unique mechanics** — all defined above.

---

## TERRAIN SYSTEM

Every faction has:
- **Home terrain** — movement bonus + minor combat bonus
- **Penalty terrain** — movement penalty + minor combat debuff
- **Neutral terrain** — no modifier

Terrain types: Plains, Forest, Highland, Corrupted, Toxic/Undead zone, Sacred/Holy, Industrial/Rocky, Swamp, Water, Volcanic/Barren, Wasteland, Corrupted Forest.

Penalty logic: Forge hates swamps/water. Living factions suffer in Undead toxic zones. Elves penalized on barren/volcanic. Holy Order penalized in corrupted zones. Etc.

---

## HERO SYSTEM

### Core Stats
| Stat | Description |
|---|---|
| Attack | Physical damage modifier |
| Defense | Physical damage reduction |
| Blood Power | Effectiveness of Blood magic |
| Light Power | Effectiveness of Light magic |
| Death Power | Effectiveness of Death/Darkness magic |
| Nature Power | Effectiveness of Nature magic |
| Forge Power | Effectiveness of Runic/Mechanical abilities |
| Flesh Power | Effectiveness of Fleshcraft abilities |

Hero naturally scales 1-2 casting stats based on class/faction. Others accessible via skills but stat growth is starved — cross-school is possible but costly.

### Class System
- Class determined at hero creation (faction-linked but player chooses)
- Each class has a unique **hero specialty** (passive unique bonus)
- Level up: pick 1 of 2 skill offers from class pool + 1 occasional wildcard from outside class
- Example: Castle faction → Knight class or Cleric class, each with different skill pools

### Skill Progression
- 8 skill slots maximum
- Each skill has 3 tiers: Basic → Advanced → Master
- Skills unlock sub-abilities at Advanced and Master tier
- Cross-class skills possible via wildcard offers — always weaker without matching casting stat

### Skill Archetype System *(implemented 2026-06-22)*
Skills are categorized as **Might** (1xx: Offense, Defense, Archery, Leadership, Tactics, Logistics, Scouting, First Aid, Luck) or **Magic** (2xx–7xx: school skills). Stacking same-category skills gives compound bonuses applied at combat start:

| Condition | Bonus |
|-----------|-------|
| 2–3 Might skills | +1 ATK/DEF to all units |
| 4+ Might skills | +2 ATK/DEF to all units |
| 2 Magic schools | +1 to all casting stats |
| 3+ Magic schools | +2 to all casting stats |

**Archetype bonuses** (mutually exclusive — checked after flat bonuses):
- **Pure Might** (≥5 Might, 0 Magic): +1 Speed and +10% HP to all units
- **Pure Magic** (≥4 Magic, ≤1 Might): +3 to all casting stats (stacks with combo bonus)
- **Warlord** (≥3 Might AND ≥2 Magic): +1 Morale and +1 Luck to all units

Implementation: `CombatEngine::initCombat()` → `applyArchetype` lambda (after `applySkills`).

---

## COMBAT SYSTEM

### Grid
- Flat hex grid (HoMM3 style)
- **Special tiles** scattered per battle map — damage amplifier, luck bonus, defense bonus, speed boost etc.
- Tile bonuses randomized or terrain-driven depending on map/biome
- Obstacle tiles, terrain features per biome

### Unit Tiers
- **6 tiers per faction**
- Tier strength is redistributed not stacked:
  - Faction A: strong T1, weak T3, average rest
  - Faction B: weak T1, two strong mid-tiers
  - No tier is useless — every unit has role and counters
- **Weakness matrix**: every unit has type tags (Undead, Construct, Beast, Humanoid, Flying, Mechanical, Organic-Mech, Holy, Blood-bound)
- Specific counter relationships exist cross-faction (like Black Dragons vs Titans)
- **Faction passive identity**: each faction has a combat-wide passive (e.g. Undead immune to morale, Blood Monks regenerate HP each round, Forge units ignore terrain penalties in combat)

### Unit Building — Upgrade Paths
Each unit building has **2 upgrade paths** — changes unit stats and/or abilities:
- Example Blood Monks T2 building: Path A (Praying — more spell resistance, holy damage) vs Path B (Regeneration — self-heal, damage-on-cast mechanic)
- Support buildings: boost growth rate, combat stats, school power proficiency
- Economy buildings: roads between towns (travel speed/income bonus), infrastructure (passive resource income)

### Siege
- Separate siege battle map
- Destructible walls with HP
- Siege engines destroy wall sections
- Breached walls change battle flow and unit access

**Siege Engine System:**
- Pick 3 from 5 engine types before battle
- One stack (3+ units minimum) builds all 3 on turn 1 — occupied, not consumed
- All 3 engines operational turn 2

| Engine | Role | Targets |
|---|---|---|
| Battering Ram | Destroys gates, melee range, fast | Gates only |
| Catapult | Ranged wall/tower damage, medium speed | Walls, towers |
| Trebuchet | Long range, slow, massive damage | Walls only |
| Siege Tower | Delivers units over intact walls | Transport only |
| Siege Drill | Tunnels under walls, bypasses entirely, slow | Wall bypass |

**Faction Engine Upgrades:**

| Faction | Engine | Upgrade | Effect |
|---|---|---|---|
| Holy Order | Trebuchet | Divine Trebuchet | Projectiles consecrate impact tiles |
| Bloodsworn | Catapult | Blood Catapult | Hits generate Blood Pool |
| Thornkin | Siege Tower | Living Tower | Tower regenerates HP, units inside heal |
| Eternal Empire | Battering Ram | Bone Crusher | Reraises as skeleton on destruction |
| Crimson Wardens | Trebuchet | Silver Trebuchet | Bonus damage vs Bloodsworn fortifications |
| Voidkin | Catapult | Void Caster | Creates void terrain on impact |
| Iron Assembly | All 5 | Iron Engines | Build faster, higher HP, more damage |
| Amalgamate | Siege Drill | Flesh Drill | Spreads flesh terrain inside walls on breach |
| Convergence | Any | Mirrors enemy faction engine upgrade for that battle |

### Forge Faction Exception
- Units not recruited — **crafted** at Forge buildings
- Costs resources per unit batch, not time-gated by weekly growth
- Uncapped but expensive — economy investment critical
- Slowest early game, powerful late if resourced

---

## ECONOMY

### Cycle
- 7-day week, buildings unlock weekly recruitment (HoMM3 style)
- Weekly decision focus: Economy vs Units vs Magic
- Roads between friendly towns: reduce movement cost + add passive income
- Infrastructure buildings: sewers, markets, warehouses — passive gold/resource generation

### Resources

| Resource | Type | Factions |
|---|---|---|
| Gold | Universal | All |
| Iron | Universal raw | All — refined per faction |
| Faith Stones | Unique | Holy Order, Crimson Wardens |
| Blood Essence | Unique | Bloodsworn, Eternal Empire |
| Verdant Sap | Unique | Thornkin, Voidkin |
| Mercury | Unique | Eternal Empire |
| Runic Iron | Refined Iron | Iron Assembly only |
| Fleshmetal | Refined Iron | Amalgamate only |
| All mixed | Special | Convergence — costs scale with difficulty |

**Refined Iron per faction:**

| Faction | Refined Iron Name |
|---|---|
| Holy Order | Sacred Iron |
| Crimson Wardens | Silver Iron |
| Thornkin | Ironwood |
| Eternal Empire | Bonesteel |
| Bloodsworn | Bloodsteel |
| Voidkin | Voidsteel |
| Iron Assembly | Runic Iron |
| Amalgamate | Fleshmetal |
| Convergence | Null Iron |

**Map tension:** Bloodsworn + Eternal Empire compete for Blood Essence. Thornkin + Voidkin compete for Verdant Sap. Holy Order + Crimson Wardens compete for Faith Stones.

### Artifacts
- **Basic artifacts** — craftable in town (Forge towns get crafting bonuses/expanded recipes)
- **Special/Legendary artifacts** — world map finds only, not craftable
- No set bonuses (keep simple for now)

---

## WORLD MAP

### Size
Fixed options: S / M / L / XL

### Movement
- Tile-based discrete (movement points pool spent per terrain type)
- Faction home terrain = cheaper movement
- Faction penalty terrain = expensive movement
- Roads between towns reduce movement cost

### Fog of War
- Unexplored only (HoMM3 style) — explored tiles stay visible

### World Generator (Procedural)
- Noise-based terrain generation + rule passes
- Biome placement, resource node distribution
- Faction home terrain clusters near starting towns
- Balance pass: resource accessibility, town spacing

---

## CAMPAIGN

### Structure
1. Player starts as **secret 9th faction hero** — origin story, faction-neutral
2. First 1-2 maps establish world and character
3. **4 key decisions** during early maps accumulate alignment score (Good / Evil / Neutral)
4. Alignment result unlocks **faction choice** from matching group
5. Player chooses specific faction, plays remaining campaign as that faction
6. 9th faction = narrative origin, not just an unlock reward

### Scope
- One main campaign (covers all alignments via branching)
- Faction sidecampaigns added later (post-main)

### Scripting
- Lua-based trigger system
- Objectives, dialog, cutscenes, map events
- Editor exposes trigger system for custom maps

---

## MAP EDITOR

- ImGui-based embedded tool
- Place terrain, towns, heroes, resources, objects
- Trigger/scripting panel (Lua)
- Custom map save/load format
- Supports campaign mission creation

---

## HIDEOUT / CASTLE META-LAYER

- Persistent out-of-game state (SQLite)
- Castle upgrades: cosmetic skins, visual improvements
- **Does not affect campaign or custom map balance**
- One purpose: **unlock the secret 9th faction** via hideout progression milestones
- Castle generates roaming mobs on world map (custom/skirmish maps only)
- Castle can inject units into level maps via trigger system

---

## AI DESIGN *(updated 2026-06-22)*

### Philosophy
Omniscient AI — sees full map, no fog. Players accept this as "the AI is stronger," not unfair. The cheating is **informational only** (no stat inflation, no teleportation). The goal is an opponent that plays faction-optimally, not one that wins via rubber-banding.

### Combat AI (implemented)
Three difficulty levels in `src/combat/CombatEngine.cpp`:
- **Passive**: random targets — tutorial difficulty
- **Standard**: nearest enemy, pathfind and attack
- **Tactical**: focus lowest total-HP stack, ranged units kite melee, prefer Attack tiles

### World-Map AI (implemented)
Enemy heroes (`m_enemyHeroes`) take a turn each day in `Game_WorldMap.cpp`:
- Recruit from adjacent owned towns (free, immediate)
- Strength comparison (hero + army value): aggressive if ≥70% of player, retreat if <40%
- Move toward: unowned mines → world objects (XP/spells/artifacts) → neutral towns → player if aggressive
- Collect objects with meaningful effects (XP → level up, stat shrines, spell scrolls)

### Town Building AI (implemented)
Each new week, AI towns build one building from a **faction-specific priority list** (`kBuildOrder[9]` in `Game_WorldMap.cpp`). Priority reflects faction strategy:
- HolyOrder: Fort → Mage Guild early (spell-dependent)
- Bloodsworn: T1/T2 fast + Blood Altar (aggressive early)
- IronAssembly: Blueprint Vault early + PathA (Runic line)
- Voidkin: Market first (economy-gated faction)
- etc.
PathA upgrade dwellings are included for T2–T6 tiers. Falls back to generic dwelling order if priority list exhausted.

---

## MULTIPLAYER
- **Deferred** — single player only for initial build
- ENet already in stack for future implementation
- Add post-launch: LAN first, then IP connect

---

## WHAT CLAUDE CAN BUILD

| Area | Status |
|---|---|
| All C++ architecture and systems | ✅ Full |
| Hex grid math, pathfinding, FOW | ✅ Full |
| OpenGL renderer, shaders, sprite batching | ✅ Full |
| Combat system, spell system, unit stats | ✅ Full |
| Weakness matrix, faction passives | ✅ Full |
| Procedural world generator | ✅ Full |
| Town building system, upgrade paths | ✅ Full |
| AI heuristics (combat: Passive/Standard/Tactical; world-map: movement, town builds) | ✅ Full |
| Save/load, SQLite hideout state | ✅ Full |
| Lua scripting integration | ✅ Full |
| Map editor (ImGui) | ✅ Full |
| Campaign trigger/branching system | ✅ Full |
| Build system (CMake) | ✅ Full |

## WHAT IS ON YOU

| Area | Notes |
|---|---|
| **Art** | Every sprite, tile, portrait, icon, animation — thousands of assets |
| **Music + SFX** | Compose or license |
| **Balance numbers** | Unit stats, spell costs, resource values — requires playtesting |
| **Faction lore** | Names, world narrative, dialog text |
| **Playtesting** | Running it, finding feel bugs |
| **Faction unique mechanics** | 7 still TBD — you design, I implement |
| **Spell lists** | Per school — you define, I code |

---

## BUILD ORDER

### Phase 0 — Engine Foundation
- [ ] CMake project setup
- [ ] SDL2 window + game loop
- [ ] OpenGL context + sprite render
- [ ] Tileset/spritesheet loader (stb_image)
- [ ] Hex grid rendering + camera pan/zoom
- [ ] Input system (keyboard + mouse, hex picking)

### Phase 1 — World Map
- [ ] Hex map data structure
- [ ] Terrain types + tile rules + faction affinity
- [ ] Hero on map, click-to-move
- [ ] A* pathfinding (terrain cost aware)
- [ ] Fog of war
- [ ] Resource nodes, pickups

### Phase 2 — Town System
- [ ] Town data structure
- [ ] Building tree (2 upgrade paths per unit building)
- [ ] Support + economy buildings
- [ ] Weekly recruitment cycle
- [ ] Forge crafting system (resource-based, uncapped)
- [ ] Road network (movement + income)

### Phase 3 — Combat Screen
- [ ] Separate combat scene/state
- [ ] Flat hex combat grid + special tiles
- [ ] Unit placement, turn order (speed-based)
- [ ] Attack/retaliation, damage formula
- [ ] Weakness matrix (unit type tags + counters)
- [ ] Faction passive identities
- [ ] Spell system skeleton (per school)
- [ ] Combat AI (heuristic weighted scoring)
- [ ] Siege map + destructible walls

### Phase 4 — Hero System
- [ ] Core stats (Attack/Defense + 6 school casting stats)
- [ ] Class system + hero specialties
- [ ] Level up skill offer system
- [ ] 8 skill slots, 3 tiers each
- [ ] Cross-class wildcard skill logic
- [ ] Artifact system (basic craft + legendary find)

### Phase 5 — UI Framework
- [ ] Widget base (button, label, panel, tooltip, modal)
- [ ] Hero screen
- [ ] Town screen
- [ ] Combat UI (unit info, spell bar, end turn)
- [ ] World map UI (minimap, resource bar, hero list)

### Phase 6 — Save / Load
- [ ] JSON map serialization
- [ ] Game state save/load
- [ ] SQLite hideout persistence

### Phase 7 — World Generator + Editor
- [ ] Procedural gen (noise + rule passes + balance pass)
- [ ] ImGui map editor
- [ ] Custom map format
- [ ] Lua trigger/scripting system

### Phase 8 — Campaign
- [ ] Campaign state machine
- [ ] Alignment scoring system (4 decisions)
- [ ] Faction unlock branching
- [ ] Mission scripting (Lua)
- [ ] Actual campaign content (your design)

### Phase 9 — Hideout Meta-Layer
- [ ] Persistent SQLite state
- [ ] Castle upgrade tree (cosmetic)
- [ ] 9th faction unlock milestones
- [ ] World map mob spawn scheduler
- [ ] Level map unit injection

### Phase 10 — Polish
- [ ] SDL_mixer audio system
- [ ] Particle effects
- [ ] Animations
- [ ] Balance pass
- [ ] Multiplayer (future)

---

## FOLDER STRUCTURE
```
game/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/          # game loop, state machine, input
│   ├── renderer/      # OpenGL, sprites, camera
│   ├── ui/            # custom widget framework
│   ├── world/         # hex map, terrain, fog of war, world gen
│   ├── town/          # buildings, recruitment, economy, roads
│   ├── combat/        # combat grid, units, spells, siege
│   ├── hero/          # stats, skills, classes, artifacts
│   ├── ai/            # heuristics, pathfinding
│   ├── campaign/      # state machine, alignment, branching
│   ├── scripting/     # Lua integration
│   ├── meta/          # hideout/castle persistent layer
│   └── data/          # loaders, JSON/SQLite serialization
├── assets/
│   ├── sprites/
│   ├── tiles/
│   ├── audio/
│   └── data/          # JSON game data (units, spells, buildings)
├── maps/
├── campaigns/
└── editor/
```

---

## FACTION DETAILS

---

### FACTION 1 — HOLY ORDER
**Alignment:** Good
**Magic School:** Light Power
**Aesthetic:** Baroque plague — flagellants, suffering devotion, dark gold, crumbling cathedrals, candlelight. Visually dark for a good faction. They fight evil at enormous personal cost.
**Home Terrain:** Plains/Sacred
**Penalty Terrain:** Corrupted/Toxic
**Resource:** A

**Faction Mechanic — Dual Meter System:**
- **Desperation** (per unit) — unit takes damage + sees allies die = gets stronger, faster, hits harder. Last surviving stack of any unit type is terrifying.
- **Inspiration** (faction-wide) — watching other units sacrifice for the cause buffs nearby survivors. Full stack wiped = massive surge to all remaining units.
- Opponent dilemma: kill fast and trigger Desperation, or hold back and let them recover. No clean answer.

**Playstyle:** Feed T1s deliberately, build both meters, T6 Hussars become godlike late battle. Intentionally fragile early, exponentially dangerous when losing.

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Penitent | Convicted criminals, cannon fodder, no armor, high count | Dies fast, feeds Desperation meter hard | Tanky, slow meter feed |
| T2 | Torch Bearer | Pitchfork militia, fire utility, empowers faction | Spreads fire on death (AOE) | Empowers nearby units while alive |
| T3 | Plague Doctor | Support — heals allies, spreads disease to enemies | Self-sacrifice heal (costs own HP) | Toxic cloud AOE on death |
| T4 | Penitent Knight | Former noble doing penance, self-damages to hit harder | Shields from first hit each round | Bleeds enemy on every strike |
| T5 | Seraph | Tormented angel, chained wings, Inspiration aura | Bigger Inspiration aura radius | Chains break at low HP — becomes flying unbound |
| T6 | Winged Hussar | Faction ultimate, scales hard with meters, relatively normal raw stats | Scales with Desperation meter only | Scales with both meters combined |

**Tier Strength Distribution:** T1 weakest raw, T2-T5 average, T6 weak baseline but scales to strongest unit in game when meters are charged.

---

### FACTION 2 — BLOODSWORN
**Alignment:** Evil
**Magic School:** Blood Power (enemy sacrifice)
**Aesthetic:** Vampire nobility — aristocratic, seductive, blood as currency and power. Gothic manors, crimson silk, obsidian and gold.
**Home Terrain:** Corrupted/Swamp
**Penalty Terrain:** Sacred/Plains
**Resource:** B

**Faction Mechanics — Dual System:**
- **Blood Pool** — built from enemy kills, spent on powerful faction abilities. More kills = more power available.
- **Ascension** — certain units transform mid-battle when kill threshold reached. Servants evolve into nobles.

**Playstyle:** Snowball faction — starts average, gets exponentially stronger as kills accumulate. Blood Pool fuels late-battle dominance. Mirror of Holy Order (they feed on enemy deaths, Holy Order feeds on own deaths).

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Blood Serf | Willing servant, addicted to vampire bite, expendable | Weakens nearby enemies (addiction aura) | Shields vampires behind them |
| T2 | Fledgling | Newly turned vampire, weak — ascends to noble vampire at kill threshold | Ascends faster (lower kill threshold) | Ascended form much stronger |
| T3 | Sanguine Priest | Support — fills Blood Pool faster, weak combat | Heals vampire units from pool | Curses enemies reducing resistance |
| T4 | Crimson Mage | Ranged blood spells, pools damage into Blood Pool | More pool generated per hit | AOE blood nova spell |
| T5 | Blood Witch | Curses enemies, weakens armor, sets up T6 Ancient | Stronger curse AOE | Curse transfers to adjacent on enemy death |
| T6 | Ancient | Eldest vampire, passive drain aura hits all enemies each round | Passive drain aura stronger | Can spend Blood Pool to resurrect fallen vampires |

**Tier Strength Distribution:** T1-T2 weak early, T3 pure support, T4-T5 strong mid, T6 dominant late when pool is charged.

---

### FACTION 3 — ELVES
**Alignment:** Good
**Magic School:** Nature Power
**Aesthetic:** Celtic druid — wild, animistic, nature spirits, sacrifice. No graceful Tolkien elves — these are feral, ancient, dangerous.
**Home Terrain:** Forest
**Penalty Terrain:** Volcanic/Barren
**Resource:** C

**Faction Mechanic — Symbiosis + Bond Breaking:**
Every unit is a bonded pair (elf + animal companion). Each half has independent HP.
- **Companion dies** → elf goes **berserk** (attacks randomly, ignores orders, higher damage)
- **Elf dies** → companion becomes **useless** (cowers, no actions)
- Opponent tactical choice: kill the elf or the animal — both outcomes have consequences

**Playstyle:** Flexible — ranged, melee, siege, support all covered. Forest terrain dramatically boosts effectiveness. Bond mechanic creates constant micro-decisions for both players.

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Druid + Wolf | Melee caster pair, cannon fodder | Wolf more aggressive | Druid heals on cast |
| T2 | Archer + Hawk | Ranged pair, hawk scouts/disrupts | Hawk blinds on hit | Archer pierces multiple targets |
| T3 | Warden + Stag | Heavy melee, territorial, forest bonus | Massive forest terrain bonus | Stag charge knockback |
| T4 | Stonecaller + Ram | Siege pair, destroys walls | Ram destroys walls faster | Stonecaller petrifies enemies hit |
| T5 | Spiritwalker + Spirit Wolf | Ethereal pair, partially immune to physical | More physical immunity | Spirit wolf haunts killer on death |
| T6 | Worldtree Avatar + Treant Lord | Massive regen tank pair | Regen scales with forest tiles on map | Treant Lord splits into two on death |

**Tier Strength Distribution:** T2 strong ranged, T3 strong melee, T4 siege specialist, T6 unkillable in forest terrain.

---

---

---

### FACTION 4 — UNDEAD
**Alignment:** Evil
**Magic School:** Death Power
**Aesthetic:** Fallen empire — dead civilization still functioning. Cold, aristocratic, disciplined. Bureaucracy, military hierarchy, nobility all intact — just undead. No chaos, no rot. Eternal patience.
**Home Terrain:** Toxic/Corrupted
**Penalty Terrain:** Sacred/Holy ground
**Resource:** TBD

**Faction Mechanic — Eternal Command (Death Toll):**
Every unit has two lives. First life = full unit. On death → reraises as skeleton remnant (weaker but still fighting). Enemy must kill everything twice. Death Emperor aura makes reraised units full strength.

**Playstyle:** Attrition faction — never fully collapses. Opponent must deal double damage effectively. Morale debuffs cripple enemies while undead are immune to morale entirely.

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Conscript Skeleton | Former soldier, formation fighter, expendable | Higher count | Formation bonus for adjacent units |
| T2 | Wight Soldier | Armored, slow, drains enemy attack on hit | Drains more attack | Drains defense instead |
| T3 | Lich Apprentice | Ranged death spells, glass cannon | One shot massive damage then dies | Stays back, longer range |
| T4 | Bone Golem | Constructed from corpses, massive, slow, nearly unkillable | Nearly unkillable tank | Explodes on death AOE |
| T5 | Dread Chancellor | Former empire official, debuffs enemy leadership/morale | Wider morale debuff aura | Single target breaks enemy hero morale |
| T6 | Death Emperor | On throne carried by servants, passive aura makes all reraised units full strength | Reraised units keep upgrade paths | Reraised units count double for Death Toll |

**Tier Strength Distribution:** T1 expendable fodder, T2-T4 durable mid, T5 support/debuff, T6 passive aura that makes entire faction terrifying late battle.

---

### FACTION 5 — TBD GOOD FACTION
Intentional gap. Third Good faction designed later when inspiration hits. Lore reason: evil is winning, good is outnumbered.

---

---

### FACTION 6 — CORRUPTED ELVES
**Alignment:** Evil
**Magic School:** Nature Power (corrupted)
**Aesthetic:** Void-touched — darkness bleeding through nature, shadow and decay. Hollow dark eyes, bark skin cracking with void light, roots that drain rather than grow.
**Home Terrain:** Corrupted Forest
**Penalty Terrain:** Holy/Sacred
**Resource:** TBD

**Faction Mechanic — Possession:**
Units possess enemies — possessed unit attacks its own allies then dies. Against Elves specifically: possesses the animal companion directly, severing the bond and turning the animal feral against its own elf.
- Shadow Druid extends possession duration and strength
- Possession is the core win condition — turning enemy army against itself

**Playstyle:** Chaos and disruption. No brute force — unravel enemy formation from inside. Hardest counter to Elves (steals companions), strong against morale-dependent factions.

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Shade Runner | Invisible until strikes, single possession then vanishes | Can possess twice | Explodes after possession killing both |
| T2 | Wither Sprite | Flying, drains enemy unit stats on hit | Drains more stats | Stolen stats buff Corrupted units |
| T3 | Shadow Druid | Support — extends possession duration, buffs possessing units | Buffs multiple possessing units simultaneously | Can possess hero directly |
| T4 | Corrupted Stag | Former elf companion, massive, charges through lines | Knockback through multiple units | Reraises as void skeleton |
| T5 | Void Treant | Corrupted worldtree fragment, roots enemies, decays terrain — counters Elf forest bonuses | Burst — corrupts forest tiles immediately large AOE | Creeping — decay spreads to adjacent tiles each round wider |
| T6 | Void Serpent | Ancient corrupted beast, swallows units whole removing them from battle entirely (counters Undead double-life) | Swallows multiple units at once | Swallowed units regurgitated as void slaves |

**Tier Strength Distribution:** T1 stealth disruptor, T2 flying debuffer, T3 pure support, T4 heavy bruiser, T5 terrain control, T6 removal specialist.

---

---

### FACTION 7 — FORGE
**Alignment:** Neutral (leans good-neutral)
**Magic School:** Forge Power (Runic/Mechanical)
**Aesthetic:** War factory — brutal utilitarian, assembly lines, no aesthetics just function. Grey iron, sparks, smoke, efficiency above all.
**Home Terrain:** Industrial/Rocky
**Penalty Terrain:** Swamp/Water
**Resource:** TBD

**Faction Mechanics — Three Systems:**
- **Crafting** — units manufactured not recruited. Resource-based, uncapped by weekly growth. Slow to snowball, powerful if resourced. Economy investment critical.
- **Blueprint System** — research new unit variants over time, expanding available roster. No fixed roster — invested research changes what you can build.
- **Salvage** — fallen friendly units (and enemy constructs) stripped for parts mid-battle. Player chooses per salvage: buff attack (better weapons) or defense (armor plating). Dead soldiers become upgrades for survivors.

**Playstyle:** Slow start, resource-hungry, devastating late. Immune to possession (mechanical minds). Hard counter to Corrupted Elves. Unique artifact crafting bonuses at Forge towns.

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Scrap Drone | Tiny flying collector, gathers salvage automatically, low combat | Collects salvage faster | Survives longer before being salvaged itself |
| T2 | Iron Grunt | Basic infantry, cheap, expendable cannon fodder | Higher count cheaper | Explodes on death AOE |
| T3 | Cannon Platform | Stationary siege, massive ranged damage | Longer range | AOE splash damage |
| T4 | War Automaton | Heavy melee construct, nearly unkillable, slow | Taunts enemies — forces attacks onto it | Self-repairs each round |
| T5 | Prototype | Blueprint exclusive, strongest T5, costs rare resources | Cheaper to craft | Blueprint unlocks faster variant |
| T6 | Dreadnought | Mobile fortress, ranged+melee combined, immune to possession | Ranged and melee same turn | Scales harder with salvage stacks |

**Tier Strength Distribution:** T1 utility not combat, T2 expendable fodder, T3 siege specialist, T4 tank anchor, T5 blueprint reward, T6 unstoppable late game.

---

---

### FACTION 8 — FLESHCRAFT
**Alignment:** True Neutral (just want to evolve, no morality)
**Magic School:** Flesh Power
**Aesthetic:** Warhammer 40k Tyranid/Dreadnought crossover — organic-machine hybrids, flesh fused with metal, grotesque engineering. Contrast to Forge's clean precision.
**Home Terrain:** Wasteland/Flesh zones
**Penalty Terrain:** Sacred/Clean terrain
**Resource:** TBD

**Faction Mechanic — Adaptation (in and out of combat):**
Units gain resistances to damage types that hurt them mid-battle. Resistances persist between battles across the campaign. Starts as weakest faction, becomes hardest to kill over a long campaign. Every loss makes them stronger against whoever beat them.

**Playstyle:** Worst early game, best late campaign. Snowballs through suffering rather than kills (contrast to Bloodsworn). True neutral — no allegiance, pure evolution. Flesh terrain spreads passively from T1 deaths.

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Spore Pod | Immobile, explodes spreading flesh terrain | Spreads more flesh terrain | Explodes faster smaller AOE |
| T2 | Flesh Crawler | Fast melee swarm, gains speed adaptation | Gains speed faster | Speed adaptation transfers to nearby crawlers |
| T3 | Synapse Node | Support — shares adaptations to nearby units | Shares adaptations wider radius | Shares adaptations faster |
| T4 | Amalgam | Fused organic-machine mass, adapts two resistances simultaneously | Shares own adaptations to adjacent units | Immune to debuffs |
| T5 | Hive Mind | Connects all units, shared adaptations faction-wide for one battle | Connects more units | Connection persists after Hive Mind dies |
| T6 | The Evolved | Singular perfect organism, carries ALL adaptations earned this campaign | Gains new adaptation each round in combat | Carries one extra adaptation slot |

**Tier Strength Distribution:** T1 terrain utility, T2 fast swarm, T3 pure support, T4 durable mid, T5 battle-wide buff, T6 campaign-scaling ultimate.

---

### FACTION 9 — SECRET (LOCKED)
**Alignment:** Locked — unlocked via hideout progression
**Magic School:** Synthesizes all schools
**Role:** Campaign origin faction — player starts as this faction, first 1-2 maps are origin story, then alignment choices unlock a permanent faction
**Unique:** Adaptive terrain (no home/penalty — fits everywhere at average)

---

## CROSS-FACTION INTERACTION MATRIX

### Terrain Interactions in Combat

| Terrain | Benefits | Penalizes | Special Effect |
|---|---|---|---|
| Sacred/Holy | Holy Order (combat bonus) | Undead (prevents reraise on 2 tiles per map), Bloodsworn (Blood Power weakened + self-damages caster) | Light Power spells boosted |
| Forest | Elves (combat bonus) | Forge (machines hate organic terrain, movement+combat penalty) | Void Treant corrupts forest tiles |
| Corrupted/Toxic | Undead, Bloodsworn | Holy Order, Elves | Spreads if Void Treant present |
| Flesh terrain | Fleshcraft | Holy Order, Forge | Spreads from Spore Pod deaths |
| Industrial/Rocky | Forge | Fleshcraft | None |
| Wasteland | Fleshcraft | Holy Order | Undead ignore penalty here |

### Faction vs Faction Specific Interactions

| Attacker | Defender | Mechanic |
|---|---|---|
| Holy Order | Undead | Light Power spells deal bonus damage to Undead |
| Holy Order | Bloodsworn | Light Power spells deal bonus damage to Bloodsworn |
| Holy Order Torch Bearer | Self | Fire tiles charge Desperation meter faster — Torch Bearer now has clear combat purpose |
| Bloodsworn | Holy Order | Blood Power weakened on Sacred terrain + self-damages caster |
| Corrupted Elves | Elves | Possession targets animal companions first automatically + bonus possession chance vs Elves |
| Void Treant | Elves | Corrupts forest tiles — removes Elf terrain bonuses directly |
| Void Serpent | Undead | Swallowed units removed entirely — bypasses double-life reraise mechanic |
| Forge | Fleshcraft | Salvage on Fleshcraft units steals adaptations as stat buffs |
| Undead | Fleshcraft | Both ignore each other's terrain penalties (wasteland ≈ toxic) |
| Fleshcraft | Undead | Adapts to Death Power after first exposure — gains partial immunity |
| Any faction | Elves | Killing animal companion triggers berserk — any faction can exploit by targeting companions first |
| Undead | Sacred terrain | Sacred terrain prevents reraise entirely on 2 tiles per map |

### Alignment Counter Chart (General)

| Good vs | Evil | Neutral |
|---|---|---|
| Light Power bonus damage | Undead, Bloodsworn | No bonus |
| Sacred terrain | Penalizes Evil factions | No effect |

| Evil vs | Good | Neutral |
|---|---|---|
| Blood Power | Weakened on Sacred terrain | Normal |
| Possession | Corrupted Elves exploit Elf companions | Forge immune |

| Neutral vs | Good | Evil |
|---|---|---|
| Forge | No alignment interaction | Immune to possession |
| Fleshcraft | Adapts to everything | Adapts to everything |

### Still Undefined (design later)
- Nature Power pure (Elves) vs Nature Power corrupted (Corrupted Elves) — direct school clash mechanic
- Forge blueprint system vs Fleshcraft adaptation — philosophical opposites, potential special event
- Secret 9th faction interactions — synthesizes all, define when faction is designed


- Faction unique mechanics for factions 1-6 and 8 (TBD)
- Full spell lists per school (Blood/Light/Death/Nature/Runic/Flesh)
- Unit rosters per faction (6 tiers × 9 factions = 54 base units + upgrades)
- Hero class names and specialty list per faction
- Resource names (A/B/C/D — give them actual names)
- World lore, faction names (some placeholder above)
- Siege engine unit types
- Special combat tile types and frequency
- Hideout upgrade tree shape and unlock milestones


---

## HERO CLASSES

Each faction has 4 classes. Player picks 1 at hero creation. Other 3 become wildcard skill offer sources. Each class has 1 unique specialty passive.

---

### HOLY ORDER CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Inquisitor | Heresy Detection / Null Field / Bind (pick 1 at creation) | Light Power | Anti-magic, disrupts spellcasting |
| Confessor | Last Rites — deaths near hero = double Inspiration | Light Power | Support, Inspiration meter, heals |
| Crusader | Veteran — units gain +1 stats per previous battle | Attack | Combat leader, unit attack buffs |
| Flagellant Marshal | Blood Penance — hero loses HP, Desperation charges faster | Attack + Light Power | Desperation meter, self-damage buffs |

---

### BLOODSWORN CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Blood Prince | Feast — hero drains HP from one unit per round to fill Blood Pool | Blood Power | Blood Pool generation, charisma unit buffs |
| Crimson Mage | Exsanguinate — one Blood Power spell per battle costs no pool | Blood Power | Spell-focused, Blood Power scaling |
| Thrall Master | Swarm — all Fledglings start battle at ascension threshold | Attack | Unit control, ascension focus |
| Assassin Lord | Predator — permanent Attack for each hero killed this campaign | Attack + Blood Power | Fast, targets enemy hero directly |

---

### ELVES CLASSES ⚠️ NOT STARTED
### UNDEAD CLASSES ⚠️ NOT STARTED
### CORRUPTED ELVES CLASSES ⚠️ NOT STARTED
### FORGE CLASSES ⚠️ NOT STARTED
### FLESHCRAFT CLASSES ⚠️ NOT STARTED


---

### ELVES CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Beastcaller | Wild Growth — dead companions respawn as spirit versions | Nature Power | Companion strength, bond preservation |
| Pathfinder | Overgrowth — place 3 forest tiles before battle | Attack | Terrain, movement, forest specialist |
| Stormbark | Lightning Rod — first enemy spell redirected back at caster | Nature Power | Offensive caster, storms |
| Warsinger | Harmony — all bonded pairs +1 stats while hero lives | Attack + Nature Power | Combat leader, buffs all pairs |

---

### UNDEAD CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Death Herald | Soul Harvest — enemy kills restore hero HP | Death Power | Spellcaster, Death Power focus |
| Iron General | Eternal Legion — reraised units retain formation bonuses | Attack | Formation buffs, attack |
| Lich | Phylactery — hero respawns next battle at half stats | Death Power | Reraise efficiency |
| Grave Diplomat | Negotiated Weakness — reveals enemy hero specialty before battle | Attack + Death Power | Morale debuffs, hero weakening |

---

### CORRUPTED ELVES CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Void Weaver | Void Link — possession spreads to adjacent ally on death | Nature Power (corrupt) | Possession specialist |
| Shadow Stalker | Ghost Walk — hero invisible on world map always | Attack | Stealth, assassination |
| Blight Caller | Blight Aura — corrupts sacred terrain tiles passively | Nature Power (corrupt) | Terrain corruption |
| Fell Druid | Wither — enemy units lose 1 stat permanently per round in aura | Attack + Nature Power | Debuffs, decay |

---

### FORGE CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Master Engineer | Efficient — all units cost 20% less to craft | Forge Power | Crafting costs, blueprints |
| Warlord Mechanic | Iron Discipline — constructs immune to morale/fear | Attack | Combat leader, construct buffs |
| Salvage Lord | Recycler — salvage buffs permanent across battles | Attack + Forge Power | Salvage maximization |
| Runesmith | Living Rune — one unit gets permanent stat increase per battle | Forge Power | Runic enchants, buffs |

---

### FLESHCRAFT CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Evolver | Rapid Evolution — units gain adaptations after 1 hit | Flesh Power | Adaptation speed |
| Hive Controller | Collective — transfer one adaptation between any units | Flesh Power | Shared adaptations, connections |
| Flesh Architect | Infestation — flesh terrain spreads every round passively | Attack + Flesh Power | Terrain spreading |
| Apex Hunter | Apex — hero starts with all adaptations The Evolved has accumulated | Attack | Combat predator, personal evolution |


---

## HERO COMBAT MECHANICS

**Role:** Hybrid C — occupies fixed rear hex (protected zone)
**Has:** HP pool, Attack stat, casting stats (school-specific)
**Actions per round:** Cast one spell OR use hero ability OR attack (if melee range breached)
**Targeting:** Protected from normal attacks. Targetable by: special abilities (Hunt, Assassin Lord), ranged spells, flying units that bypass front line
**Hero death:** Instant battle loss
**Aura:** Passive buffs radiate to nearby units based on class

**Between battles:**
- Always survives if army retreats (loses units, keeps hero + XP)
- Army wiped + no retreat = hero captured
- Captured = rescue mission spawned on map OR ransom in Gold
- Phylactery (Lich specialty) = auto-escapes capture once per campaign

---

## SPELL LISTS

Spells organized by school. Each spell has: Name, Type (damage/buff/debuff/summon/utility), Cost (casting stat threshold), Effect.
Tiers: Basic (low casting stat) → Advanced → Master (high casting stat required)

---

### LIGHT POWER — Holy Order, TBD Good faction

| Tier | Name | Type | Effect |
|---|---|---|---|
| Basic | Smite | Damage | Single target bonus damage, double vs Undead/Bloodsworn |
| Basic | Heal | Buff | Restores HP to one friendly unit |
| Basic | Bless | Buff | Target unit gains +2 Attack and Defense for 3 rounds |
| Basic | Blind | Debuff | Target enemy unit cannot attack for 1 round |
| Advanced | Holy Light | Damage | AOE damage to all Undead/Bloodsworn units on field |
| Advanced | Divine Shield | Buff | One unit absorbs next 3 hits completely |
| Advanced | Consecrate | Utility | Converts 2 tiles to Sacred terrain — penalizes Undead/Bloodsworn |
| Advanced | Purify | Utility | Removes all debuffs from all friendly units |
| Advanced | Martyr's Call | Buff | Charges Inspiration meter fully instantly |
| Master | Judgement | Damage | Massive single target damage, instakills Undead units below 30% HP |
| Master | Radiance | Damage | Full field AOE light damage, damages all non-Light faction units |
| Master | Resurrection | Summon | Brings back one destroyed friendly unit stack at 50% strength |
| Master | Divine Wrath | Damage | Triple damage vs Bloodsworn specifically, corrupts their Blood Pool |

---

### BLOOD POWER — Bloodsworn (evil), future Good blood faction

| Tier | Name | Type | Effect |
|---|---|---|---|
| Basic | Bloodletting | Damage | Deals damage, generates Blood Pool |
| Basic | Sanguine Bolt | Damage | Ranged single target, bonus damage if target is bleeding |
| Basic | Enfeeble | Debuff | Target loses Attack for 3 rounds |
| Basic | Blood Haste | Buff | Target friendly unit moves twice this round |
| Advanced | Hemorrhage | Debuff | Target bleeds — loses HP each round for 5 rounds |
| Advanced | Drain Life | Damage | Steals HP from enemy unit, heals hero |
| Advanced | Blood Frenzy | Buff | All units gain Attack bonus scaling with Blood Pool level |
| Advanced | Crimson Veil | Utility | Hero invisible to enemy targeting for 2 rounds |
| Advanced | Sanguine Nova | Damage | AOE blood explosion around target, spends Blood Pool |
| Master | Exsanguinate | Damage | Empties target unit's HP to 1, transfers stolen HP to Blood Pool |
| Master | Blood Tide | Damage | Massive AOE, damage scales with current Blood Pool size |
| Master | Ascend | Buff | All Fledglings on field instantly complete ascension |
| Master | Crimson Pact | Buff | Hero sacrifices half HP — all units gain massive stats for 3 rounds |

---

### DEATH POWER — Undead

| Tier | Name | Type | Effect |
|---|---|---|---|
| Basic | Chill | Debuff | Target slowed, Defense reduced |
| Basic | Bone Bolt | Damage | Ranged single target, pierces armor |
| Basic | Fear | Debuff | Target unit skips next action |
| Basic | Dark Mend | Buff | Heals undead unit (does nothing to living) |
| Advanced | Soul Rip | Damage | Deals damage, generates Death Toll charge |
| Advanced | Wail of the Dead | Debuff | AOE morale penalty to all living units |
| Advanced | Corpse Rise | Summon | Raises one destroyed enemy unit as skeleton slave |
| Advanced | Death Mark | Debuff | Target takes double damage from all sources for 3 rounds |
| Advanced | Decay | Debuff | Target unit loses 1 stat permanently this battle |
| Master | Death Coil | Damage | Massive single target, instakills living units below 20% HP |
| Master | Army of the Dead | Summon | Raises all destroyed friendly units as skeleton remnants simultaneously |
| Master | Eternal Night | Utility | Converts entire combat map to Toxic terrain for 5 rounds |
| Master | Soul Crush | Debuff | Destroys enemy hero's highest casting stat permanently this battle |

---

### NATURE POWER — Elves (pure), Corrupted Elves (twisted versions)

**Pure versions (Elves):**

| Tier | Name | Type | Effect |
|---|---|---|---|
| Basic | Entangle | Debuff | Roots target in place for 2 rounds |
| Basic | Barkskin | Buff | Target gains bonus Defense |
| Basic | Gust | Utility | Pushes target unit 2 hexes back |
| Basic | Regrowth | Buff | Target regenerates HP each round for 3 rounds |
| Advanced | Thornwall | Utility | Places impassable thorn barrier on 3 hexes |
| Advanced | Call Lightning | Damage | Ranged AOE storm damage |
| Advanced | Animal Bond | Buff | Strengthens one bonded pair — companion cannot go berserk this battle |
| Advanced | Overgrowth | Utility | Grows 4 forest tiles on combat map |
| Advanced | Swarm | Summon | Summons insect swarm that harasses one enemy unit each round |
| Master | Worldtree's Blessing | Buff | All bonded pairs fully healed and buffed |
| Master | Hurricane | Damage | Full field storm damage, knocks all flying units down |
| Master | Nature's Wrath | Damage | Massive damage scaling with number of forest tiles on map |
| Master | Ancient Grove | Utility | Converts entire map to forest terrain permanently this battle |

**Corrupted versions (same school, twisted effects):**
- Entangle → Void Snare: roots AND drains HP each round
- Barkskin → Chitinplate: Defense bonus but unit becomes immune to healing
- Regrowth → Rot: target loses HP each round instead
- Call Lightning → Void Storm: AOE void terrain created where it hits
- Overgrowth → Blight Spread: grows void terrain tiles instead of forest
- Nature's Wrath → Void Surge: damage scales with void terrain tiles instead

---

### FORGE POWER — Forge

| Tier | Name | Type | Effect |
|---|---|---|---|
| Basic | Spark | Damage | Small ranged damage, bonus vs organic units |
| Basic | Reinforce | Buff | Target construct gains bonus Defense this battle |
| Basic | Overclock | Buff | Target unit acts twice next round, then slowed |
| Basic | Oil Slick | Utility | Creates hazard tile — units moving through it are slowed |
| Advanced | Cannon Volley | Damage | Ranged AOE, bonus damage vs walls and structures |
| Advanced | Field Repair | Buff | Restores HP to one construct unit |
| Advanced | Chain Lightning | Damage | Hits one unit then jumps to 2 adjacent — bonus vs Fleshcraft |
| Advanced | Magnetic Pull | Utility | Drags metal-bearing enemy unit 3 hexes toward hero |
| Advanced | Blueprint Surge | Utility | One unit gains stats from next blueprint tier temporarily |
| Master | Forge Strike | Damage | Massive single target, destroys armor permanently |
| Master | Iron Rain | Damage | Full field bombardment, damages all non-Forge units |
| Master | Overclock All | Buff | Every construct acts twice for one full round |
| Master | Salvage Storm | Utility | Destroys one friendly unit — buffs all others with salvage simultaneously |

---

### FLESH POWER — Fleshcraft

| Tier | Name | Type | Effect |
|---|---|---|---|
| Basic | Acid Spit | Damage | Ranged, reduces target Defense permanently this battle |
| Basic | Mutate | Buff | Target gains random adaptation immediately |
| Basic | Spore Burst | Utility | Creates flesh terrain tile at target location |
| Basic | Nerve Pulse | Debuff | Target unit acts last in round for 2 rounds |
| Advanced | Assimilate | Utility | Copies one stat from defeated enemy unit to target friendly |
| Advanced | Flesh Surge | Damage | AOE organic explosion, bonus vs Forge constructs |
| Advanced | Viral Spread | Debuff | Target unit spreads weakness to adjacent allies each round |
| Advanced | Graft | Buff | Two units temporarily share HP pool this battle |
| Advanced | Overgrowth | Utility | Spreads flesh terrain to 4 tiles simultaneously |
| Master | Consume | Damage | Destroys one enemy unit entirely, hero gains their primary stat |
| Master | Evolution Pulse | Buff | All units instantly gain 2 adaptations simultaneously |
| Master | The Great Hunger | Damage | Full field AOE, living units take bonus damage, constructs take half |
| Master | Perfect Form | Buff | One unit becomes temporarily immune to all damage for 2 rounds |

---

## FACTION 9 — CONVERGENCE

**Alignment:** Locked — unlocked via hideout progression
**Identity:** Chameleon — copies enemy faction mechanics temporarily
**Magic School:** All schools at reduced power natively, full power when copying
**Aesthetic:** TBD — design when unlocked lore-wise
**Home Terrain:** Adaptive — no penalty terrain, no bonus terrain (average everywhere)
**Resource:** All resources at mixed cost — scales harder at higher difficulties

**Faction Mechanic — Mirroring:**
At battle start hero observes enemy faction. Can activate Mirror once per battle — copies enemy faction's unique mechanic for 5 rounds.
- vs Holy Order → gains Desperation/Inspiration meters temporarily
- vs Bloodsworn → gains Blood Pool temporarily
- vs Thornkin → all units gain temporary animal companions
- vs Eternal Empire → units get one reraise this battle
- vs Voidkin → one unit can possess
- vs Iron Assembly → salvage system activates for this battle
- vs Amalgamate → units gain 1 random adaptation immediately

**Scaling:** Weakest faction without mirror active. Strongest potential of any faction when mirrored + fully resourced. Mastery faction — highest skill ceiling.

**Hero Classes:**

| Class | Alignment | Specialty | Casting Stat | Focus |
|---|---|---|---|---|
| Lightbringer | Good | Radiance — Mirror lasts 7 rounds instead of 5 | Light Power | Extended mirror, Good factions |
| Oathbound | Good | Covenant — linked units share mirrored buffs | Light + Nature Power | Support, spreads mirror effects |
| Shadowlord | Evil | Predator — Mirror activates instantly, no delay | Death Power | Fast mirror, Evil factions |
| Voidcaller | Evil | Corruption — mirrored terrain effects linger after mirror expires | Blood + Death Power | Terrain manipulation |
| Ironweaver | Neutral | Synthesis — hold two mirrors simultaneously | Forge Power | Double mirror, Neutral factions |
| Fleshbinder | Neutral | Adaptation — units gain one adaptation per mirrored battle | Flesh Power | Persistent cross-battle bonuses |

**Unit Roster:**

| Tier | Name | Base Role | Base Ability | Mirror Gain |
|---|---|---|---|---|
| T1 | Hollow | Cannon fodder, high count | Absorbs one debuff per battle | Gains T1 ability of mirrored faction |
| T2 | Echo Scout | Fast, low combat, disruptor | Reveals enemy unit stats before battle | Gains utility ability of mirrored faction |
| T3 | Resonant | Caster support | Extends mirror duration +1 round | Gains support ability of mirrored faction |
| T4 | Convergent | Heavy melee, durable | Immune to first faction-specific mechanic used against it | Gains T4 combat ability of mirrored faction |
| T5 | Synthesis | Hybrid ranged/melee | Stores one used mirror ability, reuses next battle | Gains T5 ability of mirrored faction |
| T6 | The Mirror | Weakest T6 baseline in game | Reflects one attack back at attacker per round | Becomes full T6 of mirrored faction — strongest T6 in game when charged |

**Tier Strength Distribution:** Worst baseline of all factions. Mirror active = competitive with any faction. Full resource investment = scales harder than any single faction.

**Campaign Role:**
Player starts campaign as this faction. First 1-2 maps origin story. 4 alignment decisions → unlock permanent faction choice. After that, Convergence hero becomes optional secret playthrough for players who complete full campaign.


---

## CORE MECHANICS

---

### MORALE SYSTEM

**Scope:** Army baseline + per unit modifier
**Mechanic:** Meter-based — fills to 100%, grants guaranteed bonus action, resets

**Meter fills from:**
- Kills
- Terrain bonus (home terrain)
- Inspiration events (Holy Order)
- Artifacts, hero aura

**Meter drains from:**
- Allies dying nearby
- Penalty terrain
- Dread Chancellor aura
- Enemy debuff spells

**At 100%:** Unit gets guaranteed bonus action this round, meter resets to 0

**Exceptions:**
- Undead — no morale meter, fully immune
- Forge constructs — immune (Iron Discipline)
- Fleshcraft — immune (no organic psychology)

---

### ARMY STACKS

- Max 7 stacks per army (HoMM3 style)
- Unlimited units per stack — just a number
- Stack slots fixed — no expansion mechanic
- Hero occupies rear hex, not a stack slot

---

### HERO PROGRESSION

**Level cap:** None — exponential XP curve
- Early levels fast (1-10)
- Mid levels moderate (11-25)
- High levels require serious investment (26+)
- Practical endgame territory: levels 30-40

**XP Sources:**
- Kills — scales with unit tier (T1 low, T6 high)
- Kill XP scales with enemy strength relative to yours — beating stronger = bonus XP, beating weaker = diminishing returns
- Beating higher level hero = massive XP bonus
- Objectives — campaign/map goals
- Exploration — finding map objects, clearing neutral sites

**Per level:** Pick 1 of 2 skill offers (class pool) + occasional wildcard from outside class

---

### COMBAT SPECIAL TILES

**Placement:** Terrain-driven — biome determines tile type distribution
**Types:** 3 types only

| Tile | Effect | Primary Biomes |
|---|---|---|
| Attack | Bonus damage while standing on tile | Corrupted, Toxic, Wasteland, Flesh |
| Defense | Damage reduction while standing on tile | Sacred, Plains, Industrial, Rocky |
| Speed | Movement bonus on combat grid | Forest, Highland |
| Speed (negative) | Movement penalty | Swamp, Water |

**Frequency:** 3-6 special tiles per combat map depending on map size
**Faction interaction:** Forest Speed tiles amplify Elf advantage. Corrupted Attack tiles amplify Bloodsworn/Undead aggression.


---

## FACTION 5 — CRIMSON WARDENS
**Alignment:** Good
**Magic School:** Blood Power (self-sacrifice, protection)
**Aesthetic:** Renaissance vampire hunters — Van Helsing style. Long coats, crossbows, alchemical vials, silver weapons, technical and magical combined. Stylish but functional.
**Home Terrain:** Highland/Fortress
**Penalty Terrain:** Corrupted/Swamp
**Resource:** Faith Stones + Blood Essence (Good faction using Evil resource — creates map tension)
**Direct Nemesis:** Bloodsworn — bonus damage vs them specifically

**Faction Mechanic — Warden's Mark:**
Mark one enemy unit — all attacks against it deal bonus damage. Entire army focuses fire on marked target. Natural counter to Bloodsworn Blood Pool buildup — kill the engine before it charges. Mark one target at a time (unless hero has Double Mark).

**Playstyle:** Focus-fire burst faction. Mark high value target, delete it fast, mark next. Covenant links between units create web of shared heals. Blood Saint T6 is ultimate sacrifice — death buffs or marks everything simultaneously.

**Unit Roster:**

| Tier | Name | Role | Upgrade A | Upgrade B |
|---|---|---|---|---|
| T1 | Tracker | Fast scout, marks targets on contact, sets up army | Marks faster | Marked target debuffed too |
| T2 | Oath Keeper | Medium infantry, blood oath links them to Trackers | Stronger melee | Links to more units simultaneously |
| T3 | Alchemist | Throws vials, debuffs, support role | More vial types | Vials AOE instead of single target |
| T4 | Warden Knight | Heavy cavalry, charges marked targets for massive bonus damage | Charge triple damage vs marked | Charge knocks marked unit back stunning it |
| T5 | Saintguard | Protects Blood Saint, sacrifices self so Saint lives longer | Absorbs one killing blow for Saint | Death buffs Saint massively |
| T6 | Blood Saint | Self-sacrifices to massively buff entire army | Saint death marks all enemies simultaneously | Saint death fully heals all allies |

**Tier Strength Distribution:** T1 utility/setup, T2 frontline, T3 support, T4 burst damage vs marked, T5 protector, T6 sacrifice win condition.

---

### CRIMSON WARDENS CLASSES

| Class | Specialty | Casting Stat | Focus |
|---|---|---|---|
| Warden Captain | Coordinated Strike — marked target takes bonus damage from every attacker same round | Attack | Combat leader, mark amplifier |
| Blood Sage | Elixir — once per battle fully heals one unit | Blood Power | Caster, alchemical spells, support |
| Oathmaster | Blood Web — linked units heal when any linked unit kills | Blood Power | Covenant links, shared buffs |
| Inquisitor Hunter | Blood Scent — always knows exact location of Bloodsworn heroes on map | Attack + Blood Power | Tracks enemy heroes, Bloodsworn nemesis |


---

## WORLD LORE

### The Age of Balance
The world was never peaceful — Good and Evil factions warred endlessly over territory, resources, and holy ground. The Iron Assembly and Amalgamate held the center. Not out of morality — pure self interest. War disrupted trade, trade fed them both. Two neutral factions became the world's bankers, brokers, and suppliers. Everyone needed Iron. Everyone needed someone to sell to.

### The Convergence Problem
The 9th faction — The Convergence — existed quietly. Scholars, diplomats, mirrors of everyone. No home terrain, no allegiance. They studied all factions, traded with all factions, were trusted by all factions. Then they vanished. No war. No disaster. Just gone.

### The Slow Stranglehold
Decades pass. Rumors — Convergence found something. A mirror. An artifact. A truth about the world's magic schools that nobody else understood. They returned changed. Not militarily — economically. Compound interest on old debts. Trade route taxation. Resource monopolies built quietly over generations. By the time factions noticed the leash, it was already tight.

### Current State
| Faction | Response to Convergence Control |
|---|---|
| Holy Order | Resist — call it corruption, demand crusade |
| Crimson Wardens | Resist — hunt Convergence agents like monsters |
| Thornkin | Withdrew into deep forest — refused all trade |
| Eternal Empire | Ignored it — they have eternity, debts mean nothing to the undead |
| Bloodsworn | Profit from chaos — destabilized factions are easier prey |
| Voidkin | Accelerated it — void corruption spreads faster in destabilized territories |
| Iron Assembly | Sided with Convergence leverage — economic partners |
| Amalgamate | Withdrew entirely — no allegiance, pure evolution |

### Campaign Hook
Player starts as a Convergence hero — not yet aware of what the faction became. Origin story is discovering it. 4 alignment decisions are responses to that discovery. Who do you become when you learn what your faction did?

---

## HIDEOUT UPGRADE TREE

**Shape:** Tree with branches per category
**Currency:** XP from playing — battle pass style predefined rewards
**Balance:** Cosmetic only — does not affect campaign or custom map balance
**Endgame:** 9th faction (Convergence) unlocked via Sanctum branch

| Branch | Upgrades | Unlocks |
|---|---|---|
| Castle | Visual tiers — ruins → fortress → grand castle | Required for Sanctum |
| Barracks | Mob strength/variety on world map, unit injection quality | Required for Sanctum |
| Vault | Cosmetic hero skins, portrait unlocks | Required for Sanctum |
| Shrine | XP gain rate bonuses, battle pass tier accelerators | Optional |
| Sanctum | Convergence faction unlock — gated behind Castle + Barracks + Vault milestones | Convergence playable |

---

## NATURE POWER — SCHOOL CLASH (Thornkin vs Voidkin)

Same magic school, opposite alignment. Direct spell interaction when both are in battle:

| Rule | Detail |
|---|---|
| Same tier cast | Full cancel — both spells nullified, both mana costs paid |
| Goal for both | Never cast same tier as opponent |
| Thornkin strategy | Match corrupted tier to cancel, drain mana — Voidkin spells cost more so they run dry first |
| Voidkin strategy | Avoid tier matching, win through possession and units instead |
| Corrupted power | Stronger effect than pure equivalent but costs more |
| Pure advantage | Cheaper, reliable — wins the mana war long term |

---

## FORGE VS AMALGAMATE — COUNTER SYSTEM

Iron Assembly hard counters Amalgamate's adaptation mechanic via Blueprints:

| Blueprint | Effect vs Amalgamate |
|---|---|
| Nullifier Rounds | Attacks strip one adaptation per hit |
| Adaptation Lock | Target cannot gain new adaptations for 3 rounds |
| Biomass Converter | Destroyed Amalgamate units yield extra resources instead of adapting |
| EMP Pulse | Disables Synapse Node connections — breaks shared adaptation network |

| Scenario | Result |
|---|---|
| Iron Assembly prepared | Strips adaptations faster than Amalgamate gains them |
| Iron Assembly unprepared | Amalgamate adapts freely, becomes unkillable late |

