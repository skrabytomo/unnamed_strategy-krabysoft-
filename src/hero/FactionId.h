#pragma once
#include <cstdint>

enum class FactionId : uint8_t
{
    HolyOrder = 0,
    CrimsonWardens,
    Thornkin,
    EternalEmpire,
    Bloodsworn,
    Voidkin,
    IronAssembly,
    Amalgamate,
    Convergence,
    None
};

static constexpr int FACTION_COUNT = static_cast<int>(FactionId::Convergence) + 1;

// Canonical display name. Game_Core.cpp / Game_MainMenu.cpp / Game_WorldMap.cpp
// each still carry their own copy of this table; prefer this one in new code.
inline const char* factionName(FactionId f) {
    switch (f) {
        case FactionId::HolyOrder:      return "Holy Order";
        case FactionId::CrimsonWardens: return "Crimson Wardens";
        case FactionId::Thornkin:       return "Thornkin";
        case FactionId::EternalEmpire:  return "Eternal Empire";
        case FactionId::Bloodsworn:     return "Bloodsworn";
        case FactionId::Voidkin:        return "Voidkin";
        case FactionId::IronAssembly:   return "Iron Assembly";
        case FactionId::Amalgamate:     return "Amalgamate";
        case FactionId::Convergence:    return "Convergence";
        default:                        return "Unaligned";
    }
}
