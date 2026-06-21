#pragma once
#include <cstdint>
#include "Resources.h"
#include "../world/HexMap.h"

struct ResourceNode
{
    uint32_t     id          = 0;
    HexCoord     pos         = {0, 0};
    ResourceType type        = ResourceType::Gold;
    int          amount      = 0;    // units per week if mined
    bool         depleted    = false;
    uint32_t     ownedBy     = 0;    // 0=neutral, 1=player, >1=enemy hero id
    uint32_t     guardId     = 0;    // neutral hero id guarding node (0 = free)
    bool         guardBeaten = false; // true after player defeats the mine guards
};
