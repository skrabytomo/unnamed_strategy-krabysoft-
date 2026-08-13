// ============================================================
// PATCH 5: WATCH_MODE_PERSISTENCE.cpp
// Game continues until 1 player remains; watched player stays in view
// Apply to: src/game/GameLoop.cpp  and src/ui/WatchModeOverlay.cpp
// ============================================================

// --- FIX 5a: Game end condition ---
// OLD: game ends when watched player is eliminated
// NEW: game ends when only ONE player remains alive

bool Game::checkVictoryCondition() {
    int alive_count = 0;
    int last_alive_id = -1;

    for (const auto& p : players) {
        if (p.is_alive) {
            alive_count++;
            last_alive_id = p.id;
        }
    }

    if (alive_count <= 1) {
        // Game over — declare winner
        if (last_alive_id >= 0) {
            log_info("[VICTORY] P{} is the last survivor! Game over.", last_alive_id);
            showVictoryScreen(last_alive_id);
        } else {
            log_info("[DRAW] All players eliminated.");
        }
        return true; // signal game over
    }

    // --- NEW: If watched player was eliminated, keep spectating ---
    if (!players[watched_player_id].is_alive && !spectator_switch_done) {
        log_info("[WATCH] P{} eliminated. Switching to full spectator mode.", watched_player_id);
        spectator_switch_done = true;

        // Optional: auto-follow the strongest remaining player
        int strongest_alive = findStrongestAlivePlayer();
        if (strongest_alive >= 0) {
            camera.followPlayer(strongest_alive);
            log_info("[WATCH] Now spectating strongest remaining player: P{}", strongest_alive);
        }
    }

    return false; // game continues
}

// --- FIX 5b: Camera / UI follow logic ---
// Ensure the camera doesn't break when watched player dies
void Camera::updateFollowTarget(const Game& game) {
    if (follow_mode == FOLLOW_WATCHED_PLAYER) {
        const Player& watched = game.getPlayer(game.watched_player_id);

        if (watched.is_alive && watched.hasHeroes()) {
            // Normal: follow watched player's first hero
            target_pos = watched.heroes[0]->position;
        } else {
            // Watched player dead — follow nearest battle, or strongest player
            if (auto* battle = game.findNearestActiveBattle(last_view_pos)) {
                target_pos = battle->position;
            } else if (int strongest = game.findStrongestAlivePlayer(); strongest >= 0) {
                const Player& p = game.getPlayer(strongest);
                if (!p.heroes.empty()) target_pos = p.heroes[0]->position;
            }
        }
    }
}

// --- FIX 5c: Turn scheduler must include dead players' remaining heroes ---
// If a player is "eliminated" (no towns) but still has heroes, they get turns
void TurnScheduler::buildTurnQueue(const Game& game) {
    queue.clear();

    for (const auto& p : game.players) {
        // OLD: if (!p.is_alive) continue;
        // NEW: include everyone who has at least one hero OR one town
        if (p.hero_count == 0 && p.town_count == 0) continue; // truly dead

        for (auto& hero : p.heroes) {
            if (hero.is_alive && hero.movement_points > 0) {
                queue.push_back({p.id, hero.id, hero.initiative});
            }
        }
    }

    // Sort by initiative or player order
    std::sort(queue.begin(), queue.end(), 
              [](const auto& a, const auto& b) { return a.initiative > b.initiative; });
}

// --- FIX 5d: AI should still attack eliminated player's roaming heroes ---
// In the AI threat map, don't ignore "dead" players who still have heroes
void AIThreatMap::update(const Game& game) {
    for (const auto& p : game.players) {
        if (p.id == my_player_id) continue;

        // OLD: if (!p.is_alive) continue;
        // NEW: threat exists if they have heroes OR towns
        if (p.hero_count == 0 && p.town_count == 0) continue;

        for (const auto& hero : p.heroes) {
            addThreat(hero.position, hero.army_strength, p.id);
        }
    }
}
