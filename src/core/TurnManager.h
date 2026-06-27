#pragma once
#include <vector>
#include "../data/Resources.h"
#include "../town/Town.h"
#include "../town/BuildingRegistry.h"
#include "../hero/Hero.h"

class TurnManager
{
public:
    int  day()  const { return m_day; }
    int  week() const { return m_week; }
    void setDayWeek(int day, int week) { m_day = day; m_week = week; }

    // Call when player clicks End Turn
    // Returns true if a new week started
    bool endTurn(std::vector<Town>& towns,
                 std::vector<Hero>& heroes,
                 Resources&         playerResources,
                 const BuildingRegistry& registry);

    // Resource income from all owned towns this week
    Resources calculateWeeklyIncome(const std::vector<Town>& towns,
                                     uint32_t ownerId) const;

private:
    int m_day  = 1;  // 1-7
    int m_week = 1;

    void onNewWeek(std::vector<Town>& towns,
                   Resources& playerResources,
                   const BuildingRegistry& registry);
};
