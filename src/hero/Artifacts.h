#pragma once
#include <string>
#include <vector>
#include "../data/Resources.h"

enum class ArtifactSlot : uint8_t
{
    Helm, Armor, Weapon, Shield, Ring, Boots, Cloak, Misc,
    COUNT
};

enum class ArtifactRarity : uint8_t
{
    Basic,      // craftable in town
    Special,    // world map find only
    Legendary,  // world map find only, unique
};

// ── Stat bonuses an artifact grants ───────────────────────────────────────────
struct ArtifactBonus
{
    int attack      = 0;
    int defense     = 0;
    int lightPower  = 0;
    int bloodPower  = 0;
    int deathPower  = 0;
    int naturePower = 0;
    int forgePower  = 0;
    int fleshPower  = 0;
    int moveBonus   = 0;
    int visionBonus = 0;
    int manaBonus   = 0;
    int hpBonus     = 0;
};

struct ArtifactDef
{
    int           id       = 0;
    std::string   name;
    std::string   description;
    ArtifactSlot  slot     = ArtifactSlot::Misc;
    ArtifactRarity rarity  = ArtifactRarity::Basic;

    ArtifactBonus bonus;
    Resources     craftCost;    // empty = not craftable
    bool          isCraftable   = false;
};

// ── Per-hero equipped artifacts ────────────────────────────────────────────────
struct HeroArtifacts
{
    static constexpr int SLOT_COUNT = static_cast<int>(ArtifactSlot::COUNT);

    int equippedIds[SLOT_COUNT] = {};  // 0 = empty

    bool equip(int artifactId, ArtifactSlot slot) {
        equippedIds[static_cast<int>(slot)] = artifactId;
        return true;
    }
    void unequip(ArtifactSlot slot) {
        equippedIds[static_cast<int>(slot)] = 0;
    }
    int getEquipped(ArtifactSlot slot) const {
        return equippedIds[static_cast<int>(slot)];
    }
};

// ── Artifact registry ──────────────────────────────────────────────────────────
class ArtifactRegistry
{
public:
    void init();

    const ArtifactDef* getDef(int id) const;
    std::vector<const ArtifactDef*> getCraftable() const;
    std::vector<const ArtifactDef*> getByRarity(ArtifactRarity r) const;

    // Sum all bonuses for a hero's equipped artifacts
    ArtifactBonus totalBonus(const HeroArtifacts& equipped) const;

    const std::vector<ArtifactDef>& artifacts() const { return m_artifacts; }

private:
    std::vector<ArtifactDef> m_artifacts;
};
