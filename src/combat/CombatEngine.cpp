#include "../core/DevLog.h"
#include "CombatEngine.h"
#include "../hero/SkillRegistry.h"
#include "../magic/SpellRegistry.h"
#include <algorithm>
#include <random>
#include <stdio.h>
#include <sstream>

static thread_local std::mt19937 s_turnRng{42};

// ── Faction terrain tables (from design doc) ───────────────────────────────────
static bool isFactionHomeTerrain(FactionId f, Terrain t) {
    switch (f) {
    case FactionId::HolyOrder:      return t == Terrain::Plains    || t == Terrain::Sacred;
    case FactionId::CrimsonWardens: return t == Terrain::Highland  || t == Terrain::Rocky;
    case FactionId::Thornkin:       return t == Terrain::Forest;
    case FactionId::EternalEmpire:  return t == Terrain::Toxic     || t == Terrain::Corrupted;
    case FactionId::Bloodsworn:     return t == Terrain::Corrupted || t == Terrain::Swamp;
    case FactionId::Voidkin:        return t == Terrain::CorruptedForest;
    case FactionId::IronAssembly:   return t == Terrain::Industrial || t == Terrain::Rocky;
    case FactionId::Amalgamate:     return t == Terrain::Wasteland || t == Terrain::FleshZone;
    default: return false;
    }
}
static bool isFactionPenaltyTerrain(FactionId f, Terrain t) {
    switch (f) {
    case FactionId::HolyOrder:      return t == Terrain::Corrupted || t == Terrain::Toxic;
    case FactionId::CrimsonWardens: return t == Terrain::Corrupted || t == Terrain::Swamp;
    case FactionId::Thornkin:       return t == Terrain::Volcanic  || t == Terrain::Barren;
    case FactionId::EternalEmpire:  return t == Terrain::Sacred;
    case FactionId::Bloodsworn:     return t == Terrain::Sacred    || t == Terrain::Plains;
    case FactionId::Voidkin:        return t == Terrain::Sacred;
    case FactionId::IronAssembly:   return t == Terrain::Swamp     || t == Terrain::Water;
    case FactionId::Amalgamate:     return t == Terrain::Sacred;
    default: return false;
    }
}

void CombatEngine::seedTurnRng(uint32_t seed) { s_turnRng.seed(seed); }

void CombatEngine::addLog(const std::string& msg)
{
    if (m_silent) return;
    m_log.push_back({msg});
    if (m_logCb) m_logCb(msg);
    else gLog("[Combat] %s\n", msg.c_str());
}

void CombatEngine::logMoraleSurge(const CombatUnit& u)
{
    addLog(u.name + " morale surge \xe2\x80\x94 bonus action!");
    if (m_moraleCb) m_moraleCb(u.id, u.pos);
}


// ── Start battle ───────────────────────────────────────────────────────────────
void CombatEngine::startBattle(
    const Hero& playerHero, const std::vector<CombatUnit>& playerUnits,
    const Hero& enemyHero,  const std::vector<CombatUnit>& enemyUnits,
    bool isSiege, Terrain terrain)
{
    m_playerHero    = playerHero;
    m_enemyHero     = enemyHero;
    m_round         = 1;
    m_turnIndex     = 0;
    m_battleTerrain = terrain;
    m_bloodPool     = 0;
    m_ascended      = false;
    m_log.clear();
    m_waitQueue.clear();

    m_isSiege = isSiege;
    m_grid.init(48.0f);
    if (!isSiege) {
        m_grid.placeRandomSpecialTiles(4, s_turnRng());
    } else {
        // Siege: walls at column 5; fort building adds +50% wall HP
        m_grid.placeSiegeWalls(40, 20);
    }

    // Place player units on left side (columns 0-1 normally, or 0-3 for siege engines)
    for (auto& u : playerUnits) {
        CombatUnit copy = u;
        copy.isPlayer  = true;
        copy.shotsLeft = u.shots;
        // Siege engines start on turn 1 as "building" (acted already)
        if (isSiege && copy.isSiegeEngine && !copy.wallBypass) copy.hasActed = true;
        uint32_t id = m_grid.addUnit(copy);
        CombatUnit* placed = m_grid.getUnit(id);
        if (placed) {
            // Siege drill: place behind the wall (columns 6-7)
            int startQ = 0;
            if (isSiege && placed->wallBypass) startQ = 6;
            for (int row = 0; row < CombatGrid::ROWS; ++row) {
                int q = startQ + (placed->stackSlot % 2);
                int r = row - (q - (q & 1)) / 2;
                HexCoord h{q, r};
                if (m_grid.inBounds(h) && !m_grid.getTile(h)->occupied
                    && m_grid.getTile(h)->type != CombatTileType::Wall) {
                    m_grid.placeUnit(*placed, h);
                    break;
                }
            }
        }
    }

    // Place enemy units: normal combat right (cols 9-10), siege defenders (cols 7-10)
    for (auto& u : enemyUnits) {
        CombatUnit copy = u;
        copy.isPlayer  = false;
        copy.shotsLeft = u.shots;
        uint32_t id = m_grid.addUnit(copy);
        CombatUnit* placed = m_grid.getUnit(id);
        if (placed) {
            int startQ = isSiege ? (CombatGrid::COLS - 4 + (placed->stackSlot % 4))
                                 : (CombatGrid::COLS - 1 - (placed->stackSlot % 2));
            for (int row = 0; row < CombatGrid::ROWS; ++row) {
                int q = startQ;
                int r = row - (q - (q & 1)) / 2;
                HexCoord h{q, r};
                if (m_grid.inBounds(h) && !m_grid.getTile(h)->occupied
                    && m_grid.getTile(h)->type != CombatTileType::Wall) {
                    m_grid.placeUnit(*placed, h);
                    break;
                }
            }
        }
    }

    // Tally starting enemy count for XP calculation
    m_enemyStartCount = 0;
    for (auto& u : m_grid.units())
        if (!u.isPlayer) m_enemyStartCount += u.count;

    buildTurnOrder();

    // Apply TACTICS skill: bonus to hero copies before transferring to units
    auto applyTactics = [](Hero& hero) {
        if (const SkillInstance* s = hero.skills.getSkill(SkillID::TACTICS)) {
            if (const SkillDef* def = findSkillDef(SkillID::TACTICS)) {
                int v = def->values[static_cast<int>(s->tier)];
                hero.attack  += v;
                hero.defense += v;
            }
        }
    };
    applyTactics(m_playerHero);
    applyTactics(m_enemyHero);

    // Apply hero attack/defense bonuses to their unit stacks
    for (auto& u : m_grid.units()) {
        if (u.isPlayer) {
            u.attack  += m_playerHero.attack;
            u.defense += m_playerHero.defense;
        } else {
            u.attack  += m_enemyHero.attack;
            u.defense += m_enemyHero.defense;
        }
    }

    // Apply passive skill bonuses
    auto applySkills = [this](const Hero& hero, bool isPlayer) {
        const HeroSkills& skills = hero.skills;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive) continue;
            auto applyIf = [&](int sid, auto fn) {
                if (const SkillInstance* s = skills.getSkill(sid)) {
                    if (const SkillDef* def = findSkillDef(sid))
                        fn(u, def->values[static_cast<int>(s->tier)]);
                }
            };
            applyIf(SkillID::OFFENSE,       [](CombatUnit& u, int v){ u.attack  += v; });
            applyIf(SkillID::DEFENSE_SKILL, [](CombatUnit& u, int v){ u.defense += v; });
            applyIf(SkillID::LEADERSHIP,    [](CombatUnit& u, int v){
                u.morale = std::min(100, u.morale + v);
            });
            applyIf(SkillID::LUCK, [](CombatUnit& u, int v){ u.luck += v; });
            // ARCHERY — only for ranged units
            if (u.range > 0 && u.shotsLeft > 0)
                applyIf(SkillID::ARCHERY, [](CombatUnit& u, int v){ u.attack += v; });
        }

        // ETERNAL_CMD: grant second-life to all this hero's units (+% stats on revival)
        if (const SkillInstance* s = hero.skills.getSkill(SkillID::ETERNAL_CMD)) {
            bool fullHeal = hero.eternalLegionSpecialty;
            int strBonus = 0;
            if (const SkillDef* def = findSkillDef(SkillID::ETERNAL_CMD))
                strBonus = def->values[static_cast<int>(s->tier)]; // 10/20/30
            for (auto& u : m_grid.units()) {
                if (u.isPlayer != isPlayer || !u.alive) continue;
                u.hasSecondLife       = true;
                u.secondLifeFullHeal  = fullHeal;
                u.secondLifeStrBonus  = strBonus;
            }
            addLog(hero.name + " EternalCmd: units gain second life (+"
                   + std::to_string(strBonus) + "% stats on revival"
                   + (fullHeal ? ", full HP" : "") + ")");
        }

        // IronDiscipline specialty: Warlord Mechanic's constructs ignore morale loss
        if (hero.ironDiscipline) {
            for (auto& u : m_grid.units())
                if (u.isPlayer == isPlayer && u.alive) u.moraleImmune = true;
        }

        // DESPERATION (HolyOrder): Holy units start with a pre-charged desperation meter
        if (const SkillInstance* s = skills.getSkill(SkillID::DESPERATION)) {
            if (const SkillDef* def = findSkillDef(SkillID::DESPERATION)) {
                int headStart = def->values[static_cast<int>(s->tier)];
                for (auto& u : m_grid.units()) {
                    if (u.isPlayer != isPlayer || !u.alive) continue;
                    if (hasTag(u.tags, UnitTag::Holy))
                        u.desperationMeter = std::min(100, u.desperationMeter + headStart);
                }
                addLog(hero.name + " Desperation: Holy units start at " +
                       std::to_string(headStart) + "/100 charge");
            }
        }

        // INSPIRATION (HolyOrder): all allied units receive a morale bonus at battle start
        if (const SkillInstance* s = skills.getSkill(SkillID::INSPIRATION)) {
            if (const SkillDef* def = findSkillDef(SkillID::INSPIRATION)) {
                int moraleGain = def->values[static_cast<int>(s->tier)];
                for (auto& u : m_grid.units()) {
                    if (u.isPlayer != isPlayer || !u.alive || u.moraleImmune) continue;
                    u.morale = std::min(100, u.morale + moraleGain);
                }
                addLog(hero.name + " Inspiration: all units +" + std::to_string(moraleGain) + " morale");
            }
        }

        // ADAPTATION (Amalgamate): OrganicMech units adapt faster under fire
        // Basic: threshold 2 hits; Advanced: threshold 2 hits + double stat gain; Master: every hit
        if (const SkillInstance* s = skills.getSkill(SkillID::ADAPTATION)) {
            for (auto& u : m_grid.units()) {
                if (u.isPlayer != isPlayer || !u.alive) continue;
                if (!hasTag(u.tags, UnitTag::OrganicMech)) continue;
                if (s->tier == SkillTier::Master) {
                    u.rapidEvolution = true;
                } else {
                    u.adaptationFast = true;
                    if (s->tier == SkillTier::Advanced) u.adaptationDouble = true;
                }
            }
            const char* tier_desc = s->tier == SkillTier::Master ? "every hit" :
                                    s->tier == SkillTier::Advanced ? "after 2 hits (+2 stat)" : "after 2 hits";
            addLog(hero.name + " Adaptation: OrganicMech units adapt " + tier_desc);
        }

        // BLOOD_POOL (Bloodsworn): 5 BloodBound kills → Ascension burst (tracked in processKillEvents)
        if (skills.getSkill(SkillID::BLOOD_POOL)) {
            addLog(hero.name + " Blood Pool active — 5 BloodBound kills trigger Ascension!");
        }

        // POSSESSION (Voidkin): Void unit kills reanimate the target for 1 round (processKillEvents)
        if (skills.getSkill(SkillID::POSSESSION)) {
            addLog(hero.name + " Possession active — Void kills will reanimate targets!");
        }

        // MIRRORING (Convergence): Humanoid units gain a bonus to their weaker combat stat
        if (const SkillInstance* s = skills.getSkill(SkillID::MIRRORING)) {
            int mirrorBonus = 0;
            if (const SkillDef* def = findSkillDef(SkillID::MIRRORING))
                mirrorBonus = def->values[static_cast<int>(s->tier)];
            for (auto& u : m_grid.units()) {
                if (u.isPlayer != isPlayer || !u.alive) continue;
                if (!hasTag(u.tags, UnitTag::Humanoid)) continue;
                if (u.attack <= u.defense) u.attack  += mirrorBonus;
                else                       u.defense += mirrorBonus;
            }
            addLog(hero.name + " Mirroring: Humanoid units +" + std::to_string(mirrorBonus) + " to weaker stat");
        }
    };
    applySkills(m_playerHero, true);
    applySkills(m_enemyHero,  false);

    // ── Skill archetype bonuses — compound rewards for committed playstyle ─────
    auto applyArchetype = [this](Hero& hero, bool isPlayer) {
        const HeroSkills& skills = hero.skills;

        static const int kMight[] = {
            SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::ARCHERY,
            SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::LOGISTICS,
            SkillID::SCOUTING, SkillID::FIRST_AID, SkillID::LUCK
        };
        static const int kMagic[] = {
            SkillID::LIGHT_MAGIC, SkillID::BLOOD_MAGIC, SkillID::DEATH_MAGIC,
            SkillID::NATURE_MAGIC, SkillID::FORGE_MAGIC, SkillID::FLESH_MAGIC
        };

        int mightCount = 0, magicCount = 0;
        for (int sid : kMight) if (skills.hasSkill(sid)) ++mightCount;
        for (int sid : kMagic) if (skills.hasSkill(sid)) ++magicCount;

        if (mightCount == 0 && magicCount == 0) return;

        // Flat combo bonuses — stacking more skills of same type gives unit bonuses
        int mightBonus = (mightCount >= 4) ? 2 : (mightCount >= 2) ? 1 : 0;
        int magicBonus = (magicCount >= 3) ? 2 : (magicCount >= 2) ? 1 : 0;

        if (mightBonus > 0) {
            for (auto& u : m_grid.units()) {
                if (u.isPlayer != isPlayer || !u.alive) continue;
                u.attack  += mightBonus;
                u.defense += mightBonus;
            }
            addLog(hero.name + " Might synergy (x" + std::to_string(mightCount)
                   + "): +" + std::to_string(mightBonus) + " ATK/DEF to all units");
        }
        if (magicBonus > 0) {
            hero.lightPower  += magicBonus;
            hero.bloodPower  += magicBonus;
            hero.deathPower  += magicBonus;
            hero.naturePower += magicBonus;
            hero.forgePower  += magicBonus;
            hero.fleshPower  += magicBonus;
            addLog(hero.name + " Magic synergy (x" + std::to_string(magicCount)
                   + "): +" + std::to_string(magicBonus) + " to all casting stats");
        }

        // Archetype bonuses — pure commitment gets a special bonus
        bool pureMight = (mightCount >= 5 && magicCount == 0);
        bool pureMagic = (magicCount >= 4 && mightCount <= 1);
        bool warlord   = (!pureMight && !pureMagic && mightCount >= 3 && magicCount >= 2);

        if (pureMight) {
            for (auto& u : m_grid.units()) {
                if (u.isPlayer != isPlayer || !u.alive) continue;
                u.speed += 1;
                int hpGain = (u.hp + 9) / 10;
                u.hp    += hpGain;
                u.maxHp += (u.maxHp + 9) / 10;
            }
            addLog(hero.name + " PURE MIGHT: all units +1 Speed, +10% HP");
        } else if (pureMagic) {
            hero.lightPower  += 3;
            hero.bloodPower  += 3;
            hero.deathPower  += 3;
            hero.naturePower += 3;
            hero.forgePower  += 3;
            hero.fleshPower  += 3;
            addLog(hero.name + " PURE MAGIC: +3 to all casting stats");
        } else if (warlord) {
            for (auto& u : m_grid.units()) {
                if (u.isPlayer != isPlayer || !u.alive) continue;
                u.morale = std::min(100, u.morale + 1);
                u.luck   = std::min(5, u.luck + 1);
            }
            addLog(hero.name + " WARLORD: all units +1 Morale, +1 Luck");
        }
    };
    applyArchetype(m_playerHero, true);
    applyArchetype(m_enemyHero,  false);

    // Harmony specialty (Warsinger) — each unit adjacent to an ally gets +1 ATK/DEF
    auto applyHarmony = [this](const Hero& hero, bool isPlayer) {
        if (!hero.harmonySpecialty) return;
        int bonusCount = 0;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive) continue;
            for (const auto& other : m_grid.units()) {
                if (other.id == u.id || !other.alive || other.isPlayer != isPlayer) continue;
                if (HexGrid::distance(u.pos, other.pos) <= 1) {
                    u.attack++;
                    u.defense++;
                    bonusCount++;
                    break;
                }
            }
        }
        if (bonusCount > 0)
            addLog(hero.name + " Harmony: " + std::to_string(bonusCount) +
                   " units in pairs gain +1 ATK/DEF");
    };
    applyHarmony(m_playerHero, true);
    applyHarmony(m_enemyHero,  false);

    // BloodPenance specialty (Flagellant Marshal) — units start with +2 ATK; hero bleeds each round
    auto applyBloodPenance = [this](const Hero& hero, bool isPlayer) {
        if (!hero.bloodPenanceSpecialty) return;
        for (auto& u : m_grid.units())
            if (u.isPlayer == isPlayer && u.alive) u.attack += 2;
        addLog(hero.name + " Blood Penance: all units +2 ATK (hero will bleed each round)");
    };
    applyBloodPenance(m_playerHero, true);
    applyBloodPenance(m_enemyHero,  false);

    // Overgrowth specialty (Pathfinder) — place 3 Speed tiles on caster's side
    auto applyOvergrowth = [this](const Hero& hero, bool isPlayer) {
        if (!hero.overgrowthSpecialty) return;
        int placed = 0;
        for (const auto& coord : m_grid.allCoords()) {
            if (placed >= 3) break;
            bool inZone = isPlayer ? (coord.q <= 3) : (coord.q >= CombatGrid::COLS - 4);
            if (!inZone) continue;
            CombatTile* t = m_grid.getTile(coord);
            if (t && t->type == CombatTileType::Normal && !t->occupied) {
                m_grid.setTileType(coord, CombatTileType::Speed);
                placed++;
            }
        }
        if (placed > 0)
            addLog(hero.name + " Overgrowth: " + std::to_string(placed) + " forest tiles placed");
    };
    applyOvergrowth(m_playerHero, true);
    applyOvergrowth(m_enemyHero,  false);

    // Swarm specialty (Thrall Master) — BloodBound units begin at full morale
    auto applySwarm = [this](const Hero& hero, bool isPlayer) {
        if (!hero.swarmSpecialty) return;
        int boosted = 0;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive) continue;
            if (hasTag(u.tags, UnitTag::BloodBound)) {
                u.morale = 100;
                boosted++;
            }
        }
        if (boosted > 0)
            addLog(hero.name + " Swarm: " + std::to_string(boosted) +
                   " BloodBound units start at full morale");
    };
    applySwarm(m_playerHero, true);
    applySwarm(m_enemyHero,  false);

    // RapidEvolution specialty (Evolver): OrganicMech units adapt on every hit
    auto applyRapidEvolution = [this](const Hero& hero, bool isPlayer) {
        if (!hero.rapidEvolutionSpecialty) return;
        for (auto& u : m_grid.units())
            if (u.isPlayer == isPlayer && u.alive && hasTag(u.tags, UnitTag::OrganicMech))
                u.rapidEvolution = true;
        addLog(hero.name + " RapidEvolution: OrganicMech units adapt instantly");
    };
    applyRapidEvolution(m_playerHero, true);
    applyRapidEvolution(m_enemyHero,  false);

    // Collective: at round start, all allied OrganicMech units share the best adaptation bonus
    // (Implemented as a per-round effect in processRoundStartEffects — see below)
    // But at battle start, pre-set the flag on units for reference
    auto applyCollective = [this](const Hero& hero, bool isPlayer) {
        if (!hero.collectiveSpecialty) return;
        // Find best adaptation count among allied OrganicMech units
        int bestAdapt = 0;
        for (const auto& u : m_grid.units())
            if (u.isPlayer == isPlayer && u.alive && hasTag(u.tags, UnitTag::OrganicMech))
                bestAdapt = std::max(bestAdapt, u.adaptationsGained);
        if (bestAdapt <= 0) return;
        // Give all OrganicMech units +1 ATK to represent shared knowledge
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive || !hasTag(u.tags, UnitTag::OrganicMech)) continue;
            if (u.adaptationsGained < bestAdapt) {
                u.attack++;
                u.adaptationsGained++;  // count it so future sharing builds on this
            }
        }
        addLog(hero.name + " Collective: OrganicMech units share adaptations (best=" + std::to_string(bestAdapt) + ")");
    };
    applyCollective(m_playerHero, true);
    applyCollective(m_enemyHero,  false);

    // Apex: all allied OrganicMech units start with full adaptations (6 stat gains)
    auto applyApex = [this](const Hero& hero, bool isPlayer) {
        if (!hero.apexSpecialty) return;
        int boosted = 0;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive || !hasTag(u.tags, UnitTag::OrganicMech)) continue;
            int gap = 6 - u.adaptationsGained;
            if (gap <= 0) continue;
            // Even adaptations give ATK, odd give DEF (mirror the DamageCalc logic)
            for (int i = u.adaptationsGained; i < 6; i++) {
                if (i % 2 == 0) u.attack++;
                else             u.defense++;
            }
            u.adaptationsGained = 6;
            boosted++;
        }
        if (boosted > 0)
            addLog(hero.name + " Apex: " + std::to_string(boosted) + " OrganicMech units start fully adapted");
    };
    applyApex(m_playerHero, true);
    applyApex(m_enemyHero,  false);

    applyTerrainBonuses();

    m_coordinatedStrikeTarget = 0;
    m_wildGrowthGhosted.clear();

    auto* firstUnit = activeUnit();
    m_phase = (firstUnit && !firstUnit->isPlayer) ? CombatPhase::EnemyTurn
                                                  : CombatPhase::PlayerTurn;
    addLog("Battle started! Round 1");
}

