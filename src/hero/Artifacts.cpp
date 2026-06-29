#include "Artifacts.h"

void ArtifactRegistry::init()
{
    m_artifacts.clear();
    int id = 1;

    auto add = [&](const char* name, ArtifactSlot slot, ArtifactRarity rarity,
                   ArtifactBonus bonus, Resources cost, const char* desc = "") {
        ArtifactDef a;
        a.id = id++; a.name = name; a.description = desc;
        a.slot = slot; a.rarity = rarity; a.bonus = bonus;
        a.craftCost = cost;
        a.isCraftable = (rarity == ArtifactRarity::Basic && cost.get(ResourceType::Gold) > 0);
        m_artifacts.push_back(a);
    };

    using S = ArtifactSlot;
    using R = ArtifactRarity;
    Resources none;

    // ── BASIC CRAFTABLE ────────────────────────────────────────────────────────

    // Weapons
    { ArtifactBonus b; b.attack = 2;
      add("Iron Sword",    S::Weapon, R::Basic, b, Resources::gold(1000), "+2 Attack"); }
    { ArtifactBonus b; b.attack = 4;
      add("Steel Blade",   S::Weapon, R::Basic, b, Resources::gold(2500), "+4 Attack"); }
    { ArtifactBonus b; b.attack = 2; b.lightPower = 2;
      add("Holy Blade",    S::Weapon, R::Basic, b,
          []{ Resources r = Resources::gold(2000); r.add(ResourceType::FaithStones,2); return r; }(),
          "+2 Attack, +2 Light Power"); }
    { ArtifactBonus b; b.bloodPower = 3;
      add("Blood Dagger",  S::Weapon, R::Basic, b,
          []{ Resources r = Resources::gold(1500); r.add(ResourceType::BloodEssence,2); return r; }(),
          "+3 Blood Power"); }

    // Helms
    { ArtifactBonus b; b.defense = 2;
      add("Iron Helm",     S::Helm, R::Basic, b, Resources::gold(800), "+2 Defense"); }
    { ArtifactBonus b; b.defense = 3; b.visionBonus = 1;
      add("Scout's Helm",  S::Helm, R::Basic, b, Resources::gold(1200), "+3 Defense, +1 Vision"); }
    { ArtifactBonus b; b.lightPower = 3;
      add("Halo Circlet",  S::Helm, R::Basic, b,
          []{ Resources r = Resources::gold(1500); r.add(ResourceType::FaithStones,2); return r; }(),
          "+3 Light Power"); }

    // Armor
    { ArtifactBonus b; b.defense = 3;
      add("Chainmail",     S::Armor, R::Basic, b, Resources::gold(1200), "+3 Defense"); }
    { ArtifactBonus b; b.defense = 5; b.hpBonus = 20;
      add("Plate Armor",   S::Armor, R::Basic, b,
          []{ Resources r = Resources::gold(3000); r.add(ResourceType::Iron,4); return r; }(),
          "+5 Defense, +20 HP"); }
    { ArtifactBonus b; b.naturePower = 3; b.hpBonus = 15;
      add("Bark Armor",    S::Armor, R::Basic, b,
          []{ Resources r = Resources::gold(2000); r.add(ResourceType::VerdantSap,2); return r; }(),
          "+3 Nature Power, +15 HP"); }

    // Boots
    { ArtifactBonus b; b.moveBonus = 3;
      add("Traveler's Boots", S::Boots, R::Basic, b, Resources::gold(1000), "+3 Movement"); }
    { ArtifactBonus b; b.moveBonus = 5; b.visionBonus = 1;
      add("Ranger's Boots",   S::Boots, R::Basic, b, Resources::gold(2000), "+5 Movement, +1 Vision"); }

    // Rings
    { ArtifactBonus b; b.manaBonus = 5;
      add("Mana Ring",     S::Ring, R::Basic, b, Resources::gold(1500), "+5 Mana"); }
    { ArtifactBonus b; b.manaBonus = 3; b.lightPower = 2;
      add("Cleric's Ring", S::Ring, R::Basic, b,
          []{ Resources r = Resources::gold(2000); r.add(ResourceType::FaithStones,1); return r; }(),
          "+3 Mana, +2 Light Power"); }
    { ArtifactBonus b; b.deathPower = 3;
      add("Death Ring",    S::Ring, R::Basic, b,
          []{ Resources r = Resources::gold(1800); r.add(ResourceType::Mercury,2); return r; }(),
          "+3 Death Power"); }

    // ── SPECIAL (world map finds + shop-buyable) ──────────────────────────────

    { ArtifactBonus b; b.attack = 6; b.defense = 4;
      add("Champion's Blade", S::Weapon, R::Special, b, none, "+6 Attack, +4 Defense");
      m_artifacts.back().shopPrice = 4000; }
    { ArtifactBonus b; b.lightPower = 6; b.manaBonus = 10;
      add("Radiant Staff", S::Weapon, R::Special, b, none, "+6 Light Power, +10 Mana");
      m_artifacts.back().shopPrice = 4000; }
    { ArtifactBonus b; b.deathPower = 6; b.hpBonus = 40;
      add("Scepter of Ending", S::Weapon, R::Special, b, none, "+6 Death Power, +40 HP");
      m_artifacts.back().shopPrice = 4000; }
    { ArtifactBonus b; b.naturePower = 6; b.moveBonus = 4;
      add("Root Staff", S::Weapon, R::Special, b, none, "+6 Nature Power, +4 Movement");
      m_artifacts.back().shopPrice = 4000; }
    { ArtifactBonus b; b.forgePower = 6;
      add("Runic Gauntlet", S::Weapon, R::Special, b, none, "+6 Forge Power");
      m_artifacts.back().shopPrice = 3500; }
    { ArtifactBonus b; b.defense = 8; b.hpBonus = 50;
      add("Fortress Shield", S::Shield, R::Special, b, none, "+8 Defense, +50 HP");
      m_artifacts.back().shopPrice = 3500; }
    { ArtifactBonus b; b.moveBonus = 10; b.visionBonus = 3;
      add("Wings of Speed", S::Cloak, R::Special, b, none, "+10 Movement, +3 Vision");
      m_artifacts.back().shopPrice = 3000; }
    { ArtifactBonus b; b.attack=4; b.defense=4; b.manaBonus=8;
      add("Warlord's Crown", S::Helm, R::Special, b, none, "+4 Attack, +4 Defense, +8 Mana");
      m_artifacts.back().shopPrice = 3500; }

    // ── LEGENDARY (unique, world map only) ───────────────────────────────────

    { ArtifactBonus b; b.attack=8; b.lightPower=8; b.manaBonus=15; b.hpBonus=50;
      add("The Eternal Flame", S::Weapon, R::Legendary, b, none,
          "Ancient relic of the Holy Order — +8 Attack, +8 Light Power, +15 Mana, +50 HP"); }
    { ArtifactBonus b; b.bloodPower=10; b.manaBonus=20; b.hpBonus=80;
      add("Crimson Chalice", S::Misc, R::Legendary, b, none,
          "Blood flows upward in this cup — +10 Blood Power, +20 Mana, +80 HP"); }
    { ArtifactBonus b; b.naturePower=10; b.forgePower=5; b.moveBonus=8;
      add("Heart of the World Tree", S::Misc, R::Legendary, b, none,
          "A fragment of the first tree — +10 Nature Power, +5 Forge Power, +8 Movement"); }
    { ArtifactBonus b; b.deathPower=10; b.defense=6; b.hpBonus=100;
      add("Crown of the Undying Emperor", S::Helm, R::Legendary, b, none,
          "The empire never truly fell — +10 Death Power, +6 Defense, +100 HP"); }
    { ArtifactBonus b; b.forgePower=10; b.fleshPower=5; b.attack=6;
      add("The Iron Heart", S::Armor, R::Legendary, b, none,
          "Machine and flesh as one — +10 Forge Power, +5 Flesh Power, +6 Attack"); }
    { ArtifactBonus b;
      b.attack=5; b.defense=5; b.lightPower=3; b.bloodPower=3;
      b.deathPower=3; b.naturePower=3; b.forgePower=3; b.fleshPower=3;
      b.manaBonus=20; b.hpBonus=60; b.moveBonus=5;
      add("The Mirror Shard", S::Misc, R::Legendary, b, none,
          "Fragment of what the Convergence sought — +5 Attack/Defense, +3 to all magic schools, +20 Mana, +60 HP, +5 Movement"); }
}

