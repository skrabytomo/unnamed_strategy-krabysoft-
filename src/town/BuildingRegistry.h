#pragma once
#include <vector>
#include "BuildingDef.h"
#include "UnitDef.h"

// Building IDs — unique across all factions
// 1xx = Holy Order, 2xx = Crimson Wardens, 3xx = Thornkin
// 4xx = Eternal Empire, 5xx = Bloodsworn, 6xx = Voidkin
// 7xx = Iron Assembly, 8xx = Amalgamate, 9xx = Convergence
// 001-009 = shared (fort, market, etc.)

namespace BID {
    // Shared
    constexpr int FORT         = 1;
    constexpr int MARKET       = 2;
    constexpr int WAREHOUSE    = 3;
    constexpr int ROAD         = 4;
    constexpr int MAGE_GUILD    = 5;   // T1: 2 spells
    constexpr int MAGE_GUILD_T2 = 6;  // T2: 4 spells
    constexpr int WAREHOUSE_T2  = 7;  // +4 Iron/wk
    constexpr int WAREHOUSE_T3  = 8;  // +6 Iron/wk
    constexpr int MAGE_GUILD_T3 = 9;  // T3: all 4 spells at 30% discount
    constexpr int MAGE_GUILD_T4 = 10; // T4: all 4 spells at 50% discount (magic factions)
    constexpr int TOWN_HALL     = 11; // +1500 Gold/wk; requires Market
    constexpr int CITY_HALL     = 12; // +3500 Gold/wk; requires Town Hall, unlocks at week 3
    // Faction Capitols — unique tier-4 economy, requires City Hall + faction hall, week 5
    constexpr int HO_SANCTUM    = 13; // Holy Order: Sacred Sanctum
    constexpr int CW_NECROPOLIS = 14; // Crimson Wardens: Grand Necropolis
    constexpr int TK_HEARTWOOD  = 15; // Thornkin: Ancient Heartwood
    constexpr int EE_CITADEL    = 16; // Eternal Empire: Eternal Citadel
    constexpr int BS_BLOODSPIRE = 17; // Bloodsworn: Bloodspire Fortress
    constexpr int VK_VOIDCORE   = 18; // Voidkin: Void Core Nexus
    constexpr int IA_MEGAFORGE  = 19; // Iron Assembly: Grand Megaforge
    constexpr int AM_FLESHPIT   = 20; // Amalgamate: Grand Fleshpit
    constexpr int CV_NEXUS      = 21; // Convergence: Synthesis Nexus

    // Holy Order
    constexpr int HO_HALL         = 100; // Town Hall (base income)
    constexpr int HO_T1_BASE      = 101; // Penitent dwelling
    constexpr int HO_T1_A         = 102; // Path A upgrade
    constexpr int HO_T1_B         = 103; // Path B upgrade
    constexpr int HO_T2_BASE      = 104; // Torch Bearer
    constexpr int HO_T2_A         = 105;
    constexpr int HO_T2_B         = 106;
    constexpr int HO_T3_BASE      = 107; // Plague Doctor
    constexpr int HO_T3_A         = 108;
    constexpr int HO_T3_B         = 109;
    constexpr int HO_T4_BASE      = 110; // Penitent Knight
    constexpr int HO_T4_A         = 111;
    constexpr int HO_T4_B         = 112;
    constexpr int HO_T5_BASE      = 113; // Seraph
    constexpr int HO_T5_A         = 114;
    constexpr int HO_T5_B         = 115;
    constexpr int HO_T6_BASE      = 116; // Winged Hussar
    constexpr int HO_T6_A         = 117;
    constexpr int HO_T6_B         = 118;
    constexpr int HO_LIGHT_SHRINE = 119; // +Light Power
    constexpr int HO_RELIQUARY    = 120; // +Desperation meter speed

    // Crimson Wardens (2xx)
    constexpr int CW_HALL         = 200;
    constexpr int CW_T1           = 201; // Skeleton
    constexpr int CW_T2           = 202; // Bone Archer
    constexpr int CW_T3           = 203; // Wight
    constexpr int CW_T4           = 204; // Vampire
    constexpr int CW_T5           = 205; // Lich
    constexpr int CW_T6           = 206; // Bone Dragon
    constexpr int CW_DEATH_ALTAR  = 207; // +Death Power
    constexpr int CW_WARDEN_BRAND = 208; // Warden's Mark support