void CombatEngine::applyTerrainBonuses()
{
    for (auto& u : m_grid.units()) {
        if (!u.alive) continue;
        FactionId fac = u.isPlayer ? m_playerHero.faction : m_enemyHero.faction;
        if (isFactionHomeTerrain(fac, m_battleTerrain)) {
            u.attack  += 1;
            u.defense += 1;
        }
        if (isFactionPenaltyTerrain(fac, m_battleTerrain)) {
            u.attack  = std::max(1, u.attack  - 1);
            u.defense = std::max(1, u.defense - 1);
        }
    }
    // Log if either faction has a terrain bonus/penalty
    auto logTerrain = [&](const Hero& hero, bool isPlayer) {
        FactionId fac = hero.faction;
        if (isFactionHomeTerrain(fac, m_battleTerrain))
            addLog(hero.name + ": home terrain — all units +1 ATK/DEF");
        if (isFactionPenaltyTerrain(fac, m_battleTerrain))
            addLog(hero.name + ": penalty terrain — all units -1 ATK/DEF");
    };
    logTerrain(m_playerHero, true);
    logTerrain(m_enemyHero,  false);
}

void CombatEngine::applyTerrainObstacles(int count)
{
    m_grid.placeObstacleTiles(count, s_turnRng());
}

// ── Artifact bonuses ───────────────────────────────────────────────────────────
void CombatEngine::applyArtifactBonuses(const ArtifactBonus& pb, const ArtifactBonus& eb)
{
    for (auto& u : m_grid.units()) {
        const ArtifactBonus& b = u.isPlayer ? pb : eb;
        u.attack  += b.attack;
        u.defense += b.defense;
        u.speed   += b.moveBonus;
        if (b.hpBonus > 0) {
            u.maxHp += b.hpBonus;
            u.hp    += b.hpBonus;
        }
    }
    // Hero-level bonuses affect spell casting inside the engine
    auto applyHero = [](Hero& h, const ArtifactBonus& b) {
        h.lightPower  += b.lightPower;
        h.bloodPower  += b.bloodPower;
        h.deathPower  += b.deathPower;
        h.naturePower += b.naturePower;
        h.forgePower  += b.forgePower;
        h.fleshPower  += b.fleshPower;
        h.mana        += b.manaBonus;
        h.maxMana     += b.manaBonus;
    };
    applyHero(m_playerHero, pb);
    applyHero(m_enemyHero,  eb);
}

void CombatEngine::applyPlayerTownBonus(int lightP, int bloodP, int deathP,
                                        int natureP, int forgeP, int fleshP,
                                        int mechSpeedBonus, int holyDespBonus,
                                        bool eternalMonument, bool wardenBrand,
                                        bool symbiosisWeb, bool warShrine,
                                        bool voidLens, bool mergeChamber,
                                        bool resonanceWell, bool mirrorChamber)
{
    m_playerHero.lightPower  += lightP;
    m_playerHero.bloodPower  += bloodP;
    m_playerHero.deathPower  += deathP;
    m_playerHero.naturePower += natureP;
    m_playerHero.forgePower  += forgeP;
    m_playerHero.fleshPower  += fleshP;

    m_wardenBrand  = wardenBrand;
    m_symbiosisWeb = symbiosisWeb;

    for (auto& u : m_grid.units()) {
        if (!u.isPlayer) continue;
        if (mechSpeedBonus > 0 && hasTag(u.tags, UnitTag::Mechanical))
            u.speed += mechSpeedBonus;
        if (holyDespBonus > 0 && hasTag(u.tags, UnitTag::Holy))
            u.desperationMeter = std::min(100, u.desperationMeter + holyDespBonus);
        if (eternalMonument && (hasTag(u.tags, UnitTag::Undead) || hasTag(u.tags, UnitTag::Holy)))
            u.hasSecondLife = true;
        // War Shrine: BloodBound start with bonus morale
        if (warShrine && hasTag(u.tags, UnitTag::BloodBound) && !u.moraleImmune)
            u.morale = std::min(100, u.morale + 15);
        // Void Lens: Void units gain +1 ATK at battle start
        if (voidLens && hasTag(u.tags, UnitTag::Void))
            u.attack++;
        // Merge Chamber: OrganicMech units adapt after 2 hits instead of 3
        if (mergeChamber && hasTag(u.tags, UnitTag::OrganicMech))
            u.adaptationFast = true;
        // Resonance Well: Convergence (Humanoid) units start with +1 ATK+DEF
        if (resonanceWell && hasTag(u.tags, UnitTag::Humanoid)) {
            u.attack++;
            u.defense++;
        }
        // Mirror Chamber (requires Resonance Well): additional +1 ATK+DEF
        if (mirrorChamber && hasTag(u.tags, UnitTag::Humanoid)) {
            u.attack++;
            u.defense++;
        }
    }
    // Mirror Chamber also buffs the hero
    if (mirrorChamber) {
        m_playerHero.attack++;
        m_playerHero.defense++;
    }

    if (holyDespBonus > 0)
        addLog("Reliquary: Holy units +" + std::to_string(holyDespBonus) + " Desperation");
    if (eternalMonument)
        addLog("Eternal Monument: Undead/Holy units gain second life");
    if (warShrine)
        addLog("War Shrine: BloodBound units +15 morale");
    if (voidLens)
        addLog("Void Lens: Void units +1 ATK");
    if (mergeChamber)
        addLog("Merge Chamber: OrganicMech units adapt after 2 hits");
    if (wardenBrand)
        addLog("Warden's Brand Chamber: Warden's Mark splashes +1 target");
    if (symbiosisWeb)
        addLog("Symbiosis Web: Beast bond cap raised to full skill tier value");
    if (resonanceWell)
        addLog("Resonance Well: Convergence units +1 ATK+DEF");
    if (mirrorChamber)
        addLog("Mirror Chamber: Convergence units +1 ATK+DEF more, hero +1 ATK+DEF");
}

