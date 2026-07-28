#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ── Conquest quests (Phase 3) ─────────────────────────────────────────────────
// Daily (3, reroll at local midnight) and weekly (3, reroll on ISO week change)
// quests. Progress is event-driven: gameplay calls ConquestMode::reportEvent()
// which bumps matching quests. Completed quests are claimed for rewards.

enum class QuestEvent : uint8_t
{
    BattleWon,          // any conquest map battle won
    NodeCleared,        // any node cleared (battle or treasure)
    SideNodeCleared,    // a side-branch node cleared
    ArenaWon,           // arena fight won (Phase 5; wired now, harmless until then)
    ChestOpened,        // any chest opened
    MultiFactionWin,    // won a battle with 3+ distinct factions in the team
    // ── Skirmish-sourced events (reported from REGULAR games, not Conquest's
    // own node map). Conquest is the persistent out-of-game progression layer;
    // skirmish/watch games are where you actually play, and their outcomes
    // feed Conquest quests/rewards. param encodes event-specific context (see
    // each quest's generation comment) and -1 in a quest's `param` means "any".
    SkirmishPlayed,     // finished a skirmish game (win or lose), any faction
    SkirmishWonDifficulty, // won a skirmish; quest.param = required difficulty (0/1/2), -1 = any
    SkirmishWonRandomFaction, // won a skirmish where YOUR faction was randomized at setup
};

enum class QuestRewardKind : uint8_t { Gold, Gems, WoodenChest, IronChest, Key };

struct QuestReward
{
    QuestRewardKind kind = QuestRewardKind::Gold;
    int amount           = 0;   // gold/gems count, or chest/key count
};

struct Quest
{
    int         id       = 0;   // db row id
    bool        weekly   = false;
    QuestEvent  event    = QuestEvent::BattleWon;
    int         param    = 0;   // event-specific (unused for now)
    int         progress = 0;
    int         target   = 1;
    int64_t     expiry   = 0;   // unix time; quest replaced after this
    bool        claimed  = false;
    std::string text;           // human-readable, derived from event+target

    bool complete() const { return progress >= target; }
};

// Reward tables (kept here so UI and grant logic agree).
namespace QuestRewards
{
    // A quest's reward is derived from (weekly?, event, target). Simple and
    // deterministic — no need to persist the reward, just recompute it.
    QuestReward forQuest(const Quest& q);
    std::string describe(const QuestReward& r);
}

// Builds the human-readable text for a freshly generated quest.
std::string questText(QuestEvent e, int target, bool weekly, int param = -1);
