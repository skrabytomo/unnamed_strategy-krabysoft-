// ============================================================
// PATCH 2: PLAYER_ELIMINATION_MINES.cpp
// Dead player mines -> neutral; conqueror takes all on final blow
// Apply to: src/game/PlayerElimination.cpp  (or Game::eliminatePlayer)
// ============================================================

#include "World.h"
#include "Player.h"
#include "Mine.h"
#include "Log.h"

void Game::eliminatePlayer(int defeated_id, int conqueror_id /* = -1 */) {
    Player& defeated = players[defeated_id];
    if (defeated.is_alive == false) return; // already dead

    defeated.is_alive = false;
    defeated.eliminated_week = current_week;
    defeated.eliminated_day  = current_day;

    log_info("P{} eliminated — no town for {} weeks, {} hero(es) starved out (week {})",
             defeated_id, defeated.weeks_without_town, defeated.hero_count, current_week);

    // --- FIX 2a: All mines go neutral immediately ---
    int mines_neutralized = 0;
    for (auto& mine : world.mines) {
        if (mine.owner_id == defeated_id) {
            mine.owner_id = NEUTRAL_PLAYER_ID;  // 0 or -1 depending on your enum
            mine.has_guard = true;                // respawn guards so others must fight
            mine.guard_strength = mine.base_guard_strength; // reset to full
            mines_neutralized++;
        }
    }
    if (mines_neutralized > 0) {
        log_info("P{} mines neutralized: {}", defeated_id, mines_neutralized);
    }

    // --- FIX 2b: If conqueror delivered final blow, transfer mines INSTEAD of neutralizing ---
    if (conqueror_id >= 0 && conqueror_id != defeated_id && players[conqueror_id].is_alive) {
        // Reverse the neutralization and give to conqueror
        int mines_conquered = 0;
        for (auto& mine : world.mines) {
            if (mine.owner_id == NEUTRAL_PLAYER_ID && mine.last_owner == defeated_id) {
                mine.owner_id = conqueror_id;
                mine.has_guard = false; // already cleared by conqueror's victory
                mines_conquered++;
            }
        }
        if (mines_conquered > 0) {
            log_info("P{} inherits {} mines from defeated P{}", conqueror_id, mines_conquered, defeated_id);
            players[conqueror_id].gold += defeated.gold / 4; // 25% gold spoils
        }
    }

    // Recalculate incomes immediately so the economy report is correct next turn
    recalculateAllIncomes();
}

// ============================================================
// PATCH 2b: LAST_TOWN_CAPTURED hook
// Call this from CombatResolution or SiegeEngine when a town falls
// Apply to: src/combat/SiegeResolution.cpp  or similar
// ============================================================

void SiegeResolution::onTownCaptured(Town& town, int attacker_id) {
    int previous_owner = town.owner_id;

    // Transfer town ownership
    town.owner_id = attacker_id;
    town.buildings.clear(); // or convert to attacker's faction style

    // Check if previous owner is now townless
    Player& previous = game.getPlayer(previous_owner);
    bool still_has_towns = false;
    for (auto& t : game.world.towns) {
        if (t.owner_id == previous_owner) {
            still_has_towns = true;
            break;
        }
    }

    if (!still_has_towns) {
        // Check if they still have heroes
        bool has_heroes = (previous.hero_count > 0);

        if (!has_heroes) {
            // Immediate elimination — conqueror gets everything
            game.eliminatePlayer(previous_owner, attacker_id);
        } else {
            // Hero is still roaming — mark as "dying" but don't eliminate yet
            previous.weeks_without_town++;
            log_info("P{} lost last town; {} hero(es) remain. Mines held until hero falls.",
                     previous_owner, previous.hero_count);
        }
    }
}

// ============================================================
// PATCH 2c: HERO_DEFEATED hook
// If this was the defeated player's last hero AND they have no towns,
// trigger elimination with conqueror getting mines.
// Apply to: src/combat/FieldCombat.cpp  or Hero::onDefeated
// ============================================================

void Hero::onDefeated(int victor_player_id) {
    Player& my_owner = game.getPlayer(owner_id);

    // Remove me from the world
    game.world.removeHero(this->id);
    my_owner.hero_count--;

    // Check if this was the last hero
    bool has_towns = false;
    for (auto& t : game.world.towns) {
        if (t.owner_id == owner_id) { has_towns = true; break; }
    }

    if (my_owner.hero_count <= 0 && !has_towns) {
        // This was the final blow — transfer all mines to victor
        game.eliminatePlayer(owner_id, victor_player_id);
    } else if (!has_towns) {
        // Heroes remain but no towns — keep playing as roaming band
        log_info("P{} hero defeated; {} hero(es) still roam without a town.",
                 owner_id, my_owner.hero_count);
    }
}