// ── Turn order ─────────────────────────────────────────────────────────────────
void CombatEngine::buildTurnOrder()
{
    m_enemyHeroSpellUsed = false;
    m_coordinatedStrikeTarget = 0;  // mark expires each round
    m_turnOrder.clear();
    for (auto& u : m_grid.units())
        if (u.alive) m_turnOrder.push_back(u.id);

    // Sort by speed descending
    std::stable_sort(m_turnOrder.begin(), m_turnOrder.end(),
        [this](uint32_t a, uint32_t b) {
            auto* ua = m_grid.getUnit(a);
            auto* ub = m_grid.getUnit(b);
            if (!ua || !ub) return false;
            return ua->speed > ub->speed;
        });

    // Shuffle within each equal-speed group to remove player-side turn bias
    for (size_t i = 0; i < m_turnOrder.size(); ) {
        auto* ua = m_grid.getUnit(m_turnOrder[i]);
        int   spd = ua ? ua->speed : 0;
        size_t j = i + 1;
        while (j < m_turnOrder.size()) {
            auto* ub = m_grid.getUnit(m_turnOrder[j]);
            if (!ub || ub->speed != spd) break;
            ++j;
        }
        if (j - i > 1)
            std::shuffle(m_turnOrder.begin() + i, m_turnOrder.begin() + j, s_turnRng);
        i = j;
    }

    m_turnIndex = 0;
    m_waitQueue.clear();
}

CombatUnit* CombatEngine::activeUnit()
{
    if (m_turnIndex >= static_cast<int>(m_turnOrder.size())) return nullptr;
    return m_grid.getUnit(m_turnOrder[m_turnIndex]);
}

// ── Advance to next unit ───────────────────────────────────────────────────────
void CombatEngine::advanceTurn()
{
    // Spawn WildGrowth ghosts before removing dead units
    spawnWildGrowthGhosts();
    m_grid.removeDeadUnits();

    m_turnIndex++;

    // Skip dead units
    while (m_turnIndex < static_cast<int>(m_turnOrder.size())) {
        auto* u = m_grid.getUnit(m_turnOrder[m_turnIndex]);
        if (u && u->alive) break;
        m_turnIndex++;
    }

    // Round over — all units acted (including wait queue)
    if (m_turnIndex >= static_cast<int>(m_turnOrder.size())) {
        // Append wait queue to end
        if (!m_waitQueue.empty()) {
            m_turnOrder.insert(m_turnOrder.end(),
                m_waitQueue.begin(), m_waitQueue.end());
            m_waitQueue.clear();
        } else {
            // New round
            m_round++;
            // Mana regenerates 3 per round for both heroes
            m_playerHero.mana = std::min(m_playerHero.maxMana, m_playerHero.mana + 3);
            m_enemyHero.mana  = std::min(m_enemyHero.maxMana,  m_enemyHero.mana  + 3);
            // MYSTICISM: extra mana regen per round
            auto applyMysticism = [](Hero& h) {
                if (const SkillInstance* s = h.skills.getSkill(SkillID::MYSTICISM)) {
                    if (const SkillDef* def = findSkillDef(SkillID::MYSTICISM))
                        h.mana = std::min(h.maxMana,
                            h.mana + def->values[static_cast<int>(s->tier)]);
                }
            };
            applyMysticism(m_playerHero);
            applyMysticism(m_enemyHero);
            for (auto& u : m_grid.units()) u.newRound();
            buildTurnOrder();
            processRoundStartEffects();
            applySymbiosisRound();
            addLog("=== Round " + std::to_string(m_round) + " ===");
        }
    }

    checkVictory();
    if (m_phase == CombatPhase::Victory || m_phase == CombatPhase::Defeat) return;

    // Set phase based on whose turn it is
    auto* next = activeUnit();
    if (next) {
        // Regeneration: restore full HP of top unit at start of turn
        if (next->regenerates && next->alive) {
            int regained = next->maxHp - next->hp;
            next->hp = next->maxHp;
            addLog(next->name + " regenerates!");
            if (regained > 0 && m_healCb) m_healCb(next->id, regained, next->pos);
        }
        m_phase = next->isPlayer ? CombatPhase::PlayerTurn : CombatPhase::EnemyTurn;
        if (!next->isPlayer) processAITurn();
    }
}

// ── Submit player action ───────────────────────────────────────────────────────
bool CombatEngine::submitAction(const CombatAction& action)
{
    if (m_phase != CombatPhase::PlayerTurn) return false;

    CombatUnit* unit = activeUnit();
    if (!unit || !unit->isPlayer) return false;

    switch (action.type) {
    case ActionType::Move: {
        if (unit->hasMoved) return false;
        auto path = m_grid.findPath(unit->pos, action.target, unit->flying);
        if (path.empty() && !(action.target == unit->pos)) return false;
        if (!path.empty()) {
            m_grid.moveUnit(unit->id, action.target);
            unit->hasMoved = true;
            applyTileEffect(*unit);
            addLog(unit->name + " moved to (" +
                std::to_string(action.target.q) + "," +
                std::to_string(action.target.r) + ")");
        }
        // Auto-end turn if no attack is possible from the new position
        {
            bool canAttack = false;
            if (unit->range > 0 && unit->shotsLeft > 0) {
                canAttack = true;  // ranged unit still has shots
            } else {
                for (const auto& other : m_grid.units()) {
                    if (!other.alive || other.isPlayer) continue;
                    if (HexGrid::distance(unit->pos, other.pos) == 1) { canAttack = true; break; }
                }
            }
            // Siege: also allow action if adjacent to (or in range of) a wall tile
            if (!canAttack && m_isSiege) {
                if (unit->isSiegeEngine && unit->range > 0 && unit->shotsLeft > 0) {
                    for (const auto& coord : m_grid.allCoords()) {
                        if (m_grid.isWallTile(coord) &&
                            HexGrid::distance(unit->pos, coord) <= unit->range) {
                            canAttack = true; break;
                        }
                    }
                } else {
                    for (const auto& nb : HexGrid::neighbors(unit->pos)) {
                        if (m_grid.isWallTile(nb)) { canAttack = true; break; }
                    }
                }
            }
            if (!canAttack) {
                unit->hasActed = true;
                advanceTurn();
            }
        }
        return true;
    }
    case ActionType::Attack: {
        CombatUnit* target = m_grid.getUnit(action.targetUnitId);
        if (!target || target->isPlayer || !target->alive) return false;

        // Check adjacency for melee
        if (unit->range == 0) {
            if (HexGrid::distance(unit->pos, target->pos) > 1) return false;
        } else {
            // Ranged — check shots remaining
            if (unit->shotsLeft <= 0) return false;
            unit->shotsLeft--;
        }

        HexCoord targetPos = target->pos;
        uint32_t targetId  = target->id;

        // CoordinatedStrike: mark target; all player attacks on the marked unit get +2 ATK
        bool csBonus = false;
        if (m_playerHero.coordinatedStrikeSpecialty) {
            if (m_coordinatedStrikeTarget == target->id) {
                unit->attack += 2;
                csBonus = true;
            } else {
                if (m_coordinatedStrikeTarget == 0)
                    addLog(m_playerHero.name + " CoordinatedStrike: " + target->name + " marked!");
                m_coordinatedStrikeTarget = target->id;
            }
        }

        bool wasRanged = (unit->range > 0 && HexGrid::distance(unit->pos, target->pos) > 1);
        auto result = DamageCalc::attack(*unit, *target, m_grid, false, wasRanged);
        if (csBonus) unit->attack -= 2;

        std::ostringstream ss;
        if (result.desperationSurge) ss << "[Desperation!+3] ";
        if (result.luckTrigger) ss << "★ LUCKY HIT! ";
        if (csBonus) ss << "[Coordinated +2] ";
        ss << unit->name << " attacks " << target->name
           << " for " << result.damage << " damage";
        if (result.killed > 0) ss << " (" << result.killed << " killed)";
        addLog(ss.str());
        if (result.adaptationGained)
            addLog(target->name + " adapts: +" + (result.adaptationStat > 0 ? "ATK" : "DEF") +
                   " (total " + std::to_string(target->adaptationsGained) + ")");
        if (result.vampireHeal > 0)
            addLog(unit->name + " drains " + std::to_string(result.vampireHeal) + " HP!");

        // WARDEN_MARK skill: melee hit splashes to N adjacent enemies (cleave)
        if (unit->range == 0)
            applyWardenMarkSplash(*unit, targetPos, targetId, result.damage);

        if (m_dmgCb && result.damage > 0)
            m_dmgCb(targetId, result.damage, targetPos);

        if (!target->alive) {
            addLog(target->name + " destroyed!");
            processKillEvents(*unit, *target, result);
        }

        checkVictory();
        if (m_phase == CombatPhase::Victory || m_phase == CombatPhase::Defeat)
            return true;

        if (result.moraleTrigger) {
            logMoraleSurge(*unit);
            unit->hasActed = false;
            unit->hasMoved = false;
        } else {
            unit->hasActed = true;
            advanceTurn();
        }
        return true;
    }
    case ActionType::Wait: {
        wait();
        return true;
    }
    case ActionType::Defend: {
        if (unit->defendRoundsLeft > 0) {
            // Already defending: refresh duration without stacking
            unit->defendRoundsLeft = 3;
            addLog(unit->name + " holds the line! (defend refreshed, 3 rounds)");
        } else {
            unit->defense           += 3;
            unit->defendDefenseBonus = 3;
            unit->defendRoundsLeft   = 3;
            addLog(unit->name + " defends (+3 Defense for 3 rounds)");
        }
        unit->hasActed = true;
        advanceTurn();
        return true;
    }
    case ActionType::Shoot: {
        if (unit->range == 0 || unit->shotsLeft <= 0) return false;
        CombatUnit* target = m_grid.getUnit(action.targetUnitId);
        if (!target || target->isPlayer || !target->alive) return false;
        if (HexGrid::distance(unit->pos, target->pos) > unit->range) return false;

        unit->shotsLeft--;

        // CoordinatedStrike applies to ranged attacks too
        bool csShotBonus = false;
        if (m_playerHero.coordinatedStrikeSpecialty) {
            if (m_coordinatedStrikeTarget == target->id) {
                unit->attack += 2;
                csShotBonus = true;
            } else {
                if (m_coordinatedStrikeTarget == 0)
                    addLog(m_playerHero.name + " CoordinatedStrike: " + target->name + " marked!");
                m_coordinatedStrikeTarget = target->id;
            }
        }

        auto result = DamageCalc::attack(*unit, *target, m_grid, false, true);
        if (csShotBonus) unit->attack -= 2;

        std::ostringstream ss;
        if (csShotBonus) ss << "[Coordinated +2] ";
        ss << unit->name << " shoots " << target->name
           << " for " << result.damage << " damage";
        if (result.killed > 0) ss << " (" << result.killed << " killed)";
        addLog(ss.str());

        if (!target->alive) {
            addLog(target->name + " destroyed!");
            processKillEvents(*unit, *target, result);
        }

        unit->hasActed = true;
        advanceTurn();
        return true;
    }
    case ActionType::UseAbility: {
        const SpellDef* spell = findSpell(action.spellId);
        if (!spell) return false;

        // Deduct mana from the casting hero
        Hero& caster = unit->isPlayer ? m_playerHero : m_enemyHero;
        bool freeViaExsanguinate = unit->isPlayer
            && caster.exsanguinate && !caster.exsanguinateUsed
            && spell->school == SpellSchool::Blood;
        // PredatorMirror (Shadowlord): first spell cast each battle costs no mana
        bool freeViaMirror = unit->isPlayer
            && caster.predatorMirrorSpecialty && !caster.predatorMirrorUsed;
        if (!freeViaExsanguinate && !freeViaMirror && caster.mana < spell->manaCost) return false;
        if (freeViaExsanguinate) {
            caster.exsanguinateUsed = true;
            addLog(caster.name + " Exsanguinate: " + spell->name + " cast FREE!");
        } else if (freeViaMirror) {
            caster.predatorMirrorUsed = true;
            addLog(caster.name + " PredatorMirror: " + spell->name + " cast FREE (first spell)!");
        } else {
            caster.mana -= spell->manaCost;
        }

        // School power for scaling
        int schoolPow = 0;
        switch (spell->school) {
            case SpellSchool::Light:  schoolPow = caster.lightPower;  break;
            case SpellSchool::Blood:  schoolPow = caster.bloodPower;  break;
            case SpellSchool::Death:  schoolPow = caster.deathPower;  break;
            case SpellSchool::Nature: schoolPow = caster.naturePower; break;
            case SpellSchool::Forge:  schoolPow = caster.forgePower;  break;
            case SpellSchool::Flesh:  schoolPow = caster.fleshPower;  break;
        }
        int potency = spell->power + schoolPow;

        // Collect targets
        std::vector<CombatUnit*> targets;
        bool wantPlayer  = unit->isPlayer;   // ally
        bool wantEnemy   = !unit->isPlayer;  // enemy
        switch (spell->target) {
            case SpellTarget::SingleAlly: {
                auto* t = m_grid.getUnit(action.targetUnitId);
                if (t && t->alive && t->isPlayer == unit->isPlayer) targets.push_back(t);
                break;
            }
            case SpellTarget::SingleEnemy: {
                auto* t = m_grid.getUnit(action.targetUnitId);
                if (t && t->alive && t->isPlayer != unit->isPlayer) targets.push_back(t);
                break;
            }
            case SpellTarget::AllAllies:
                for (auto& u : m_grid.units())
                    if (u.alive && u.isPlayer == unit->isPlayer) targets.push_back(&u);
                break;
            case SpellTarget::AllEnemies:
                for (auto& u : m_grid.units())
                    if (u.alive && u.isPlayer != unit->isPlayer) targets.push_back(&u);
                break;
            case SpellTarget::Self:
                targets.push_back(unit);
                break;
        }
        if (targets.empty()) return false;

        std::ostringstream ss;
        ss << unit->name << " casts " << spell->name;

        for (CombatUnit* t : targets) {
            switch (spell->effect) {
                case SpellEffect::Damage: {
                    int dmg = std::max(1, potency);
                    HexCoord tPos = t->pos;
                    uint32_t tId  = t->id;
                    t->applyDamage(dmg);
                    ss << " → " << t->name << " takes " << dmg;
                    if (m_dmgCb) m_dmgCb(tId, dmg, tPos);
                    if (!t->alive) addLog(t->name + " destroyed!");
                    // Death Coil and Drain Life restore HP to caster's unit
                    if (action.spellId == SPL::DEATH_COIL ||
                        action.spellId == SPL::DRAIN_LIFE)
                    {
                        int heal = dmg / 2;
                        if (heal > 0) {
                            unit->hp = std::min(unit->maxHp, unit->hp + heal);
                            ss << " (+" << heal << " HP lifesteal)";
                        }
                    }
                    break;
                }
                case SpellEffect::Heal: {
                    int healed = potency;
                    t->hp = std::min(t->maxHp, t->hp + healed);
                    ss << " → " << t->name << " healed " << healed;
                    if (m_healCb) m_healCb(t->id, healed, t->pos);
                    break;
                }
                case SpellEffect::AttackBuff: {
                    int atkBuff = spell->power;
                    int atkRnds = caster.radianceSpecialty ? 4 : 2;
                    t->roundAttackBonus  += atkBuff;
                    t->buffAttackRounds   = atkRnds;
                    ss << " → " << t->name << " +" << atkBuff << " atk (" << atkRnds << " rounds)";
                    // Covenant (Oathbound): adjacent allies get half the buff
                    if (caster.covenantSpecialty) {
                        for (auto& adj : m_grid.units()) {
                            if (!adj.alive || adj.isPlayer != unit->isPlayer || adj.id == t->id) continue;
                            if (HexGrid::distance(adj.pos, t->pos) > 1) continue;
                            adj.roundAttackBonus += std::max(1, atkBuff / 2);
                            adj.buffAttackRounds  = std::max(adj.buffAttackRounds, atkRnds);
                        }
                    }
                    break;
                }
                case SpellEffect::DefenseBuff: {
                    int defBuff = spell->power;
                    int defRnds = caster.radianceSpecialty ? 4 : 2;
                    t->roundDefenseBonus += defBuff;
                    t->buffDefenseRounds  = defRnds;
                    ss << " → " << t->name << " +" << defBuff << " def (" << defRnds << " rounds)";
                    if (caster.covenantSpecialty) {
                        for (auto& adj : m_grid.units()) {
                            if (!adj.alive || adj.isPlayer != unit->isPlayer || adj.id == t->id) continue;
                            if (HexGrid::distance(adj.pos, t->pos) > 1) continue;
                            adj.roundDefenseBonus += std::max(1, defBuff / 2);
                            adj.buffDefenseRounds  = std::max(adj.buffDefenseRounds, defRnds);
                        }
                    }
                    break;
                }
                case SpellEffect::AttackDebuff: {
                    int debuffRnds = caster.radianceSpecialty ? 4 : 2;
                    t->roundAttackBonus  -= spell->power;
                    t->buffAttackRounds   = debuffRnds;
                    ss << " → " << t->name << " -" << spell->power << " atk (" << debuffRnds << " rounds)";
                    break;
                }
                case SpellEffect::DefenseDebuff: {
                    int debuffRnds = caster.radianceSpecialty ? 4 : 2;
                    t->roundDefenseBonus -= spell->power;
                    t->buffDefenseRounds  = debuffRnds;
                    ss << " → " << t->name << " -" << spell->power << " def (" << debuffRnds << " rounds)";
                    break;
                }
                case SpellEffect::MoraleBoost:
                    if (!t->moraleImmune)
                        t->morale = std::min(100, t->morale + potency);
                    ss << " → " << t->name << " morale +" << potency;
                    break;
                case SpellEffect::MoraleDrain:
                    if (!t->moraleImmune)
                        t->morale = std::max(0, t->morale - potency);
                    ss << " → " << t->name << " morale -" << potency;
                    break;
                case SpellEffect::Poison:
                    // Stack with existing poison (keep higher damage, max duration)
                    if (potency > t->poisonDamage) t->poisonDamage = potency;
                    t->poisonRounds = std::max(t->poisonRounds, 3);
                    ss << " → " << t->name << " poisoned (" << potency << " dmg/round, 3 rounds)";
                    break;
                case SpellEffect::Burn:
                    if (potency > t->burnDamage) t->burnDamage = potency;
                    t->burnRounds = std::max(t->burnRounds, 2);
                    ss << " → " << t->name << " burned (" << potency << " dmg/round, 2 rounds)";
                    break;
            }
        }
        addLog(ss.str());
        spawnWildGrowthGhosts();
        m_grid.removeDeadUnits();
        checkVictory();

        unit->hasActed = true;
        advanceTurn();
        return true;
    }
    default:
        return false;
    }
}

