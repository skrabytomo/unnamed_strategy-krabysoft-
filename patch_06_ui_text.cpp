// ============================================================
// PATCH 6: UI_MISSING_TEXT_FIX.cpp
// Fixes empty bottom-right panel in watch mode
// Apply to: src/ui/WatchModeOverlay.cpp  or WorldMapHUD.cpp
// ============================================================

#include "imgui.h"
#include "Game.h"
#include "Hero.h"
#include "Town.h"

// --- The bottom-right panel likely draws selected entity details ---
void WatchModeOverlay::drawEntityDetailsPanel(const Game& game) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 320, io.DisplaySize.y - 240), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_Always);

    if (!ImGui::Begin("World Info", nullptr, 
                      ImGuiWindowFlags_NoCollapse | 
                      ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }

    // --- FIX: Guard against null selected entity ---
    if (!game.selected_entity && !game.hovered_entity) {
        ImGui::TextUnformatted("Hover over a hero, town, or mine to see details.");
        ImGui::TextUnformatted("Watch mode: AI turns are simulated automatically.");
        ImGui::End();
        return;
    }

    const Entity* ent = game.selected_entity ? game.selected_entity : game.hovered_entity;

    switch (ent->type) {
    case EntityType::HERO: {
        const Hero* h = static_cast<const Hero*>(ent);
        ImGui::Text("Hero: %s", h->name.empty() ? "Unnamed" : h->name.c_str());
        ImGui::Text("Level %d  |  XP: %d", h->level, h->xp);
        ImGui::Text("Army Strength: %d", h->army_strength);
        ImGui::Separator();
        ImGui::Text("Movement: %d/%d", h->movement_points, h->max_movement);
        if (h->is_embarked) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ON BOAT]");
        }
        if (!h->goal_description.empty()) {
            ImGui::Text("Goal: %s", h->goal_description.c_str());
        } else {
            ImGui::Text("Goal: Idle / Garrison");
        }
        break;
    }

    case EntityType::TOWN: {
        const Town* t = static_cast<const Town*>(ent);
        ImGui::Text("Town: %s", t->name.c_str());
        ImGui::Text("Owner: P%d (%s)", t->owner_id, 
                    game.getPlayer(t->owner_id).name.c_str());
        ImGui::Text("Buildings: %zu", t->buildings.size());
        ImGui::Text("Garrison: %d units", t->garrison_total_units);
        if (t->has_shipyard) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Has Shipyard");
        }
        break;
    }

    case EntityType::MINE: {
        const Mine* m = static_cast<const Mine*>(ent);
        ImGui::Text("Resource Mine");
        ImGui::Text("Type: %s", resourceTypeToString(m->resource_type));
        if (m->owner_id >= 0) {
            ImGui::Text("Owner: P%d", m->owner_id);
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Unclaimed (guarded)");
        }
        ImGui::Text("Output: +%d/week", m->weekly_output);
        break;
    }

    default: {
        // --- FIX: Never leave blank ---
        ImGui::TextUnformatted("Unknown entity selected.");
        break;
    }
    }

    ImGui::End();
}

// --- FIX: Also ensure the top-bar watched-player info doesn't disappear ---
void WatchModeOverlay::drawTopBar(const Game& game) {
    const Player& watched = game.getPlayer(game.watched_player_id);

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 40), ImGuiCond_Always);

    ImGui::Begin("TopBar", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | 
                 ImGuiWindowFlags_NoResize | 
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar);

    // --- FIX: Show watched player even if eliminated ---
    std::string status = watched.is_alive ? "Active" : "ELIMINATED (Spectating)";
    ImU32 status_color = watched.is_alive ? IM_COL32(100, 255, 100, 255) 
                                           : IM_COL32(255, 80, 80, 255);

    ImGui::Text("WATCHING: P%d %s | Status: ", watched.id, watched.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImColor(status_color), "%s", status.c_str());

    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    ImGui::Text("Week %d, Day %d | Speed: %.1fx", 
                game.current_week, game.current_day, game.simulation_speed);

    ImGui::End();
}
