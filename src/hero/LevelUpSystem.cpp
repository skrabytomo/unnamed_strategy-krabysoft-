#include "LevelUpSystem.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>

static int randInt(int n) { return n > 0 ? rand() % n : 0; }

LevelUpOffer LevelUpSystem::makeOffer(int skillId, bool isUpgrade, bool isWildcard,
                                       const std::vector<SkillDef>& allSkills,
                                       const HeroSkills& skills)
{
    LevelUpOffer offer;
    offer.skillId   = skillId;
    offer.isUpgrade = isUpgrade;
    offer.isWildcard = isWildcard;

    // Build label
    for (auto& s : allSkills) {
        if (s.id == skillId) {
            if (isUpgrade) {
                auto* inst = skills.getSkill(skillId);
                std::string tier = "Advanced";
                if (inst && inst->tier == SkillTier::Advanced) tier = "Master";
                offer.label = "Upgrade " + s.name + " → " + tier;
            } else {
                offer.label = "Learn " + s.name;
                if (isWildcard) offer.label += " [Wildcard]";
            }
            break;
        }
    }
    return offer;
}

std::vector<LevelUpOffer> LevelUpSystem::generateOffers(
    const HeroClassDef&    heroClass,
    const HeroSkills&      currentSkills,
    int                    heroLevel,
    const std::vector<SkillDef>& allSkills,
    FactionId              faction)
{
    std::vector<LevelUpOffer> offers;
    std::vector<int> candidates;

    // Collect upgradeable skills first (already learned, not master)
    for (auto& inst : currentSkills.slots) {
        if (inst.canUpgrade()) candidates.push_back(inst.defId);
    }

    // Collect learnable skills from class pool
    for (int sid : heroClass.skillPool) {
        if (!currentSkills.hasSkill(sid) && currentSkills.canLearn(sid))
            candidates.push_back(sid);
    }

    // Shuffle
    for (int i = static_cast<int>(candidates.size()) - 1; i > 0; --i) {
        int j = randInt(i + 1);
        std::swap(candidates[i], candidates[j]);
    }

    // Pick 2 non-duplicate offers
    int picked = 0;
    for (int sid : candidates) {
        if (picked >= 2) break;
        bool isUpgrade = currentSkills.hasSkill(sid);
        offers.push_back(makeOffer(sid, isUpgrade, false, allSkills, currentSkills));
        picked++;
    }

    // Wildcard every 4 levels — offer a 3rd choice from outside the class pool
    bool addWildcard = (heroLevel % 4 == 0);
    if (addWildcard) {
        // General combat skills as wildcards
        static const int wildcardPool[] = {
            SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::ARCHERY,
            SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::LOGISTICS,
            SkillID::SCOUTING, SkillID::FIRST_AID,
            SkillID::LUCK, SkillID::MYSTICISM, SkillID::NECROMANCY
        };
        std::vector<int> wc;
        for (int sid : wildcardPool) {
            if (!currentSkills.hasSkill(sid) && currentSkills.canLearn(sid)) {
                // Make sure it's not already in class pool
                bool inPool = false;
                for (int ps : heroClass.skillPool) if (ps == sid) { inPool = true; break; }
                if (!inPool) wc.push_back(sid);
            }
        }
        if (!wc.empty()) {
            int wcSid = wc[randInt(static_cast<int>(wc.size()))];
            offers.push_back(makeOffer(wcSid, false, true, allSkills, currentSkills));
        }
    }

    // Fallback — always return at least 1 offer
    if (offers.empty()) {
        offers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
    }

    return offers;
}

bool LevelUpSystem::applyOffer(const LevelUpOffer& offer, HeroSkills& skills)
{
    if (offer.isUpgrade)
        return skills.upgrade(offer.skillId);
    else
        return skills.learn(offer.skillId);
}

void LevelUpSystem::applyStatGains(HeroStats& stats, const HeroClassDef& heroClass, int newLevel)
{
    // Every level: +1 to primary stats based on class scaling
    if (heroClass.scalesAttack)      stats.attack  += 1;
    if (!heroClass.scalesAttack)     stats.defense += 1; // non-attack classes gain defense

    // Magic stats gain every 2 levels for primary school
    if (newLevel % 2 == 0) {
        if (heroClass.scalesLightPower)  stats.casting.lightPower  += 1;
        if (heroClass.scalesBloodPower)  stats.casting.bloodPower  += 1;
        if (heroClass.scalesDeathPower)  stats.casting.deathPower  += 1;
        if (heroClass.scalesNaturePower) stats.casting.naturePower += 1;
        if (heroClass.scalesForgePower)  stats.casting.forgePower  += 1;
        if (heroClass.scalesFleshPower)  stats.casting.fleshPower  += 1;
    }

    // Mana grows every level
    stats.maxMana += 1;
    stats.mana = stats.maxMana; // restore on level up

    // HP grows every level
    stats.maxHp += 5;
    stats.hp = stats.maxHp;
}
