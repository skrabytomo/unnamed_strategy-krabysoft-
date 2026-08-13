// ============================================================
// PATCH 3: BOAT_RENDERER.cpp
// Adds visible boat sprites when heroes are embarked
// Apply to: src/render/WorldMapRenderer.cpp  (or HexMapRenderer)
// ============================================================

#include "SpriteBatch.h"
#include "TextureManager.h"
#include "World.h"
#include "Hero.h"

// --- Step 1: Load the boat texture during init ---
void WorldMapRenderer::loadTransportAssets() {
    // Add to your existing texture loading block
    // If you don't have a boat sprite yet, create one or reuse a unit sprite temporarily
    boat_texture = TextureManager::load("assets/sprites/transport_boat.png");
    if (!boat_texture) {
        // Fallback: use a tinted version of an existing sprite or generate a simple quad
        log_warn("Boat texture missing; heroes will use placeholder tint on water.");
        boat_texture = TextureManager::get("assets/terrain/water_0.png"); // temp fallback
    }
}

// --- Step 2: Draw boats in the main render loop ---
void WorldMapRenderer::renderHeroes(const Camera& cam) {
    for (const auto& hero : world.heroes) {
        if (!hero.is_visible && !hero.is_watched) continue;

        Vec2 screen_pos = hexToScreen(hero.position);

        // --- NEW: Draw transport if embarked ---
        if (hero.is_embarked && hero.transport_active) {
            // Boat sits "under" the hero, slightly offset for depth
            Vec2 boat_pos = screen_pos + Vec2(0.0f, 4.0f); // slight y-offset

            // Tint by player color so you know whose boat it is
            Color player_tint = getPlayerColor(hero.owner_id);

            sprite_batch.draw(
                boat_texture,
                boat_pos,
                /* size */ Vec2(32.0f, 32.0f), // adjust to match your hex size
                /* rotation */ 0.0f,
                /* tint */ player_tint,
                /* layer */ RENDER_LAYER_TRANSPORT
            );

            // Optional: draw a small wake/ripple effect
            if (hero.is_moving) {
                drawWakeEffect(screen_pos, hero.facing_direction);
            }
        }

        // Draw hero sprite on top
        sprite_batch.draw(
            hero.texture,
            screen_pos,
            /* size */ Vec2(32.0f, 32.0f),
            /* rotation */ 0.0f,
            /* tint */ Color::WHITE,
            /* layer */ RENDER_LAYER_HERO
        );

        // Draw hero name / level badge
        if (cam.zoom > 0.6f) {
            drawHeroBadge(hero, screen_pos);
        }
    }
}

// --- Step 3: Add wake particles (optional polish) ---
void WorldMapRenderer::drawWakeEffect(Vec2 pos, Direction dir) {
    static float wake_timer = 0.0f;
    wake_timer += delta_time;

    if (wake_timer > 0.3f) { // spawn every 300ms
        wake_timer = 0.0f;
        Particle p;
        p.pos = pos + Vec2(randf(-4,4), randf(-4,4));
        p.velocity = directionToVec(dir) * 5.0f;
        p.life = 0.8f;
        p.color = Color(200, 220, 255, 180);
        p.size = Vec2(6.0f, 6.0f);
        particle_system.spawn(p);
    }
}

// ============================================================
// PATCH 3b: ASSET_CREATION
// Create a minimal boat sprite if missing.
// Save as: assets/sprites/transport_boat.png
// If you can't create art, use this ImGui-based fallback in renderer:
// ============================================================

// --- Fallback if no texture file exists ---
void WorldMapRenderer::drawBoatFallback(Vec2 pos, int owner_id) {
    // Draw a simple colored hexagon using ImGui drawlist or primitive shapes
    Color c = getPlayerColor(owner_id);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    ImVec2 center(pos.x, pos.y);
    float r = 14.0f;

    // Hexagon points
    ImVec2 pts[6];
    for (int i = 0; i < 6; ++i) {
        float angle = i * 3.14159f / 3.0f;
        pts[i] = ImVec2(center.x + r * cosf(angle), center.y + r * sinf(angle));
    }
    dl->AddConvexPolyFilled(pts, 6, IM_COL32(c.r, c.g, c.b, 200));
    dl->AddPolyline(pts, 6, IM_COL32(255,255,255,255), true, 2.0f);

    // Small mast
    dl->AddLine(center, ImVec2(center.x, center.y - 12), IM_COL32(255,255,255,255), 2.0f);
}
