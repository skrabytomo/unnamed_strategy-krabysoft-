// ============================================================
// PATCH 1: AI_EARLY_GAME_GOALS.cpp
// Prevents Week 1 town-rush suicide marches; forces mine/buildup phase
// Apply to: src/ai/AIGoalScorer.cpp  (or wherever CommitToMarch lives)
// ============================================================

#include "AIConstants.h"

void AIGoalScorer::scoreOffensiveGoals(AIHero& hero, const WorldState& world) {
    const int currentWeek = world.getWeek();
    const Player& owner   = world.getPlayer(hero.owner_id);

    // --- NEW: Early-game buildup gate ---
    constexpr int EARLY_GAME_WEEKS = 3;
    constexpr int EARLY_MINE_MINIMUM = 4;   // must have at least this many mines
    constexpr int MIN_ARMY_STRENGTH  = 1500; // rough threshold before leaving territory

    bool inBuildupPhase = (currentWeek <= EARLY_GAME_WEEKS);
    bool poorEconomy    = (owner.mines.size() < EARLY_MINE_MINIMUM);
    bool weakArmy       = (hero.army_strength < MIN_ARMY_STRENGTH);

    // Iterate goals in priority order
    for (auto& goal : candidateGoals) {
        switch (goal.type) {

        case GoalType::CAPTURE_TOWN: {
            // --- FIX: Block long-distance town rushes during buildup ---
            if (inBuildupPhase && poorEconomy) {
                goal.score = -9999; // hard veto
                goal.reason = "[BUILDUP] Economy too weak for town assault";
                break;
            }
            if (inBuildupPhase && goal.distance > 30) {
                goal.score *= 0.1f; // heavily penalize far targets
                goal.reason = "[BUILDUP] Target too distant for early week";
                break;
            }
            // Existing pre-wall window logic can stay, but only if above passes
            if (goal.target_town && !goal.target_town->has_walls) {
                goal.score *= 1.3f;
                goal.reason += " [SCOUT: pre-wall window]";
            }
            break;
        }

        case GoalType::CAPTURE_NEUTRAL_MINE: {
            // --- FIX: Boost mines heavily in early game ---
            if (inBuildupPhase && poorEconomy) {
                goal.score += 2000;
                goal.reason = "[BUILDUP] Mine priority (early game)";
            }
            // Deprioritize mines already owned by allies
            if (goal.target_mine && goal.target_mine->owner == hero.owner_id) {
                goal.score = -1000;
            }
            break;
        }

        case GoalType::DEFEND_TOWN: {
            // Always valid
            break;
        }

        case GoalType::GAIN_XP: {
            // Camps/shrines — okay early, but not if economy is starving
            if (inBuildupPhase && poorEconomy && goal.distance > 15) {
                goal.score *= 0.5f;
            }
            break;
        }
        }
    }

    // --- NEW: Force hero out of town on Day 1 if idle ---
    if (inBuildupPhase && hero.days_idle > 0 && poorEconomy) {
        auto nearestMine = world.findNearestUnclaimedMine(hero.position, hero.owner_id);
        if (nearestMine) {
            Goal forcedMine;
            forcedMine.type = GoalType::CAPTURE_NEUTRAL_MINE;
            forcedMine.target = nearestMine->position;
            forcedMine.score = 5000; // override everything
            forcedMine.reason = "[BUILDUP] Forced: nearest unclaimed mine";
            candidateGoals.push_back(forcedMine);
        }
    }
}

// ============================================================
// Also add to: src/ai/AIPersonality.cpp  (Explorer tuning)
// ============================================================

void AIPersonality::applyPersonalityModifiers(Goal& goal, int week) const {
    switch (type) {
    case Personality::EXPLORER: {
        // Explorer was too passive — boost early expansion
        if (goal.type == GoalType::CAPTURE_NEUTRAL_MINE && week <= 4) {
            goal.score *= 2.5f;
        }
        // But don't let them turtle — reduce town-building priority
        if (goal.type == GoalType::BUILD_IN_TOWN && week <= 2) {
            goal.score *= 0.7f; // build after mines are secured
        }
        break;
    }
    case Personality::WARRIOR: {
        // Warriors were rushing towns too early — add minimum mine gate
        if (goal.type == GoalType::CAPTURE_TOWN && week <= 3) {
            goal.score *= 0.3f; // dampen, don't delete
        }
        break;
    }
    case Personality::BUILDER: {
        // Builders are fine, but ensure they still grab mines
        if (goal.type == GoalType::CAPTURE_NEUTRAL_MINE && week <= 3) {
            goal.score *= 1.4f;
        }
        break;
    }
    case Personality::MAGE: {
        // Mages need gold for spells — mines critical
        if (goal.type == GoalType::CAPTURE_NEUTRAL_MINE && week <= 3) {
            goal.score *= 1.8f;
        }
        break;
    }
    }
}
