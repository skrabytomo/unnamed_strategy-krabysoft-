#include "Game.h"
#include <imgui.h>
#include <stdio.h>

// ── Editor update ─────────────────────────────────────────────────────────────
void Game::updateEditor(float dt)
{
    (void)dt;
    const auto& mouse = m_input.mouse();
    if (mouse.wheelY != 0.0f)
        m_camera.zoomBy(mouse.wheelY > 0 ? 1.12f : 0.88f);
    if (mouse.middle)
        m_camera.pan(-static_cast<float>(mouse.dx), -static_cast<float>(mouse.dy));

    const float PAN = 200.0f * (1.0f / 60.0f);
    if (m_input.keyHeld(SDLK_LEFT))  m_camera.pan(-PAN, 0);
    if (m_input.keyHeld(SDLK_RIGHT)) m_camera.pan( PAN, 0);
    if (m_input.keyHeld(SDLK_UP))    m_camera.pan(0, -PAN);
    if (m_input.keyHeld(SDLK_DOWN))  m_camera.pan(0,  PAN);

    float wx, wy;
    m_camera.screenToWorld(static_cast<float>(mouse.x),
                           static_cast<float>(mouse.y), wx, wy);
    HexCoord h = m_hexRenderer.grid().worldToHex(wx, wy);
    m_hovered = m_map.inBounds(h) ? h : HexCoord{-999,-999};

    if (mouse.leftDown && !ImGui::GetIO().WantCaptureMouse) {
        if (m_map.inBounds(m_hovered))
            m_editor.onHexClicked(m_hovered, m_map, m_towns,
                                  m_resources, m_heroStarts, m_worldObjects);
    }

    if (m_input.keyDown(SDLK_F3))
        m_simWindow.setOpen(!m_simWindow.isOpen());
}

// ── Editor visual overlay helpers ─────────────────────────────────────────────
// Returns colour for a faction index (0-8)
static ImU32 factionColor(int idx)
{
    static const ImU32 kColors[] = {
        IM_COL32(220,200, 80,220),  // HolyOrder     gold/yellow
        IM_COL32(200, 60, 60,220),  // CrimsonWardens red
        IM_COL32( 60,180, 60,220),  // Thornkin       green
        IM_COL32(100,200,200,220),  // EternalEmpire  cyan
        IM_COL32(160, 40,200,220),  // Bloodsworn     purple
        IM_COL32( 80, 80,200,220),  // Voidkin        blue
        IM_COL32(150,150,150,220),  // IronAssembly   grey
        IM_COL32(220,120, 40,220),  // Amalgamate     orange
        IM_COL32(240,240,240,220),  // Convergence    white
    };
    return kColors[idx % 9];
}

// Returns colour for a resource type (ResourceType enum order)
static ImU32 resourceColor(int typeIdx)
{
    static const ImU32 kColors[] = {
        IM_COL32(255,215,  0,220),  // Gold
        IM_COL32(150,150,170,220),  // Iron
        IM_COL32(220,200,255,220),  // FaithStones
        IM_COL32(200, 60, 60,220),  // BloodEssence
        IM_COL32( 60,200, 80,220),  // VerdantSap
        IM_COL32( 60,220,220,220),  // Mercury
    };
    return kColors[typeIdx % 6];
}

static const char* resourceInitial(int typeIdx)
{
    static const char* kInit[] = { "G","I","F","B","V","M" };
    return kInit[typeIdx % 6];
}

