#include "ArmyBuilder.h"
#include "../hero/Skills.h"
#include <cmath>
#include <algorithm>

const UnitSimData* ArmyBuilder::getTierData(FactionId faction, int tier)
{
    for (int i = 0; i < SIM_UNIT_COUNT; ++i) {
        if (SIM_UNITS[i].faction == faction && SIM_UNITS[i].tier == tier)
            return &SIM_UNITS[i];
    }
    return nullptr;
}

int ArmyBuilder::heroLevelFromWeeks(int weeks)
{
    // 2 fights per week, each fight ~50 XP. Level threshold ramps quadratically.
    // level = 1 + floor(sqrt(weeks * 2))
    return 1 + static_cast<int>(std::sqrt(static_cast<float>(weeks) * 2.0f));
}

std::vector<CombatUnit> ArmyBuilder::buildArmy(FactionId faction, int weeks)
{
    std::vector<CombatUnit> army;
    army.reserve(6);

    for (int tier = 1; tier <= 6; ++tier) {
        const UnitSimData* d = getTierData(faction, tier);
        if (!d) continue;
        if (weeks < d->unlockWeek) continue;

        int accumulated = (weeks - d->unlockWeek + 1) * d->weeklyGrowth;
        int count = std::min(accumulated, MAX_STACK);
        if (count <= 0) continue;

        CombatUnit u;
        u.name          = d->name;
        u.count         = count;
        u.hp            = d->hp;
        u.maxHp         = d->hp;
        u.attack        = d->attack;
        u.defense       = d->defense;
        u.damageMin     = d->damageMin;
        u.damageMax     = d->damageMax;
        u.speed         = d->speed;
        u.range         = d->range;
        u.shots         = d->shots;
        u.shotsLeft     = d->shots;
        u.flying        = d->flying;
        u.tags          = d->tags;
        u.stackSlot     = tier - 1;
        u.alive         = true;
        u.moraleImmune  = hasTag(d->tags, UnitTag::Undead) || hasTag(d->tags, UnitTag::Mechanical);
        u.hasSecondLife = d->hasSecondLife;
        u.morale        = 50;

        army.push_back(u);
    }

    return army;
}

int ArmyBuilder::armyGoldCost(FactionId faction, int weeks)
{
    int total = 0;
    for (int tier = 1; tier <= 6; ++tier) {
        const UnitSimData* d = getTierData(faction, tier);
        if (!d || weeks < d->unlockWeek) continue;
        int count = std::min((weeks - d->unlockWeek + 1) * d->weeklyGrowth, MAX_STACK);
        if (count > 0) total += count * d->goldCost;
    }
    return total;
}

Hero ArmyBuilder::buildHero(FactionId faction, int weeks)
{
    Hero h;
    h.faction = faction;
    h.name    = "SimHero";
    h.level   = heroLevelFromWeeks(weeks);

    // +1 attack every 2 levels, +1 defense every 2 levels (starting level 2)
    int bonus = (h.level - 1) / 2;
    h.attack  = 2 + bonus;
    h.defense = 2 + bonus;

    // Faction skill packages — each faction learns its signature skills and upgrades
    // them as the hero levels up. Level 3 = 1 skill Basic; Level 5 = 1 Advanced + 1 Basic.
    int level = h.level;

    // Universal combat skill (all factions)
    h.skills.learn(SkillID::OFFENSE);
    if (level >= 5) h.skills.upgrade(SkillID::OFFENSE);  // Advanced at level 5

    // Faction signature skills
    switch (faction) {
    case FactionId::HolyOrder:
        h.skills.learn(SkillID::LEADERSHIP);
        if (level >= 5) h.skills.upgrade(SkillID::LEADERSHIP);
        if (level >= 7) h.skills.learn(SkillID::LIGHT_MAGIC);
        break;
    case FactionId::CrimsonWardens:
        h.skills.learn(SkillID::ARCHERY);
        if (level >= 5) h.skills.upgrade(SkillID::ARCHERY);
        h.skills.learn(SkillID::DEATH_MAGIC);
        if (level >= 7) h.skills.upgrade(SkillID::DEATH_MAGIC);
        break;
    case FactionId::Thornkin:
        // SYMBIOSIS removed: base stats already include +1 equivalent (avoids double-count).
        h.skills.learn(SkillID::NATURE_MAGIC);
        if (level >= 5) h.skills.upgrade(SkillID::NATURE_MAGIC);
        if (level >= 7) h.skills.upgrade(SkillID::NATURE_MAGIC);
        break;
    case FactionId::EternalEmpire:
        h.skills.learn(SkillID::ETERNAL_CMD);
        h.skills.learn(SkillID::DEFENSE_SKILL);
        if (level >= 5) h.skills.upgrade(SkillID::DEFENSE_SKILL);
        break;
    case FactionId::Bloodsworn:
        h.skills.learn(SkillID::BLOOD_MAGIC);
        if (level >= 5) h.skills.upgrade(SkillID::BLOOD_MAGIC);
        if (level >= 7) h.skills.upgrade(SkillID::BLOOD_MAGIC);  // Master
        break;
    case FactionId::Voidkin:
        h.skills.learn(SkillID::DEFENSE_SKILL);
        if (level >= 5) h.skills.learn(SkillID::TACTICS);
        break;
    case FactionId::IronAssembly:
        h.skills.learn(SkillID::ARCHERY);
        if (level >= 5) h.skills.upgrade(SkillID::ARCHERY);
        h.skills.learn(SkillID::FORGE_MAGIC);
        if (level >= 7) h.skills.upgrade(SkillID::FORGE_MAGIC);
        break;
    case FactionId::Amalgamate:
        // ADAPTATION learned at level 5+ so early game uses default threshold=3.
        // Faster adaptation at higher levels rewards surviving into mid/late game.
        if (level >= 5) h.skills.learn(SkillID::ADAPTATION);   // Basic (threshold 2) at level 5
        if (level >= 7) h.skills.upgrade(SkillID::ADAPTATION); // Master (threshold 1) at level 7
        h.skills.learn(SkillID::FLESH_MAGIC);
        if (level >= 5) h.skills.upgrade(SkillID::FLESH_MAGIC);
        if (level >= 7) h.skills.upgrade(SkillID::FLESH_MAGIC);
        break;
    case FactionId::Convergence:
        h.skills.learn(SkillID::DEFENSE_SKILL);
        if (level >= 7) h.skills.upgrade(SkillID::DEFENSE_SKILL);
        if (level >= 7) h.skills.learn(SkillID::TACTICS);
        break;
    default:
        break;
    }

    return h;
}
