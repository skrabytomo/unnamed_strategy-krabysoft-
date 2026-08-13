// ============================================================
// PATCH 4: SCORE_TABLE.cpp
// Shows ALL players (alive & dead), sorted, with largest army
// Apply to: src/ui/WatchModeOverlay.cpp  or EconomyReporter.cpp
// ============================================================

#include <algorithm>
#include <vector>
#include <string>

struct PlayerScoreLine {
    int player_id;
    std::string name;
    std::string personality;
    int strength;
    int heroes;
    int towns;
    int mines;
    int gold;
    int largest_army;      // --- NEW ---
    bool is_alive;
    bool is_watched;
    bool is_eliminated_recently;
};

// --- NEW: Compute largest army for a player ---
int computeLargestArmy(const Player& p, const World& world) {
    int max_stack = 0;
    for (const auto& hero : world.heroes) {
        if (hero.owner_id != p.id) continue;
        int stack_size = 0;
        for (const auto& stack : hero.army_stacks) {
            stack_size += stack.count;
        }
        if (stack_size > max_stack) max_stack = stack_size;
    }
    // Also check garrisons
    for (const auto& town : world.towns) {
        if (town.owner_id != p.id) continue;
        int garrison_size = 0;
        for (const auto& stack : town.garrison) {
            garrison_size += stack.count;
        }
        if (garrison_size > max_stack) max_stack = garrison_size;
    }
    return max_stack;
}

// --- REPLACEMENT: printEconomyReport() ---
void EconomyReporter::printWeeklyReport(const Game& game) {
    std::vector<PlayerScoreLine> lines;

    for (const auto& p : game.players) {
        PlayerScoreLine line;
        line.player_id = p.id;
        line.name = p.name.empty() ? "P" + std::to_string(p.id) : p.name;
        line.personality = personalityToString(p.ai_personality);
        line.strength = p.total_strength;
        line.heroes = p.hero_count;
        line.towns = p.town_count;
        line.mines = (int)p.mines.size();
        line.gold = p.gold;
        line.is_alive = p.is_alive;
        line.is_watched = (p.id == game.watched_player_id);
        line.largest_army = computeLargestArmy(p, game.world);
        lines.push_back(line);
    }

    // --- FIX: Sort by strength descending, but keep watched player pinned if desired ---
    std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) {
        if (a.is_alive != b.is_alive) return a.is_alive > b.is_alive; // alive first
        return a.strength > b.strength;
    });

    log_info("=== WEEK {} ECONOMY REPORT ===", game.current_week);

    // Header
    log_info("{:>4} {:>12} {:>10} {:>7} {:>6} {:>6} {:>8} {:>10} {:>12}",
             "ID", "Name", "Personality", "Str", "Heroes", "Towns", "Mines", "Gold", "LargestArmy");

    for (const auto& l : lines) {
        std::string status_tag;
        if (l.is_watched) status_tag += "[WATCHED] ";
        if (!l.is_alive) status_tag += "[ELIMINATED] ";

        // Color-code alive vs dead in ImGui (if this feeds into UI)
        ImU32 color = l.is_alive 
            ? IM_COL32(200, 255, 200, 255) 
            : IM_COL32(150, 150, 150, 255);

        if (l.is_watched) color = IM_COL32(255, 220, 100, 255); // gold for watched

        log_info("P{:<3} {:>12} {:>10} {:>7} {:>6} {:>6} {:>8} {:>10} {:>12} {}",
                 l.player_id,
                 l.name,
                 l.personality,
                 l.strength,
                 l.heroes,
                 l.towns,
                 l.mines,
                 l.gold,
                 l.largest_army,
                 status_tag);
    }
}

// --- ImGui overlay version (if you use ImGui for the watch HUD) ---
void WatchModeOverlay::drawScoreTable(const Game& game) {
    if (!ImGui::Begin("Scoreboard")) { ImGui::End(); return; }

    ImGui::Text("Week %d, Day %d", game.current_week, game.current_day);
    ImGui::Separator();

    // Table header
    ImGui::Columns(9, "scoreboard_cols", true);
    ImGui::Text("Player"); ImGui::NextColumn();
    ImGui::Text("Type"); ImGui::NextColumn();
    ImGui::Text("Str"); ImGui::NextColumn();
    ImGui::Text("Heroes"); ImGui::NextColumn();
    ImGui::Text("Towns"); ImGui::NextColumn();
    ImGui::Text("Mines"); ImGui::NextColumn();
    ImGui::Text("Gold"); ImGui::NextColumn();
    ImGui::Text("Largest Army"); ImGui::NextColumn();  // --- NEW ---
    ImGui::Text("Status"); ImGui::NextColumn();
    ImGui::Separator();

    std::vector<PlayerScoreLine> lines;
    for (const auto& p : game.players) {
        PlayerScoreLine l;
        l.player_id = p.id;
        l.name = "P" + std::to_string(p.id);
        l.personality = personalityToString(p.ai_personality);
        l.strength = p.total_strength;
        l.heroes = p.hero_count;
        l.towns = p.town_count;
        l.mines = (int)p.mines.size();
        l.gold = p.gold;
        l.is_alive = p.is_alive;
        l.is_watched = (p.id == game.watched_player_id);
        l.largest_army = computeLargestArmy(p, game.world);
        lines.push_back(l);
    }

    std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) {
        if (a.is_alive != b.is_alive) return a.is_alive > b.is_alive;
        return a.strength > b.strength;
    });

    for (const auto& l : lines) {
        ImU32 col = l.is_alive ? IM_COL32(255,255,255,255) : IM_COL32(128,128,128,255);
        if (l.is_watched) col = IM_COL32(255, 220, 80, 255);

        ImGui::PushStyleColor(ImGuiCol_Text, col);

        ImGui::Text("%s %s", l.name.c_str(), l.is_watched ? "*" : ""); ImGui::NextColumn();
        ImGui::Text("%s", l.personality.c_str()); ImGui::NextColumn();
        ImGui::Text("%d", l.strength); ImGui::NextColumn();
        ImGui::Text("%d", l.heroes); ImGui::NextColumn();
        ImGui::Text("%d", l.towns); ImGui::NextColumn();
        ImGui::Text("%d", l.mines); ImGui::NextColumn();
        ImGui::Text("%d", l.gold); ImGui::NextColumn();
        ImGui::Text("%d", l.largest_army); ImGui::NextColumn();  // --- NEW ---
        ImGui::Text("%s", l.is_alive ? "Alive" : "Eliminated"); ImGui::NextColumn();

        ImGui::PopStyleColor();
    }

    ImGui::Columns(1);
    ImGui::End();
}