const ArtifactDef* ArtifactRegistry::getDef(int id) const {
    for (auto& a : m_artifacts) if (a.id == id) return &a;
    return nullptr;
}

std::vector<const ArtifactDef*> ArtifactRegistry::getCraftable() const {
    std::vector<const ArtifactDef*> r;
    for (auto& a : m_artifacts) if (a.isCraftable) r.push_back(&a);
    return r;
}

std::vector<const ArtifactDef*> ArtifactRegistry::getByRarity(ArtifactRarity rarity) const {
    std::vector<const ArtifactDef*> r;
    for (auto& a : m_artifacts) if (a.rarity == rarity) r.push_back(&a);
    return r;
}

ArtifactBonus ArtifactRegistry::totalBonus(const HeroArtifacts& equipped) const
{
    ArtifactBonus total;
    for (int i = 0; i < HeroArtifacts::SLOT_COUNT; ++i) {
        int aid = equipped.equippedIds[i];
        if (!aid) continue;
        const ArtifactDef* def = getDef(aid);
        if (!def) continue;
        total.attack      += def->bonus.attack;
        total.defense     += def->bonus.defense;
        total.lightPower  += def->bonus.lightPower;
        total.bloodPower  += def->bonus.bloodPower;
        total.deathPower  += def->bonus.deathPower;
        total.naturePower += def->bonus.naturePower;
        total.forgePower  += def->bonus.forgePower;
        total.fleshPower  += def->bonus.fleshPower;
        total.moveBonus   += def->bonus.moveBonus;
        total.visionBonus += def->bonus.visionBonus;
        total.manaBonus   += def->bonus.manaBonus;
        total.hpBonus     += def->bonus.hpBonus;
    }
    return total;
}
