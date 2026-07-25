# Unit roles — design-match reference (generated from assets/data/units.json)

The data half of SPRITE_HANDOFF's open "unit design-match check": for each
unit, its intended combat role. Eyeball each RANGED unit's sprite for a
weapon that reads as ranged (bow/gun/casting pose) and each FLY unit for an
airborne silhouette. Regenerate this file after balance edits with the
script in the header of this file's git blame, or just re-ask Claude.

## Cross-faction summary (worth a design look)

| Faction | Units | Flyers | Ranged |
|---|---|---|---|
| Holy Order | 18 | 9 | 0 |
| Crimson Wardens | 18 | 7 | 3 |
| Thornkin | 18 | 1 | 0 |
| Eternal Empire | 18 | 6 | 0 |
| Bloodsworn | 18 | 0 | 3 |
| Voidkin | 18 | 18 | 3 |
| Iron Assembly | 18 | 0 | 9 |
| Amalgamate | 18 | 6 | 3 |
| Convergence | 18 | 11 | 0 |

**Observation (2026-07-25):** four factions field *zero* shooters (Holy
Order, Thornkin, Eternal Empire, Convergence) while Iron Assembly has 9 —
a third of its roster — and Voidkin is 18/18 flying. If that asymmetry is
intentional identity, fine; if not, sieges will feel very different per
faction (shooters dominate wall fights). Balance lives in units.json.

## 0 — Holy Order

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Squire | base | melee | 5 |  |
| Squire (A) | A | melee | 7 |  |
| Squire (B) | B | melee | 4 |  |
| Paladin | base | melee | 5 |  |
| Paladin (A) | A | melee | 5 |  |
| Paladin (B) | B | melee | 5 |  |
| Crusader | base | melee | 6 |  |
| Crusader (A) | A | melee | 6 |  |
| Crusader (B) | B | melee | 6 |  |
| Battle Cleric | base | melee+FLY | 7 |  |
| Battle Cleric (A) | A | melee+FLY | 7 |  |
| Battle Cleric (B) | B | melee+FLY | 7 |  |
| Holy Champion | base | melee+FLY | 9 |  |
| Holy Champion (A) | A | melee+FLY | 9 |  |
| Holy Champion (B) | B | melee+FLY | 11 |  |
| Archangel | base | melee+FLY | 12 |  |
| Archangel (A) | A | melee+FLY | 12 |  |
| Archangel (B) | B | melee+FLY | 12 |  |

## 1 — Crimson Wardens

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Scout | base | melee | 5 |  |
| Ranger | base | melee | 5 |  |
| Hunter | base | RANGED | 6 | 3 shots, range 5 |
| Berserker | base | melee+FLY | 10 |  |
| Warden Commander | base | melee | 8 |  |
| Warlord | base | melee+FLY | 11 |  |
| Scout (A) | A | melee | 5 |  |
| Ranger (A) | A | melee+FLY | 5 |  |
| Hunter (A) | A | RANGED | 6 | 4 shots, range 5 |
| Berserker (A) | A | melee+FLY | 10 |  |
| Warden Commander (A) | A | melee | 8 |  |
| Warlord (A) | A | melee+FLY | 11 |  |
| Scout (B) | B | melee | 5 |  |
| Ranger (B) | B | melee | 5 |  |
| Hunter (B) | B | RANGED | 6 | 3 shots, range 5 |
| Berserker (B) | B | melee+FLY | 10 |  |
| Warden Commander (B) | B | melee | 8 |  |
| Warlord (B) | B | melee+FLY | 11 |  |

