#include "TurnManager.h"
#include "DevLog.h"
#include <stdio.h>

bool TurnManager::endTurn(std::vector<Town>& towns,
                           std::vector<Hero>& heroes,
                           Resources& playerResources,
                           const BuildingRegistry& registry)
{
    // Restore hero movement and regenerate a small amount of mana each day
    for (auto& hero : heroes) {
        hero.movePool = hero.maxMove;
        hero.path.clear();
        hero.pathStep = 0;
        // Daily mana regen: 2 + 10% of max, minimum 2
        int manaRegen = std::max(2, 2 + hero.maxMana / 10);
        hero.mana = std::min(hero.maxMana, hero.mana + manaRegen);
    }

    m_day++;
    bool newWeek = false;

    if (m_day > 7) {
        m_day = 1;
        m_week++;
        newWeek = true;
        onNewWeek(towns, playerResources, registry);
    }

    gLog("Day %d Week %d | Gold: %d\n",
        m_day, m_week,
        playerResources.get(ResourceType::Gold));

    return newWeek;
}

void TurnManager::onNewWeek(std::vector<Town>& towns,
                             Resources& playerResources,
                             const BuildingRegistry& registry)
{
    gLog("=== WEEK %d BEGINS ===\n", m_week);

    for (auto& town : towns) {
        // Add weekly resource income — only player-owned towns
        if (town.ownerId == 1)
            playerResources.addAll(town.weeklyIncome);

        // Add unit growth to dwellings
        town.onWeekStart(registry.buildings());
    }
}

Resources TurnManager::calculateWeeklyIncome(const std::vector<Town>& towns,
                                               uint32_t ownerId) const
{
    Resources total;
    for (auto& town : towns)
        if (town.ownerId == ownerId)
            total.addAll(town.weeklyIncome);
    return total;
}
