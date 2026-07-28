#include "ConquestQuests.h"
#include <cstdio>

std::string questText(QuestEvent e, int target, bool weekly, int param)
{
    char buf[128];
    switch (e) {
    case QuestEvent::BattleWon:
        std::snprintf(buf, sizeof(buf), "Win %d %s", target, target == 1 ? "battle" : "battles");
        break;
    case QuestEvent::NodeCleared:
        std::snprintf(buf, sizeof(buf), "Clear %d map %s", target, target == 1 ? "node" : "nodes");
        break;
    case QuestEvent::SideNodeCleared:
        std::snprintf(buf, sizeof(buf), "Clear %d side %s", target, target == 1 ? "branch" : "branches");
        break;
    case QuestEvent::ArenaWon:
        std::snprintf(buf, sizeof(buf), "Win %d arena %s", target, target == 1 ? "fight" : "fights");
        break;
    case QuestEvent::ChestOpened:
        std::snprintf(buf, sizeof(buf), "Open %d %s", target, target == 1 ? "chest" : "chests");
        break;
    case QuestEvent::MultiFactionWin:
        std::snprintf(buf, sizeof(buf), "Win %d %s with 3+ factions in your team",
                      target, target == 1 ? "battle" : "battles");
        break;
    case QuestEvent::SkirmishPlayed:
        std::snprintf(buf, sizeof(buf), "Play %d skirmish %s (any faction)",
                      target, target == 1 ? "game" : "games");
        break;
    case QuestEvent::SkirmishWonDifficulty: {
        const char* diffName = param == 2 ? "Hard" : param == 1 ? "Normal" : param == 0 ? "Easy" : "any";
        std::snprintf(buf, sizeof(buf), "Win %d skirmish %s on %s difficulty",
                      target, target == 1 ? "game" : "games", diffName);
        break;
    }
    case QuestEvent::SkirmishWonRandomFaction:
        std::snprintf(buf, sizeof(buf), "Win %d skirmish %s with a randomized faction",
                      target, target == 1 ? "game" : "games");
        break;
    default:
        std::snprintf(buf, sizeof(buf), "Objective");
        break;
    }
    (void)weekly;
    return buf;
}

namespace QuestRewards
{

QuestReward forQuest(const Quest& q)
{
    QuestReward r;
    if (q.weekly) {
        // Weekly quests pay bigger: an Iron chest + a key, or a gem stack.
        switch (q.event) {
        case QuestEvent::NodeCleared:     r = {QuestRewardKind::IronChest, 1}; break;
        case QuestEvent::ArenaWon:        r = {QuestRewardKind::Key,       1}; break;
        case QuestEvent::ChestOpened:     r = {QuestRewardKind::Gems,     50}; break;
        default:                          r = {QuestRewardKind::Gems,     50}; break;
        }
    } else {
        // Daily quests pay a Wooden chest + gems/gold.
        switch (q.event) {
        case QuestEvent::BattleWon:       r = {QuestRewardKind::WoodenChest, 1}; break;
        case QuestEvent::MultiFactionWin: r = {QuestRewardKind::Gems,       20}; break;
        case QuestEvent::SideNodeCleared: r = {QuestRewardKind::Gold,      300}; break;
        default:                          r = {QuestRewardKind::Gold,      200}; break;
        }
    }
    return r;
}

std::string describe(const QuestReward& r)
{
    char buf[64];
    switch (r.kind) {
    case QuestRewardKind::Gold:        std::snprintf(buf, sizeof(buf), "%d Gold", r.amount); break;
    case QuestRewardKind::Gems:        std::snprintf(buf, sizeof(buf), "%d Gems", r.amount); break;
    case QuestRewardKind::WoodenChest: std::snprintf(buf, sizeof(buf), "%d Wooden Chest", r.amount); break;
    case QuestRewardKind::IronChest:   std::snprintf(buf, sizeof(buf), "%d Iron Chest", r.amount); break;
    case QuestRewardKind::Key:         std::snprintf(buf, sizeof(buf), "%d Key", r.amount); break;
    }
    return buf;
}

} // namespace QuestRewards