void CombatEngine::wait()
{
    if (m_phase != CombatPhase::PlayerTurn) return;
    CombatUnit* unit = activeUnit();
    if (!unit) return;
    unit->waitUsed = true;
    addLog(unit->name + " waits");
    m_waitQueue.push_back(unit->id);
    advanceTurn();
}

void CombatEngine::skipUnit()
{
    CombatUnit* unit = activeUnit();
    if (unit) { unit->hasActed = true; unit->hasMoved = true; }
    advanceTurn();
}

// ── Simple AI ──────────────────────────────────────────────────────────────────
void CombatEngine::processAITurn()
{
    // Hero casts one spell at the start of each AI phase (before units move)
    tryEnemyHeroSpell();

    while (m_phase == CombatPhase::EnemyTurn && m_round <= m_maxRounds) {
        CombatUnit* unit = activeUnit();
        if (!unit || unit->isPlayer) break;
        aiActUnit(*unit);
    }
}

void CombatEngine::processPlayerAITurn()
{
    while (m_phase == CombatPhase::PlayerTurn && m_round <= m_maxRounds) {
        CombatUnit* unit = activeUnit();
        if (!unit || !unit->isPlayer) break;
        aiActUnit(*unit);
    }
}

void CombatEngine::processOneAIAction()
{
    CombatUnit* unit = activeUnit();
    if (!unit) return;
    aiActUnit(*unit);
}

void CombatEngine::aiActUnit(CombatUnit& unit)
{
    // Low-morale hesitation: if morale < 20 and not immune, 30% chance to defend
    if (!unit.moraleImmune && unit.morale < 20) {
        uint32_t roll = static_cast<uint32_t>(m_round * 7919u + unit.id * 1000003u + m_turnIndex * 31337u);
        if ((roll % 10) < 3) {
            unit.defense           += 2;
            unit.defendDefenseBonus = 2;
            unit.defendRoundsLeft   = 1;
            addLog(unit.name + " hesitates in fear! (Defends)");
            unit.hasActed = true;
            advanceTurn();
            return;
        }
    }

    AIDifficulty diff = unit.isPlayer ? m_playerAI : m_enemyAI;
    switch (diff) {
    case AIDifficulty::Passive:  aiActPassive(unit);  break;
    case AIDifficulty::Standard: aiActStandard(unit); break;
    case AIDifficulty::Tactical: aiActTactical(unit); break;
    }
}

// ── Passive: attack a random visible enemy ─────────────────────────────────────
void CombatEngine::aiActPassive(CombatUnit& unit)
{
    std::vector<CombatUnit*> enemies;
    for (auto& u : m_grid.units())
        if (u.alive && u.isPlayer != unit.isPlayer) enemies.push_back(&u);
    if (enemies.empty()) { skipUnit(); return; }

    CombatUnit* target = enemies[static_cast<size_t>(rand()) % enemies.size()];
    int dist = HexGrid::distance(unit.pos, target->pos);

    // Ranged: shoot if in range and have shots
    if (unit.range > 0 && unit.shotsLeft > 0 && dist <= unit.range) {
        unit.shotsLeft--;
        auto result = DamageCalc::attack(unit, *target, m_grid, false, true);
        std::ostringstream ss;
        ss << unit.name << " shoots " << target->name << " for " << result.damage;
        addLog(ss.str());
        if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
        if (result.moraleTrigger) {
            logMoraleSurge(unit);
            unit.hasActed = false; unit.hasMoved = false; return;
        }
        unit.hasActed = true; advanceTurn(); return;
    }

    if (dist == 1) {
        HexCoord tpos = target->pos; uint32_t tid = target->id;
        auto result = DamageCalc::attack(unit, *target, m_grid);
        std::ostringstream ss;
        ss << unit.name << " attacks " << target->name << " for " << result.damage;
        addLog(ss.str());
        applyWardenMarkSplash(unit, tpos, tid, result.damage);
        if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
        if (result.moraleTrigger) {
            logMoraleSurge(unit);
            unit.hasActed = false; unit.hasMoved = false; return;
        }
        unit.hasActed = true; advanceTurn(); return;
    }

    auto melee = m_grid.meleePositions(target->pos);
    if (!melee.empty()) {
        auto path = m_grid.findPath(unit.pos, melee[0], unit.flying);
        if (!path.empty()) {
            int steps = std::min(unit.speed, static_cast<int>(path.size()));
            m_grid.moveUnit(unit.id, path[steps - 1]);
            unit.hasMoved = true;
            // Attack immediately if movement reached melee range
            if (target->alive && HexGrid::distance(unit.pos, target->pos) == 1) {
                HexCoord tpos = target->pos; uint32_t tid = target->id;
                auto result = DamageCalc::attack(unit, *target, m_grid);
                std::ostringstream ss;
                ss << unit.name << " attacks " << target->name << " for " << result.damage;
                addLog(ss.str());
                applyWardenMarkSplash(unit, tpos, tid, result.damage);
                if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
                if (result.moraleTrigger) {
                    logMoraleSurge(unit);
                    unit.hasActed = false; unit.hasMoved = false; return;
                }
                unit.hasActed = true; advanceTurn(); return;
            }
        }
    }
    unit.hasActed = true; advanceTurn();
}

