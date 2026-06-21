#pragma once
#include "../world/HexMap.h"
#include "../hero/Hero.h"
#include <vector>

class FogOfWar
{
public:
    // Call once at game start — hide all tiles
    static void hideAll(HexMap& map);

    // Call after hero moves — reveals tiles in vision range, marks old visible as explored-only
    static void updateVision(HexMap& map, const Hero& hero);

    // Call when multiple heroes present — union of all vision ranges
    static void updateVision(HexMap& map, const std::vector<Hero>& heroes);
};