// ── Editor render ─────────────────────────────────────────────────────────────
void Game::renderEditor()
{
    m_hexRenderer.render(m_map, m_camera, m_hovered, {-999,-999}, true);

    beginImGuiFrame();

    // ── Visual overlay: draw entity markers via background draw list ──────────
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const int sw = m_width, sh = m_height;

    // Clip bounds for the overlay (avoid panels: left 200, right 200, top 70)
    const float CLIP_LEFT   = 200.f;
    const float CLIP_RIGHT  = (float)(sw - 200);
    const float CLIP_TOP    = 70.f;
    const float CLIP_BOTTOM = (float)(sh - 20);

    auto inView = [&](float sx, float sy) {
        return sx > -50.f && sx < sw + 50.f && sy > -50.f && sy < sh + 50.f;
    };

    // ── Towns ─────────────────────────────────────────────────────────────────
    for (const auto& town : m_towns) {
        float wx, wy, sx, sy;
        m_hexRenderer.grid().hexToWorld(town.pos, wx, wy);
        m_camera.worldToScreen(wx, wy, sx, sy);
        if (!inView(sx, sy)) continue;

        ImU32 col = factionColor(static_cast<int>(town.faction));
        // Filled rounded rect
        dl->AddRectFilled(
            {sx - 11, sy - 11}, {sx + 11, sy + 11},
            col, 4.0f);
        dl->AddRect(
            {sx - 11, sy - 11}, {sx + 11, sy + 11},
            IM_COL32(255,255,255,180), 4.0f, 0, 1.5f);
        // "T" label
        dl->AddText({sx - 4, sy - 7}, IM_COL32(0,0,0,255), "T");
        // Town name below (clipped)
        if (sy + 14 < CLIP_BOTTOM && sx > CLIP_LEFT && sx < CLIP_RIGHT)
            dl->AddText({sx - (float)(town.name.size() * 3), sy + 13},
                        IM_COL32(255,255,200,220), town.name.c_str());
    }

    // ── Resource nodes ────────────────────────────────────────────────────────
    for (const auto& res : m_resources) {
        float wx, wy, sx, sy;
        m_hexRenderer.grid().hexToWorld(res.pos, wx, wy);
        m_camera.worldToScreen(wx, wy, sx, sy);
        if (!inView(sx, sy)) continue;

        int typeIdx = static_cast<int>(res.type);
        ImU32 col = resourceColor(typeIdx);
        dl->AddCircleFilled({sx, sy}, 10.f, col);
        dl->AddCircle({sx, sy}, 10.f, IM_COL32(255,255,255,150), 12, 1.5f);
        const char* init = resourceInitial(typeIdx);
        dl->AddText({sx - 3, sy - 7}, IM_COL32(0,0,0,255), init);
    }

    // ── Hero starts ───────────────────────────────────────────────────────────
    for (int i = 0; i < (int)m_heroStarts.size(); ++i) {
        float wx, wy, sx, sy;
        m_hexRenderer.grid().hexToWorld(m_heroStarts[i], wx, wy);
        m_camera.worldToScreen(wx, wy, sx, sy);
        if (!inView(sx, sy)) continue;

        // Green triangle (flag shape pointing up)
        dl->AddTriangleFilled(
            {sx,      sy - 14},
            {sx + 10, sy + 6},
            {sx - 10, sy + 6},
            IM_COL32(50, 220, 80, 220));
        dl->AddTriangle(
            {sx,      sy - 14},
            {sx + 10, sy + 6},
            {sx - 10, sy + 6},
            IM_COL32(255,255,255,180), 1.5f);

        char label[16];
        snprintf(label, sizeof(label), "P%d", i + 1);
        dl->AddText({sx - 5, sy + 8}, IM_COL32(255,255,100,220), label);
    }

    // ── World objects ─────────────────────────────────────────────────────────
    for (const auto& wo : m_worldObjects) {
        float wx, wy, sx, sy;
        m_hexRenderer.grid().hexToWorld(wo.pos, wx, wy);
        m_camera.worldToScreen(wx, wy, sx, sy);
        if (!inView(sx, sy)) continue;

        const char* shortName = nullptr;

        switch (wo.type) {
            // Yellow diamonds — treasure/reward objects
            case WorldObjectType::TreasureChest:
            case WorldObjectType::ArtifactChest:
            case WorldObjectType::ResourceCache:
            case WorldObjectType::XPShrine: {
                ImU32 col = IM_COL32(240, 200, 40, 220);
                float r = 9.f;
                dl->AddQuadFilled(
                    {sx,     sy - r},
                    {sx + r, sy    },
                    {sx,     sy + r},
                    {sx - r, sy    },
                    col);
                dl->AddQuad(
                    {sx,     sy - r},
                    {sx + r, sy    },
                    {sx,     sy + r},
                    {sx - r, sy    },
                    IM_COL32(255,255,255,160), 1.5f);
                shortName = (wo.type == WorldObjectType::TreasureChest) ? "TC"
                          : (wo.type == WorldObjectType::ArtifactChest) ? "AC"
                          : (wo.type == WorldObjectType::ResourceCache)  ? "RC"
                          : "XP";
                dl->AddText({sx - 5, sy - 7}, IM_COL32(0,0,0,255), shortName);
                shortName = nullptr;  // label already drawn
                break;
            }

            // Cyan circles — special locations
            case WorldObjectType::WitchHut:
            case WorldObjectType::Stables:
            case WorldObjectType::TreeOfKnowledge:
            case WorldObjectType::StatShrine:
            case WorldObjectType::HolyFountain:
            case WorldObjectType::Oasis:
            case WorldObjectType::Campfire:
            case WorldObjectType::ForestShrine:
            case WorldObjectType::HighlandRuin:
            case WorldObjectType::LavaCrystal:
            case WorldObjectType::SwampAltar:
            case WorldObjectType::SpellScroll:
            case WorldObjectType::Observatory:
            case WorldObjectType::Landmark: {
                dl->AddCircleFilled({sx, sy}, 9.f, IM_COL32(40, 210, 210, 220));
                dl->AddCircle({sx, sy}, 9.f, IM_COL32(255,255,255,160), 12, 1.5f);
                switch (wo.type) {
                    case WorldObjectType::WitchHut:        shortName = "WH"; break;
                    case WorldObjectType::Stables:         shortName = "ST"; break;
                    case WorldObjectType::TreeOfKnowledge: shortName = "TK"; break;
                    case WorldObjectType::StatShrine:      shortName = "SS"; break;
                    case WorldObjectType::HolyFountain:    shortName = "HF"; break;
                    case WorldObjectType::Oasis:           shortName = "OA"; break;
                    case WorldObjectType::Campfire:        shortName = "CF"; break;
                    case WorldObjectType::ForestShrine:    shortName = "FS"; break;
                    case WorldObjectType::HighlandRuin:    shortName = "HR"; break;
                    case WorldObjectType::LavaCrystal:     shortName = "LC"; break;
                    case WorldObjectType::SwampAltar:      shortName = "SA"; break;
                    case WorldObjectType::SpellScroll:     shortName = "SC"; break;
                    case WorldObjectType::Observatory:     shortName = "OB"; break;
                    case WorldObjectType::Landmark:        shortName = "LM"; break;
                    default: shortName = "??"; break;
                }
                break;
            }

            // Red squares — dangerous/guarded
            case WorldObjectType::Crypt:
            case WorldObjectType::Utopia:
            case WorldObjectType::CursedGround:
            case WorldObjectType::BanditCamp: {
                dl->AddRectFilled({sx - 9, sy - 9}, {sx + 9, sy + 9},
                                  IM_COL32(200, 40, 40, 220));
                dl->AddRect({sx - 9, sy - 9}, {sx + 9, sy + 9},
                            IM_COL32(255,255,255,160), 0.f, 0, 1.5f);
                switch (wo.type) {
                    case WorldObjectType::Crypt:       shortName = "CR"; break;
                    case WorldObjectType::Utopia:      shortName = "UT"; break;
                    case WorldObjectType::CursedGround:shortName = "CG"; break;
                    case WorldObjectType::BanditCamp:  shortName = "BC"; break;
                    default: shortName = "??"; break;
                }
                break;
            }

            // Orange circles — dwellings/outposts
            case WorldObjectType::UnitDwelling:
            case WorldObjectType::NeutralOutpost: {
                dl->AddCircleFilled({sx, sy}, 9.f, IM_COL32(230, 130, 30, 220));
                dl->AddCircle({sx, sy}, 9.f, IM_COL32(255,255,255,160), 12, 1.5f);
                shortName = (wo.type == WorldObjectType::UnitDwelling) ? "UD" : "NO";
                break;
            }

            // Magenta circles — quests
            case WorldObjectType::QuestGiver:
            case WorldObjectType::QuestTarget: {
                dl->AddCircleFilled({sx, sy}, 9.f, IM_COL32(220, 60, 220, 220));
                dl->AddCircle({sx, sy}, 9.f, IM_COL32(255,255,255,160), 12, 1.5f);
                shortName = (wo.type == WorldObjectType::QuestGiver) ? "QG" : "QT";
                break;
            }

            case WorldObjectType::Barrier: {
                dl->AddRectFilled({sx - 9, sy - 9}, {sx + 9, sy + 9},
                                  IM_COL32(80, 70, 60, 220));
                dl->AddRect({sx - 9, sy - 9}, {sx + 9, sy + 9},
                            IM_COL32(200, 190, 170, 220), 0.f, 0, 2.0f);
                dl->AddLine({sx - 6, sy - 6}, {sx + 6, sy + 6}, IM_COL32(200, 190, 170, 220), 2.0f);
                dl->AddLine({sx + 6, sy - 6}, {sx - 6, sy + 6}, IM_COL32(200, 190, 170, 220), 2.0f);
                break;
            }
            case WorldObjectType::ChokeGuard: {
                // Dark-red shield shape with "CG" label
                ImVec2 pts[5] = {
                    {sx,       sy - 11.f},
                    {sx + 9.f, sy -  5.f},
                    {sx + 9.f, sy +  4.f},
                    {sx,       sy + 11.f},
                    {sx - 9.f, sy +  4.f},
                };
                dl->AddConvexPolyFilled(pts, 5, IM_COL32(120, 25, 25, 220));
                dl->AddPolyline(pts, 5, IM_COL32(220, 80, 80, 220), ImDrawFlags_Closed, 1.5f);
                shortName = "CG";
                break;
            }
            case WorldObjectType::Shipyard: {
                // Blue anchor icon (simplified)
                dl->AddCircleFilled({sx, sy}, 9.f, IM_COL32(30, 80, 180, 220));
                dl->AddCircle({sx, sy}, 9.f, IM_COL32(130, 190, 255, 220), 12, 1.5f);
                shortName = "SY";
                break;
            }
            case WorldObjectType::FishingHouse: {
                // Green house icon
                dl->AddRectFilled({sx - 7, sy - 3}, {sx + 7, sy + 8},
                                  IM_COL32(30, 130, 60, 220));
                ImVec2 roof[3] = {{sx - 9.f, sy - 3.f}, {sx, sy - 11.f}, {sx + 9.f, sy - 3.f}};
                dl->AddConvexPolyFilled(roof, 3, IM_COL32(20, 100, 40, 220));
                shortName = "FH";
                break;
            }
            default: {
                dl->AddCircleFilled({sx, sy}, 7.f, IM_COL32(180, 180, 180, 200));
                shortName = "?";
                break;
            }
        }

        // Draw abbreviation text (if not already drawn inline)
        if (shortName) {
            dl->AddText({sx - 5, sy - 7}, IM_COL32(255, 255, 255, 255), shortName);
        }

        // Short type label below marker (only within clip bounds)
        if (sy + 12 < CLIP_BOTTOM && sx > CLIP_LEFT && sx < CLIP_RIGHT) {
            // Reuse shortName for label
        }
    }

    m_editor.renderImGui(m_map, m_towns, m_resources, m_heroStarts, m_worldObjects);
    m_simWindow.render();
    endImGuiFrame();
}

// ── State transitions ─────────────────────────────────────────────────────────
void Game::enterEditor()
{
    m_state = GameState::Editor;
    gLog("Entered map editor (F2 to exit)\n");
}

void Game::exitEditor()
{
    m_state = GameState::WorldMap;
    gLog("Exited map editor\n");
}