// ── Standard: nearest enemy, move/attack ──────────────────────────────────────
void CombatEngine::aiActStandard(CombatUnit& unit)
{
    CombatUnit* target = nullptr;
    int bestDist = 9999;
    for (auto& u : m_grid.units()) {
        if (!u.alive || u.isPlayer == unit.isPlayer) continue;
        int d = HexGrid::distance(unit.pos, u.pos);
        if (d < bestDist) { bestDist = d; target = &u; }
    }
    if (!target) { skipUnit(); return; }

    // Ranged: shoot nearest enemy if in range and have shots
    if (unit.range > 0 && unit.shotsLeft > 0 && bestDist <= unit.range) {
        unit.shotsLeft--;
        auto result = DamageCalc::attack(unit, *target, m_grid, false, true);
        std::ostringstream ss;
        ss << unit.name << " shoots " << target->name << " for " << result.damage << " dmg";
        if (result.killed) ss << " (" << result.killed << " killed)";
        addLog(ss.str());
        if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
        if (result.moraleTrigger) {
            logMoraleSurge(unit);
            unit.hasActed = false; unit.hasMoved = false; return;
        }
        unit.hasActed = true; advanceTurn(); return;
    }

    if (bestDist == 1) {
        HexCoord tpos = target->pos; uint32_t tid = target->id;
        auto result = DamageCalc::attack(unit, *target, m_grid);
        std::ostringstream ss;
        ss << unit.name << " attacks " << target->name << " for " << result.damage << " dmg";
        if (result.killed) ss << " (" << result.killed << " killed)";
        addLog(ss.str());
        applyWardenMarkSplash(unit, tpos, tid, result.damage);
        if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
        if (result.moraleTrigger) {
            logMoraleSurge(unit);
            unit.hasActed = false; unit.hasMoved = false; return;
        }
        unit.hasActed = true; advanceTurn(); return;
    }

    auto melee = m_grid.meleePositions(target->pos);
    if (!melee.empty()) {
        HexCoord best = melee[0]; int d = HexGrid::distance(unit.pos, best);
        for (auto& h : melee) { int nd = HexGrid::distance(unit.pos, h); if (nd < d) { d = nd; best = h; } }
        auto path = m_grid.findPath(unit.pos, best, unit.flying);
        if (!path.empty()) {
            int steps = std::min(unit.speed, static_cast<int>(path.size()));
            m_grid.moveUnit(unit.id, path[steps - 1]);
            unit.hasMoved = true;
            applyTileEffect(unit);
            addLog(unit.name + " moves toward " + target->name);
            // Attack immediately if movement reached melee range
            if (target->alive && HexGrid::distance(unit.pos, target->pos) == 1) {
                HexCoord tpos = target->pos; uint32_t tid = target->id;
                auto result = DamageCalc::attack(unit, *target, m_grid);
                std::ostringstream ss;
                ss << unit.name << " attacks " << target->name << " for " << result.damage << " dmg";
                if (result.killed) ss << " (" << result.killed << " killed)";
                addLog(ss.str());
                applyWardenMarkSplash(unit, tpos, tid, result.damage);
                if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
                if (result.moraleTrigger) {
                    logMoraleSurge(unit);
                    unit.hasActed = false; unit.hasMoved = false; return;
                }
                unit.hasActed = true; advanceTurn(); return;
            }
        }
    }
    unit.hasActed = true; advanceTurn();
}

// ── Tactical: focus weakest stack, ranged units kite back ─────────────────────
void CombatEngine::aiActTactical(CombatUnit& unit)
{
    // Ranged units: move away from melee threats, shoot lowest-HP enemy in range
    if (unit.range > 0 && unit.shotsLeft > 0) {
        // Find closest enemy threatening melee
        CombatUnit* threat = nullptr;
        int threatDist = 9999;
        for (auto& u : m_grid.units()) {
            if (!u.alive || u.isPlayer == unit.isPlayer || u.range > 0) continue;
            int d = HexGrid::distance(unit.pos, u.pos);
            if (d < threatDist) { threatDist = d; threat = &u; }
        }

        // If melee threat is very close (≤2), try to back away
        if (threat && threatDist <= 2 && !unit.hasMoved) {
            // Pick the hex adjacent to us that maximises distance from threat
            auto neighbours = HexGrid::neighbors(unit.pos);
            HexCoord best = unit.pos; int bestD = threatDist;
            for (auto& h : neighbours) {
                if (!m_grid.inBounds(h)) continue;
                auto* tile = m_grid.getTile(h);
                if (!tile || tile->occupied || tile->type == CombatTileType::Obstacle) continue;
                int nd = HexGrid::distance(h, threat->pos);
                if (nd > bestD) { bestD = nd; best = h; }
            }
            if (!(best == unit.pos)) {
                m_grid.moveUnit(unit.id, best); unit.hasMoved = true;
                addLog(unit.name + " falls back");
            }
        }

        // Shoot enemy with lowest total HP that is within range
        CombatUnit* shtTarget = nullptr;
        int lowestHp = INT32_MAX;
        for (auto& u : m_grid.units()) {
            if (!u.alive || u.isPlayer == unit.isPlayer) continue;
            if (HexGrid::distance(unit.pos, u.pos) > unit.range) continue;
            if (u.totalHp() < lowestHp) { lowestHp = u.totalHp(); shtTarget = &u; }
        }
        if (shtTarget) {
            unit.shotsLeft--;
            auto result = DamageCalc::attack(unit, *shtTarget, m_grid, false, true);
            std::ostringstream ss;
            ss << unit.name << " shoots " << shtTarget->name << " for " << result.damage;
            addLog(ss.str());
            if (!shtTarget->alive) { addLog(shtTarget->name + " destroyed!"); processKillEvents(unit, *shtTarget, result); }
            if (result.moraleTrigger) {
                logMoraleSurge(unit);
                unit.hasActed = false; unit.hasMoved = false; return;
            }
            unit.hasActed = true; advanceTurn(); return;
        }
    }

    // Melee: focus weakest enemy stack by total HP
    CombatUnit* target = nullptr;
    int lowestHp = INT32_MAX;
    int bestDist = 9999;
    for (auto& u : m_grid.units()) {
        if (!u.alive || u.isPlayer == unit.isPlayer) continue;
        int hp = u.totalHp();
        int d  = HexGrid::distance(unit.pos, u.pos);
        // Prefer adjacent weak targets; if none adjacent, pick weakest reachable
        if (d == 1 && hp < lowestHp) { lowestHp = hp; bestDist = d; target = &u; }
        else if (bestDist > 1 && hp < lowestHp) { lowestHp = hp; bestDist = d; target = &u; }
    }
    if (!target) { skipUnit(); return; }

    if (bestDist == 1) {
        HexCoord tpos = target->pos; uint32_t tid = target->id;
        auto result = DamageCalc::attack(unit, *target, m_grid);
        std::ostringstream ss;
        ss << unit.name << " attacks " << target->name << " for " << result.damage << " dmg";
        if (result.killed) ss << " (" << result.killed << " killed)";
        addLog(ss.str());
        applyWardenMarkSplash(unit, tpos, tid, result.damage);
        if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
        if (result.moraleTrigger) {
            logMoraleSurge(unit);
            unit.hasActed = false; unit.hasMoved = false; return;
        }
        unit.hasActed = true; advanceTurn(); return;
    }

    auto melee = m_grid.meleePositions(target->pos);
    if (!melee.empty()) {
        // Prefer melee hexes with Attack tiles; fall back to nearest
        auto tileScore = [&](HexCoord h) -> int {
            const CombatTile* t = m_grid.getTile(h);
            if (!t) return 0;
            if (t->type == CombatTileType::Attack)  return 2;
            if (t->type == CombatTileType::Defense && unit.totalHp() < unit.maxHp * unit.count / 2) return 1;
            return 0;
        };
        HexCoord best = melee[0];
        int bestScore = -HexGrid::distance(unit.pos, melee[0]) + tileScore(melee[0]);
        for (auto& h : melee) {
            int s = -HexGrid::distance(unit.pos, h) + tileScore(h);
            if (s > bestScore) { bestScore = s; best = h; }
        }
        auto path = m_grid.findPath(unit.pos, best, unit.flying);
        if (!path.empty()) {
            int steps = std::min(unit.speed, static_cast<int>(path.size()));
            // Ranged: stop advancing as soon as we enter effective range of target
            if (unit.range > 0 && unit.shotsLeft > 0) {
                for (int s = 0; s < steps; ++s) {
                    if (HexGrid::distance(path[s], target->pos) <= unit.range) {
                        steps = s + 1; break;
                    }
                }
            }
            m_grid.moveUnit(unit.id, path[steps - 1]);
            unit.hasMoved = true;
            applyTileEffect(unit);
            // If now in range: shoot immediately (ranged) or attack (melee)
            int nowDist = HexGrid::distance(unit.pos, target->pos);
            if (unit.range > 0 && unit.shotsLeft > 0 && nowDist <= unit.range && target->alive) {
                unit.shotsLeft--;
                auto result = DamageCalc::attack(unit, *target, m_grid, false, true);
                std::ostringstream ss;
                ss << unit.name << " moves+shoots " << target->name << " for " << result.damage;
                addLog(ss.str());
                if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
                if (result.moraleTrigger) {
                    logMoraleSurge(unit);
                    unit.hasActed = false; unit.hasMoved = false; return;
                }
                unit.hasActed = true; advanceTurn(); return;
            }
            if (target->alive && nowDist == 1) {
                HexCoord tpos = target->pos; uint32_t tid = target->id;
                auto result = DamageCalc::attack(unit, *target, m_grid);
                std::ostringstream ss;
                ss << unit.name << " attacks " << target->name << " for " << result.damage << " dmg";
                if (result.killed) ss << " (" << result.killed << " killed)";
                addLog(ss.str());
                applyWardenMarkSplash(unit, tpos, tid, result.damage);
                if (!target->alive) { addLog(target->name + " destroyed!"); processKillEvents(unit, *target, result); }
                if (result.moraleTrigger) {
                    logMoraleSurge(unit);
                    unit.hasActed = false; unit.hasMoved = false; return;
                }
                unit.hasActed = true; advanceTurn(); return;
            }
        }
    }

    // ── Fallback stage 1: attack any adjacent enemy ───────────────────────────
    {
        CombatUnit* adj = nullptr;
        for (auto& u : m_grid.units()) {
            if (!u.alive || u.isPlayer == unit.isPlayer) continue;
            if (HexGrid::distance(unit.pos, u.pos) == 1) { adj = &u; break; }
        }
        if (adj) {
            HexCoord tpos = adj->pos; uint32_t tid = adj->id;
            auto result = DamageCalc::attack(unit, *adj, m_grid);
            std::ostringstream ss;
            ss << unit.name << " attacks " << adj->name << " for " << result.damage << " dmg";
            if (result.killed) ss << " (" << result.killed << " killed)";
            addLog(ss.str());
            applyWardenMarkSplash(unit, tpos, tid, result.damage);
            if (!adj->alive) { addLog(adj->name + " destroyed!"); processKillEvents(unit, *adj, result); }
            if (result.moraleTrigger) {
                logMoraleSurge(unit);
                unit.hasActed = false; unit.hasMoved = false; return;
            }
            unit.hasActed = true; advanceTurn(); return;
        }
    }

    // ── Fallback stage 2: move toward the nearest reachable enemy ─────────────
    if (!unit.hasMoved) {
        CombatUnit* nearest = nullptr; int nearDist = 9999;
        for (auto& u : m_grid.units()) {
            if (!u.alive || u.isPlayer == unit.isPlayer) continue;
            int d = HexGrid::distance(unit.pos, u.pos);
            if (d < nearDist) { nearDist = d; nearest = &u; }
        }
        if (nearest) {
            auto nearMelee = m_grid.meleePositions(nearest->pos);
            if (nearMelee.empty()) nearMelee.push_back(nearest->pos);
            HexCoord dest = nearMelee[0];
            int bestDst = HexGrid::distance(unit.pos, dest);
            for (auto& h : nearMelee) {
                int d = HexGrid::distance(unit.pos, h);
                if (d < bestDst) { bestDst = d; dest = h; }
            }
            auto path2 = m_grid.findPath(unit.pos, dest, unit.flying);
            if (!path2.empty()) {
                int steps = std::min(unit.speed, static_cast<int>(path2.size()));
                m_grid.moveUnit(unit.id, path2[steps - 1]);
                unit.hasMoved = true;
                applyTileEffect(unit);
            }
        }
    }

    unit.hasActed = true; advanceTurn();
}

// ── Headless simulation ────────────────────────────────────────────────────────
CombatPhase CombatEngine::runHeadless(int maxRounds)
{
    m_maxRounds = maxRounds;

    // Process all player-side units using the same AI as the enemy
    auto processPlayerAI = [this]() {
        while (m_phase == CombatPhase::PlayerTurn && m_round <= m_maxRounds) {
            CombatUnit* unit = activeUnit();
            if (!unit || !unit->isPlayer) break;
            aiActUnit(*unit);
        }
    };

    // Kick off whichever side goes first
    if (m_phase == CombatPhase::EnemyTurn)
        processAITurn();
    else if (m_phase == CombatPhase::PlayerTurn)
        processPlayerAI();

    int safety = maxRounds * 300;
    while (--safety > 0 && m_round <= maxRounds) {
        if (m_phase == CombatPhase::Victory || m_phase == CombatPhase::Defeat)
            break;
        if (m_phase == CombatPhase::EnemyTurn)
            processAITurn();
        if (m_phase == CombatPhase::Victory || m_phase == CombatPhase::Defeat)
            break;
        if (m_phase == CombatPhase::PlayerTurn)
            processPlayerAI();
    }
    return m_phase;
}

