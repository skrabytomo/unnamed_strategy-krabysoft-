#include "Hero.h"
#include <algorithm>

int Hero::moveCost(Terrain t) const
{
    if (onBoat && t == Terrain::Water) return 2;  // fast sailing

    int base = BASE_MOVE_COST[static_cast<int>(t)];

    // Home terrain bonus — costs 1 less (min 1)
    // Penalty terrain — costs 1 more
    // These are defined per faction
    switch (faction) {
    case FactionId::HolyOrder:
        if (t == Terrain::Plains || t == Terrain::Sacred)    base = std::max(1, base - 1);
        if (t == Terrain::Corrupted || t == Terrain::Toxic)  base += 1;
        break;
    case FactionId::CrimsonWardens:
        if (t == Terrain::Highland)                          base = std::max(1, base - 1);
        if (t == Terrain::Corrupted || t == Terrain::Swamp)  base += 1;
        break;
    case FactionId::Thornkin:
        if (t == Terrain::Forest)                            base = std::max(1, base - 1);
        if (t == Terrain::Volcanic || t == Terrain::Barren)  base += 1;
        break;
    case FactionId::EternalEmpire:
        if (t == Terrain::Toxic || t == Terrain::Corrupted)  base = std::max(1, base - 1);
        if (t == Terrain::Sacred)                            base += 1;
        break;
    case FactionId::Bloodsworn:
        if (t == Terrain::Corrupted || t == Terrain::Swamp)  base = std::max(1, base - 1);
        if (t == Terrain::Sacred || t == Terrain::Plains)    base += 1;
        break;
    case FactionId::Voidkin:
        if (t == Terrain::CorruptedForest)                   base = std::max(1, base - 1);
        if (t == Terrain::Sacred)                            base += 1;
        break;
    case FactionId::IronAssembly:
        if (t == Terrain::Industrial || t == Terrain::Rocky) base = std::max(1, base - 1);
        if (t == Terrain::Swamp || t == Terrain::Water)      base += 2;
        break;
    case FactionId::Amalgamate:
        if (t == Terrain::Wasteland || t == Terrain::FleshZone) base = std::max(1, base - 1);
        if (t == Terrain::Sacred)                            base += 1;
        break;
    case FactionId::Convergence:
        // No bonus or penalty — average everywhere
        break;
    default:
        break;
    }

    return base;
}

bool Hero::canEnter(Terrain t) const
{
    if (t == Terrain::Mountain) return false;
    if (t == Terrain::Water)    return onBoat;  // need a boat to sail
    return true;
}
