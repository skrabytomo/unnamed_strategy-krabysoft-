#pragma once
#include <string>
#include <vector>
#include <functional>
#include "FactionId.h"

// ── Skill tiers ────────────────────────────────────────────────────────────────
enum class SkillTier : uint8_t { Basic = 0, Advanced, Master };

// ── Skill IDs ─────────────────────────────────────────────────────────────────
// 1xx = combat skills (any faction)
// 2xx = Light Power skills
// 3xx = Blood Power skills
// 4xx = Death Power skills
// 5xx = Nature Power skills
// 6xx = Forge Power skills
// 7xx = Flesh Power skills
// 8xx = faction-specific

namespace SkillID {
    // Combat
    constexpr int OFFENSE       = 101;
    constexpr int DEFENSE_SKILL = 102;
    constexpr int ARCHERY       = 103;
    constexpr int LEADERSHIP    = 104;
    constexpr int TACTICS       = 105;
    constexpr int LOGISTICS     = 106;  // +movement
    constexpr int SCOUTING      = 107;  // +vision
    constexpr int FIRST_AID     = 108;  // heal units between battles
    constexpr int LUCK          = 109;  // +5/10/15% chance for double-damage lucky hit
    constexpr int MYSTICISM     = 110;  // +1/2/3 extra mana regen per combat round
    constexpr int NECROMANCY    = 111;  // raise 10/20/30% of killed enemies as skeletons

    // Magic schools
    constexpr int LIGHT_MAGIC   = 201;
    constexpr int BLOOD_MAGIC   = 301;
    constexpr int DEATH_MAGIC   = 401;
    constexpr int NATURE_MAGIC  = 501;
    constexpr int FORGE_MAGIC   = 601;
    constexpr int FLESH_MAGIC   = 701;

    // Faction-specific
    constexpr int DESPERATION   = 801;  // Holy Order — meter charges faster
    constexpr int INSPIRATION   = 802;  // Holy Order — wider aura
    constexpr int BLOOD_POOL    = 803;  // Bloodsworn — pool fills faster
    constexpr int POSSESSION    = 804;  // Voidkin — longer duration
    constexpr int BLUEPRINT     = 805;  // Iron Assembly — unlock faster
    constexpr int ADAPTATION    = 806;  // Amalgamate — gain faster
    constexpr int ETERNAL_CMD   = 807;  // Eternal Empire — reraised units stronger
    constexpr int SYMBIOSIS     = 808;  // Thornkin — bond buffs
    constexpr int WARDEN_MARK   = 809;  // Crimson Wardens — double mark
    constexpr int MIRRORING     = 810;  // Convergence — longer mirror duration
}

// ── Skill effect types ─────────────────────────────────────────────────────────
enum class SkillEffectType
{
    StatBonus,          // flat bonus to a hero stat
    UnitStatBonus,      // bonus to all units' attack/defense/speed
    MovementBonus,      // +movement pool
    VisionBonus,        // +vision range
    MagicSchoolBonus,   // +casting stat for a school
    SpecialMechanic,    // faction unique — handled by game logic
};

// ── Skill definition ──────────────────────────────────────────────────────────
struct SkillDef
{
    int         id          = 0;
    std::string name;
    std::string description;
    FactionId   faction     = FactionId::None;  // None = any class via wildcard

    SkillEffectType effectType = SkillEffectType::StatBonus;

    // Effect values per tier [Basic, Advanced, Master]
    int values[3] = {0, 0, 0};

    // Which stat this affects (for StatBonus / UnitStatBonus types)
    // "attack", "defense", "speed", "lightPower", etc.
    std::string statName;

    // Prerequisites — skill ID that must be learned first at any tier
    int prerequisiteId = 0;
};

// ── Per-hero skill instance ────────────────────────────────────────────────────
struct SkillInstance
{
    int       defId = 0;
    SkillTier tier  = SkillTier::Basic;

    bool canUpgrade() const { return tier != SkillTier::Master; }
    void upgrade() {
        if (tier == SkillTier::Basic)    tier = SkillTier::Advanced;
        else if (tier == SkillTier::Advanced) tier = SkillTier::Master;
    }
};

// ── Hero skill slots (max 8) ──────────────────────────────────────────────────
struct HeroSkills
{
    static constexpr int MAX_SLOTS = 8;

    std::vector<SkillInstance> slots;  // max 8

    bool hasSkill(int defId) const {
        for (auto& s : slots) if (s.defId == defId) return true;
        return false;
    }

    SkillInstance* getSkill(int defId) {
        for (auto& s : slots) if (s.defId == defId) return &s;
        return nullptr;
    }
    const SkillInstance* getSkill(int defId) const {
        for (const auto& s : slots) if (s.defId == defId) return &s;
        return nullptr;
    }

    bool canLearn(int defId) const {
        return !hasSkill(defId) && static_cast<int>(slots.size()) < MAX_SLOTS;
    }

    bool learn(int defId) {
        if (!canLearn(defId)) return false;
        slots.push_back({defId, SkillTier::Basic});
        return true;
    }

    bool upgrade(int defId) {
        auto* s = getSkill(defId);
        if (!s || !s->canUpgrade()) return false;
        s->upgrade();
        return true;
    }
};