    // Thornkin (3xx)
    constexpr int TK_GROVE_HEART  = 300;
    constexpr int TK_T1           = 301; // Sproutling
    constexpr int TK_T2           = 302; // Briar
    constexpr int TK_T3           = 303; // Vine Crawler
    constexpr int TK_T4           = 304; // Grove Guardian
    constexpr int TK_T5           = 305; // Ancient Oak
    constexpr int TK_T6           = 306; // World Thorn
    constexpr int TK_ANCIENT_CIRCLE = 307; // +Nature Power
    constexpr int TK_SYMBIOSIS_WEB = 308; // Symbiosis bond support

    // Eternal Empire (4xx)
    constexpr int EE_THRONE       = 400;
    constexpr int EE_T1           = 401; // Conscript
    constexpr int EE_T2           = 402; // Revenant
    constexpr int EE_T3           = 403; // Shade Archer
    constexpr int EE_T4           = 404; // Steel Guardian
    constexpr int EE_T5           = 405; // Phantom Knight
    constexpr int EE_T6           = 406; // Immortal
    constexpr int EE_NECROPOLIS   = 407; // +Death Power
    constexpr int EE_MONUMENT     = 408; // Eternal Command support

    // Bloodsworn (5xx)
    constexpr int BS_WAR_HALL     = 500;
    constexpr int BS_T1           = 501; // Bloodling
    constexpr int BS_T2           = 502; // Berserker
    constexpr int BS_T3           = 503; // Blood Shaman
    constexpr int BS_T4           = 504; // Ravager
    constexpr int BS_T5           = 505; // Bloodtide Warlord
    constexpr int BS_T6           = 506; // Crimson Avatar
    constexpr int BS_BLOOD_ALTAR  = 507; // +Blood Power
    constexpr int BS_WAR_SHRINE   = 508; // Blood Pool faster

    // Voidkin (6xx)
    constexpr int VK_NEXUS        = 600;
    constexpr int VK_T1           = 601; // Void Wisp
    constexpr int VK_T2           = 602; // Phase Walker
    constexpr int VK_T3           = 603; // Rift Archer
    constexpr int VK_T4           = 604; // Void Stalker
    constexpr int VK_T5           = 605; // Entropy Wraith
    constexpr int VK_T6           = 606; // Void Colossus
    constexpr int VK_RIFT_GATE    = 607; // +Nature Power (void resonance)
    constexpr int VK_VOID_LENS    = 608; // Possession duration

    // Iron Assembly (7xx)
    constexpr int IA_FORGE_HALL   = 700;
    constexpr int IA_T1           = 701; // Automaton
    constexpr int IA_T2           = 702; // Gun Construct
    constexpr int IA_T3           = 703; // Steam Walker
    constexpr int IA_T4           = 704; // Siege Bot
    constexpr int IA_T5           = 705; // Titan Construct
    constexpr int IA_T6           = 706; // Colossus Prime
    constexpr int IA_BLUEPRINT_VAULT = 707; // +Forge Power
    constexpr int IA_OVERCLOCK    = 708; // Construct speed support

    // Amalgamate (8xx)
    constexpr int AM_GRAFTING_HALL = 800;
    constexpr int AM_T1           = 801; // Flesh Crawler
    constexpr int AM_T2           = 802; // Graft Soldier
    constexpr int AM_T3           = 803; // Bone Machine
    constexpr int AM_T4           = 804; // Fleshwork Knight
    constexpr int AM_T5           = 805; // Undying Juggernaut
    constexpr int AM_T6           = 806; // Convergence Spawn
    constexpr int AM_FLESH_VAULT  = 807; // +Flesh Power
    constexpr int AM_MERGE_CHAMBER = 808; // Adaptation support

    // Convergence (9xx)
    constexpr int CV_SYNTHESIS_HUB = 900;
    constexpr int CV_T1           = 901; // Awakened
    constexpr int CV_T2           = 902; // Synthesized
    constexpr int CV_T3           = 903; // Harmonized
    constexpr int CV_T4           = 904; // Resonant
    constexpr int CV_T5           = 905; // Transcendent
    constexpr int CV_T6           = 906; // Unified Form
    constexpr int CV_RESONANCE_WELL = 907; // Mirroring support
    constexpr int CV_MIRROR_CHAMBER = 908; // Mirror duration

