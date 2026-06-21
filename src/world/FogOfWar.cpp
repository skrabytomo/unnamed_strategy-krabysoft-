#include "FogOfWar.h"
#include "HexGrid.h"

void FogOfWar::hideAll(HexMap& map)
{
    map.forEach([](HexTile& t) {
        t.explored = false;
        t.visible  = false;
    });
}

void FogOfWar::updateVision(HexMap& map, const Hero& hero)
{
    // Clear current visibility (keep explored)
    map.forEach([](HexTile& t) { t.visible = false; });

    // Reveal tiles within vision range
    auto inRange = HexGrid::range(hero.pos, hero.visionRange);
    for (auto& h : inRange) {
        HexTile* tile = map.getTile(h);
        if (!tile) continue;
        tile->visible  = true;
        tile->explored = true;
    }
}

void FogOfWar::updateVision(HexMap& map, const std::vector<Hero>& heroes)
{
    map.forEach([](HexTile& t) { t.visible = false; });

    for (auto& hero : heroes) {
        auto inRange = HexGrid::range(hero.pos, hero.visionRange);
        for (auto& h : inRange) {
            HexTile* tile = map.getTile(h);
            if (!tile) continue;
            tile->visible  = true;
            tile->explored = true;
        }
    }
}