## 2 — Thornkin

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Vine Sprite | base | melee | 5 |  |
| Thornkin Warrior | base | melee | 5 |  |
| Forest Guardian | base | melee | 5 |  |
| Treant | base | melee | 5 |  |
| Elder Thornkin | base | melee | 6 |  |
| Ancient Colossus | base | melee | 7 |  |
| Vine Sprite (A) | A | melee | 5 |  |
| Thornkin Warrior (A) | A | melee | 5 |  |
| Forest Guardian (A) | A | melee | 5 |  |
| Treant (A) | A | melee | 5 |  |
| Elder Thornkin (A) | A | melee | 6 |  |
| Ancient Colossus (A) | A | melee+FLY | 7 |  |
| Vine Sprite (B) | B | melee | 5 |  |
| Thornkin Warrior (B) | B | melee | 5 |  |
| Forest Guardian (B) | B | melee | 5 |  |
| Treant (B) | B | melee | 5 |  |
| Elder Thornkin (B) | B | melee | 6 |  |
| Ancient Colossus (B) | B | melee | 7 |  |

## 3 — Eternal Empire

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Skeleton Soldier | base | melee | 4 |  |
| Armoured Skeleton | base | melee | 5 |  |
| Zombie Warrior | base | melee | 6 |  |
| Death Knight | base | melee | 7 |  |
| Lich | base | melee+FLY | 8 |  |
| Eternal Emperor | base | melee+FLY | 10 |  |
| Skeleton Soldier (A) | A | melee | 4 |  |
| Armoured Skeleton (A) | A | melee | 5 |  |
| Zombie Warrior (A) | A | melee | 6 |  |
| Death Knight (A) | A | melee | 7 |  |
| Lich (A) | A | melee+FLY | 8 |  |
| Eternal Emperor (A) | A | melee+FLY | 10 |  |
| Skeleton Soldier (B) | B | melee | 4 |  |
| Armoured Skeleton (B) | B | melee | 5 |  |
| Zombie Warrior (B) | B | melee | 6 |  |
| Death Knight (B) | B | melee | 7 |  |
| Lich (B) | B | melee+FLY | 8 |  |
| Eternal Emperor (B) | B | melee+FLY | 10 |  |

## 4 — Bloodsworn

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Cultist | base | melee | 5 |  |
| Blood Warrior | base | melee | 6 |  |
| Berserker | base | melee | 6 |  |
| Blood Champion | base | melee | 8 |  |
| Oracle | base | RANGED | 9 | 4 shots, range 4 |
| Bloodsworn Avatar | base | melee | 11 |  |
| Cultist (A) | A | melee | 5 |  |
| Blood Warrior (A) | A | melee | 6 |  |
| Berserker (A) | A | melee | 6 |  |
| Blood Champion (A) | A | melee | 8 |  |
| Oracle (A) | A | RANGED | 9 | 4 shots, range 4 |
| Bloodsworn Avatar (A) | A | melee | 11 |  |
| Cultist (B) | B | melee | 5 |  |
| Blood Warrior (B) | B | melee | 6 |  |
| Berserker (B) | B | melee | 6 |  |
| Blood Champion (B) | B | melee | 8 |  |
| Oracle (B) | B | RANGED | 9 | 4 shots, range 4 |
| Bloodsworn Avatar (B) | B | melee | 11 |  |

## 5 — Voidkin

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Void Sprite | base | melee+FLY | 6 |  |
| Void Scout | base | melee+FLY | 6 |  |
| Void Stalker | base | melee+FLY | 7 |  |
| Void Mage | base | RANGED+FLY | 10 | 3 shots, range 5 |
| Void Wraith | base | melee+FLY | 12 |  |
| Void Herald | base | melee+FLY | 13 |  |
| Void Sprite (A) | A | melee+FLY | 7 |  |
| Void Scout (A) | A | melee+FLY | 7 |  |
| Void Stalker (A) | A | melee+FLY | 8 |  |
| Void Mage (A) | A | RANGED+FLY | 11 | 3 shots, range 6 |
| Void Wraith (A) | A | melee+FLY | 13 |  |
| Void Herald (A) | A | melee+FLY | 14 |  |
| Void Sprite (B) | B | melee+FLY | 5 |  |
| Void Scout (B) | B | melee+FLY | 5 |  |
| Void Stalker (B) | B | melee+FLY | 7 |  |
| Void Mage (B) | B | RANGED+FLY | 9 | 3 shots, range 5 |
| Void Wraith (B) | B | melee+FLY | 11 |  |
| Void Herald (B) | B | melee+FLY | 12 |  |