    // CrimsonWardens upgrades (21x)
    constexpr int CW_T1_A = 211, CW_T1_B = 212;
    constexpr int CW_T2_A = 213, CW_T2_B = 214;
    constexpr int CW_T3_A = 215, CW_T3_B = 216;
    constexpr int CW_T4_A = 217, CW_T4_B = 218;
    constexpr int CW_T5_A = 219, CW_T5_B = 220;
    constexpr int CW_T6_A = 221, CW_T6_B = 222;
    // Thornkin upgrades (31x)
    constexpr int TK_T1_A = 311, TK_T1_B = 312;
    constexpr int TK_T2_A = 313, TK_T2_B = 314;
    constexpr int TK_T3_A = 315, TK_T3_B = 316;
    constexpr int TK_T4_A = 317, TK_T4_B = 318;
    constexpr int TK_T5_A = 319, TK_T5_B = 320;
    constexpr int TK_T6_A = 321, TK_T6_B = 322;
    // EternalEmpire upgrades (41x)
    constexpr int EE_T1_A = 411, EE_T1_B = 412;
    constexpr int EE_T2_A = 413, EE_T2_B = 414;
    constexpr int EE_T3_A = 415, EE_T3_B = 416;
    constexpr int EE_T4_A = 417, EE_T4_B = 418;
    constexpr int EE_T5_A = 419, EE_T5_B = 420;
    constexpr int EE_T6_A = 421, EE_T6_B = 422;
    // Bloodsworn upgrades (51x)
    constexpr int BS_T1_A = 511, BS_T1_B = 512;
    constexpr int BS_T2_A = 513, BS_T2_B = 514;
    constexpr int BS_T3_A = 515, BS_T3_B = 516;
    constexpr int BS_T4_A = 517, BS_T4_B = 518;
    constexpr int BS_T5_A = 519, BS_T5_B = 520;
    constexpr int BS_T6_A = 521, BS_T6_B = 522;
    // Voidkin upgrades (61x)
    constexpr int VK_T1_A = 611, VK_T1_B = 612;
    constexpr int VK_T2_A = 613, VK_T2_B = 614;
    constexpr int VK_T3_A = 615, VK_T3_B = 616;
    constexpr int VK_T4_A = 617, VK_T4_B = 618;
    constexpr int VK_T5_A = 619, VK_T5_B = 620;
    constexpr int VK_T6_A = 621, VK_T6_B = 622;
    // IronAssembly upgrades (71x)
    constexpr int IA_T1_A = 711, IA_T1_B = 712;
    constexpr int IA_T2_A = 713, IA_T2_B = 714;
    constexpr int IA_T3_A = 715, IA_T3_B = 716;
    constexpr int IA_T4_A = 717, IA_T4_B = 718;
    constexpr int IA_T5_A = 719, IA_T5_B = 720;
    constexpr int IA_T6_A = 721, IA_T6_B = 722;
    // Amalgamate upgrades (81x)
    constexpr int AM_T1_A = 811, AM_T1_B = 812;
    constexpr int AM_T2_A = 813, AM_T2_B = 814;
    constexpr int AM_T3_A = 815, AM_T3_B = 816;
    constexpr int AM_T4_A = 817, AM_T4_B = 818;
    constexpr int AM_T5_A = 819, AM_T5_B = 820;
    constexpr int AM_T6_A = 821, AM_T6_B = 822;
    // Convergence upgrades (91x)
    constexpr int CV_T1_A = 911, CV_T1_B = 912;
    constexpr int CV_T2_A = 913, CV_T2_B = 914;
    constexpr int CV_T3_A = 915, CV_T3_B = 916;
    constexpr int CV_T4_A = 917, CV_T4_B = 918;
    constexpr int CV_T5_A = 919, CV_T5_B = 920;
    constexpr int CV_T6_A = 921, CV_T6_B = 922;
}

class BuildingRegistry
{
public:
    void init(); // populate all definitions

    const BuildingDef* getBuildingDef(int id) const;
    const UnitDef*     getUnitDef(int id)     const;

    // Get all buildings for a faction
    std::vector<const BuildingDef*> getBuildingsForFaction(FactionId f) const;

    const std::vector<BuildingDef>& buildings() const { return m_buildings; }
    const std::vector<UnitDef>&     units()     const { return m_units; }

private:
    std::vector<BuildingDef> m_buildings;
    std::vector<UnitDef>     m_units;
};
