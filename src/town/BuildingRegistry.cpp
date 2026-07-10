#include "BuildingRegistry.h"
#include <algorithm>

// ── Helper macros for cleaner data entry ──────────────────────────────────────
static Resources gold(int g) { return Resources::gold(g); }
static Resources res(ResourceType t, int v) { return Resources::make(t, v); }

static Resources goldAndRes(int g, ResourceType t, int v) {
    Resources r = Resources::gold(g);
    r.add(t, v);
    return r;
}

void BuildingRegistry::init()
{
    m_buildings.clear();
    m_units.clear();

    // ── SHARED BUILDINGS ──────────────────────────────────────────────────────
    {
        BuildingDef b;
        b.id = BID::FORT; b.name = "Fort";
        b.description = "Adds walls and gate - enables siege defense";
        b.category = BuildingCategory::Fort;
        b.cost = goldAndRes(2000, ResourceType::Iron, 4);
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::MARKET; b.name = "Market";
        b.description = "Converts resources - +500 Gold weekly, +1 unit growth";
        b.category = BuildingCategory::Economy;
        b.cost = gold(1000);
        b.weeklyIncome = gold(500);
        b.growthBonus = 1;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::TOWN_SHIPYARD; b.name = "Shipyard";
        b.description = "Heroes can buy boats here - a way off island starts";
        b.category = BuildingCategory::Support;
        b.cost = goldAndRes(1500, ResourceType::Iron, 6);
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::BASTION; b.name = "Bastion";
        b.description = "Unlocks siege defense preparations: spikes, nets, shield walls, plating";
        b.category = BuildingCategory::Fort;
        b.cost = goldAndRes(1800, ResourceType::Iron, 8);
        b.prerequisites = {BID::FORT};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::WAREHOUSE; b.name = "Warehouse";
        b.description = "Resource storage - +2 Iron weekly";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(600, ResourceType::Iron, 2);
        b.weeklyIncome = res(ResourceType::Iron, 2);
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::WAREHOUSE_T2; b.name = "Warehouse (Expanded)";
        b.description = "Expanded storage - +4 Iron weekly";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(1200, ResourceType::Iron, 4);
        b.weeklyIncome = res(ResourceType::Iron, 4);
        b.prerequisites = {BID::WAREHOUSE};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::WAREHOUSE_T3; b.name = "Warehouse (Industrial)";
        b.description = "Industrial storage complex - +6 Iron weekly";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(2500, ResourceType::Iron, 6);
        b.weeklyIncome = res(ResourceType::Iron, 6);
        b.minWeek = 3;
        b.prerequisites = {BID::WAREHOUSE_T2};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::TOWN_HALL; b.name = "Town Hall";
        b.description = "Organized governance - +1500 Gold weekly";
        b.category = BuildingCategory::Economy;
        b.cost = gold(2500);
        b.weeklyIncome = gold(1500);
        b.prerequisites = {BID::MARKET};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::CITY_HALL; b.name = "City Hall";
        b.description = "Thriving metropolis - +3500 Gold weekly";
        b.category = BuildingCategory::Economy;
        b.cost = gold(7000);
        b.weeklyIncome = gold(3500);
        b.minWeek = 3;
        b.prerequisites = {BID::TOWN_HALL};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::MAGE_GUILD; b.name = "Mage Guild";
        b.description = "Teaches 2 faction spells. Upgrade for more spells and discounts.";
        b.category = BuildingCategory::MageGuild;
        b.cost = goldAndRes(2000, ResourceType::FaithStones, 3);
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::MAGE_GUILD_T2; b.name = "Mage Guild (Tier 2)";
        b.description = "Unlocks all 4 faction spells for purchase.";
        b.category = BuildingCategory::MageGuild;
        b.cost = goldAndRes(3000, ResourceType::FaithStones, 5);
        b.prerequisites = {BID::MAGE_GUILD};
        m_buildings.push_back(b);
    }
    // ── FACTION CAPITOLS (unique tier-4 economy, require City Hall + faction hall) ─
    {
        BuildingDef b;
        b.id = BID::HO_SANCTUM; b.name = "Sacred Sanctum";
        b.description = "Holy Order capitol — +6000 Gold/wk, +5 Faith Stones/wk, hero gains +3 Light Power on visit";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::FaithStones, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::FaithStones, 5);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::HO_HALL};
        b.faction = FactionId::HolyOrder;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::CW_NECROPOLIS; b.name = "Warden's Citadel";
        b.description = "Crimson Wardens capitol — +6000 Gold/wk, +5 Faith Stones/wk";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::FaithStones, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::FaithStones, 5);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::CW_HALL};
        b.faction = FactionId::CrimsonWardens;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::TK_HEARTWOOD; b.name = "Ancient Heartwood";
        b.description = "Thornkin capitol — +6000 Gold/wk, +5 Verdant Sap/wk, all unit growth +3/wk";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::VerdantSap, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::VerdantSap, 5);
        b.growthBonus = 3;
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::TK_GROVE_HEART};
        b.faction = FactionId::Thornkin;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::EE_CITADEL; b.name = "Eternal Citadel";
        b.description = "Eternal Empire capitol — +6000 Gold/wk, +5 Mercury/wk, garrison gets +4 defense";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::Mercury, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::Mercury, 5);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::EE_THRONE};
        b.faction = FactionId::EternalEmpire;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::BS_BLOODSPIRE; b.name = "Bloodspire Fortress";
        b.description = "Bloodsworn capitol — +6000 Gold/wk, +5 Faith Stones/wk, Blood Pool fills 2x faster";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::FaithStones, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::FaithStones, 5);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::BS_WAR_HALL};
        b.faction = FactionId::Bloodsworn;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::VK_VOIDCORE; b.name = "Void Core Nexus";
        b.description = "Voidkin capitol — +6000 Gold/wk, +5 Mercury/wk, Void abilities cost -2 mana";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::Mercury, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::Mercury, 5);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::VK_NEXUS};
        b.faction = FactionId::Voidkin;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::IA_MEGAFORGE; b.name = "Grand Megaforge";
        b.description = "Iron Assembly capitol — +6000 Gold/wk, +8 Iron/wk, all Constructs gain +2 ATK";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::Iron, 15);
        b.weeklyIncome = goldAndRes(6000, ResourceType::Iron, 8);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::IA_FORGE_HALL};
        b.faction = FactionId::IronAssembly;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::AM_FLESHPIT; b.name = "Grand Fleshpit";
        b.description = "Amalgamate capitol — +6000 Gold/wk, +5 Blood Essence/wk, grafted units regenerate +5 HP/round";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::BloodEssence, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::BloodEssence, 5);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::AM_GRAFTING_HALL};
        b.faction = FactionId::Amalgamate;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::CV_NEXUS; b.name = "Synthesis Nexus";
        b.description = "Convergence capitol — +6000 Gold/wk, +5 Verdant Sap/wk, mirrored abilities persist 1 extra round";
        b.category = BuildingCategory::Economy;
        b.cost = goldAndRes(12000, ResourceType::VerdantSap, 10);
        b.weeklyIncome = goldAndRes(6000, ResourceType::VerdantSap, 5);
        b.minWeek = 5;
        b.prerequisites = {BID::CITY_HALL, BID::CV_SYNTHESIS_HUB};
        b.faction = FactionId::Convergence;
        m_buildings.push_back(b);
    }

    {
        BuildingDef b;
        b.id = BID::MAGE_GUILD_T3; b.name = "Mage Guild (Tier 3)";
        b.description = "All 4 spells available at 30% discount. Advanced magical research.";
        b.category = BuildingCategory::MageGuild;
        b.cost = goldAndRes(5000, ResourceType::FaithStones, 8);
        b.minWeek = 3;
        b.prerequisites = {BID::MAGE_GUILD_T2};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::MAGE_GUILD_T4; b.name = "Mage Guild (Tier 4)";
        b.description = "All 4 spells at 50% discount. Hero gains +5 max mana when visiting.";
        b.category = BuildingCategory::MageGuild;
        b.cost = goldAndRes(8000, ResourceType::FaithStones, 12);
        b.minWeek = 5;
        b.prerequisites = {BID::MAGE_GUILD_T3};
        m_buildings.push_back(b);
    }

    // ── HOLY ORDER BUILDINGS ──────────────────────────────────────────────────
    {
        BuildingDef b;
        b.id = BID::HO_HALL; b.name = "Cathedral Hall";
        b.description = "Town Hall - +1000 Gold weekly, +2 unit growth";
        b.category = BuildingCategory::Economy;
        b.faction = FactionId::HolyOrder;
        b.cost = goldAndRes(500, ResourceType::FaithStones, 2);
        b.weeklyIncome = gold(1000);
        b.growthBonus = 2;
        m_buildings.push_back(b);
    }

    // T1 - Penitent
    {
        BuildingDef b;
        b.id = BID::HO_T1_BASE; b.name = "Prison Yard";
        b.description = "Produces Penitents each week";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 1; b.weeklyGrowth = 14;
        b.cost = gold(300);
        b.upgradeA = BID::HO_T1_A; b.upgradeB = BID::HO_T1_B;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::HO_T1_A; b.name = "Prison Yard - Fast Death";
        b.description = "Penitents die faster, feed Desperation harder";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 1; b.weeklyGrowth = 18; // more units
        b.cost = gold(500);
        b.path = UpgradePath::PathA;
        b.prerequisites = {BID::HO_T1_BASE};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::HO_T1_B; b.name = "Prison Yard - Hardened";
        b.description = "Penitents tankier, slower meter feed";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 1; b.weeklyGrowth = 12;
        b.cost = gold(500);
        b.path = UpgradePath::PathB;
        b.prerequisites = {BID::HO_T1_BASE};
        m_buildings.push_back(b);
    }

    // T2 - Torch Bearer
    {
        BuildingDef b;
        b.id = BID::HO_T2_BASE; b.name = "Militia Barracks";
        b.description = "Produces Torch Bearers";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 2; b.weeklyGrowth = 10;
        b.cost = goldAndRes(500, ResourceType::Iron, 2);
        b.prerequisites = {BID::HO_T1_BASE};
        b.upgradeA = BID::HO_T2_A; b.upgradeB = BID::HO_T2_B;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::HO_T2_A; b.name = "Militia Barracks - Arsonist";
        b.description = "Torch Bearers spread fire on death";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 2; b.weeklyGrowth = 10;
        b.cost = goldAndRes(700, ResourceType::Iron, 1);
        b.path = UpgradePath::PathA;
        b.prerequisites = {BID::HO_T2_BASE};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::HO_T2_B; b.name = "Militia Barracks - Devoted";
        b.description = "Torch Bearers empower nearby units while alive";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 2; b.weeklyGrowth = 9;
        b.cost = goldAndRes(700, ResourceType::FaithStones, 1);
        b.path = UpgradePath::PathB;
        b.prerequisites = {BID::HO_T2_BASE};
        m_buildings.push_back(b);
    }

    // T3 - Plague Doctor
    {
        BuildingDef b;
        b.id = BID::HO_T3_BASE; b.name = "Apothecary";
        b.description = "Produces Plague Doctors";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 3; b.weeklyGrowth = 7;
        b.cost = goldAndRes(800, ResourceType::FaithStones, 2);
        b.prerequisites = {BID::HO_T2_BASE};
        b.upgradeA = BID::HO_T3_A; b.upgradeB = BID::HO_T3_B;
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::HO_T3_A; b.name = "Apothecary - Sacrifice";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 3; b.weeklyGrowth = 7;
        b.cost = goldAndRes(1000, ResourceType::FaithStones, 1);
        b.path = UpgradePath::PathA;
        b.prerequisites = {BID::HO_T3_BASE};
        m_buildings.push_back(b);
    }
    {
        BuildingDef b;
        b.id = BID::HO_T3_B; b.name = "Apothecary - Toxic Cloud";
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.tier = 3; b.weeklyGrowth = 6;
        b.cost = goldAndRes(1000, ResourceType::Iron, 2);
        b.path = UpgradePath::PathB;
        b.prerequisites = {BID::HO_T3_BASE};
        m_buildings.push_back(b);
    }

    // T4 - Penitent Knight
    {
        BuildingDef b;
        b.id = BID::HO_T4_BASE; b.name = "Knight's Penance Hall";
        b.tier = 4; b.weeklyGrowth = 5;
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.cost = goldAndRes(1500, ResourceType::Iron, 4);
        b.prerequisites = {BID::HO_T3_BASE};
        b.upgradeA = BID::HO_T4_A; b.upgradeB = BID::HO_T4_B;
        m_buildings.push_back(b);
    }
    { BuildingDef b; b.id=BID::HO_T4_A; b.name="Knight's Penance - Shield";
      b.tier=4; b.weeklyGrowth=5; b.category=BuildingCategory::UnitDwelling;
      b.faction=FactionId::HolyOrder; b.cost=goldAndRes(2000,ResourceType::Iron,2);
      b.path=UpgradePath::PathA;
      b.prerequisites={BID::HO_T4_BASE}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::HO_T4_B; b.name="Knight's Penance - Bleed";
      b.tier=4; b.weeklyGrowth=4; b.category=BuildingCategory::UnitDwelling;
      b.faction=FactionId::HolyOrder; b.cost=goldAndRes(2000,ResourceType::BloodEssence,1);
      b.path=UpgradePath::PathB;
      b.prerequisites={BID::HO_T4_BASE}; m_buildings.push_back(b); }

    // T5 - Seraph
    {
        BuildingDef b;
        b.id = BID::HO_T5_BASE; b.name = "Binding Spire";
        b.tier = 5; b.weeklyGrowth = 3;
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.cost = goldAndRes(3000, ResourceType::FaithStones, 5);
        b.prerequisites = {BID::HO_T4_BASE};
        b.upgradeA = BID::HO_T5_A; b.upgradeB = BID::HO_T5_B;
        m_buildings.push_back(b);
    }
    { BuildingDef b; b.id=BID::HO_T5_A; b.name="Binding Spire - Wide Aura";
      b.tier=5; b.weeklyGrowth=3; b.category=BuildingCategory::UnitDwelling;
      b.faction=FactionId::HolyOrder; b.cost=goldAndRes(4000,ResourceType::FaithStones,3);
      b.path=UpgradePath::PathA;
      b.prerequisites={BID::HO_T5_BASE}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::HO_T5_B; b.name="Binding Spire - Unchained";
      b.tier=5; b.weeklyGrowth=2; b.category=BuildingCategory::UnitDwelling;
      b.faction=FactionId::HolyOrder; b.cost=goldAndRes(4000,ResourceType::FaithStones,4);
      b.path=UpgradePath::PathB;
      b.prerequisites={BID::HO_T5_BASE}; m_buildings.push_back(b); }

    // T6 - Winged Hussar
    {
        BuildingDef b;
        b.id = BID::HO_T6_BASE; b.name = "Hussar Sanctum";
        b.tier = 6; b.weeklyGrowth = 1;
        b.category = BuildingCategory::UnitDwelling;
        b.faction = FactionId::HolyOrder;
        b.cost = goldAndRes(6000, ResourceType::FaithStones, 8);
        b.prerequisites = {BID::HO_T5_BASE};
        b.upgradeA = BID::HO_T6_A; b.upgradeB = BID::HO_T6_B;
        m_buildings.push_back(b);
    }
    { BuildingDef b; b.id=BID::HO_T6_A; b.name="Hussar Sanctum - Desperation";
      b.tier=6; b.weeklyGrowth=1; b.category=BuildingCategory::UnitDwelling;
      b.faction=FactionId::HolyOrder; b.cost=goldAndRes(8000,ResourceType::FaithStones,6);
      b.path=UpgradePath::PathA;
      b.prerequisites={BID::HO_T6_BASE}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::HO_T6_B; b.name="Hussar Sanctum - Both Meters";
      b.tier=6; b.weeklyGrowth=1; b.category=BuildingCategory::UnitDwelling;
      b.faction=FactionId::HolyOrder; b.cost=goldAndRes(8000,ResourceType::FaithStones,8);
      b.path=UpgradePath::PathB;
      b.prerequisites={BID::HO_T6_BASE}; m_buildings.push_back(b); }

    // Support buildings
    { BuildingDef b; b.id=BID::HO_LIGHT_SHRINE; b.name="Light Shrine";
      b.description="+2 Light Power for all heroes garrisoned here";
      b.category=BuildingCategory::Support; b.faction=FactionId::HolyOrder;
      b.cost=goldAndRes(1500,ResourceType::FaithStones,3);
      b.prerequisites={BID::HO_HALL}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::HO_RELIQUARY; b.name="Reliquary";
      b.description="Holy units start battle with +20 Desperation pre-charged";
      b.category=BuildingCategory::Support; b.faction=FactionId::HolyOrder;
      b.cost=goldAndRes(2000,ResourceType::FaithStones,4);
      b.prerequisites={BID::HO_LIGHT_SHRINE}; m_buildings.push_back(b); }

    // ── HOLY ORDER UNITS ──────────────────────────────────────────────────────
    auto addUnit = [&](int id, const char* name, FactionId f, int tier,
                       UpgradePath path, int hp, int atk, int def,
                       int dmin, int dmax, int spd, Resources cost,
                       UnitTag tags, bool flying = false) {
        UnitDef u;
        u.id=id; u.name=name; u.faction=f; u.tier=tier; u.path=path;
        u.hp=hp; u.attack=atk; u.defense=def;
        u.damage_min=dmin; u.damage_max=dmax; u.speed=spd;
        u.cost=cost; u.tags=tags; u.flying=flying;
        m_units.push_back(u);
    };

    using F = FactionId;
    using P = UpgradePath;
    using T = UnitTag;

    // T1 Penitent
    addUnit(1001,"Squire",       F::HolyOrder,1,P::None, 16,3,3,1,3,5, gold(65),  T::Humanoid|T::Holy);
    addUnit(1002,"Squire (A)",    F::HolyOrder,1,P::PathA,13,3,3,1,4,7, gold(65),  T::Humanoid|T::Holy);
    addUnit(1003,"Squire (B)",    F::HolyOrder,1,P::PathB,18,3,4,1,2,4, gold(70),  T::Humanoid|T::Holy);
    // T2 Torch Bearer
    addUnit(1004,"Paladin",   F::HolyOrder,2,P::None, 20,4,5,2,5,5, goldAndRes(80,ResourceType::Iron,1), T::Humanoid|T::Holy);
    addUnit(1005,"Paladin (A)",F::HolyOrder,2,P::PathA,18,4,5,3,6,5, goldAndRes(90,ResourceType::Iron,1), T::Humanoid|T::Holy);
    addUnit(1006,"Paladin (B)",F::HolyOrder,2,P::PathB,21,5,5,2,4,5, goldAndRes(90,ResourceType::FaithStones,1), T::Humanoid|T::Holy);
    // T3 Plague Doctor
    addUnit(1007,"Crusader",  F::HolyOrder,3,P::None, 28,5,5,4,7,6, goldAndRes(150,ResourceType::FaithStones,1), T::Humanoid|T::Holy);
    addUnit(1008,"Crusader (A)",F::HolyOrder,3,P::PathA,26,4,4,3,6,6, goldAndRes(170,ResourceType::FaithStones,1), T::Humanoid|T::Holy);
    addUnit(1009,"Crusader (B)",F::HolyOrder,3,P::PathB,30,5,5,5,8,6, goldAndRes(160,ResourceType::Iron,1), T::Humanoid|T::Holy);
    // T4 Penitent Knight (flying)
    addUnit(1010,"Battle Cleric",   F::HolyOrder,4,P::None, 58,9,9,7,13,7, goldAndRes(300,ResourceType::Iron,2), T::Humanoid|T::Holy|T::Flying, true);
    addUnit(1011,"Battle Cleric (A)",F::HolyOrder,4,P::PathA,63,9,10,7,13,7, goldAndRes(350,ResourceType::Iron,2), T::Humanoid|T::Holy|T::Flying, true);
    addUnit(1012,"Battle Cleric (B)",F::HolyOrder,4,P::PathB,55,10,7,8,14,7, goldAndRes(330,ResourceType::BloodEssence,1), T::Humanoid|T::BloodBound|T::Flying, true);
    // T5 Seraph
    addUnit(1013,"Holy Champion",         F::HolyOrder,5,P::None, 95,13,12,14,26,9, goldAndRes(700,ResourceType::FaithStones,3), T::Humanoid|T::Holy|T::Flying, true);
    addUnit(1014,"Holy Champion (A)",      F::HolyOrder,5,P::PathA,95,13,12,14,26,9, goldAndRes(800,ResourceType::FaithStones,3), T::Humanoid|T::Holy|T::Flying, true);
    addUnit(1015,"Holy Champion (B)",      F::HolyOrder,5,P::PathB,90,15,11,16,28,11,goldAndRes(750,ResourceType::FaithStones,4), T::Humanoid|T::Holy|T::Flying, true);
    // T6 Winged Hussar
    addUnit(1016,"Archangel",   F::HolyOrder,6,P::None,165,17,15,23,38,12,goldAndRes(1500,ResourceType::FaithStones,5), T::Humanoid|T::Holy|T::Flying, true);
    addUnit(1017,"Archangel (A)",F::HolyOrder,6,P::PathA,165,17,15,23,38,12,goldAndRes(1800,ResourceType::FaithStones,6), T::Humanoid|T::Holy|T::Flying, true);
    addUnit(1018,"Archangel (B)",F::HolyOrder,6,P::PathB,165,17,15,23,38,12,goldAndRes(1800,ResourceType::FaithStones,8), T::Humanoid|T::Holy|T::Flying, true);

    // ── CRIMSON WARDENS ───────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::CW_HALL; b.name="Warden's Hold";
      b.description="Town Hall - +1000 Gold weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::CrimsonWardens; b.cost=goldAndRes(500,ResourceType::FaithStones,2); b.weeklyIncome=gold(1000); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T1; b.name="Scout Camp"; b.tier=1; b.weeklyGrowth=15;
      b.description="Produces Scouts"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::CrimsonWardens; b.cost=gold(300);
      b.upgradeA=BID::CW_T1_A; b.upgradeB=BID::CW_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T2; b.name="Ranger Lodge"; b.tier=2; b.weeklyGrowth=11;
      b.description="Produces Rangers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::CrimsonWardens; b.cost=goldAndRes(600,ResourceType::FaithStones,1);
      b.prerequisites={BID::CW_T1};
      b.upgradeA=BID::CW_T2_A; b.upgradeB=BID::CW_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T3; b.name="Hunter's Lodge"; b.tier=3; b.weeklyGrowth=7;
      b.description="Produces Hunters"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::CrimsonWardens; b.cost=goldAndRes(1000,ResourceType::FaithStones,2);
      b.prerequisites={BID::CW_T2};
      b.upgradeA=BID::CW_T3_A; b.upgradeB=BID::CW_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T4; b.name="Berserker Hall"; b.tier=4; b.weeklyGrowth=5;
      b.description="Produces Berserkers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::CrimsonWardens; b.cost=goldAndRes(1800,ResourceType::FaithStones,3);
      b.prerequisites={BID::CW_T3};
      b.upgradeA=BID::CW_T4_A; b.upgradeB=BID::CW_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T5; b.name="Warden's Tower"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Warden Commanders"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::CrimsonWardens; b.cost=goldAndRes(3000,ResourceType::FaithStones,4);
      b.prerequisites={BID::CW_T4};
      b.upgradeA=BID::CW_T5_A; b.upgradeB=BID::CW_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T6; b.name="Warlord's Bastion"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces Warlords"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::CrimsonWardens; b.cost=goldAndRes(5500,ResourceType::FaithStones,6);
      b.prerequisites={BID::CW_T5};
      b.upgradeA=BID::CW_T6_A; b.upgradeB=BID::CW_T6_B; m_buildings.push_back(b); }
    // CrimsonWardens PathA upgrades — Crusader line (Holy, FaithStones)
    { BuildingDef b; b.id=BID::CW_T1_A; b.name="Scout Camp (A)";
      b.description="Upgrades Scouts to Scouts (A)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=14;
      b.cost=goldAndRes(500,ResourceType::FaithStones,2);
      b.prerequisites={BID::CW_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T2_A; b.name="Ranger Lodge (A)";
      b.description="Upgrades Rangers to Rangers (A)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=10;
      b.cost=goldAndRes(700,ResourceType::FaithStones,2);
      b.prerequisites={BID::CW_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T3_A; b.name="Hunter's Lodge (A)";
      b.description="Upgrades Hunters to Hunters (A)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=6;
      b.cost=goldAndRes(1100,ResourceType::FaithStones,2);
      b.prerequisites={BID::CW_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T4_A; b.name="Berserker Hall (A)";
      b.description="Upgrades Berserkers to Berserkers (A)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=4;
      b.cost=goldAndRes(2000,ResourceType::FaithStones,3);
      b.prerequisites={BID::CW_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T5_A; b.name="Warden's Tower (A)";
      b.description="Upgrades Warden Commanders to Warden Commanders (A)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=goldAndRes(3500,ResourceType::FaithStones,4);
      b.prerequisites={BID::CW_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T6_A; b.name="Warlord's Bastion (A)";
      b.description="Upgrades Warlords to Warlords (A)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=goldAndRes(6500,ResourceType::FaithStones,6);
      b.prerequisites={BID::CW_T6}; m_buildings.push_back(b); }
    // CrimsonWardens PathB upgrades — Warden line (HP/DEF, BloodEssence)
    { BuildingDef b; b.id=BID::CW_T1_B; b.name="Scout Camp (B)";
      b.description="Upgrades Scouts to Scouts (B)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=15;
      b.cost=goldAndRes(500,ResourceType::BloodEssence,2);
      b.prerequisites={BID::CW_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T2_B; b.name="Ranger Lodge (B)";
      b.description="Upgrades Rangers to Rangers (B)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=11;
      b.cost=goldAndRes(700,ResourceType::BloodEssence,2);
      b.prerequisites={BID::CW_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T3_B; b.name="Hunter's Lodge (B)";
      b.description="Upgrades Hunters to Hunters (B)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=7;
      b.cost=goldAndRes(1100,ResourceType::BloodEssence,2);
      b.prerequisites={BID::CW_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T4_B; b.name="Berserker Hall (B)";
      b.description="Upgrades Berserkers to Berserkers (B)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=5;
      b.cost=goldAndRes(2000,ResourceType::BloodEssence,3);
      b.prerequisites={BID::CW_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T5_B; b.name="Warden's Tower (B)";
      b.description="Upgrades Warden Commanders to Warden Commanders (B)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=goldAndRes(3500,ResourceType::BloodEssence,4);
      b.prerequisites={BID::CW_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_T6_B; b.name="Warlord's Bastion (B)";
      b.description="Upgrades Warlords to Warlords (B)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::CrimsonWardens;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=goldAndRes(6500,ResourceType::BloodEssence,6);
      b.prerequisites={BID::CW_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_DEATH_ALTAR; b.name="Death Altar";
      b.description="+3 Death Power for all heroes garrisoned here";
      b.category=BuildingCategory::Support; b.faction=F::CrimsonWardens;
      b.cost=goldAndRes(1500,ResourceType::FaithStones,3);
      b.prerequisites={BID::CW_HALL}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CW_WARDEN_BRAND; b.name="Warden's Brand Chamber";
      b.description="Warden's Mark affects one additional target";
      b.category=BuildingCategory::Support; b.faction=F::CrimsonWardens;
      b.cost=goldAndRes(2500,ResourceType::FaithStones,4);
      b.prerequisites={BID::CW_DEATH_ALTAR}; m_buildings.push_back(b); }

    addUnit(2001,"Scout",    F::CrimsonWardens,1,P::None, 15,3,3,1, 3,5, gold(60),        T::Humanoid);
    addUnit(2002,"Ranger", F::CrimsonWardens,2,P::None, 22,4,4,3, 5,5, gold(115),       T::Humanoid);
    addUnit(2003,"Hunter",       F::CrimsonWardens,3,P::None, 45,6,6,6,10,6, goldAndRes(180,ResourceType::FaithStones,1), T::Humanoid);
    m_units.back().regenerates = true; m_units.back().range = 5; m_units.back().shots = 3;
    addUnit(2004,"Berserker",     F::CrimsonWardens,4,P::None, 55,9,6,8,14,10,goldAndRes(360,ResourceType::FaithStones,2), T::Humanoid|T::Flying, true);
    m_units.back().vampiric = true;
    addUnit(2005,"Warden Commander",        F::CrimsonWardens,5,P::None, 75,12,9,13,21,8,goldAndRes(610,ResourceType::FaithStones,3), T::Humanoid);
    addUnit(2006,"Warlord", F::CrimsonWardens,6,P::None,155,17,14,22,38,11,goldAndRes(1250,ResourceType::FaithStones,5), T::Humanoid|T::Flying, true);
    // Crimson Wardens PathA — Crusader line (Holy tag, FaithStones)
    addUnit(2011,"Scout (A)",    F::CrimsonWardens,1,P::PathA, 14,4,3,1, 4,5, goldAndRes(70,ResourceType::FaithStones,1), T::Humanoid|T::Holy);
    addUnit(2012,"Ranger (A)",      F::CrimsonWardens,2,P::PathA, 22,5,4,3, 5,5, goldAndRes(130,ResourceType::FaithStones,1), T::Humanoid|T::Flying, true);
    addUnit(2013,"Hunter (A)",    F::CrimsonWardens,3,P::PathA, 48,8,6,6,10,6, goldAndRes(210,ResourceType::FaithStones,1), T::Humanoid|T::Holy);
    m_units.back().range=5; m_units.back().shots=4;
    addUnit(2014,"Berserker (A)", F::CrimsonWardens,4,P::PathA, 60,11,7,8,14,10,goldAndRes(380,ResourceType::FaithStones,2), T::Humanoid|T::Holy|T::Flying, true);
    addUnit(2015,"Warden Commander (A)",        F::CrimsonWardens,5,P::PathA, 80,14,10,13,21,8,goldAndRes(650,ResourceType::FaithStones,3), T::Humanoid);
    addUnit(2016,"Warlord (A)", F::CrimsonWardens,6,P::PathA,160,19,14,22,38,11,goldAndRes(1350,ResourceType::FaithStones,5), T::Humanoid|T::Holy|T::Flying, true);
    // Crimson Wardens PathB — Warden line (HP/DEF, BloodEssence)
    addUnit(2021,"Scout (B)",     F::CrimsonWardens,1,P::PathB, 16,2,5,1, 3,5, goldAndRes(70,ResourceType::BloodEssence,1), T::Humanoid);
    addUnit(2022,"Ranger (B)",    F::CrimsonWardens,2,P::PathB, 26,4,5,3, 5,5, goldAndRes(130,ResourceType::BloodEssence,1), T::Humanoid);
    addUnit(2023,"Hunter (B)",    F::CrimsonWardens,3,P::PathB, 55,6,8,6,10,6, goldAndRes(210,ResourceType::BloodEssence,1), T::Humanoid);
    m_units.back().regenerates=true; m_units.back().range=5; m_units.back().shots=3;
    addUnit(2024,"Berserker (B)",    F::CrimsonWardens,4,P::PathB, 55,10,6,8,14,10,goldAndRes(380,ResourceType::BloodEssence,2), T::Humanoid|T::Flying, true);
    m_units.back().vampiric=true;
    addUnit(2025,"Warden Commander (B)",     F::CrimsonWardens,5,P::PathB, 90,11,12,13,21,8,goldAndRes(650,ResourceType::BloodEssence,3), T::Humanoid);
    m_units.back().regenerates=true;
    addUnit(2026,"Warlord (B)",   F::CrimsonWardens,6,P::PathB,175,17,16,22,38,11,goldAndRes(1350,ResourceType::BloodEssence,5), T::Humanoid|T::Flying, true);
    m_units.back().vampiric=true;

    // ── THORNKIN ─────────────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::TK_GROVE_HEART; b.name="Grove Heart";
      b.description="Town Hall - +1000 Gold weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::Thornkin; b.cost=goldAndRes(500,ResourceType::VerdantSap,2); b.weeklyIncome=gold(1000); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T1; b.name="Sprout Hollow"; b.tier=1; b.weeklyGrowth=14;
      b.description="Produces Sproutlings"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Thornkin; b.cost=gold(300);
      b.upgradeA=BID::TK_T1_A; b.upgradeB=BID::TK_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T2; b.name="Briar Thicket"; b.tier=2; b.weeklyGrowth=10;
      b.description="Produces Briars"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Thornkin; b.cost=goldAndRes(600,ResourceType::VerdantSap,1);
      b.prerequisites={BID::TK_T1};
      b.upgradeA=BID::TK_T2_A; b.upgradeB=BID::TK_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T3; b.name="Vine Den"; b.tier=3; b.weeklyGrowth=7;
      b.description="Produces Vine Crawlers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Thornkin; b.cost=goldAndRes(1000,ResourceType::VerdantSap,2);
      b.prerequisites={BID::TK_T2};
      b.upgradeA=BID::TK_T3_A; b.upgradeB=BID::TK_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T4; b.name="Guardian Grove"; b.tier=4; b.weeklyGrowth=5;
      b.description="Produces Grove Guardians"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Thornkin; b.cost=goldAndRes(1800,ResourceType::VerdantSap,3);
      b.prerequisites={BID::TK_T3};
      b.upgradeA=BID::TK_T4_A; b.upgradeB=BID::TK_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T5; b.name="Elder Circle"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Ancient Oaks"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Thornkin; b.cost=goldAndRes(3000,ResourceType::VerdantSap,4);
      b.prerequisites={BID::TK_T4};
      b.upgradeA=BID::TK_T5_A; b.upgradeB=BID::TK_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T6; b.name="World Tree Root"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces World Thorns"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Thornkin; b.cost=goldAndRes(5500,ResourceType::VerdantSap,6);
      b.prerequisites={BID::TK_T5};
      b.upgradeA=BID::TK_T6_A; b.upgradeB=BID::TK_T6_B; m_buildings.push_back(b); }
    // Thornkin PathA upgrades — Bonded line (symbiosis flavor, VerdantSap)
    { BuildingDef b; b.id=BID::TK_T1_A; b.name="Seedling Twin Hollow";
      b.description="Upgrades Sproutlings to Seedling Twins";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=13;
      b.cost=goldAndRes(500,ResourceType::VerdantSap,2);
      b.prerequisites={BID::TK_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T2_A; b.name="Briar Pair Thicket";
      b.description="Upgrades Briars to Briar Pairs";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=9;
      b.cost=goldAndRes(700,ResourceType::VerdantSap,2);
      b.prerequisites={BID::TK_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T3_A; b.name="Vine Duo Den";
      b.description="Upgrades Vine Crawlers to Vine Duos";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=6;
      b.cost=goldAndRes(1100,ResourceType::VerdantSap,2);
      b.prerequisites={BID::TK_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T4_A; b.name="Grove Bonded Sanctuary";
      b.description="Upgrades Grove Guardians to Grove Bonded";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=4;
      b.cost=goldAndRes(2000,ResourceType::VerdantSap,3);
      b.prerequisites={BID::TK_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T5_A; b.name="Ancient Pair Circle";
      b.description="Upgrades Ancient Oaks to Ancient Pairs";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=goldAndRes(3500,ResourceType::VerdantSap,4);
      b.prerequisites={BID::TK_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T6_A; b.name="Twin Thorn Canopy";
      b.description="Upgrades World Thorns to Twin Thorns (Flying)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=goldAndRes(6500,ResourceType::VerdantSap,6);
      b.prerequisites={BID::TK_T6}; m_buildings.push_back(b); }
    // Thornkin PathB upgrades — Ancient line (solo giants, VerdantSap)
    { BuildingDef b; b.id=BID::TK_T1_B; b.name="Ironroot Den";
      b.description="Upgrades Sproutlings to Ironroots";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=14;
      b.cost=goldAndRes(500,ResourceType::VerdantSap,2);
      b.prerequisites={BID::TK_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T2_B; b.name="Thornwall Thicket";
      b.description="Upgrades Briars to Thornwalls";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=10;
      b.cost=goldAndRes(700,ResourceType::VerdantSap,2);
      b.prerequisites={BID::TK_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T3_B; b.name="Elder Vine Den";
      b.description="Upgrades Vine Crawlers to Elder Vines";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=7;
      b.cost=goldAndRes(1100,ResourceType::VerdantSap,3);
      b.prerequisites={BID::TK_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T4_B; b.name="Ironwood Golem Grove";
      b.description="Upgrades Grove Guardians to Ironwood Golems";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=5;
      b.cost=goldAndRes(2000,ResourceType::VerdantSap,4);
      b.prerequisites={BID::TK_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T5_B; b.name="World Root Circle";
      b.description="Upgrades Ancient Oaks to World Roots";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=goldAndRes(3500,ResourceType::VerdantSap,5);
      b.prerequisites={BID::TK_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_T6_B; b.name="Elder Thorn Root";
      b.description="Upgrades World Thorns to Elder Thorns";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Thornkin;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=goldAndRes(6500,ResourceType::VerdantSap,7);
      b.prerequisites={BID::TK_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_ANCIENT_CIRCLE; b.name="Ancient Circle";
      b.description="+3 Nature Power for all heroes garrisoned here";
      b.category=BuildingCategory::Support; b.faction=F::Thornkin;
      b.cost=goldAndRes(1500,ResourceType::VerdantSap,3);
      b.prerequisites={BID::TK_GROVE_HEART}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::TK_SYMBIOSIS_WEB; b.name="Symbiosis Web";
      b.description="Bond pairs share stat bonuses from Symbiosis skill";
      b.category=BuildingCategory::Support; b.faction=F::Thornkin;
      b.cost=goldAndRes(2500,ResourceType::VerdantSap,4);
      b.prerequisites={BID::TK_ANCIENT_CIRCLE}; m_buildings.push_back(b); }

    addUnit(3001,"Vine Sprite",    F::Thornkin,1,P::None, 15,3,3,1, 3,5, gold(55),        T::Beast);
    addUnit(3002,"Thornkin Warrior",         F::Thornkin,2,P::None, 20,4,4,3, 6,5, goldAndRes(110,ResourceType::VerdantSap,1), T::Beast);
    addUnit(3003,"Forest Guardian",  F::Thornkin,3,P::None, 25,5,5,5, 9,5, goldAndRes(170,ResourceType::VerdantSap,1), T::Beast);
    m_units.back().regenerates = true;
    addUnit(3004,"Treant",F::Thornkin,4,P::None, 58,9,8,9,15,5, goldAndRes(370,ResourceType::VerdantSap,2), T::Beast);
    addUnit(3005,"Elder Thornkin",   F::Thornkin,5,P::None, 92,12,11,13,22,6,goldAndRes(630,ResourceType::VerdantSap,3), T::Beast);
    addUnit(3006,"Ancient Colossus",   F::Thornkin,6,P::None,158,16,14,24,38,7,goldAndRes(1300,ResourceType::VerdantSap,5), T::Beast);
    // Thornkin PathA — Bonded line (symbiosis flavor, slightly lower HP)
    addUnit(3011,"Vine Sprite (A)",  F::Thornkin,1,P::PathA, 10,3,3,1, 3,5, goldAndRes(60,ResourceType::VerdantSap,1), T::Beast);
    addUnit(3012,"Thornkin Warrior (A)",     F::Thornkin,2,P::PathA, 18,5,4,3, 6,5, goldAndRes(120,ResourceType::VerdantSap,1), T::Beast);
    addUnit(3013,"Forest Guardian (A)",       F::Thornkin,3,P::PathA, 22,6,5,5, 9,5, goldAndRes(195,ResourceType::VerdantSap,1), T::Beast);
    addUnit(3014,"Treant (A)",   F::Thornkin,4,P::PathA, 55,10,8,9,15,5, goldAndRes(380,ResourceType::VerdantSap,2), T::Beast);
    addUnit(3015,"Elder Thornkin (A)",   F::Thornkin,5,P::PathA, 85,13,11,13,22,6,goldAndRes(650,ResourceType::VerdantSap,3), T::Beast);
    addUnit(3016,"Ancient Colossus (A)",     F::Thornkin,6,P::PathA,150,17,14,24,38,7,goldAndRes(1350,ResourceType::VerdantSap,5), T::Beast|T::Flying, true);
    // Thornkin PathB — Ancient line (solo giants, pure bulk)
    addUnit(3021,"Vine Sprite (B)",       F::Thornkin,1,P::PathB, 16,2,4,1, 3,5, goldAndRes(65,ResourceType::VerdantSap,1), T::Beast);
    addUnit(3022,"Thornkin Warrior (B)",      F::Thornkin,2,P::PathB, 24,4,6,3, 6,5, goldAndRes(120,ResourceType::VerdantSap,1), T::Beast);
    addUnit(3023,"Forest Guardian (B)",     F::Thornkin,3,P::PathB, 32,5,7,5, 9,5, goldAndRes(200,ResourceType::VerdantSap,2), T::Beast);
    m_units.back().regenerates=true;
    addUnit(3024,"Treant (B)", F::Thornkin,4,P::PathB, 68,8,11,9,15,5, goldAndRes(400,ResourceType::VerdantSap,3), T::Beast);
    addUnit(3025,"Elder Thornkin (B)",     F::Thornkin,5,P::PathB,105,11,14,13,22,6,goldAndRes(680,ResourceType::VerdantSap,4), T::Beast);
    addUnit(3026,"Ancient Colossus (B)",    F::Thornkin,6,P::PathB,175,15,17,24,38,7,goldAndRes(1400,ResourceType::VerdantSap,6), T::Beast);

    // ── ETERNAL EMPIRE ────────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::EE_THRONE; b.name="Imperial Throne";
      b.description="Town Hall - +1000 Gold weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::EternalEmpire; b.cost=goldAndRes(500,ResourceType::Mercury,2); b.weeklyIncome=gold(1000); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T1; b.name="Conscript Pen"; b.tier=1; b.weeklyGrowth=13;
      b.description="Produces Conscripts"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::EternalEmpire; b.cost=gold(300);
      b.upgradeA=BID::EE_T1_A; b.upgradeB=BID::EE_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T2; b.name="Revenant Barracks"; b.tier=2; b.weeklyGrowth=10;
      b.description="Produces Revenants"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::EternalEmpire; b.cost=goldAndRes(600,ResourceType::BloodEssence,1);
      b.prerequisites={BID::EE_T1};
      b.upgradeA=BID::EE_T2_A; b.upgradeB=BID::EE_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T3; b.name="Shade Gallery"; b.tier=3; b.weeklyGrowth=7;
      b.description="Produces Shade Archers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::EternalEmpire; b.cost=goldAndRes(1000,ResourceType::Mercury,1);
      b.prerequisites={BID::EE_T2};
      b.upgradeA=BID::EE_T3_A; b.upgradeB=BID::EE_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T4; b.name="Steel Foundry"; b.tier=4; b.weeklyGrowth=5;
      b.description="Produces Steel Guardians"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::EternalEmpire; b.cost=goldAndRes(1800,ResourceType::Mercury,2);
      b.prerequisites={BID::EE_T3};
      b.upgradeA=BID::EE_T4_A; b.upgradeB=BID::EE_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T5; b.name="Phantom Keep"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Phantom Knights"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::EternalEmpire; b.cost=goldAndRes(3000,ResourceType::Mercury,3);
      b.prerequisites={BID::EE_T4};
      b.upgradeA=BID::EE_T5_A; b.upgradeB=BID::EE_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T6; b.name="Immortal Vault"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces Immortals (Second Life)"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::EternalEmpire; b.cost=goldAndRes(5500,ResourceType::Mercury,5);
      b.prerequisites={BID::EE_T5};
      b.upgradeA=BID::EE_T6_A; b.upgradeB=BID::EE_T6_B; m_buildings.push_back(b); }
    // EternalEmpire PathA upgrades — Eternal Command (Mercury, hasSecondLife)
    { BuildingDef b; b.id=BID::EE_T1_A; b.name="Eternal Conscript Barracks";
      b.description="Upgrades Conscripts to Eternal Conscripts";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=12;
      b.cost=goldAndRes(500,ResourceType::Mercury,2);
      b.prerequisites={BID::EE_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T2_A; b.name="Eternal Revenant Hall";
      b.description="Upgrades Revenants to Eternal Revenants";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=9;
      b.cost=goldAndRes(700,ResourceType::Mercury,2);
      b.prerequisites={BID::EE_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T3_A; b.name="Eternal Archer Gallery";
      b.description="Upgrades Shade Archers to Eternal Archers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=6;
      b.cost=goldAndRes(1100,ResourceType::Mercury,2);
      b.prerequisites={BID::EE_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T4_A; b.name="Eternal Guardian Foundry";
      b.description="Upgrades Steel Guardians to Eternal Guardians";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=4;
      b.cost=goldAndRes(2000,ResourceType::Mercury,3);
      b.prerequisites={BID::EE_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T5_A; b.name="Eternal Knight Keep";
      b.description="Upgrades Phantom Knights to Eternal Knights";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=goldAndRes(3500,ResourceType::Mercury,4);
      b.prerequisites={BID::EE_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T6_A; b.name="True Immortal Vault";
      b.description="Upgrades Immortals to True Immortals";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=goldAndRes(6500,ResourceType::Mercury,6);
      b.prerequisites={BID::EE_T6}; m_buildings.push_back(b); }
    // EternalEmpire PathB upgrades — Necromantic line (BloodEssence)
    { BuildingDef b; b.id=BID::EE_T1_B; b.name="Shade Barracks";
      b.description="Upgrades Conscripts to Shades";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=13;
      b.cost=goldAndRes(500,ResourceType::BloodEssence,2);
      b.prerequisites={BID::EE_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T2_B; b.name="Wraith Hall";
      b.description="Upgrades Revenants to Wraiths";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=10;
      b.cost=goldAndRes(700,ResourceType::BloodEssence,2);
      b.prerequisites={BID::EE_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T3_B; b.name="Soul Archer Gallery";
      b.description="Upgrades Shade Archers to Soul Archers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=7;
      b.cost=goldAndRes(1100,ResourceType::BloodEssence,2);
      b.prerequisites={BID::EE_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T4_B; b.name="Bone Knight Foundry";
      b.description="Upgrades Steel Guardians to Bone Knights";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=5;
      b.cost=goldAndRes(2000,ResourceType::BloodEssence,3);
      b.prerequisites={BID::EE_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T5_B; b.name="Void Knight Keep";
      b.description="Upgrades Phantom Knights to Void Knights";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=goldAndRes(3500,ResourceType::BloodEssence,4);
      b.prerequisites={BID::EE_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_T6_B; b.name="Undying Avatar Vault";
      b.description="Upgrades Immortals to Undying Avatars";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::EternalEmpire;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=goldAndRes(6500,ResourceType::BloodEssence,6);
      b.prerequisites={BID::EE_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_NECROPOLIS; b.name="Necropolis Gate";
      b.description="+3 Death Power; fallen enemies have 15% chance to rise as Conscripts";
      b.category=BuildingCategory::Support; b.faction=F::EternalEmpire;
      b.cost=goldAndRes(1500,ResourceType::BloodEssence,2);
      b.prerequisites={BID::EE_THRONE}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::EE_MONUMENT; b.name="Monument of Eternity";
      b.description="Undead and Holy units gain Second Life - revive once at half HP on death";
      b.category=BuildingCategory::Support; b.faction=F::EternalEmpire;
      b.cost=goldAndRes(2500,ResourceType::Mercury,4);
      b.prerequisites={BID::EE_NECROPOLIS}; m_buildings.push_back(b); }

    addUnit(4001,"Skeleton Soldier",      F::EternalEmpire,1,P::None, 12,2,3,1, 3,4, gold(65),        T::Humanoid|T::Undead);
    addUnit(4002,"Armoured Skeleton",       F::EternalEmpire,2,P::None, 20,4,4,3, 6,5, goldAndRes(120,ResourceType::BloodEssence,1), T::Undead);
    addUnit(4003,"Zombie Warrior",   F::EternalEmpire,3,P::None, 32,5,5,4, 8,6, goldAndRes(225,ResourceType::Mercury,1), T::Undead);
    addUnit(4004,"Death Knight", F::EternalEmpire,4,P::None, 55,9,10,8,14,7, goldAndRes(380,ResourceType::Mercury,2), T::Construct|T::Undead);
    addUnit(4005,"Lich", F::EternalEmpire,5,P::None, 83,12,11,11,20,8, goldAndRes(600,ResourceType::Mercury,3), T::Undead|T::Flying, true);
    addUnit(4006,"Eternal Emperor",       F::EternalEmpire,6,P::None,122,14,12,20,30,10,goldAndRes(1100,ResourceType::Mercury,5), T::Undead|T::Flying, true);
    // Eternal Empire PathA — Eternal Command (hasSecondLife, Mercury cost)
    addUnit(4011,"Skeleton Soldier (A)", F::EternalEmpire,1,P::PathA, 14,2,3,1, 3,4, goldAndRes(70,ResourceType::Mercury,1),  T::Humanoid|T::Undead);
    m_units.back().hasSecondLife=true;
    addUnit(4012,"Armoured Skeleton (A)",  F::EternalEmpire,2,P::PathA, 22,4,4,3, 6,5, goldAndRes(130,ResourceType::Mercury,1), T::Undead);
    m_units.back().hasSecondLife=true;
    addUnit(4013,"Zombie Warrior (A)",    F::EternalEmpire,3,P::PathA, 35,5,5,4, 8,6, goldAndRes(215,ResourceType::Mercury,1), T::Undead);
    m_units.back().hasSecondLife=true;
    addUnit(4014,"Death Knight (A)",  F::EternalEmpire,4,P::PathA, 58,9,10,8,14,7, goldAndRes(390,ResourceType::Mercury,2), T::Undead|T::Construct);
    m_units.back().hasSecondLife=true; m_units.back().secondLifeFullHeal=true;
    addUnit(4015,"Lich (A)",    F::EternalEmpire,5,P::PathA, 88,12,11,11,20,8, goldAndRes(620,ResourceType::Mercury,3), T::Undead|T::Flying, true);
    m_units.back().hasSecondLife=true;
    addUnit(4016,"Eternal Emperor (A)",     F::EternalEmpire,6,P::PathA,130,14,12,20,30,10,goldAndRes(1150,ResourceType::Mercury,5), T::Undead|T::Flying, true);
    m_units.back().hasSecondLife=true; m_units.back().secondLifeFullHeal=true;
    // Eternal Empire PathB — Necromantic line (raw stats, BloodEssence)
    addUnit(4021,"Skeleton Soldier (B)",            F::EternalEmpire,1,P::PathB, 12,3,2,1, 3,4, goldAndRes(70,ResourceType::BloodEssence,1),  T::Undead);
    addUnit(4022,"Armoured Skeleton (B)",           F::EternalEmpire,2,P::PathB, 23,5,4,3, 6,5, goldAndRes(130,ResourceType::BloodEssence,1), T::Undead);
    addUnit(4023,"Zombie Warrior (B)",      F::EternalEmpire,3,P::PathB, 34,6,5,4, 8,6, goldAndRes(220,ResourceType::BloodEssence,1), T::Undead);
    m_units.back().moraleImmune=true;
    addUnit(4024,"Death Knight (B)",      F::EternalEmpire,4,P::PathB, 60,10,9,8,14,7, goldAndRes(395,ResourceType::BloodEssence,2), T::Undead|T::Construct);
    addUnit(4025,"Lich (B)",      F::EternalEmpire,5,P::PathB, 86,13,10,11,20,8, goldAndRes(620,ResourceType::BloodEssence,3), T::Undead|T::Void|T::Flying, true);
    addUnit(4026,"Eternal Emperor (B)",   F::EternalEmpire,6,P::PathB,135,15,11,20,30,10,goldAndRes(1150,ResourceType::BloodEssence,5), T::Undead|T::Flying, true);

    // ── BLOODSWORN ────────────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::BS_WAR_HALL; b.name="War Hall";
      b.description="Town Hall - +1000 Gold weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::Bloodsworn; b.cost=goldAndRes(500,ResourceType::BloodEssence,2); b.weeklyIncome=gold(1000); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T1; b.name="Bloodling Pen"; b.tier=1; b.weeklyGrowth=14;
      b.description="Produces Bloodlings"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Bloodsworn; b.cost=gold(300);
      b.upgradeA=BID::BS_T1_A; b.upgradeB=BID::BS_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T2; b.name="Berserker Pits"; b.tier=2; b.weeklyGrowth=10;
      b.description="Produces Berserkers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Bloodsworn; b.cost=goldAndRes(600,ResourceType::BloodEssence,1);
      b.prerequisites={BID::BS_T1};
      b.upgradeA=BID::BS_T2_A; b.upgradeB=BID::BS_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T3; b.name="Shaman Hut"; b.tier=3; b.weeklyGrowth=7;
      b.description="Produces Blood Shamans"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Bloodsworn; b.cost=goldAndRes(1000,ResourceType::BloodEssence,2);
      b.prerequisites={BID::BS_T2};
      b.upgradeA=BID::BS_T3_A; b.upgradeB=BID::BS_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T4; b.name="Ravager Corral"; b.tier=4; b.weeklyGrowth=5;
      b.description="Produces Ravagers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Bloodsworn; b.cost=goldAndRes(1800,ResourceType::BloodEssence,3);
      b.prerequisites={BID::BS_T3};
      b.upgradeA=BID::BS_T4_A; b.upgradeB=BID::BS_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T5; b.name="Warlord Pavilion"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Bloodtide Warlords"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Bloodsworn; b.cost=goldAndRes(3000,ResourceType::BloodEssence,4);
      b.prerequisites={BID::BS_T4};
      b.upgradeA=BID::BS_T5_A; b.upgradeB=BID::BS_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T6; b.name="Avatar Shrine"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces Crimson Avatars"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Bloodsworn; b.cost=goldAndRes(5500,ResourceType::BloodEssence,6);
      b.prerequisites={BID::BS_T5};
      b.upgradeA=BID::BS_T6_A; b.upgradeB=BID::BS_T6_B; m_buildings.push_back(b); }
    // Bloodsworn PathA upgrades — Blood Rush (ATK heavy, BloodEssence)
    { BuildingDef b; b.id=BID::BS_T1_A; b.name="Blood Fanatic Den";
      b.description="Upgrades Bloodlings to Blood Fanatics";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=13;
      b.cost=goldAndRes(500,ResourceType::BloodEssence,2);
      b.prerequisites={BID::BS_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T2_A; b.name="Blood Berserker Pits";
      b.description="Upgrades Berserkers to Blood Berserkers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=9;
      b.cost=goldAndRes(700,ResourceType::BloodEssence,2);
      b.prerequisites={BID::BS_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T3_A; b.name="High Shaman Hut";
      b.description="Upgrades Blood Shamans to High Shamans";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=6;
      b.cost=goldAndRes(1100,ResourceType::BloodEssence,2);
      b.prerequisites={BID::BS_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T4_A; b.name="Blood Ravager Corral";
      b.description="Upgrades Ravagers to Blood Ravagers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=4;
      b.cost=goldAndRes(2000,ResourceType::BloodEssence,3);
      b.prerequisites={BID::BS_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T5_A; b.name="Blood Avatar Pavilion";
      b.description="Upgrades Bloodtide Warlords to Blood Avatars";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=goldAndRes(3500,ResourceType::BloodEssence,4);
      b.prerequisites={BID::BS_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T6_A; b.name="Blood God Shrine";
      b.description="Upgrades Crimson Avatars to Blood Gods";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=goldAndRes(6500,ResourceType::BloodEssence,7);
      b.prerequisites={BID::BS_T6}; m_buildings.push_back(b); }
    // Bloodsworn PathB upgrades — Ritual Pact (tankier, Iron cost)
    { BuildingDef b; b.id=BID::BS_T1_B; b.name="Pact Warrior Den";
      b.description="Upgrades Bloodlings to Pact Warriors";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=14;
      b.cost=goldAndRes(500,ResourceType::Iron,2);
      b.prerequisites={BID::BS_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T2_B; b.name="Ritual Guard Pits";
      b.description="Upgrades Berserkers to Ritual Guards";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=10;
      b.cost=goldAndRes(700,ResourceType::Iron,2);
      b.prerequisites={BID::BS_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T3_B; b.name="Pact Shaman Hut";
      b.description="Upgrades Blood Shamans to Pact Shamans";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=7;
      b.cost=goldAndRes(1100,ResourceType::Iron,2);
      b.prerequisites={BID::BS_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T4_B; b.name="Pact Ravager Corral";
      b.description="Upgrades Ravagers to Pact Ravagers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=5;
      b.cost=goldAndRes(2000,ResourceType::Iron,3);
      b.prerequisites={BID::BS_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T5_B; b.name="Ritual Champion Pavilion";
      b.description="Upgrades Bloodtide Warlords to Ritual Champions";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=goldAndRes(3500,ResourceType::Iron,4);
      b.prerequisites={BID::BS_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_T6_B; b.name="Pact Titan Shrine";
      b.description="Upgrades Crimson Avatars to Pact Titans";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Bloodsworn;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=goldAndRes(6500,ResourceType::Iron,7);
      b.prerequisites={BID::BS_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_BLOOD_ALTAR; b.name="Blood Altar";
      b.description="+3 Blood Power for all heroes garrisoned here";
      b.category=BuildingCategory::Support; b.faction=F::Bloodsworn;
      b.cost=goldAndRes(1500,ResourceType::BloodEssence,3);
      b.prerequisites={BID::BS_WAR_HALL}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::BS_WAR_SHRINE; b.name="War Shrine";
      b.description="BloodBound units gain +15 Morale at battle start";
      b.category=BuildingCategory::Support; b.faction=F::Bloodsworn;
      b.cost=goldAndRes(2500,ResourceType::BloodEssence,4);
      b.prerequisites={BID::BS_BLOOD_ALTAR}; m_buildings.push_back(b); }

    addUnit(5001,"Cultist",          F::Bloodsworn,1,P::None, 13,3,2,2, 4,5, gold(65),        T::Humanoid|T::BloodBound);
    addUnit(5002,"Blood Warrior",          F::Bloodsworn,2,P::None, 20,5,4,3, 6,6, goldAndRes(135,ResourceType::BloodEssence,1), T::Humanoid|T::BloodBound);
    addUnit(5003,"Berserker",       F::Bloodsworn,3,P::None, 27,5,5,3, 6,6, goldAndRes(185,ResourceType::BloodEssence,1), T::Humanoid|T::BloodBound);
    addUnit(5004,"Blood Champion",            F::Bloodsworn,4,P::None, 54,10,7,10,18,8, goldAndRes(340,ResourceType::BloodEssence,2), T::Humanoid|T::BloodBound);
    addUnit(5005,"Oracle",  F::Bloodsworn,5,P::None, 72,13,8,14,25,9,goldAndRes(590,ResourceType::BloodEssence,3), T::Humanoid|T::BloodBound);
    m_units.back().range=4; m_units.back().shots=4;
    addUnit(5006,"Bloodsworn Avatar",     F::Bloodsworn,6,P::None,140,18,9,25,42,11,goldAndRes(1150,ResourceType::BloodEssence,5), T::Humanoid|T::BloodBound);
    // Bloodsworn PathA — Blood Rush (ATK heavy, BloodEssence)
    addUnit(5011,"Cultist (A)",    F::Bloodsworn,1,P::PathA, 14,4,2,2, 4,5, goldAndRes(70,ResourceType::BloodEssence,1),  T::Humanoid|T::BloodBound);
    addUnit(5012,"Blood Warrior (A)",  F::Bloodsworn,2,P::PathA, 21,6,4,3, 7,6, goldAndRes(120,ResourceType::BloodEssence,1), T::Humanoid|T::BloodBound);
    addUnit(5013,"Berserker (A)",      F::Bloodsworn,3,P::PathA, 28,6,5,3, 6,6, goldAndRes(190,ResourceType::BloodEssence,1), T::Humanoid|T::BloodBound);
    addUnit(5014,"Blood Champion (A)",    F::Bloodsworn,4,P::PathA, 56,12,7,10,18,8, goldAndRes(360,ResourceType::BloodEssence,2), T::Humanoid|T::BloodBound);
    addUnit(5015,"Oracle (A)",     F::Bloodsworn,5,P::PathA, 75,15,8,14,25,9, goldAndRes(610,ResourceType::BloodEssence,3), T::Humanoid|T::BloodBound);
    m_units.back().range=4; m_units.back().shots=4;
    addUnit(5016,"Bloodsworn Avatar (A)",        F::Bloodsworn,6,P::PathA,148,20,9,25,42,11,goldAndRes(1200,ResourceType::BloodEssence,6), T::Humanoid|T::BloodBound);
    // Bloodsworn PathB — Ritual Pact (tankier, Iron cost)
    addUnit(5021,"Cultist (B)",     F::Bloodsworn,1,P::PathB, 15,3,3,2, 4,5, goldAndRes(70,ResourceType::Iron,1),  T::Humanoid);
    addUnit(5022,"Blood Warrior (B)",     F::Bloodsworn,2,P::PathB, 23,4,6,3, 7,6, goldAndRes(120,ResourceType::Iron,1), T::Humanoid);
    addUnit(5023,"Berserker (B)",      F::Bloodsworn,3,P::PathB, 30,5,7,3, 6,6, goldAndRes(195,ResourceType::Iron,1), T::Humanoid);
    addUnit(5024,"Blood Champion (B)",     F::Bloodsworn,4,P::PathB, 60,9,10,10,18,8, goldAndRes(360,ResourceType::Iron,2), T::Humanoid);
    addUnit(5025,"Oracle (B)",  F::Bloodsworn,5,P::PathB, 80,12,12,14,25,9, goldAndRes(610,ResourceType::Iron,3), T::Humanoid);
    m_units.back().range=4; m_units.back().shots=4;
    addUnit(5026,"Bloodsworn Avatar (B)",       F::Bloodsworn,6,P::PathB,155,17,13,25,42,11,goldAndRes(1200,ResourceType::Iron,6), T::Humanoid);

    // ── VOIDKIN ───────────────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::VK_NEXUS; b.name="Void Nexus";
      b.description="Town Hall - +1000 Gold, +2 VerdantSap weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::Voidkin; b.cost=gold(500); b.weeklyIncome=goldAndRes(1000,ResourceType::VerdantSap,2); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T1; b.name="Wisp Hollow"; b.tier=1; b.weeklyGrowth=13;
      b.description="Produces Void Wisps"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Voidkin; b.cost=gold(300);
      b.upgradeA=BID::VK_T1_A; b.upgradeB=BID::VK_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T2; b.name="Phase Den"; b.tier=2; b.weeklyGrowth=10;
      b.description="Produces Phase Walkers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Voidkin; b.cost=gold(600);
      b.prerequisites={BID::VK_T1};
      b.upgradeA=BID::VK_T2_A; b.upgradeB=BID::VK_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T3; b.name="Rift Arch"; b.tier=3; b.weeklyGrowth=7;
      b.description="Produces Rift Archers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Voidkin; b.cost=goldAndRes(1000,ResourceType::VerdantSap,2);
      b.prerequisites={BID::VK_T2};
      b.upgradeA=BID::VK_T3_A; b.upgradeB=BID::VK_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T4; b.name="Stalker Gate"; b.tier=4; b.weeklyGrowth=5;
      b.description="Produces Void Stalkers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Voidkin; b.cost=goldAndRes(1800,ResourceType::VerdantSap,3);
      b.prerequisites={BID::VK_T3};
      b.upgradeA=BID::VK_T4_A; b.upgradeB=BID::VK_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T5; b.name="Wraith Spire"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Entropy Wraiths"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Voidkin; b.cost=goldAndRes(3000,ResourceType::VerdantSap,4);
      b.prerequisites={BID::VK_T4};
      b.upgradeA=BID::VK_T5_A; b.upgradeB=BID::VK_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T6; b.name="Colossus Rift"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces Void Colossi"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Voidkin; b.cost=goldAndRes(5500,ResourceType::VerdantSap,6);
      b.prerequisites={BID::VK_T5};
      b.upgradeA=BID::VK_T6_A; b.upgradeB=BID::VK_T6_B; m_buildings.push_back(b); }
    // Voidkin PathA upgrades — Phase line (speed+1, VerdantSap)
    { BuildingDef b; b.id=BID::VK_T1_A; b.name="Phase Wisp Hollow";
      b.description="Upgrades Void Wisps to Phase Wisps";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=12;
      b.cost=goldAndRes(500,ResourceType::VerdantSap,2);
      b.prerequisites={BID::VK_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T2_A; b.name="Flicker Den";
      b.description="Upgrades Phase Walkers to Flickers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=9;
      b.cost=goldAndRes(700,ResourceType::VerdantSap,2);
      b.prerequisites={BID::VK_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T3_A; b.name="Void Sniper Arch";
      b.description="Upgrades Rift Archers to Void Snipers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=6;
      b.cost=goldAndRes(1100,ResourceType::VerdantSap,2);
      b.prerequisites={BID::VK_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T4_A; b.name="Phase Hunter Gate";
      b.description="Upgrades Void Stalkers to Phase Hunters";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=4;
      b.cost=goldAndRes(2000,ResourceType::VerdantSap,3);
      b.prerequisites={BID::VK_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T5_A; b.name="Chaos Wraith Spire";
      b.description="Upgrades Entropy Wraiths to Chaos Wraiths";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=goldAndRes(3500,ResourceType::VerdantSap,4);
      b.prerequisites={BID::VK_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T6_A; b.name="Void Specter Rift";
      b.description="Upgrades Void Colossi to Void Specters";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=goldAndRes(6500,ResourceType::VerdantSap,6);
      b.prerequisites={BID::VK_T6}; m_buildings.push_back(b); }
    // Voidkin PathB upgrades — Void Anchor (tankier, VerdantSap)
    { BuildingDef b; b.id=BID::VK_T1_B; b.name="Void Anchor Hollow";
      b.description="Upgrades Void Wisps to Void Anchors";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=13;
      b.cost=goldAndRes(500,ResourceType::VerdantSap,2);
      b.prerequisites={BID::VK_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T2_B; b.name="Void Bulwark Den";
      b.description="Upgrades Phase Walkers to Void Bulwarks";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=10;
      b.cost=goldAndRes(700,ResourceType::VerdantSap,2);
      b.prerequisites={BID::VK_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T3_B; b.name="Anchor Archer Arch";
      b.description="Upgrades Rift Archers to Anchor Archers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=7;
      b.cost=goldAndRes(1100,ResourceType::VerdantSap,3);
      b.prerequisites={BID::VK_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T4_B; b.name="Void Monolith Gate";
      b.description="Upgrades Void Stalkers to Void Monoliths";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=5;
      b.cost=goldAndRes(2000,ResourceType::VerdantSap,3);
      b.prerequisites={BID::VK_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T5_B; b.name="Entropy Anchor Spire";
      b.description="Upgrades Entropy Wraiths to Entropy Anchors";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=goldAndRes(3500,ResourceType::VerdantSap,4);
      b.prerequisites={BID::VK_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_T6_B; b.name="Void Titan Rift";
      b.description="Upgrades Void Colossi to Void Titans";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Voidkin;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=goldAndRes(6500,ResourceType::VerdantSap,7);
      b.prerequisites={BID::VK_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_RIFT_GATE; b.name="Rift Gate";
      b.description="+3 Nature Power for all heroes garrisoned here";
      b.category=BuildingCategory::Support; b.faction=F::Voidkin;
      b.cost=goldAndRes(1500,ResourceType::VerdantSap,3);
      b.prerequisites={BID::VK_NEXUS}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::VK_VOID_LENS; b.name="Void Lens";
      b.description="Void units gain +1 Attack at battle start";
      b.category=BuildingCategory::Support; b.faction=F::Voidkin;
      b.cost=goldAndRes(2500,ResourceType::VerdantSap,4);
      b.prerequisites={BID::VK_RIFT_GATE}; m_buildings.push_back(b); }

    addUnit(6001,"Void Sprite",      F::Voidkin,1,P::None, 20,4,3,1, 3,6, gold(70),        T::Void|T::Flying, true);
    addUnit(6002,"Void Scout",   F::Voidkin,2,P::None, 27,5,6,2, 6,6, goldAndRes(130,ResourceType::VerdantSap,1), T::Void|T::Flying, true);
    addUnit(6003,"Void Stalker",    F::Voidkin,3,P::None, 28,6,5,4, 9,7, goldAndRes(205,ResourceType::VerdantSap,1), T::Void|T::Flying, true);
    addUnit(6004,"Void Mage",   F::Voidkin,4,P::None, 48,9,9,9,15,10,goldAndRes(360,ResourceType::VerdantSap,2), T::Void|T::Flying, true);
    m_units.back().range = 5; m_units.back().shots = 3;
    addUnit(6005,"Void Wraith", F::Voidkin,5,P::None, 82,12,11,15,24,12,goldAndRes(640,ResourceType::VerdantSap,3), T::Void|T::Flying, true);
    addUnit(6006,"Void Herald",  F::Voidkin,6,P::None,135,16,14,22,34,13,goldAndRes(1180,ResourceType::VerdantSap,5), T::Void|T::Flying, true);
    // Voidkin PathA — Phase line (speed+1, all flying)
    addUnit(6011,"Void Sprite (A)",     F::Voidkin,1,P::PathA, 18,3,3,1, 3,7, goldAndRes(75,ResourceType::VerdantSap,1),  T::Void|T::Flying, true);
    addUnit(6012,"Void Scout (A)",        F::Voidkin,2,P::PathA, 24,5,6,2, 6,7, goldAndRes(140,ResourceType::VerdantSap,1), T::Void|T::Flying, true);
    addUnit(6013,"Void Stalker (A)",    F::Voidkin,3,P::PathA, 28,7,5,4, 9,8, goldAndRes(235,ResourceType::VerdantSap,1), T::Void|T::Flying, true);
    addUnit(6014,"Void Mage (A)",   F::Voidkin,4,P::PathA, 44,10,9,9,15,11,goldAndRes(380,ResourceType::VerdantSap,2), T::Void|T::Flying, true);
    m_units.back().range=6; m_units.back().shots=3;
    addUnit(6015,"Void Wraith (A)",   F::Voidkin,5,P::PathA, 72,13,11,13,22,13,goldAndRes(660,ResourceType::VerdantSap,3), T::Void|T::Flying, true);
    addUnit(6016,"Void Herald (A)",   F::Voidkin,6,P::PathA,135,17,14,22,34,14,goldAndRes(1280,ResourceType::VerdantSap,5), T::Void|T::Flying, true);
    // Voidkin PathB — Void Anchor (tankier, lower speed)
    addUnit(6021,"Void Sprite (B)",    F::Voidkin,1,P::PathB, 22,2,4,1, 3,5, goldAndRes(75,ResourceType::VerdantSap,1),  T::Void|T::Flying, true);
    addUnit(6022,"Void Scout (B)",   F::Voidkin,2,P::PathB, 30,3,8,2, 6,5, goldAndRes(140,ResourceType::VerdantSap,1), T::Void|T::Flying, true);
    addUnit(6023,"Void Stalker (B)",  F::Voidkin,3,P::PathB, 34,5,6,4, 9,7, goldAndRes(235,ResourceType::VerdantSap,2), T::Void|T::Flying, true);
    addUnit(6024,"Void Mage (B)",  F::Voidkin,4,P::PathB, 55,8,13,9,15,9, goldAndRes(380,ResourceType::VerdantSap,2), T::Void|T::Flying, true);
    m_units.back().range=5; m_units.back().shots=3;
    addUnit(6025,"Void Wraith (B)", F::Voidkin,5,P::PathB, 85,10,14,13,22,11,goldAndRes(660,ResourceType::VerdantSap,3), T::Void|T::Flying, true);
    addUnit(6026,"Void Herald (B)",     F::Voidkin,6,P::PathB,155,14,18,22,34,12,goldAndRes(1280,ResourceType::VerdantSap,6), T::Void|T::Flying, true);

    // ── IRON ASSEMBLY ─────────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::IA_FORGE_HALL; b.name="Forge Hall";
      b.description="Town Hall - +1000 Gold weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::IronAssembly; b.cost=goldAndRes(500,ResourceType::Iron,3); b.weeklyIncome=gold(1000); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T1; b.name="Automaton Works"; b.tier=1; b.weeklyGrowth=12;
      b.description="Produces Automatons"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::IronAssembly; b.cost=goldAndRes(350,ResourceType::Iron,2);
      b.upgradeA=BID::IA_T1_A; b.upgradeB=BID::IA_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T2; b.name="Gun Construct Bay"; b.tier=2; b.weeklyGrowth=9;
      b.description="Produces Gun Constructs"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::IronAssembly; b.cost=goldAndRes(700,ResourceType::Iron,3);
      b.prerequisites={BID::IA_T1};
      b.upgradeA=BID::IA_T2_A; b.upgradeB=BID::IA_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T3; b.name="Steam Walker Depot"; b.tier=3; b.weeklyGrowth=6;
      b.description="Produces Steam Walkers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::IronAssembly; b.cost=goldAndRes(1100,ResourceType::Iron,4);
      b.prerequisites={BID::IA_T2};
      b.upgradeA=BID::IA_T3_A; b.upgradeB=BID::IA_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T4; b.name="Siege Bot Foundry"; b.tier=4; b.weeklyGrowth=5;
      b.description="Produces Siege Bots (available week 3)"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::IronAssembly; b.cost=goldAndRes(2000,ResourceType::Iron,6);
      b.minWeek=3; b.prerequisites={BID::IA_T3};
      b.upgradeA=BID::IA_T4_A; b.upgradeB=BID::IA_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T5; b.name="Titan Assembly"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Titan Constructs (available week 5)"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::IronAssembly; b.cost=goldAndRes(3500,ResourceType::Iron,8);
      b.minWeek=5; b.prerequisites={BID::IA_T4};
      b.upgradeA=BID::IA_T5_A; b.upgradeB=BID::IA_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T6; b.name="Colossus Prime Dock"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces Colossus Primes (available week 7)"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::IronAssembly; b.cost=goldAndRes(6000,ResourceType::Iron,12);
      b.minWeek=7; b.prerequisites={BID::IA_T5};
      b.upgradeA=BID::IA_T6_A; b.upgradeB=BID::IA_T6_B; m_buildings.push_back(b); }
    // IronAssembly PathA upgrades — Runic line (Iron cost)
    { BuildingDef b; b.id=BID::IA_T1_A; b.name="Runic Automaton Works";
      b.description="Upgrades Automatons to Runic Automatons";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=11;
      b.cost=goldAndRes(500,ResourceType::Iron,3);
      b.prerequisites={BID::IA_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T2_A; b.name="Runic Gunner Bay";
      b.description="Upgrades Gun Constructs to Runic Gunners";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=8;
      b.cost=goldAndRes(700,ResourceType::Iron,4);
      b.prerequisites={BID::IA_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T3_A; b.name="Runic Walker Depot";
      b.description="Upgrades Steam Walkers to Runic Walkers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=5;
      b.cost=goldAndRes(1200,ResourceType::Iron,5);
      b.prerequisites={BID::IA_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T4_A; b.name="Runic Siege Bot Foundry";
      b.description="Upgrades Siege Bots to Runic Siege Bots";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=4;
      b.cost=goldAndRes(2200,ResourceType::Iron,7);
      b.minWeek=3; b.prerequisites={BID::IA_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T5_A; b.name="Runic Titan Assembly";
      b.description="Upgrades Titan Constructs to Runic Titans";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=goldAndRes(4000,ResourceType::Iron,9);
      b.minWeek=5; b.prerequisites={BID::IA_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T6_A; b.name="Runic Colossus Dock";
      b.description="Upgrades Colossus Primes to Runic Colossi";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=goldAndRes(7000,ResourceType::Iron,13);
      b.minWeek=7; b.prerequisites={BID::IA_T6}; m_buildings.push_back(b); }
    // IronAssembly PathB upgrades — Salvager line (cheaper, Iron cost)
    { BuildingDef b; b.id=BID::IA_T1_B; b.name="Salvage Bot Works";
      b.description="Upgrades Automatons to Salvage Bots";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=12;
      b.cost=goldAndRes(450,ResourceType::Iron,2);
      b.prerequisites={BID::IA_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T2_B; b.name="Scrap Gunner Bay";
      b.description="Upgrades Gun Constructs to Scrap Gunners";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=9;
      b.cost=goldAndRes(650,ResourceType::Iron,3);
      b.prerequisites={BID::IA_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T3_B; b.name="Salvage Walker Depot";
      b.description="Upgrades Steam Walkers to Salvage Walkers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=6;
      b.cost=goldAndRes(1050,ResourceType::Iron,3);
      b.prerequisites={BID::IA_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T4_B; b.name="Salvage Bot MkII Foundry";
      b.description="Upgrades Siege Bots to Salvage Bots MkII";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=5;
      b.cost=goldAndRes(1900,ResourceType::Iron,5);
      b.minWeek=3; b.prerequisites={BID::IA_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T5_B; b.name="Salvage Titan Assembly";
      b.description="Upgrades Titan Constructs to Salvage Titans";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=goldAndRes(3400,ResourceType::Iron,7);
      b.minWeek=5; b.prerequisites={BID::IA_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_T6_B; b.name="Salvage Prime Dock";
      b.description="Upgrades Colossus Primes to Salvage Primes";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::IronAssembly;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=goldAndRes(5800,ResourceType::Iron,10);
      b.minWeek=7; b.prerequisites={BID::IA_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_BLUEPRINT_VAULT; b.name="Blueprint Vault";
      b.description="+3 Forge Power for all heroes garrisoned here";
      b.category=BuildingCategory::Support; b.faction=F::IronAssembly;
      b.cost=goldAndRes(1500,ResourceType::Iron,4);
      b.prerequisites={BID::IA_FORGE_HALL}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::IA_OVERCLOCK; b.name="Overclock Chamber";
      b.description="All mechanical units gain +1 speed in combat";
      b.category=BuildingCategory::Support; b.faction=F::IronAssembly;
      b.cost=goldAndRes(2500,ResourceType::Iron,6);
      b.minWeek=3; b.prerequisites={BID::IA_BLUEPRINT_VAULT}; m_buildings.push_back(b); }

    addUnit(7001,"Automaton",       F::IronAssembly,1,P::None, 12,3,3,2, 4,5, goldAndRes(75,ResourceType::Iron,1),  T::Mechanical);
    addUnit(7002,"Infantry Unit",   F::IronAssembly,2,P::None, 20,4,5,3, 6,5, goldAndRes(140,ResourceType::Iron,2), T::Mechanical);
    addUnit(7003,"Clockwork Warrior",    F::IronAssembly,3,P::None, 30,6,6,5,10,5, goldAndRes(240,ResourceType::Iron,3), T::Mechanical);
    addUnit(7004,"Gunner",       F::IronAssembly,4,P::None, 58,8,9,7,13,4, goldAndRes(400,ResourceType::Iron,5), T::Mechanical);
    m_units.back().range = 5; m_units.back().shots = 2;
    addUnit(7005,"Steam Colossus", F::IronAssembly,5,P::None, 75,11,11,15,24,5,goldAndRes(660,ResourceType::Iron,7), T::Mechanical);
    m_units.back().range = 4; m_units.back().shots = 3;
    addUnit(7006,"Iron Titan",  F::IronAssembly,6,P::None,150,15,14,26,42,6,goldAndRes(1350,ResourceType::Iron,10), T::Mechanical);
    m_units.back().range = 4; m_units.back().shots = 4;
    // Iron Assembly PathA — Runic line (more Iron cost, better stats)
    addUnit(7011,"Automaton (A)",   F::IronAssembly,1,P::PathA, 14,4,5,2, 4,5, goldAndRes(85,ResourceType::Iron,2),   T::Mechanical);
    addUnit(7012,"Infantry Unit (A)",      F::IronAssembly,2,P::PathA, 22,5,6,3, 6,5, goldAndRes(155,ResourceType::Iron,3),  T::Mechanical);
    addUnit(7013,"Clockwork Warrior (A)",      F::IronAssembly,3,P::PathA, 33,7,7,5,10,5, goldAndRes(235,ResourceType::Iron,4),  T::Mechanical);
    addUnit(7014,"Gunner (A)",   F::IronAssembly,4,P::PathA, 63,9,10,7,13,4, goldAndRes(430,ResourceType::Iron,6), T::Mechanical);
    m_units.back().range=5; m_units.back().shots=3;
    addUnit(7015,"Steam Colossus (A)",       F::IronAssembly,5,P::PathA, 82,12,13,15,24,5,goldAndRes(700,ResourceType::Iron,8), T::Mechanical);
    m_units.back().range=4; m_units.back().shots=3;
    addUnit(7016,"Iron Titan (A)",    F::IronAssembly,6,P::PathA,160,16,16,26,42,6,goldAndRes(1450,ResourceType::Iron,11), T::Mechanical);
    m_units.back().range=4; m_units.back().shots=4;
    // Iron Assembly PathB — Salvager line (cheaper, same stats)
    addUnit(7021,"Automaton (B)",       F::IronAssembly,1,P::PathB, 12,3,4,2, 4,5, goldAndRes(75,ResourceType::Iron,1),   T::Mechanical);
    addUnit(7022,"Infantry Unit (B)",      F::IronAssembly,2,P::PathB, 20,4,5,3, 6,5, goldAndRes(135,ResourceType::Iron,2),  T::Mechanical);
    addUnit(7023,"Clockwork Warrior (B)",    F::IronAssembly,3,P::PathB, 30,6,6,5,10,5, goldAndRes(210,ResourceType::Iron,2),  T::Mechanical);
    addUnit(7024,"Gunner (B)",  F::IronAssembly,4,P::PathB, 58,8,9,7,13,4, goldAndRes(385,ResourceType::Iron,4),  T::Mechanical);
    m_units.back().range=5; m_units.back().shots=3;
    addUnit(7025,"Steam Colossus (B)",     F::IronAssembly,5,P::PathB, 75,11,11,15,24,5,goldAndRes(645,ResourceType::Iron,6), T::Mechanical);
    m_units.back().range=4; m_units.back().shots=3;
    addUnit(7026,"Iron Titan (B)",     F::IronAssembly,6,P::PathB,150,15,14,26,42,6,goldAndRes(1290,ResourceType::Iron,8), T::Mechanical);
    m_units.back().range=4; m_units.back().shots=4;

    // ── AMALGAMATE ────────────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::AM_GRAFTING_HALL; b.name="Grafting Hall";
      b.description="Town Hall - +1000 Gold weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::Amalgamate; b.cost=goldAndRes(500,ResourceType::BloodEssence,2); b.weeklyIncome=gold(1000); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T1; b.name="Flesh Crawler Vat"; b.tier=1; b.weeklyGrowth=13;
      b.description="Produces Flesh Crawlers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Amalgamate; b.cost=gold(300);
      b.upgradeA=BID::AM_T1_A; b.upgradeB=BID::AM_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T2; b.name="Graft Soldier Bay"; b.tier=2; b.weeklyGrowth=9;
      b.description="Produces Graft Soldiers"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Amalgamate; b.cost=goldAndRes(650,ResourceType::Iron,1);
      b.prerequisites={BID::AM_T1};
      b.upgradeA=BID::AM_T2_A; b.upgradeB=BID::AM_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T3; b.name="Bone Machine Works"; b.tier=3; b.weeklyGrowth=6;
      b.description="Produces Bone Machines"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Amalgamate; b.cost=goldAndRes(1100,ResourceType::Iron,2);
      b.prerequisites={BID::AM_T2};
      b.upgradeA=BID::AM_T3_A; b.upgradeB=BID::AM_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T4; b.name="Fleshwork Forge"; b.tier=4; b.weeklyGrowth=5;
      b.description="Produces Fleshwork Knights"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Amalgamate; b.cost=goldAndRes(1900,ResourceType::BloodEssence,2);
      b.prerequisites={BID::AM_T3};
      b.upgradeA=BID::AM_T4_A; b.upgradeB=BID::AM_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T5; b.name="Juggernaut Pit"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Undying Juggernauts"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Amalgamate; b.cost=goldAndRes(3200,ResourceType::BloodEssence,3);
      b.prerequisites={BID::AM_T4};
      b.upgradeA=BID::AM_T5_A; b.upgradeB=BID::AM_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T6; b.name="Spawn Chamber"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces Convergence Spawns"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Amalgamate; b.cost=goldAndRes(5800,ResourceType::BloodEssence,5);
      b.prerequisites={BID::AM_T5};
      b.upgradeA=BID::AM_T6_A; b.upgradeB=BID::AM_T6_B; m_buildings.push_back(b); }
    // Amalgamate PathA upgrades — Rapid Evolution (Iron cost)
    { BuildingDef b; b.id=BID::AM_T1_A; b.name="Rapid Crawler Vat";
      b.description="Upgrades Flesh Crawlers to Rapid Crawlers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=12;
      b.cost=goldAndRes(500,ResourceType::Iron,2);
      b.prerequisites={BID::AM_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T2_A; b.name="Rapid Soldier Bay";
      b.description="Upgrades Graft Soldiers to Rapid Soldiers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=8;
      b.cost=goldAndRes(700,ResourceType::Iron,2);
      b.prerequisites={BID::AM_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T3_A; b.name="Rapid Machine Works";
      b.description="Upgrades Bone Machines to Rapid Machines";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=5;
      b.cost=goldAndRes(1200,ResourceType::Iron,3);
      b.prerequisites={BID::AM_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T4_A; b.name="Rapid Knight Forge";
      b.description="Upgrades Fleshwork Knights to Rapid Knights";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=4;
      b.cost=goldAndRes(2100,ResourceType::Iron,3);
      b.prerequisites={BID::AM_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T5_A; b.name="Rapid Juggernaut Pit";
      b.description="Upgrades Undying Juggernauts to Rapid Juggernauts";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=goldAndRes(3500,ResourceType::Iron,4);
      b.prerequisites={BID::AM_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T6_A; b.name="Rapid Spawn Chamber";
      b.description="Upgrades Convergence Spawns to Rapid Spawns";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=goldAndRes(6500,ResourceType::Iron,6);
      b.prerequisites={BID::AM_T6}; m_buildings.push_back(b); }
    // Amalgamate PathB upgrades — Fused (BloodEssence cost)
    { BuildingDef b; b.id=BID::AM_T1_B; b.name="Fused Crawler Vat";
      b.description="Upgrades Flesh Crawlers to Fused Crawlers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=13;
      b.cost=goldAndRes(500,ResourceType::BloodEssence,2);
      b.prerequisites={BID::AM_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T2_B; b.name="Fused Soldier Bay";
      b.description="Upgrades Graft Soldiers to Fused Soldiers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=9;
      b.cost=goldAndRes(700,ResourceType::BloodEssence,2);
      b.prerequisites={BID::AM_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T3_B; b.name="Fused Machine Works";
      b.description="Upgrades Bone Machines to Fused Machines";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=6;
      b.cost=goldAndRes(1200,ResourceType::BloodEssence,3);
      b.prerequisites={BID::AM_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T4_B; b.name="Fused Knight Forge";
      b.description="Upgrades Fleshwork Knights to Fused Knights";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=5;
      b.cost=goldAndRes(2100,ResourceType::BloodEssence,3);
      b.prerequisites={BID::AM_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T5_B; b.name="Fused Juggernaut Pit";
      b.description="Upgrades Undying Juggernauts to Fused Juggernauts";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=goldAndRes(3500,ResourceType::BloodEssence,4);
      b.prerequisites={BID::AM_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_T6_B; b.name="Fused Spawn Chamber";
      b.description="Upgrades Convergence Spawns to Fused Spawns";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Amalgamate;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=goldAndRes(6500,ResourceType::BloodEssence,6);
      b.prerequisites={BID::AM_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_FLESH_VAULT; b.name="Flesh Vault";
      b.description="+3 Flesh Power for all heroes garrisoned here";
      b.category=BuildingCategory::Support; b.faction=F::Amalgamate;
      b.cost=goldAndRes(1500,ResourceType::BloodEssence,2);
      b.prerequisites={BID::AM_GRAFTING_HALL}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::AM_MERGE_CHAMBER; b.name="Merge Chamber";
      b.description="OrganicMech units adapt after 2 hits instead of 3";
      b.category=BuildingCategory::Support; b.faction=F::Amalgamate;
      b.cost=goldAndRes(2500,ResourceType::Iron,3);
      b.prerequisites={BID::AM_FLESH_VAULT}; m_buildings.push_back(b); }

    addUnit(8001,"Crawler",     F::Amalgamate,1,P::None, 14,2,3,1, 4,5, gold(65),        T::OrganicMech);
    addUnit(8002,"Flesh Warrior",     F::Amalgamate,2,P::None, 18,4,4,3, 6,5, goldAndRes(105,ResourceType::Iron,1), T::OrganicMech);
    addUnit(8003,"Brute",      F::Amalgamate,3,P::None, 28,5,5,4, 7,6, goldAndRes(195,ResourceType::Iron,2), T::OrganicMech);
    addUnit(8004,"Behemoth",  F::Amalgamate,4,P::None, 60,9,7,9,16,7, goldAndRes(360,ResourceType::BloodEssence,2), T::OrganicMech|T::Flying, true);
    addUnit(8005,"Flesh Colossus",F::Amalgamate,5,P::None, 95,12,10,14,23,8,goldAndRes(600,ResourceType::BloodEssence,3), T::OrganicMech);
    m_units.back().range = 3; m_units.back().shots = 3;
    addUnit(8006,"Apex", F::Amalgamate,6,P::None,155,16,14,23,37,9,goldAndRes(1200,ResourceType::BloodEssence,5), T::OrganicMech|T::Flying, true);
    // Amalgamate PathA — Rapid Evolution (rapidEvolution=true, adapt on every hit)
    addUnit(8011,"Crawler (A)",      F::Amalgamate,1,P::PathA, 11,2,3,1, 4,5, goldAndRes(70,ResourceType::Iron,1),         T::OrganicMech);
    m_units.back().rapidEvolution=true;
    addUnit(8012,"Flesh Warrior (A)",      F::Amalgamate,2,P::PathA, 18,4,4,3, 6,5, goldAndRes(125,ResourceType::Iron,2),        T::OrganicMech);
    m_units.back().rapidEvolution=true;
    addUnit(8013,"Brute (A)",      F::Amalgamate,3,P::PathA, 28,5,5,4, 7,6, goldAndRes(205,ResourceType::Iron,2),        T::OrganicMech);
    m_units.back().rapidEvolution=true;
    addUnit(8014,"Behemoth (A)",       F::Amalgamate,4,P::PathA, 60,9,7,9,16,7, goldAndRes(380,ResourceType::Iron,3),        T::OrganicMech|T::Flying, true);
    m_units.back().rapidEvolution=true;
    addUnit(8015,"Flesh Colossus (A)",   F::Amalgamate,5,P::PathA, 95,12,10,14,23,8,goldAndRes(640,ResourceType::Iron,4),      T::OrganicMech);
    m_units.back().rapidEvolution=true; m_units.back().range=3; m_units.back().shots=3;
    addUnit(8016,"Apex (A)",        F::Amalgamate,6,P::PathA,155,16,14,23,37,9,goldAndRes(1250,ResourceType::Iron,5),     T::OrganicMech|T::Flying, true);
    m_units.back().rapidEvolution=true;
    // Amalgamate PathB — Fused (adaptationDouble=true, gain +2 per adaptation, more HP)
    addUnit(8021,"Crawler (B)",      F::Amalgamate,1,P::PathB, 14,2,3,1, 4,5, goldAndRes(70,ResourceType::BloodEssence,1),  T::OrganicMech);
    m_units.back().adaptationDouble=true;
    addUnit(8022,"Flesh Warrior (B)",      F::Amalgamate,2,P::PathB, 22,4,4,3, 6,5, goldAndRes(120,ResourceType::BloodEssence,1), T::OrganicMech);
    m_units.back().adaptationDouble=true;
    addUnit(8023,"Brute (B)",      F::Amalgamate,3,P::PathB, 34,5,5,4, 7,6, goldAndRes(205,ResourceType::BloodEssence,2), T::OrganicMech);
    m_units.back().adaptationDouble=true;
    addUnit(8024,"Behemoth (B)",       F::Amalgamate,4,P::PathB, 70,9,7,9,16,7, goldAndRes(370,ResourceType::BloodEssence,2), T::OrganicMech|T::Flying, true);
    m_units.back().adaptationDouble=true;
    addUnit(8025,"Flesh Colossus (B)",   F::Amalgamate,5,P::PathB,110,12,10,14,23,8,goldAndRes(620,ResourceType::BloodEssence,3),T::OrganicMech);
    m_units.back().adaptationDouble=true; m_units.back().range=3; m_units.back().shots=3;
    addUnit(8026,"Apex (B)",        F::Amalgamate,6,P::PathB,175,15,14,23,37,9,goldAndRes(1200,ResourceType::BloodEssence,5),T::OrganicMech|T::Flying, true);
    m_units.back().adaptationDouble=true;

    // ── CONVERGENCE ───────────────────────────────────────────────────────────
    { BuildingDef b; b.id=BID::CV_SYNTHESIS_HUB; b.name="Synthesis Hub";
      b.description="Town Hall - +1000 Gold weekly, +2 unit growth"; b.category=BuildingCategory::Economy;
      b.faction=F::Convergence; b.cost=goldAndRes(500,ResourceType::Mercury,1); b.weeklyIncome=gold(1000); b.growthBonus=2;
      m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T1; b.name="Awakening Chamber"; b.tier=1; b.weeklyGrowth=11;
      b.description="Produces Awakened"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Convergence; b.cost=gold(350);
      b.upgradeA=BID::CV_T1_A; b.upgradeB=BID::CV_T1_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T2; b.name="Synthesis Lab"; b.tier=2; b.weeklyGrowth=8;
      b.description="Produces Synthesized"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Convergence; b.cost=gold(700);
      b.prerequisites={BID::CV_T1};
      b.upgradeA=BID::CV_T2_A; b.upgradeB=BID::CV_T2_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T3; b.name="Harmony Hall"; b.tier=3; b.weeklyGrowth=6;
      b.description="Produces Harmonized"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Convergence; b.cost=gold(1100);
      b.prerequisites={BID::CV_T2};
      b.upgradeA=BID::CV_T3_A; b.upgradeB=BID::CV_T3_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T4; b.name="Resonance Spire"; b.tier=4; b.weeklyGrowth=4;
      b.description="Produces Resonants"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Convergence; b.cost=gold(2000);
      b.prerequisites={BID::CV_T3};
      b.upgradeA=BID::CV_T4_A; b.upgradeB=BID::CV_T4_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T5; b.name="Transcendence Gate"; b.tier=5; b.weeklyGrowth=3;
      b.description="Produces Transcendents"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Convergence; b.cost=gold(3200);
      b.prerequisites={BID::CV_T4};
      b.upgradeA=BID::CV_T5_A; b.upgradeB=BID::CV_T5_B; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T6; b.name="Unity Forge"; b.tier=6; b.weeklyGrowth=2;
      b.description="Produces Unified Forms"; b.category=BuildingCategory::UnitDwelling;
      b.faction=F::Convergence; b.cost=gold(5500);
      b.prerequisites={BID::CV_T5};
      b.upgradeA=BID::CV_T6_A; b.upgradeB=BID::CV_T6_B; m_buildings.push_back(b); }
    // Convergence PathA upgrades — Mirror line (gold only)
    { BuildingDef b; b.id=BID::CV_T1_A; b.name="Mirror Awakening Chamber";
      b.description="Upgrades Awakened to Mirror Awakened";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=1; b.path=UpgradePath::PathA; b.weeklyGrowth=10;
      b.cost=gold(550);
      b.prerequisites={BID::CV_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T2_A; b.name="Mirror Synthesis Lab";
      b.description="Upgrades Synthesized to Mirror Synths (Flying)";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=2; b.path=UpgradePath::PathA; b.weeklyGrowth=7;
      b.cost=gold(900);
      b.prerequisites={BID::CV_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T3_A; b.name="Mirror Harmony Hall";
      b.description="Upgrades Harmonized to Mirror Harmonized";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=3; b.path=UpgradePath::PathA; b.weeklyGrowth=5;
      b.cost=gold(1350);
      b.prerequisites={BID::CV_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T4_A; b.name="Mirror Resonance Spire";
      b.description="Upgrades Resonants to Mirror Resonants";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=4; b.path=UpgradePath::PathA; b.weeklyGrowth=3;
      b.cost=gold(2400);
      b.prerequisites={BID::CV_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T5_A; b.name="Mirror Form Gate";
      b.description="Upgrades Transcendents to Mirror Forms";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=5; b.path=UpgradePath::PathA; b.weeklyGrowth=2;
      b.cost=gold(3900);
      b.prerequisites={BID::CV_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T6_A; b.name="Mirror Unity Forge";
      b.description="Upgrades Unified Forms to Mirror Unity";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=6; b.path=UpgradePath::PathA; b.weeklyGrowth=1;
      b.cost=gold(6600);
      b.prerequisites={BID::CV_T6}; m_buildings.push_back(b); }
    // Convergence PathB upgrades — Harmony line (gold only)
    { BuildingDef b; b.id=BID::CV_T1_B; b.name="Harmony Seeker Chamber";
      b.description="Upgrades Awakened to Harmony Seekers";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=1; b.path=UpgradePath::PathB; b.weeklyGrowth=11;
      b.cost=gold(550);
      b.prerequisites={BID::CV_T1}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T2_B; b.name="Harmony Bound Lab";
      b.description="Upgrades Synthesized to Harmony Bound";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=2; b.path=UpgradePath::PathB; b.weeklyGrowth=8;
      b.cost=gold(900);
      b.prerequisites={BID::CV_T2}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T3_B; b.name="Resonance Core Hall";
      b.description="Upgrades Harmonized to Resonance Cores";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=3; b.path=UpgradePath::PathB; b.weeklyGrowth=6;
      b.cost=gold(1350);
      b.prerequisites={BID::CV_T3}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T4_B; b.name="Harmony Knight Spire";
      b.description="Upgrades Resonants to Harmony Knights";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=4; b.path=UpgradePath::PathB; b.weeklyGrowth=4;
      b.cost=gold(2400);
      b.prerequisites={BID::CV_T4}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T5_B; b.name="Transcendent Prime Gate";
      b.description="Upgrades Transcendents to Transcendent Primes";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=5; b.path=UpgradePath::PathB; b.weeklyGrowth=3;
      b.cost=gold(3900);
      b.prerequisites={BID::CV_T5}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_T6_B; b.name="Harmonic Unity Forge";
      b.description="Upgrades Unified Forms to Harmonic Unity";
      b.category=BuildingCategory::UnitDwelling; b.faction=F::Convergence;
      b.tier=6; b.path=UpgradePath::PathB; b.weeklyGrowth=2;
      b.cost=gold(6600);
      b.prerequisites={BID::CV_T6}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_RESONANCE_WELL; b.name="Resonance Well";
      b.description="Convergence units gain +1 Attack and Defense at battle start (synergizes with Mirroring)";
      b.category=BuildingCategory::Support; b.faction=F::Convergence;
      b.cost=gold(2000); b.prerequisites={BID::CV_SYNTHESIS_HUB}; m_buildings.push_back(b); }
    { BuildingDef b; b.id=BID::CV_MIRROR_CHAMBER; b.name="Mirror Chamber";
      b.description="Convergence units gain an additional +1 Attack and Defense; hero gains +1 Attack and Defense";
      b.category=BuildingCategory::Support; b.faction=F::Convergence;
      b.cost=gold(3500); b.prerequisites={BID::CV_RESONANCE_WELL}; m_buildings.push_back(b); }

    addUnit(9001,"Initiate",      F::Convergence,1,P::None, 12,3,3,2, 4,5, gold(70),   T::Humanoid);
    addUnit(9002,"Soldier",   F::Convergence,2,P::None, 21,5,5,3, 6,6, gold(125),  T::Humanoid);
    addUnit(9003,"Mirror Warrior",    F::Convergence,3,P::None, 33,6,6,5,10,6, gold(230),  T::Humanoid);
    addUnit(9004,"Champion",      F::Convergence,4,P::None, 58,10,10,9,16,8, gold(370), T::Humanoid|T::Flying, true);
    addUnit(9005,"Elite",  F::Convergence,5,P::None, 74,12,13,14,23,10,gold(640), T::Humanoid|T::Flying, true);
    addUnit(9006,"Convergence Prime",  F::Convergence,6,P::None,132,16,15,23,37,12,gold(1250), T::Humanoid|T::Flying, true);
    // Convergence PathA — Mirror line (flying at T2+, matching enemy mobility)
    addUnit(9011,"Initiate (A)",    F::Convergence,1,P::PathA, 12,3,3,2, 4,5, gold(75),   T::Humanoid);
    addUnit(9012,"Soldier (A)",       F::Convergence,2,P::PathA, 21,5,5,3, 6,6, gold(135),  T::Humanoid|T::Flying, true);
    addUnit(9013,"Mirror Warrior (A)",  F::Convergence,3,P::PathA, 33,7,7,5,10,6, gold(225),  T::Humanoid|T::Flying, true);
    addUnit(9014,"Champion (A)",    F::Convergence,4,P::PathA, 58,10,10,9,16,8, gold(390), T::Humanoid|T::Flying, true);
    addUnit(9015,"Elite (A)",        F::Convergence,5,P::PathA, 74,12,13,14,23,10,gold(670), T::Humanoid|T::Flying, true);
    addUnit(9016,"Convergence Prime (A)",       F::Convergence,6,P::PathA,132,16,15,23,37,12,gold(1320), T::Humanoid|T::Flying, true);
    // Convergence PathB — Harmony line (higher HP, grounded, stronger bulk)
    addUnit(9021,"Initiate (B)",     F::Convergence,1,P::PathB, 13,3,3,2, 4,5, gold(75),   T::Humanoid);
    addUnit(9022,"Soldier (B)",      F::Convergence,2,P::PathB, 23,5,5,3, 6,6, gold(135),  T::Humanoid);
    addUnit(9023,"Mirror Warrior (B)",     F::Convergence,3,P::PathB, 36,7,7,5,10,6, gold(225),  T::Humanoid);
    addUnit(9024,"Champion (B)",     F::Convergence,4,P::PathB, 62,10,10,9,16,8, gold(390), T::Humanoid|T::Flying, true);
    addUnit(9025,"Elite (B)", F::Convergence,5,P::PathB, 80,12,13,14,23,10,gold(670), T::Humanoid|T::Flying, true);
    addUnit(9026,"Convergence Prime (B)",     F::Convergence,6,P::PathB,145,16,15,23,37,12,gold(1320), T::Humanoid|T::Flying, true);

    // ── Refined iron for upgrades (design: every faction refines iron) ────────
    // All PathA/PathB upgrade dwellings additionally cost iron, scaling with tier
    // (T1 = 2 ... T6 = 7). Applied uniformly here rather than in each of the ~108
    // per-faction definitions above.
    for (auto& b : m_buildings) {
        if (b.category == BuildingCategory::UnitDwelling
            && b.path != UpgradePath::None && b.tier >= 1)
            b.cost.add(ResourceType::Iron, 1 + b.tier);
    }
}

const BuildingDef* BuildingRegistry::getBuildingDef(int id) const {
    for (auto& b : m_buildings) if (b.id == id) return &b;
    return nullptr;
}

const UnitDef* BuildingRegistry::getUnitDef(int id) const {
    for (auto& u : m_units) if (u.id == id) return &u;
    return nullptr;
}

std::vector<const BuildingDef*> BuildingRegistry::getBuildingsForFaction(FactionId f) const {
    std::vector<const BuildingDef*> result;
    for (auto& b : m_buildings)
        if (b.faction == f || b.faction == FactionId::None)
            result.push_back(&b);
    return result;
}