## 6 — Iron Assembly

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Automaton | base | melee | 5 |  |
| Infantry Unit | base | melee | 5 |  |
| Clockwork Warrior | base | melee | 5 |  |
| Gunner | base | RANGED | 4 | 2 shots, range 5 |
| Steam Colossus | base | RANGED | 5 | 3 shots, range 4 |
| Iron Titan | base | RANGED | 6 | 4 shots, range 4 |
| Automaton (A) | A | melee | 5 |  |
| Infantry Unit (A) | A | melee | 5 |  |
| Clockwork Warrior (A) | A | melee | 5 |  |
| Gunner (A) | A | RANGED | 4 | 3 shots, range 5 |
| Steam Colossus (A) | A | RANGED | 5 | 3 shots, range 4 |
| Iron Titan (A) | A | RANGED | 6 | 4 shots, range 4 |
| Automaton (B) | B | melee | 5 |  |
| Infantry Unit (B) | B | melee | 5 |  |
| Clockwork Warrior (B) | B | melee | 5 |  |
| Gunner (B) | B | RANGED | 4 | 3 shots, range 5 |
| Steam Colossus (B) | B | RANGED | 5 | 3 shots, range 4 |
| Iron Titan (B) | B | RANGED | 6 | 4 shots, range 4 |

## 7 — Amalgamate

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Crawler | base | melee | 5 |  |
| Flesh Warrior | base | melee | 5 |  |
| Brute | base | melee | 6 |  |
| Behemoth | base | melee+FLY | 7 |  |
| Flesh Colossus | base | RANGED | 8 | 3 shots, range 3 |
| Apex | base | melee+FLY | 9 |  |
| Crawler (A) | A | melee | 5 |  |
| Flesh Warrior (A) | A | melee | 5 |  |
| Brute (A) | A | melee | 6 |  |
| Behemoth (A) | A | melee+FLY | 7 |  |
| Flesh Colossus (A) | A | RANGED | 8 | 3 shots, range 3 |
| Apex (A) | A | melee+FLY | 9 |  |
| Crawler (B) | B | melee | 5 |  |
| Flesh Warrior (B) | B | melee | 5 |  |
| Brute (B) | B | melee | 6 |  |
| Behemoth (B) | B | melee+FLY | 7 |  |
| Flesh Colossus (B) | B | RANGED | 8 | 3 shots, range 3 |
| Apex (B) | B | melee+FLY | 9 |  |

## 8 — Convergence

| Unit | Path | Role | Speed | Notes |
|---|---|---|---|---|
| Initiate | base | melee | 5 |  |
| Soldier | base | melee | 6 |  |
| Mirror Warrior | base | melee | 6 |  |
| Champion | base | melee+FLY | 8 |  |
| Elite | base | melee+FLY | 10 |  |
| Convergence Prime | base | melee+FLY | 12 |  |
| Initiate (A) | A | melee | 5 |  |
| Soldier (A) | A | melee+FLY | 6 |  |
| Mirror Warrior (A) | A | melee+FLY | 6 |  |
| Champion (A) | A | melee+FLY | 8 |  |
| Elite (A) | A | melee+FLY | 10 |  |
| Convergence Prime (A) | A | melee+FLY | 12 |  |
| Initiate (B) | B | melee | 5 |  |
| Soldier (B) | B | melee | 6 |  |
| Mirror Warrior (B) | B | melee | 6 |  |
| Champion (B) | B | melee+FLY | 8 |  |
| Elite (B) | B | melee+FLY | 10 |  |
| Convergence Prime (B) | B | melee+FLY | 12 |  |