// ── Tile effects on entry ──────────────────────────────────────────────────────
void CombatEngine::applyTileEffect(CombatUnit& unit)
{
    const CombatTile* tile = m_grid.getTile(unit.pos);
    if (!tile) return;
    switch (tile->type) {
    case CombatTileType::Speed:
        if (!unit.moraleImmune)
            unit.morale = std::min(100, unit.morale + 5);
        break;
    case CombatTileType::Attack:
        // Consecrated ground — +2 ATK this round (stacks, expires next round)
        unit.roundAttackBonus += 2;
        unit.buffAttackRounds  = std::max(unit.buffAttackRounds, 1);
        if (!m_silent) addLog(unit.name + " gains +2 ATK from power tile");
        break;
    case CombatTileType::Defense:
        // Fortified ground — +2 DEF this round
        unit.roundDefenseBonus += 2;
        unit.buffDefenseRounds  = std::max(unit.buffDefenseRounds, 1);
        if (!m_silent) addLog(unit.name + " gains +2 DEF from shield tile");
        break;
    case CombatTileType::SpeedPenalty:
        if (!unit.moraleImmune)
            unit.morale = std::max(0, unit.morale - 5);
        break;
    default: break;
    }
}

// ── Thornkin Symbiosis — Beast bond bonus applied at round start ───────────────
void CombatEngine::applySymbiosisRound()
{
    // Check if either hero has SYMBIOSIS skill; apply per side
    auto getSymbiosisValue = [](const Hero& hero) -> int {
        if (const SkillInstance* s = hero.skills.getSkill(SkillID::SYMBIOSIS)) {
            if (const SkillDef* def = findSkillDef(SkillID::SYMBIOSIS))
                return def->values[static_cast<int>(s->tier)];
        }
        return 0;
    };

    int playerVal = getSymbiosisValue(m_playerHero);
    int enemyVal  = getSymbiosisValue(m_enemyHero);
    if (playerVal == 0 && enemyVal == 0) return;

    for (auto& unit : m_grid.units()) {
        if (!unit.alive) continue;
        if (!hasTag(unit.tags, UnitTag::Beast)) continue;

        int val = unit.isPlayer ? playerVal : enemyVal;
        if (val == 0) continue;

        // Check for at least one adjacent Beast ally
        bool hasBond = false;
        for (const auto& other : m_grid.units()) {
            if (!other.alive) continue;
            if (other.id == unit.id) continue;
            if (other.isPlayer != unit.isPlayer) continue;
            if (!hasTag(other.tags, UnitTag::Beast)) continue;
            if (HexGrid::distance(unit.pos, other.pos) <= 1) { hasBond = true; break; }
        }

        if (hasBond) {
            // Without building: cap at 1 (basic bond). With building: full val (1/2/3 by tier).
            int cap = m_symbiosisWeb ? val : 1;
            int gain = std::min(val, std::max(0, cap - unit.roundAttackBonus));
            unit.roundAttackBonus  += gain;
            unit.roundDefenseBonus += gain;
        }
    }
}

// ── DoT tick + round-start effects ────────────────────────────────────────────
void CombatEngine::processRoundStartEffects()
{
    for (auto& u : m_grid.units()) {
        if (!u.alive) continue;
        if (u.poisonRounds > 0) {
            int dmg = u.poisonDamage;
            u.applyDamage(dmg);
            std::ostringstream ss;
            ss << u.name << " takes " << dmg << " poison damage";
            if (!u.alive) ss << " and perishes!";
            addLog(ss.str());
            if (m_dmgCb) m_dmgCb(u.id, dmg, u.pos);
            --u.poisonRounds;
            if (u.poisonRounds == 0) u.poisonDamage = 0;
        }
        if (u.burnRounds > 0) {
            int dmg = u.burnDamage;
            u.applyDamage(dmg);
            std::ostringstream ss;
            ss << u.name << " takes " << dmg << " burn damage";
            if (!u.alive) ss << " and is incinerated!";
            addLog(ss.str());
            if (m_dmgCb) m_dmgCb(u.id, dmg, u.pos);
            --u.burnRounds;
            if (u.burnRounds == 0) u.burnDamage = 0;
        }
    }

    // Possession countdown — possessed units dissolve after their round
    for (auto& u : m_grid.units()) {
        if (!u.possessed) continue;
        if (--u.possessedRoundsLeft <= 0) {
            u.alive     = false;
            u.possessed = false;
            addLog(u.name + " possession ends — stack dissolves.");
        }
    }
    m_grid.removeDeadUnits();

    // Feast specialty (Blood Prince) — drain 2 HP from largest own unit, heal hero
    auto applyFeast = [&](Hero& hero, bool isPlayer) {
        if (!hero.feastSpecialty) return;
        CombatUnit* biggest = nullptr;
        for (auto& u : m_grid.units())
            if (u.isPlayer == isPlayer && u.alive && u.count > 0)
                if (!biggest || u.count > biggest->count) biggest = &u;
        if (!biggest) return;
        int drain = std::min(2, biggest->hp - 1);
        if (drain <= 0) return;
        biggest->hp -= drain;
        int heal = std::min(drain, hero.heroMaxHp - hero.heroHp);
        if (heal > 0) {
            hero.heroHp += heal;
            addLog(hero.name + " Feast: drained " + std::to_string(drain) +
                   " HP from " + biggest->name + ", hero +HP");
        }
    };
    applyFeast(m_playerHero, true);
    applyFeast(m_enemyHero,  false);

    // Wither specialty (Fell Druid) — enemies lose 1 ATK each round
    auto applyWither = [&](const Hero& hero, bool isPlayer) {
        if (!hero.witherSpecialty) return;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer == isPlayer || !u.alive) continue;
            u.attack = std::max(0, u.attack - 1);
        }
        addLog(hero.name + " Wither: enemy units lose 1 ATK");
    };
    applyWither(m_playerHero, true);
    applyWither(m_enemyHero,  false);

    // Elixir specialty (Blood Sage) — at round 2 start, fully restore the most damaged unit
    auto applyElixir = [&](Hero& hero, bool isPlayer) {
        if (!hero.elixirSpecialty || hero.elixirUsed || m_round != 2) return;
        CombatUnit* worst = nullptr;
        float lowestRatio = 1.0f;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive) continue;
            float ratio = static_cast<float>(u.hp) / static_cast<float>(std::max(1, u.maxHp));
            if (ratio < lowestRatio) { lowestRatio = ratio; worst = &u; }
        }
        if (!worst) return;
        int healed = worst->maxHp - worst->hp;
        worst->hp = worst->maxHp;
        hero.elixirUsed = true;
        addLog(hero.name + " Elixir: " + worst->name + " fully restored (+" +
               std::to_string(healed) + " HP)!");
    };
    applyElixir(m_playerHero, true);
    applyElixir(m_enemyHero,  false);

    // BloodPenance specialty — hero bleeds 5 HP per round after round 1
    auto applyBloodPenanceDrain = [&](Hero& hero) {
        if (!hero.bloodPenanceSpecialty || m_round <= 1) return;
        int drain = std::min(5, hero.heroHp - 1);
        if (drain <= 0) return;
        hero.heroHp -= drain;
        addLog(hero.name + " Blood Penance: -" + std::to_string(drain) + " HP");
    };
    applyBloodPenanceDrain(m_playerHero);
    applyBloodPenanceDrain(m_enemyHero);

    // Collective: share best adaptation each round
    auto applyCollectiveRound = [&](const Hero& hero, bool isPlayer) {
        if (!hero.collectiveSpecialty) return;
        int bestAdapt = 0;
        for (const auto& u : m_grid.units())
            if (u.isPlayer == isPlayer && u.alive && hasTag(u.tags, UnitTag::OrganicMech))
                bestAdapt = std::max(bestAdapt, u.adaptationsGained);
        if (bestAdapt <= 0) return;
        bool shared = false;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive || !hasTag(u.tags, UnitTag::OrganicMech)) continue;
            if (u.adaptationsGained < bestAdapt) {
                u.attack++;
                u.adaptationsGained++;
                shared = true;
            }
        }
        if (shared)
            addLog(hero.name + " Collective: OrganicMech share adaptation (best=" + std::to_string(bestAdapt) + ")");
    };
    applyCollectiveRound(m_playerHero, true);
    applyCollectiveRound(m_enemyHero,  false);

    // Corruption: enemy units lose -1 DEF per round (Voidcaller specialty)
    auto applyCorruption = [&](Hero& hero, bool isPlayer) {
        if (!hero.corruptionSpecialty || m_round <= 1) return;
        bool affected = false;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer == isPlayer || !u.alive) continue;  // target opposite side
            if (u.defense > 0) { u.defense--; affected = true; }
        }
        if (affected)
            addLog(hero.name + " Corruption: enemy units -1 DEF");
    };
    applyCorruption(m_playerHero, true);
    applyCorruption(m_enemyHero,  false);

    // Synthesis: hero regenerates +2 mana per round (Ironweaver specialty)
    auto applySynthesis = [&](Hero& hero) {
        if (!hero.synthesisSpecialty) return;
        hero.mana = std::min(hero.maxMana, hero.mana + 2);
    };
    applySynthesis(m_playerHero);
    applySynthesis(m_enemyHero);

    // BloodScent (Inquisitor Hunter): player units get +1 ATK vs BloodBound enemies this round
    auto applyBloodScent = [&](const Hero& hero, bool isPlayer) {
        if (!hero.bloodScentSpecialty) return;
        // Check if any opponent has BloodBound tag
        bool hasBloodBound = false;
        for (const auto& u : m_grid.units())
            if (u.isPlayer != isPlayer && u.alive && hasTag(u.tags, UnitTag::BloodBound))
                { hasBloodBound = true; break; }
        if (!hasBloodBound) return;
        int boosted = 0;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive) continue;
            u.roundAttackBonus += 1;
            u.buffAttackRounds = std::max(u.buffAttackRounds, 1);
            ++boosted;
        }
        if (boosted > 0)
            addLog(hero.name + " Blood Scent: " + std::to_string(boosted) +
                   " units gain +1 ATK vs BloodBound");
    };
    applyBloodScent(m_playerHero, true);
    applyBloodScent(m_enemyHero,  false);

    // Infestation (Flesh Architect): each OrganicMech unit spreads flesh to 1 adjacent Normal tile
    // Enemy units caught on infested tiles take 2 poison damage
    auto applyInfestation = [&](const Hero& hero, bool isPlayer) {
        if (!hero.infestationSpecialty || m_round < 2) return;
        int spreads = 0;
        uint32_t rng = static_cast<uint32_t>(m_round * 7919u + m_turnIndex * 1000003u);
        auto lcg = [&]() { rng = rng * 1664525u + 1013904223u; return rng; };
        for (const auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || !u.alive) continue;
            if (!hasTag(u.tags, UnitTag::OrganicMech)) continue;
            auto neighbors = HexGrid::neighbors(u.pos);
            // Pick one random neighbour tile that is Normal
            lcg();
            int start = static_cast<int>(rng % neighbors.size());
            for (int i = 0; i < static_cast<int>(neighbors.size()); ++i) {
                HexCoord nc = neighbors[(start + i) % neighbors.size()];
                CombatTile* t = m_grid.getTile(nc);
                if (!t || t->type != CombatTileType::Normal) continue;
                m_grid.setTileType(nc, CombatTileType::SpeedPenalty);
                ++spreads;
                // Poison any enemy unit standing there
                for (auto& target : m_grid.units()) {
                    if (target.alive && target.isPlayer != isPlayer && target.pos == nc) {
                        target.poisonRounds = std::max(target.poisonRounds, 2);
                        if (target.poisonDamage < 3) target.poisonDamage = 3;
                        addLog(target.name + " is caught in spreading flesh! (Poison 3 x2)");
                    }
                }
                break;
            }
        }
        if (spreads > 0)
            addLog(hero.name + " Infestation: flesh spreads to " +
                   std::to_string(spreads) + " tile(s)");
    };
    applyInfestation(m_playerHero, true);
    applyInfestation(m_enemyHero,  false);

    spawnWildGrowthGhosts();
    m_grid.removeDeadUnits();
    checkVictory();
}

