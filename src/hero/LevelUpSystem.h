#pragma once
#include <vector>
#include <string>
#include "HeroStats.h"
#include "HeroClass.h"
#include "Skills.h"

struct LevelUpOffer
{
    int  skillId    = 0;
    bool isUpgrade  = false;   // true = upgrade existing skill tier
    bool isWildcard = false;   // true = from outside class pool
    std::string label;
};

class LevelUpSystem
{
public:
    // Generate 2 offers from class pool + occasional wildcard (every 4 levels)
    // Returns 2 offers to present to player
    static std::vector<LevelUpOffer> generateOffers(
        const HeroClassDef&    heroClass,
        const HeroSkills&      currentSkills,
        int                    heroLevel,
        const std::vector<SkillDef>& allSkills,
        FactionId              faction
    );

    // Apply chosen offer to hero
    static bool applyOffer(const LevelUpOffer& offer, HeroSkills& skills);

    // Stat gains on level up (based on class scaling)
    static void applyStatGains(HeroStats& stats, const HeroClassDef& heroClass, int newLevel);

private:
    static LevelUpOffer makeOffer(int skillId, bool isUpgrade,
                                   bool isWildcard,
                                   const std::vector<SkillDef>& allSkills,
                                   const HeroSkills& skills);
};