// ── Enemy hero AI spell casting (one spell per round) ─────────────────────────
void CombatEngine::tryEnemyHeroSpell()
{
    if (m_enemyHeroSpellUsed) return;
    if (m_phase != CombatPhase::EnemyTurn) return;

    Hero& hero = m_enemyHero;
    if (hero.knownSpells.empty() || hero.mana <= 0) return;

    const SpellDef* bestSpell    = nullptr;
    uint32_t        bestTargetId = 0;
    float           bestScore    = 0.0f;

    for (int sid : hero.knownSpells) {
        const SpellDef* spell = findSpell(sid);
        if (!spell || hero.mana < spell->manaCost) continue;

        int schoolPow = 0;
        switch (spell->school) {
            case SpellSchool::Light:  schoolPow = hero.lightPower;  break;
            case SpellSchool::Blood:  schoolPow = hero.bloodPower;  break;
            case SpellSchool::Death:  schoolPow = hero.deathPower;  break;
            case SpellSchool::Nature: schoolPow = hero.naturePower; break;
            case SpellSchool::Forge:  schoolPow = hero.forgePower;  break;
            case SpellSchool::Flesh:  schoolPow = hero.fleshPower;  break;
        }
        int potency = spell->power + schoolPow;

        // Score offensive spells against player units
        switch (spell->effect) {
            case SpellEffect::Damage: {
                if (spell->target == SpellTarget::SingleEnemy) {
                    // Prefer the unit most killable for damage cost
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || !u.isPlayer) continue;
                        float s = (float)potency * 2.0f / std::max(1, u.totalHp());
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                } else if (spell->target == SpellTarget::AllEnemies) {
                    int n = 0;
                    for (auto& u : m_grid.units()) if (u.alive && u.isPlayer) ++n;
                    float s = (float)potency * n * 0.4f;
                    if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                }
                break;
            }
            case SpellEffect::Poison: {
                if (spell->target == SpellTarget::SingleEnemy) {
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || !u.isPlayer || u.poisonRounds > 0) continue;
                        float s = (float)potency * 3.0f; // 3 rounds of value
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                } else if (spell->target == SpellTarget::AllEnemies) {
                    int n = 0;
                    for (auto& u : m_grid.units()) if (u.alive && u.isPlayer && u.poisonRounds == 0) ++n;
                    float s = (float)potency * n * 2.0f;
                    if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                }
                break;
            }
            case SpellEffect::Burn: {
                if (spell->target == SpellTarget::SingleEnemy) {
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || !u.isPlayer || u.burnRounds > 0) continue;
                        float s = (float)potency * 2.0f; // 2 rounds of value
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                } else if (spell->target == SpellTarget::AllEnemies) {
                    int n = 0;
                    for (auto& u : m_grid.units()) if (u.alive && u.isPlayer && u.burnRounds == 0) ++n;
                    float s = (float)potency * n * 1.5f;
                    if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                }
                break;
            }
            case SpellEffect::AttackDebuff:
            case SpellEffect::DefenseDebuff: {
                if (spell->target == SpellTarget::SingleEnemy) {
                    // Debuff the strongest player unit (highest attack)
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || !u.isPlayer) continue;
                        if (u.buffAttackRounds > 0 || u.buffDefenseRounds > 0) continue;
                        float s = (float)potency * 1.5f + u.attack * 0.5f;
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                } else if (spell->target == SpellTarget::AllEnemies) {
                    int n = 0;
                    for (auto& u : m_grid.units()) if (u.alive && u.isPlayer) ++n;
                    float s = (float)potency * n * 1.0f;
                    if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                }
                break;
            }
            case SpellEffect::MoraleDrain: {
                int n = 0;
                for (auto& u : m_grid.units()) if (u.alive && u.isPlayer && !u.moraleImmune) ++n;
                float s = (float)potency * n * 0.6f;
                if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                break;
            }
            case SpellEffect::AttackBuff: {
                // Buff the allied unit with the most total HP (biggest investment)
                if (spell->target == SpellTarget::SingleAlly) {
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || u.isPlayer) continue;
                        float s = (float)spell->power * 1.5f + u.totalHp() * 0.01f;
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                } else if (spell->target == SpellTarget::AllAllies) {
                    int n = 0;
                    for (auto& u : m_grid.units()) if (u.alive && !u.isPlayer) ++n;
                    float s = (float)spell->power * n * 1.2f;
                    if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                }
                break;
            }
            case SpellEffect::DefenseBuff: {
                if (spell->target == SpellTarget::SingleAlly) {
                    // Buff the most threatened (lowest HP ratio) allied unit
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || u.isPlayer) continue;
                        float ratio = u.maxHp > 0 ? (float)u.hp / u.maxHp : 1.0f;
                        float s = (float)spell->power * 1.2f * (1.5f - ratio);
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                } else if (spell->target == SpellTarget::AllAllies) {
                    int n = 0;
                    for (auto& u : m_grid.units()) if (u.alive && !u.isPlayer) ++n;
                    float s = (float)spell->power * n * 0.9f;
                    if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                }
                break;
            }
            case SpellEffect::Heal: {
                if (spell->target == SpellTarget::SingleAlly) {
                    // Heal the most damaged allied unit
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || u.isPlayer) continue;
                        int missing = u.maxHp - u.hp;
                        if (missing <= 0) continue;
                        float s = (float)std::min(potency, missing) * 1.8f;
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                }
                break;
            }
            case SpellEffect::MoraleBoost: {
                if (spell->target == SpellTarget::AllAllies) {
                    int n = 0;
                    for (auto& u : m_grid.units()) if (u.alive && !u.isPlayer && !u.moraleImmune) ++n;
                    float s = (float)potency * n * 0.5f;
                    if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = 0; }
                } else if (spell->target == SpellTarget::SingleAlly) {
                    for (auto& u : m_grid.units()) {
                        if (!u.alive || u.isPlayer || u.moraleImmune) continue;
                        float s = (float)potency * 0.6f;
                        if (s > bestScore) { bestScore = s; bestSpell = spell; bestTargetId = u.id; }
                    }
                }
                break;
            }
            default: break;
        }
    }

    if (!bestSpell || bestScore <= 0.0f) return;

    // HeresyDetection: player's Inquisitor can negate the first enemy spell cast
    if (m_playerHero.heresyDetection && !m_playerHero.heresyDetectionUsed) {
        m_playerHero.heresyDetectionUsed = true;
        hero.mana -= bestSpell->manaCost;
        m_enemyHeroSpellUsed = true;
        addLog(m_playerHero.name + " Heresy Detection: " +
               std::string(bestSpell->name) + " NEGATED!");
        return;
    }

    // LightningRod: player's Stormbark reflects the first enemy spell back at the caster's side
    if (m_playerHero.lightningRodSpecialty && !m_playerHero.lightningRodUsed) {
        m_playerHero.lightningRodUsed = true;
        hero.mana -= bestSpell->manaCost;
        m_enemyHeroSpellUsed = true;

        int spow = 0;
        switch (bestSpell->school) {
            case SpellSchool::Light:  spow = hero.lightPower;  break;
            case SpellSchool::Blood:  spow = hero.bloodPower;  break;
            case SpellSchool::Death:  spow = hero.deathPower;  break;
            case SpellSchool::Nature: spow = hero.naturePower; break;
            case SpellSchool::Forge:  spow = hero.forgePower;  break;
            case SpellSchool::Flesh:  spow = hero.fleshPower;  break;
        }
        int potency2 = bestSpell->power + spow;

        std::vector<CombatUnit*> reflTargets;
        switch (bestSpell->target) {
            case SpellTarget::SingleEnemy:
                for (auto& u : m_grid.units())
                    if (u.alive && !u.isPlayer) { reflTargets.push_back(&u); break; }
                break;
            case SpellTarget::AllEnemies:
                for (auto& u : m_grid.units())
                    if (u.alive && !u.isPlayer) reflTargets.push_back(&u);
                break;
            default: break;
        }

        std::ostringstream rss;
        rss << m_playerHero.name << " Lightning Rod: " << bestSpell->name << " REFLECTED!";
        for (CombatUnit* t : reflTargets) {
            switch (bestSpell->effect) {
                case SpellEffect::Damage: {
                    int dmg = std::max(1, potency2);
                    HexCoord tp = t->pos; uint32_t tid = t->id;
                    t->applyDamage(dmg);
                    rss << " → " << t->name << " -" << dmg;
                    if (m_dmgCb) m_dmgCb(tid, dmg, tp);
                    if (!t->alive) rss << " (destroyed)";
                    break;
                }
                case SpellEffect::Poison:
                    if (potency2 > t->poisonDamage) t->poisonDamage = potency2;
                    t->poisonRounds = std::max(t->poisonRounds, 3);
                    rss << " → " << t->name << " poisoned";
                    break;
                case SpellEffect::Burn:
                    if (potency2 > t->burnDamage) t->burnDamage = potency2;
                    t->burnRounds = std::max(t->burnRounds, 2);
                    rss << " → " << t->name << " burned";
                    break;
                default: break;
            }
        }
        addLog(rss.str());
        m_grid.removeDeadUnits();
        checkVictory();
        return;
    }

    // Deduct mana and mark as used
    hero.mana -= bestSpell->manaCost;
    m_enemyHeroSpellUsed = true;

    int schoolPow = 0;
    switch (bestSpell->school) {
        case SpellSchool::Light:  schoolPow = hero.lightPower;  break;
        case SpellSchool::Blood:  schoolPow = hero.bloodPower;  break;
        case SpellSchool::Death:  schoolPow = hero.deathPower;  break;
        case SpellSchool::Nature: schoolPow = hero.naturePower; break;
        case SpellSchool::Forge:  schoolPow = hero.forgePower;  break;
        case SpellSchool::Flesh:  schoolPow = hero.fleshPower;  break;
    }
    int potency = bestSpell->power + schoolPow;

    // Gather targets
    std::vector<CombatUnit*> targets;
    switch (bestSpell->target) {
        case SpellTarget::SingleEnemy: {
            auto* t = m_grid.getUnit(bestTargetId);
            if (t && t->alive && t->isPlayer) targets.push_back(t);
            break;
        }
        case SpellTarget::AllEnemies:
            for (auto& u : m_grid.units())
                if (u.alive && u.isPlayer) targets.push_back(&u);
            break;
        case SpellTarget::SingleAlly: {
            auto* t = m_grid.getUnit(bestTargetId);
            if (t && t->alive && !t->isPlayer) targets.push_back(t);
            break;
        }
        case SpellTarget::AllAllies:
            for (auto& u : m_grid.units())
                if (u.alive && !u.isPlayer) targets.push_back(&u);
            break;
        default: break;
    }
    if (targets.empty()) return;

    std::ostringstream ss;
    ss << "[Enemy Hero] casts " << bestSpell->name;

    for (CombatUnit* t : targets) {
        switch (bestSpell->effect) {
            case SpellEffect::Damage: {
                int dmg = std::max(1, potency);
                HexCoord tp = t->pos; uint32_t tid = t->id;
                t->applyDamage(dmg);
                ss << " → " << t->name << " -" << dmg << " HP";
                if (m_dmgCb) m_dmgCb(tid, dmg, tp);
                if (!t->alive) ss << " (destroyed)";
                break;
            }
            case SpellEffect::Poison:
                if (potency > t->poisonDamage) t->poisonDamage = potency;
                t->poisonRounds = std::max(t->poisonRounds, 3);
                ss << " → " << t->name << " poisoned (" << potency << "/round)";
                break;
            case SpellEffect::Burn:
                if (potency > t->burnDamage) t->burnDamage = potency;
                t->burnRounds = std::max(t->burnRounds, 2);
                ss << " → " << t->name << " burned (" << potency << "/round)";
                break;
            case SpellEffect::AttackDebuff: {
                int debuffRnds = hero.radianceSpecialty ? 4 : 2;
                t->roundAttackBonus  -= bestSpell->power;
                t->buffAttackRounds   = debuffRnds;
                ss << " → " << t->name << " -" << bestSpell->power << " atk (" << debuffRnds << " rounds)";
                break;
            }
            case SpellEffect::DefenseDebuff: {
                int debuffRnds = hero.radianceSpecialty ? 4 : 2;
                t->roundDefenseBonus -= bestSpell->power;
                t->buffDefenseRounds  = debuffRnds;
                ss << " → " << t->name << " -" << bestSpell->power << " def (" << debuffRnds << " rounds)";
                break;
            }
            case SpellEffect::MoraleDrain:
                if (!t->moraleImmune) t->morale = std::max(0, t->morale - potency);
                ss << " → " << t->name << " morale -" << potency;
                break;
            case SpellEffect::AttackBuff: {
                int atkRnds = hero.radianceSpecialty ? 4 : 2;
                t->roundAttackBonus += bestSpell->power;
                t->buffAttackRounds  = std::max(t->buffAttackRounds, atkRnds);
                ss << " → " << t->name << " +" << bestSpell->power << " atk (" << atkRnds << " rounds)";
                if (hero.covenantSpecialty) {
                    for (auto& adj : m_grid.units()) {
                        if (!adj.alive || adj.isPlayer || adj.id == t->id) continue;
                        if (HexGrid::distance(adj.pos, t->pos) > 1) continue;
                        adj.roundAttackBonus += std::max(1, bestSpell->power / 2);
                        adj.buffAttackRounds  = std::max(adj.buffAttackRounds, atkRnds);
                    }
                }
                break;
            }
            case SpellEffect::DefenseBuff: {
                int defRnds = hero.radianceSpecialty ? 4 : 2;
                t->roundDefenseBonus += bestSpell->power;
                t->buffDefenseRounds  = std::max(t->buffDefenseRounds, defRnds);
                ss << " → " << t->name << " +" << bestSpell->power << " def (" << defRnds << " rounds)";
                if (hero.covenantSpecialty) {
                    for (auto& adj : m_grid.units()) {
                        if (!adj.alive || adj.isPlayer || adj.id == t->id) continue;
                        if (HexGrid::distance(adj.pos, t->pos) > 1) continue;
                        adj.roundDefenseBonus += std::max(1, bestSpell->power / 2);
                        adj.buffDefenseRounds  = std::max(adj.buffDefenseRounds, defRnds);
                    }
                }
                break;
            }
            case SpellEffect::Heal: {
                int healed = std::min(potency, t->maxHp - t->hp);
                if (healed > 0) { t->hp += healed; if (m_healCb) m_healCb(t->id, healed, t->pos); }
                ss << " → " << t->name << " healed " << healed;
                break;
            }
            case SpellEffect::MoraleBoost:
                if (!t->moraleImmune)
                    t->morale = std::min(100, t->morale + potency);
                ss << " → " << t->name << " morale +" << potency;
                break;
            default: break;
        }
    }

    addLog(ss.str());
    m_grid.removeDeadUnits();
    checkVictory();
}

// ── Warden's Mark melee cleave splash ─────────────────────────────────────────
void CombatEngine::applyWardenMarkSplash(CombatUnit& attacker, HexCoord targetPos,
                                          uint32_t targetId, int damage)
{
    const Hero& hero = attacker.isPlayer ? m_playerHero : m_enemyHero;
    const SkillInstance* si = hero.skills.getSkill(SkillID::WARDEN_MARK);
    if (!si) return;
    const SkillDef* def = findSkillDef(SkillID::WARDEN_MARK);
    if (!def) return;

    int splashMax = def->values[static_cast<int>(si->tier)]
                  + (attacker.isPlayer && m_wardenBrand ? 1 : 0);
    int splashDmg = std::max(1, damage / 2);
    int splashHit = 0;
    for (auto& foe : m_grid.units()) {
        if (splashHit >= splashMax) break;
        if (!foe.alive || foe.isPlayer == attacker.isPlayer || foe.id == targetId) continue;
        if (HexGrid::distance(foe.pos, targetPos) > 1) continue;
        foe.applyDamage(splashDmg);
        addLog("  Warden's Mark: " + foe.name + " hit for " + std::to_string(splashDmg));
        if (!foe.alive) addLog("  " + foe.name + " destroyed!");
        splashHit++;
    }
    if (splashHit > 0)
        addLog(hero.name + " Warden's Mark: " + std::to_string(splashHit) +
               " adjacent unit(s) splashed");
}

// ── Kill event processing: fires all on-death specialties ─────────────────────
// Called whenever an attack reduces target to 0 HP, from any attack path.
// BloodWeb fires when an ENEMY of the hero's side dies (heals own allies).
// SoulHarvest fires only when the attacker's hero kills an enemy of that hero.
// AdaptationMirror fires when an ALLY of the hero's side dies.
void CombatEngine::processKillEvents(CombatUnit& attacker, CombatUnit& target,
                                      const DamageResult& result)
{
    HexCoord deathPos      = target.pos;
    bool     targetIsPlayer = target.isPlayer;

    // LastRites (Confessor): any nearby death charges Holy ally Desperation meters
    auto applyLastRites = [&](const Hero& hero, bool heroIsPlayer) {
        if (!hero.lastRitesSpecialty) return;
        int charged = 0;
        for (auto& ally : m_grid.units()) {
            if (!ally.alive || ally.isPlayer != heroIsPlayer) continue;
            if (!hasTag(ally.tags, UnitTag::Holy)) continue;
            if (HexGrid::distance(ally.pos, deathPos) > 3) continue;
            ally.desperationMeter = std::min(100, ally.desperationMeter + 20);
            charged++;
        }
        if (charged > 0)
            addLog(hero.name + " LastRites: " + std::to_string(charged) +
                   " Holy units +20 Desperation");
    };
    applyLastRites(m_playerHero, true);
    applyLastRites(m_enemyHero,  false);

    // VoidLink (Void Weaver): dead Void ally disrupts adjacent foes, empowers nearby Void allies
    if (hasTag(target.tags, UnitTag::Void)) {
        auto applyVoidLink = [&](const Hero& hero, bool heroIsPlayer) {
            if (!hero.voidLinkSpecialty) return;
            if (targetIsPlayer != heroIsPlayer) return;  // only own Void unit deaths trigger it
            int disrupted = 0;
            for (auto& foe : m_grid.units()) {
                if (!foe.alive || foe.isPlayer == heroIsPlayer) continue;
                if (HexGrid::distance(foe.pos, deathPos) > 1) continue;
                foe.roundAttackBonus -= 1;
                foe.buffAttackRounds  = std::max(foe.buffAttackRounds, 2);
                disrupted++;
            }
            int empowered = 0;
            for (auto& ally : m_grid.units()) {
                if (!ally.alive || ally.isPlayer != heroIsPlayer) continue;
                if (!hasTag(ally.tags, UnitTag::Void)) continue;
                if (HexGrid::distance(ally.pos, deathPos) > 3) continue;
                ally.roundAttackBonus += 1;
                ally.buffAttackRounds  = std::max(ally.buffAttackRounds, 2);
                empowered++;
            }
            if (disrupted > 0 || empowered > 0)
                addLog(hero.name + " VoidLink: " + target.name + " death — "
                       + std::to_string(disrupted) + " enemies disrupted, "
                       + std::to_string(empowered) + " Void allies empowered");
        };
        applyVoidLink(m_playerHero, true);
        applyVoidLink(m_enemyHero,  false);
    }

    // BloodWeb (Oathmaster): allies heal when an enemy of this hero's side dies
    if (result.killed > 0) {
        auto applyBloodWeb = [&](const Hero& hero, bool heroIsPlayer) {
            if (!hero.bloodWebSpecialty) return;
            if (targetIsPlayer == heroIsPlayer) return;  // only enemy deaths trigger it
            int healAmt = result.killed * 4;
            int healed  = 0;
            for (auto& ally : m_grid.units()) {
                if (!ally.alive || ally.isPlayer != heroIsPlayer) continue;
                int prev = ally.hp;
                ally.hp  = std::min(ally.maxHp, ally.hp + healAmt);
                if (ally.hp > prev) healed++;
            }
            if (healed > 0)
                addLog(hero.name + " BloodWeb: " + std::to_string(healed) +
                       " allies healed " + std::to_string(healAmt) + " HP from kill");
        };
        applyBloodWeb(m_playerHero, true);
        applyBloodWeb(m_enemyHero,  false);
    }

    // SoulHarvest (Death Herald): attacker's hero heals +5 HP per enemy unit killed
    if (result.killed > 0) {
        bool attackerIsPlayer = attacker.isPlayer;
        auto applySoulHarvest = [&](Hero& hero, bool heroIsPlayer) {
            if (!hero.soulHarvestSpecialty) return;
            if (heroIsPlayer != attackerIsPlayer) return;  // only the attacker's hero benefits
            if (targetIsPlayer == attackerIsPlayer) return;  // only from enemy kills
            int heal = std::min(5 * result.killed, hero.heroMaxHp - hero.heroHp);
            if (heal > 0) {
                hero.heroHp += heal;
                addLog(hero.name + " Soul Harvest: +" + std::to_string(heal) + " HP from kill");
            }
        };
        applySoulHarvest(m_playerHero, true);
        applySoulHarvest(m_enemyHero,  false);
    }

    // AdaptationMirror (Fleshbinder): ally death grants friendly OrganicMech an adaptation
    {
        auto applyAdaptationMirror = [&](const Hero& hero, bool heroIsPlayer) {
            if (!hero.adaptationMirrorSpecialty) return;
            if (targetIsPlayer != heroIsPlayer) return;  // only own ally deaths trigger it
            for (auto& ally : m_grid.units()) {
                if (!ally.alive || ally.isPlayer != heroIsPlayer) continue;
                if (!hasTag(ally.tags, UnitTag::OrganicMech)) continue;
                if (ally.adaptationsGained >= 6) continue;
                if (ally.adaptationsGained % 2 == 0) ally.attack++;
                else                                   ally.defense++;
                ally.adaptationsGained++;
            }
            addLog(hero.name + " AdaptationMirror: OrganicMech adapt from ally death");
        };
        applyAdaptationMirror(m_playerHero, true);
        applyAdaptationMirror(m_enemyHero,  false);
    }

    // ── Blood Pool + Ascension (Bloodsworn) ──────────────────────────────────
    // Player BloodBound unit kills enemy → increment pool; 5 kills trigger Ascension
    if (attacker.isPlayer && !targetIsPlayer && result.killed > 0
        && hasTag(attacker.tags, UnitTag::BloodBound)
        && m_playerHero.skills.getSkill(SkillID::BLOOD_POOL)
        && !m_ascended) {
        ++m_bloodPool;
        addLog("Blood Pool: " + std::to_string(m_bloodPool) + "/5");
        if (m_bloodPool >= 5) {
            m_ascended = true;
            int boosted = 0;
            for (auto& u : m_grid.units()) {
                if (!u.alive || !u.isPlayer) continue;
                if (hasTag(u.tags, UnitTag::BloodBound)) {
                    u.attack  += 3;
                    u.defense += 1;
                    u.morale   = std::min(100, u.morale + 20);
                    boosted++;
                }
            }
            if (boosted > 0)
                addLog("ASCENSION! " + std::to_string(boosted) +
                       " BloodBound units surge: +3 ATK, +1 DEF, +20 morale!");
        }
    }

    // ── Possession (Voidkin) ──────────────────────────────────────────────────
    // Player Void unit kills enemy stack → reanimate it for 1 round on player side
    if (attacker.isPlayer && !targetIsPlayer && !target.alive
        && hasTag(attacker.tags, UnitTag::Void)
        && m_playerHero.skills.getSkill(SkillID::POSSESSION)
        && target.count > 0 && !target.possessed) {
        target.alive              = true;
        target.count              = std::max(1, target.count / 2);
        target.hp                 = target.maxHp;
        target.possessed          = true;
        target.possessedRoundsLeft = 1;
        target.isPlayer           = true;   // switch to player side
        target.hasActed           = false;
        target.hasMoved           = false;
        addLog(target.name + " is Possessed! Fights for you this round.");
        // Do NOT fall through to removeDeadUnits — unit stays alive
        spawnWildGrowthGhosts();
        return;
    }

    spawnWildGrowthGhosts();
    m_grid.removeDeadUnits();
}

// ── WildGrowth: dead Beast units respawn as ghost at half stats ───────────────
void CombatEngine::spawnWildGrowthGhosts()
{
    auto trySpawn = [&](const Hero& hero, bool isPlayer) {
        if (!hero.wildGrowthSpecialty) return;
        for (auto& u : m_grid.units()) {
            if (u.isPlayer != isPlayer || u.alive) continue;
            if (!hasTag(u.tags, UnitTag::Beast)) continue;
            if (m_wildGrowthGhosted.count(u.id)) continue;
            m_wildGrowthGhosted.insert(u.id);

            int ghostCount = std::max(1, u.count / 2);
            CombatUnit ghost = u;
            ghost.alive    = true;
            ghost.count    = ghostCount;
            ghost.hp       = std::max(1, ghost.maxHp / 2);
            ghost.attack   = std::max(1, ghost.attack  / 2);
            ghost.defense  = std::max(1, ghost.defense / 2);
            ghost.name     = "Ghost " + u.name;
            ghost.hasActed = true;  // ghosts don't act on the turn they spawn
            ghost.hasMoved = true;

            uint32_t gid = m_grid.addUnit(ghost);
            CombatUnit* placed = m_grid.getUnit(gid);
            if (!placed) continue;

            // Try to place on the same tile first, then adjacent free tiles
            bool ok = m_grid.placeUnit(*placed, u.pos);
            if (!ok) {
                for (const auto& nb : HexGrid::neighbors(u.pos)) {
                    if (!m_grid.inBounds(nb)) continue;
                    auto* t = m_grid.getTile(nb);
                    if (!t || t->occupied) continue;
                    ok = m_grid.placeUnit(*placed, nb);
                    if (ok) break;
                }
            }
            if (ok)
                addLog(hero.name + " WildGrowth: " + u.name + " → " + ghost.name +
                       " (x" + std::to_string(ghostCount) + ")!");
        }
    };
    trySpawn(m_playerHero, true);
    trySpawn(m_enemyHero,  false);
}

// ── Victory check ──────────────────────────────────────────────────────────────
void CombatEngine::checkVictory()
{
    bool playerAlive = false, enemyAlive = false;
    for (auto& u : m_grid.units()) {
        if (!u.alive) continue;
        if (u.isPlayer)  playerAlive = true;
        else             enemyAlive  = true;
    }
    if (!enemyAlive)  { m_phase = CombatPhase::Victory; addLog("VICTORY!"); }
    if (!playerAlive) { m_phase = CombatPhase::Defeat;  addLog("DEFEAT!"); }
}

// ── Siege: attack a wall tile ──────────────────────────────────────────────────
bool CombatEngine::attackWall(HexCoord wallHex)
{
    if (!m_isSiege || m_phase != CombatPhase::PlayerTurn) return false;
    CombatUnit* unit = activeUnit();
    if (!unit || !unit->isPlayer) return false;

    CombatTile* tile = m_grid.getTile(wallHex);
    if (!tile || tile->type != CombatTileType::Wall || tile->wallHP <= 0) return false;

    // Siege engines use their wallDamage; normal melee units deal regular damage
    int dmg = 0;
    if (unit->isSiegeEngine && unit->wallDamage > 0) {
        // Battering Ram: gate only
        if (unit->gateOnly && !(wallHex == m_grid.gateHex())) return false;
        dmg = unit->wallDamage;
        // Catapult/Trebuchet: check range
        if (unit->range > 0) {
            if (HexGrid::distance(unit->pos, wallHex) > unit->range) return false;
            if (unit->shotsLeft <= 0) return false;
            --unit->shotsLeft;
        } else {
            if (HexGrid::distance(unit->pos, wallHex) > 1) return false;
        }
    } else {
        // Normal unit melee-attacking a wall
        if (HexGrid::distance(unit->pos, wallHex) > 1) return false;
        dmg = std::max(1, (unit->attack * unit->count) / 4);
    }

    bool breached = m_grid.damageWall(wallHex, dmg);
    addLog(unit->name + " attacked wall for " + std::to_string(dmg) + " dmg"
           + (breached ? " — BREACHED!" : ""));

    unit->hasActed = true;
    advanceTurn();
    return true;
}
