#include "Game.h"
#include "../meta/ScoreDB.h"
#include "../data/SaveLoad.h"
#include "../hero/HeroClass.h"
#include "../sim/ArmyBuilder.h"
#include <imgui.h>
#include <string>
#include <cstdio>
#include <cstdlib>

// ── Save slot metadata ────────────────────────────────────────────────────────
struct SlotMeta {
    bool        exists      = false;
    std::string heroName;
    std::string factionName;
    int         day         = 0;
    int         week        = 0;
    bool        isCampaign  = false;
    int         missionIdx  = 0;
};

static const char* factionShortName(int factionId)
{
    switch (factionId) {
    case 0: return "Holy Order";
    case 1: return "Crimson Wardens";
    case 2: return "Thornkin";
    case 3: return "Eternal Empire";
    case 4: return "Bloodsworn";
    case 5: return "Voidkin";
    case 6: return "Iron Assembly";
    case 7: return "Amalgamate";
    case 8: return "Convergence";
    default: return "Unknown";
    }
}

// Unreferenced today — scaffolding for a save-slot picker that shows
// day/week/hero/faction per slot. Left in place (not deleted) because the
// struct above describes exactly what such a picker needs.
[[maybe_unused]] static SlotMeta readSlotMeta(const std::string& path)
{
    SlotMeta m;
    GameSaveData data;
    if (!SaveLoad::loadGame(path, data)) return m;
    m.exists      = true;
    m.day         = data.day;
    m.week        = data.week;
    m.isCampaign  = data.campaign.active;
    m.missionIdx  = data.campaign.missionIdx;
    if (!data.heroes.empty()) {
        m.heroName    = data.heroes[0].name;
        m.factionName = factionShortName(data.heroes[0].faction);
    } else {
        m.heroName    = "Unknown";
        m.factionName = "";
    }
    return m;
}

void Game::updateMainMenu(float dt) { (void)dt; }

void Game::renderMainMenu()
{
    beginImGuiFrame();

    ImGuiIO& io = ImGui::GetIO();
    float cx = io.DisplaySize.x * 0.5f;
    float cy = io.DisplaySize.y * 0.5f;

    // Full-screen menu backdrop (behind the panel) — aspect-correct cover at any
    // resolution, with a gentle scrim so the panel stays readable over bright art.
    drawMenuBackdrop(io.DisplaySize.x, io.DisplaySize.y, 70);

    ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    // The New Game (1) and Watch AI (6) screens host the multi-column player
    // lobby — 400px squeezed the faction/hero cells down to postage stamps.
    // Give those two screens a wide window so the picker cells read at HUD
    // scale; every other menu screen stays the compact 400px column.
    bool wideLobby = (m_menuMode == 1 || m_menuMode == 6);
    ImGui::SetNextWindowSize(ImVec2(wideLobby ? 660.0f : 400.0f, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;

    if (!ImGui::Begin("##mainmenu", nullptr, wf)) { ImGui::End(); endImGuiFrame(); return; }

    float bw = ImGui::GetWindowWidth() - 32.0f;

    auto header = [&](const char* text) {
        float tw = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - tw) * 0.5f);
        ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "%s", text);
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    };

    // ── New Game / Watch AI setup body — IDENTICAL for both: map size,
    // faction + hero class, difficulty, and the 2-4 player slot lobby
    // (Human/Bot toggle, faction cycle, starting bonus). Watch mode only
    // adds the auto-advance speed slider and starts in spectator mode —
    // slot 0 stays the "human" slot internally (startNewGame requires one)
    // but m_watchingAI auto-plays it, so nobody needs to touch the controls.
    auto renderSetupBody = [&](bool watchMode) {
        // Map size
        ImGui::Text("Map Size:");
        static const char* kMapSizeLabels[] = { "Small (24)", "Medium (36)", "Large (52)", "XLarge (72)" };
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine();
            bool sel = (m_newGameMapSize == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.3f, 0.1f, 1.f));
            char msLbl[32]; std::snprintf(msLbl, sizeof(msLbl), "%s##ms%d", kMapSizeLabels[i], i);
            if (ImGui::Button(msLbl, ImVec2((bw - 6) / 4.f, 26))) m_newGameMapSize = i;
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        // Map shape — layout/topology, independent of size. Hexagon is a
        // wide-open circular island (no chokepoints); the Jebus Cross shapes
        // connect zones through narrow guarded bridges instead.
        ImGui::Text("Map Shape:");
        static const char* kShapeNames[] = { "Hexagon", "Jebus Cross", "Jebus Cross 3", "Ring" };
        static const char* kShapeTooltips[] = {
            "Open circular island. No chokepoints — every zone borders every other.",
            "Zones connected by narrow water bridges, each guarded by a chokepoint monster stack.",
            "Jebus Cross plus a rich, heavily-guarded sacred zone at the map centre.",
            "Donut-shaped: players ring a central water body.",
        };
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine();
            bool sel = (m_newGameMapShape == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.3f, 0.1f, 1.f));
            char shLbl[32]; std::snprintf(shLbl, sizeof(shLbl), "%s##sh%d", kShapeNames[i], i);
            if (ImGui::Button(shLbl, ImVec2((bw - 6) / 4.f, 26))) m_newGameMapShape = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kShapeTooltips[i]);
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        // Difficulty
        ImGui::Text("Difficulty:");
        static const char* kDiffNames[]    = { "Easy", "Normal", "Hard" };
        static const char* kDiffTooltips[] = {
            "Easy: +2 ATK/DEF hero, full starting resources, timid AI. Good for learning.",
            "Normal: You start with 90% of the AI's resources. Same economy rules. Recommended.",
            "Hard: You start with 80% of the AI's resources; the AI is more aggressive and fields more heroes."
        };
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            bool sel = (m_newGameDifficulty == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                i == 0 ? ImVec4(0.1f, 0.4f, 0.1f, 1.f) :
                i == 1 ? ImVec4(0.4f, 0.3f, 0.1f, 1.f) :
                         ImVec4(0.5f, 0.1f, 0.1f, 1.f));
            char dlbl[24]; std::snprintf(dlbl, sizeof(dlbl), "%s##df%d", kDiffNames[i], i);
            if (ImGui::Button(dlbl, ImVec2((bw - 4) / 3.f, 26))) m_newGameDifficulty = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kDiffTooltips[i]);
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        // ── Player slots (classic-strategy-style lobby) ─────────────────────────────────
        ImGui::Text("Players:");
        constexpr int kNumPlayerChoices = MAX_SETUP_SLOTS - 1; // 2..8
        float pcw = (bw - 4.0f * (kNumPlayerChoices - 1)) / kNumPlayerChoices;
        for (int pc = 2; pc <= MAX_SETUP_SLOTS; ++pc) {
            if (pc > 2) ImGui::SameLine(0, 4);
            bool sel = (m_setupPlayerCount == pc);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.f));
            char pcl[16]; std::snprintf(pcl, sizeof(pcl), "%d##pcnt%d", pc, pc);
            if (ImGui::Button(pcl, ImVec2(pcw, 26))) m_setupPlayerCount = pc;
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        {
            static const char* kSlotFacNames[] = {
                "Holy Order","Crimson Wardens","Thornkin","Eternal Empire",
                "Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence",
                "Random"
            };
            static const char* kBonusNames[] = {
                "Artifact", "+5 Resource", "+1500 Gold"
            };
            // Real in-game icons from the resource atlas (assets/icons.png,
            // 8x6 grid): Gold = cell 32, the selected faction's key resource
            // for "+5 Resource", and the treasure-chest (cell 6) for Artifact.
            // Falls back to a coloured dot only if the atlas failed to load.
            auto factionResAtlas = [](int f) -> int {
                switch (f) {
                    case 0: case 1: return 34;   // Holy Order / Crimson Wardens → Faith Stones
                    case 2: case 5: return 36;   // Thornkin / Voidkin          → Verdant Sap
                    case 3: case 8: return 37;   // Eternal Empire / Convergence → Mercury
                    case 4: case 7: return 35;   // Bloodsworn / Amalgamate      → Blood Essence
                    case 6:         return 33;   // Iron Assembly                → Iron
                    default:        return 33;   // Random/unknown               → Iron (game default)
                }
            };
            auto drawBonusIcon = [this, factionResAtlas]
                                 (ImDrawList* dl, ImVec2 tl, float sz, int b, int fac) {
                int atlas = (b == 0) ? 6 : (b == 2) ? 32 : factionResAtlas(fac);
                if (m_iconTex.ok()) {
                    float col = (float)(atlas % 8), row = (float)(atlas / 8);
                    ImVec2 uv0(col / 8.f, row / 6.f), uv1((col + 1.f) / 8.f, (row + 1.f) / 6.f);
                    dl->AddImage((ImTextureID)(uintptr_t)m_iconTex.id(),
                                 tl, {tl.x + sz, tl.y + sz}, uv0, uv1);
                    return;
                }
                ImVec2 c = { tl.x + sz * 0.5f, tl.y + sz * 0.5f };
                dl->AddCircleFilled(c, sz * 0.34f,
                    b == 0 ? IM_COL32(180,120,235,255)
                  : b == 2 ? IM_COL32(230,190,60,255)
                           : IM_COL32(90,200,120,255));
            };
            m_slotType[0]    = 0;                    // slot 0 is always you
            m_slotFaction[0] = m_newGameFaction;     // kept in sync for startNewGame
            // Column layout: [type] [faction] [hero] [bonus] [team swatch]
            // 34 was too small to read the crest/portrait art at all —
            // "the town selection and hero is too small" — the cell art is the
            // whole point of the picker now, so give it room. 64px matches the
            // in-game hero-panel portrait scale, which is the size the player
            // asked for ("similar to ingame huds size").
            float rowH   = 64.f;
            float typeW  = (bw - 16) * 0.13f;
            float facW   = (bw - 16) * 0.27f;
            float heroW  = (bw - 16) * 0.27f;
            float bonW   = (bw - 16) * 0.23f;
            float teamW  = (bw - 16) * 0.10f;

            // Header row
            ImGui::TextDisabled("%-*s", 0, "");
            {
                ImGui::Dummy(ImVec2(typeW, 1)); ImGui::SameLine(0,4);
                ImGui::TextDisabled("Faction"); ImGui::SameLine(typeW + facW * 0.5f);
                ImGui::TextDisabled("Hero");    ImGui::SameLine(typeW + facW + heroW * 0.5f);
                ImGui::TextDisabled("Bonus");   ImGui::SameLine(typeW + facW + heroW + bonW * 0.5f);
                ImGui::TextDisabled("Ally");
            }

            for (int s = 0; s < m_setupPlayerCount; ++s) {
                ImGui::PushID(s);
                uint32_t ownerId = (uint32_t)(s + 1);

                // ── Human / Bot / You ─────────────────────────────────────────
                if (s == 0) {
                    ImGui::Button("You", ImVec2(typeW, rowH));
                } else {
                    bool isHuman = (m_slotType[s] == 0);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        isHuman ? ImVec4(0.1f, 0.3f, 0.6f, 1.f) : ImVec4(0.45f, 0.2f, 0.1f, 1.f));
                    if (ImGui::Button(isHuman ? "Human" : "Bot", ImVec2(typeW, rowH)))
                        m_slotType[s] = isHuman ? 1 : 0;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to switch Human/Bot. Humans play hot-seat.");
                    ImGui::PopStyleColor();
                }

                // ── Faction: clickable crest opens a PICTURE GRID picker ──────
                // (was a tiny crest + text combo — "the combobox never was
                // replaced with actual picture choosing")
                ImGui::SameLine(0, 4);
                {
                    int fsel = std::clamp(m_slotFaction[s], 0, 9);
                    char popId[24]; std::snprintf(popId, sizeof(popId), "##facpick%d", s);

                    // The whole cell is one button: crest + name.
                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    if (ImGui::Button(popId, ImVec2(facW, rowH)))   // "##" prefix = no label
                        ImGui::OpenPopup(popId);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to choose faction");
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float ic = rowH - 4;
                    if (fsel < 9 && m_townTex[fsel].ok())
                        dl->AddImage((ImTextureID)(uintptr_t)m_townTex[fsel].id(),
                                     {cur.x + 2, cur.y + 2}, {cur.x + 2 + ic, cur.y + 2 + ic});
                    else
                        dl->AddRectFilled({cur.x + 2, cur.y + 2}, {cur.x + 2 + ic, cur.y + 2 + ic},
                                          IM_COL32(70,70,80,255), 3.f);
                    dl->AddText({cur.x + ic + 8, cur.y + rowH * 0.5f - 7},
                                IM_COL32(230,230,230,255), kSlotFacNames[fsel]);

                    // Picture-grid popup: 5 crest tiles per row, name underneath.
                    if (ImGui::BeginPopup(popId)) {
                        const float tile = 84.f, cell = tile + 10.f;
                        for (int fi = 0; fi < 10; ++fi) {
                            if (fi % 5) ImGui::SameLine();
                            ImGui::BeginGroup();
                            ImGui::PushID(fi);
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            bool sel = (fsel == fi);
                            if (ImGui::InvisibleButton("##t", ImVec2(cell, tile + 20))) {
                                m_slotFaction[s] = fi;
                                m_slotClassId[s] = 0;               // reset hero on faction change
                                if (s == 0) { m_newGameFaction = fi; m_newGameClassId = 0; }
                                ImGui::CloseCurrentPopup();
                            }
                            bool hov = ImGui::IsItemHovered();
                            ImDrawList* pd = ImGui::GetWindowDrawList();
                            // Tile background / crest art (fi==9 is Random)
                            if (fi < 9 && m_townTex[fi].ok())
                                pd->AddImage((ImTextureID)(uintptr_t)m_townTex[fi].id(),
                                             {p.x + 5, p.y}, {p.x + 5 + tile, p.y + tile});
                            else {
                                pd->AddRectFilled({p.x + 5, p.y}, {p.x + 5 + tile, p.y + tile},
                                                  IM_COL32(60,60,74,255), 4.f);
                                pd->AddText({p.x + 5 + tile * 0.34f, p.y + tile * 0.4f},
                                            IM_COL32(200,200,200,255), "?");
                            }
                            // Selection / hover frame
                            if (sel)
                                pd->AddRect({p.x + 4, p.y - 1}, {p.x + 6 + tile, p.y + tile + 1},
                                            IM_COL32(255,215,80,255), 4.f, 0, 3.f);
                            else if (hov)
                                pd->AddRect({p.x + 4, p.y - 1}, {p.x + 6 + tile, p.y + tile + 1},
                                            IM_COL32(160,200,255,200), 4.f, 0, 2.f);
                            // Centred name under the tile
                            float tw = ImGui::CalcTextSize(kSlotFacNames[fi]).x;
                            pd->AddText({p.x + 5 + (tile - tw) * 0.5f, p.y + tile + 3},
                                        sel ? IM_COL32(255,215,80,255) : IM_COL32(210,210,210,255),
                                        kSlotFacNames[fi]);
                            ImGui::PopID();
                            ImGui::EndGroup();
                        }
                        ImGui::EndPopup();
                    }
                }

                // ── Hero: clickable portrait opens a PICTURE GRID picker ──────
                ImGui::SameLine(0, 4);
                {
                    int fsel = std::clamp(m_slotFaction[s], 0, 8);
                    auto classes = m_classRegistry.getClassesForFaction((FactionId)fsel);
                    if (!classes.empty()) {
                        bool ok = false;
                        for (auto* c : classes) if (c->id == m_slotClassId[s]) ok = true;
                        if (!ok) m_slotClassId[s] = classes[0]->id;
                    }
                    const char* curName = "(random)";
                    for (auto* c : classes) if (c->id == m_slotClassId[s]) curName = c->name.c_str();

                    char popId[24]; std::snprintf(popId, sizeof(popId), "##heropick%d", s);
                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    if (ImGui::Button(popId, ImVec2(heroW, rowH)) && !classes.empty())
                        ImGui::OpenPopup(popId);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to choose hero class");
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float ic = rowH - 4;
                    // portrait = this class's HERO PORTRAIT (per-class so different
                    // classes of a faction show different faces)
                    unsigned pid = portraitTexIdFor(fsel, (uint32_t)m_slotClassId[s]);
                    if (m_slotFaction[s] < 9 && pid)
                        dl->AddImage((ImTextureID)(uintptr_t)pid,
                                     {cur.x + 2, cur.y + 2}, {cur.x + 2 + ic, cur.y + 2 + ic});
                    else
                        dl->AddRectFilled({cur.x + 2, cur.y + 2}, {cur.x + 2 + ic, cur.y + 2 + ic},
                                          IM_COL32(60,70,60,255), 3.f);
                    dl->AddText({cur.x + ic + 8, cur.y + rowH * 0.5f - 7},
                                IM_COL32(230,230,230,255), curName);

                    // Portrait-grid popup: one tile per class of this faction,
                    // each showing that class's own portrait (keyed by class id
                    // across the faction's portrait set) plus name + specialty.
                    if (ImGui::BeginPopup(popId)) {
                        const float tile = 84.f, cell = tile + 10.f;
                        int i = 0;
                        for (auto* c : classes) {
                            if (i++ % 4) ImGui::SameLine();
                            ImGui::PushID(c->id);
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            bool sel = (m_slotClassId[s] == c->id);
                            if (ImGui::InvisibleButton("##h", ImVec2(cell, tile + 20))) {
                                m_slotClassId[s] = c->id;
                                if (s == 0) m_newGameClassId = c->id;
                                ImGui::CloseCurrentPopup();
                            }
                            bool hov = ImGui::IsItemHovered();
                            if (hov && !c->specialtyDesc.empty())
                                ImGui::SetTooltip("Specialty: %s", c->specialtyDesc.c_str());
                            ImDrawList* pd = ImGui::GetWindowDrawList();
                            unsigned tpid = portraitTexIdFor(fsel, (uint32_t)c->id);
                            if (tpid)
                                pd->AddImage((ImTextureID)(uintptr_t)tpid,
                                             {p.x + 5, p.y}, {p.x + 5 + tile, p.y + tile});
                            else
                                pd->AddRectFilled({p.x + 5, p.y}, {p.x + 5 + tile, p.y + tile},
                                                  IM_COL32(60,70,60,255), 4.f);
                            if (sel)
                                pd->AddRect({p.x + 4, p.y - 1}, {p.x + 6 + tile, p.y + tile + 1},
                                            IM_COL32(255,215,80,255), 4.f, 0, 3.f);
                            else if (hov)
                                pd->AddRect({p.x + 4, p.y - 1}, {p.x + 6 + tile, p.y + tile + 1},
                                            IM_COL32(160,200,255,200), 4.f, 0, 2.f);
                            float tw = ImGui::CalcTextSize(c->name.c_str()).x;
                            pd->AddText({p.x + 5 + (tile - tw) * 0.5f, p.y + tile + 3},
                                        sel ? IM_COL32(255,215,80,255) : IM_COL32(210,210,210,255),
                                        c->name.c_str());
                            ImGui::PopID();
                        }
                        ImGui::EndPopup();
                    }
                }

                // ── Starting bonus: clickable icon opens a PICTURE GRID ───────
                // (was a plain combobox — "the resource is still combobox not
                // same as the first 2"; now it matches faction/hero.)
                ImGui::SameLine(0, 4);
                {
                    int bsel = std::clamp(m_slotBonus[s], 0, 2);
                    char popId[24]; std::snprintf(popId, sizeof(popId), "##bonpick%d", s);

                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    if (ImGui::Button(popId, ImVec2(bonW, rowH)))
                        ImGui::OpenPopup(popId);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Starting bonus: random artifact, +5 key resource, or +1500 gold.");
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float ic = rowH - 4;
                    int fac = std::clamp(m_slotFaction[s], 0, 9);
                    drawBonusIcon(dl, {cur.x + 2, cur.y + 2}, ic, bsel, fac);
                    dl->AddText({cur.x + ic + 8, cur.y + rowH * 0.5f - 7},
                                IM_COL32(230,230,230,255), kBonusNames[bsel]);

                    // Picture-grid popup: one tile per bonus, name underneath.
                    if (ImGui::BeginPopup(popId)) {
                        const float tile = 84.f, cell = tile + 10.f;
                        for (int bi = 0; bi < 3; ++bi) {
                            if (bi) ImGui::SameLine();
                            ImGui::PushID(bi);
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            bool sel = (bsel == bi);
                            if (ImGui::InvisibleButton("##b", ImVec2(cell, tile + 20))) {
                                m_slotBonus[s] = bi;
                                ImGui::CloseCurrentPopup();
                            }
                            bool hov = ImGui::IsItemHovered();
                            ImDrawList* pd = ImGui::GetWindowDrawList();
                            pd->AddRectFilled({p.x + 5, p.y}, {p.x + 5 + tile, p.y + tile},
                                              IM_COL32(48, 48, 60, 255), 4.f);
                            drawBonusIcon(pd, {p.x + 5, p.y}, tile, bi,
                                          std::clamp(m_slotFaction[s], 0, 9));
                            if (sel)
                                pd->AddRect({p.x + 4, p.y - 1}, {p.x + 6 + tile, p.y + tile + 1},
                                            IM_COL32(255,215,80,255), 4.f, 0, 3.f);
                            else if (hov)
                                pd->AddRect({p.x + 4, p.y - 1}, {p.x + 6 + tile, p.y + tile + 1},
                                            IM_COL32(160,200,255,200), 4.f, 0, 2.f);
                            float tw = ImGui::CalcTextSize(kBonusNames[bi]).x;
                            pd->AddText({p.x + 5 + (tile - tw) * 0.5f, p.y + tile + 3},
                                        sel ? IM_COL32(255,215,80,255) : IM_COL32(210,210,210,255),
                                        kBonusNames[bi]);
                            ImGui::PopID();
                        }
                        ImGui::EndPopup();
                    }
                }

                // ── Alliance: colored swatch (click to cycle FFA / Team 1-4) ──
                ImGui::SameLine(0, 4);
                {
                    // Team 0 (FFA) = this player's own owner color (no alliance).
                    // Team 1-4 = a shared alliance color.
                    static const ImU32 teamCols[5] = {
                        IM_COL32(90, 90, 90, 255),    // FFA — grey (no ally)
                        IM_COL32(90, 160, 255, 255),  // Team 1 — blue
                        IM_COL32(255, 110, 90, 255),  // Team 2 — red
                        IM_COL32(110, 220, 120, 255), // Team 3 — green
                        IM_COL32(230, 200, 70, 255),  // Team 4 — gold
                    };
                    int tsel = std::clamp(m_slotTeam[s], 0, 4);
                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        {cur.x, cur.y}, {cur.x + teamW, cur.y + rowH}, teamCols[tsel], 4.f);
                    ImGui::GetWindowDrawList()->AddRect(
                        {cur.x, cur.y}, {cur.x + teamW, cur.y + rowH}, IM_COL32(220,220,220,180), 4.f, 0, 1.5f);
                    // Label inside the swatch
                    const char* tlabel = (tsel == 0) ? "FFA" : (tsel==1?"A":tsel==2?"B":tsel==3?"C":"D");
                    ImVec2 ts = ImGui::CalcTextSize(tlabel);
                    ImGui::GetWindowDrawList()->AddText(
                        {cur.x + (teamW - ts.x)*0.5f, cur.y + (rowH - ts.y)*0.5f},
                        IM_COL32(20,20,20,255), tlabel);
                    if (ImGui::InvisibleButton("##team", ImVec2(teamW, rowH)))
                        m_slotTeam[s] = (tsel + 1) % 5;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Alliance: FFA = no allies. Same letter = allied team (never fight each other). Click to cycle.");
                    (void)ownerId;
                }

                ImGui::PopID();
            }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        if (watchMode) {
            ImGui::Text("Auto-advance speed:");
            // Same 0.25–8.0 range as the in-game HUD. The menu used to cap at
            // 4x while the map allowed 8x, so the number changed under you the
            // moment the game started.
            static const float kMenuSpeeds[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
            static const char* kMenuSpeedLbl[] = { "0.5x", "1x", "2x", "4x", "8x" };
            float step = (bw - 4.0f * 6.0f) / 5.0f;
            for (int i = 0; i < 5; ++i) {
                if (i) ImGui::SameLine(0, 6);
                bool active = (std::fabs(m_watchAISpeed - kMenuSpeeds[i]) < 0.01f);
                if (active)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.85f, 1.0f));
                if (ImGui::Button(kMenuSpeedLbl[i], ImVec2(step, 26)))
                    m_watchAISpeed = kMenuSpeeds[i];
                if (active) ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1.0 = 1 end-turn per second. Higher = faster.\n"
                                  "You can change this any time from the in-game HUD.");
            ImGui::Spacing();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, watchMode ? ImVec4(0.15f, 0.45f, 0.15f, 1.0f)
                                                          : ImVec4(0.2f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button(watchMode ? "Start Watching" : "Start New Game", ImVec2(bw, 42))) {
            m_activeSaveId = 0; // will create a new row on first save
            startNewGame();
            if (watchMode) {
                m_watchingAI   = true;
                m_fogDisabled  = true;
                m_watchAITimer = 1.0f / m_watchAISpeed;
            }
            m_state    = GameState::WorldMap;
            m_menuMode = 0;
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button(watchMode ? "Back##wai" : "Back##ng", ImVec2(bw, 30))) m_menuMode = 0;
    };

    // ── 0: Main ──────────────────────────────────────────────────────────────
    if (m_menuMode == 0) {
        // Ornate wings-and-compass emblem above the title, matching the
        // ornate button frames below.
        if (m_menuHeaderEmblemTex.ok()) {
            float emW = bw * 0.5f, emH = emW; // source art is square
            ImVec2 cpos = ImGui::GetCursorScreenPos();
            float offX = (bw - emW) * 0.5f;
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(uintptr_t)m_menuHeaderEmblemTex.id(),
                {cpos.x + offX, cpos.y}, {cpos.x + offX + emW, cpos.y + emH});
            ImGui::Dummy({bw, emH});
        }
        header("UNNAMED STRATEGY");

        // Ornate frame-and-icon button — the frame art is a single reusable
        // wide bar (see assets/ui/menu_button_frame.png), icon on the left,
        // gold label text on the right, hit-tested via an invisible button.
        auto fancyButton = [&](const char* label, const Texture& icon) -> bool {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos  = ImGui::GetCursorScreenPos();
            float  h    = 46.0f;
            ImVec2 size = {bw, h};

            if (m_menuButtonFrameTex.ok())
                dl->AddImage((ImTextureID)(uintptr_t)m_menuButtonFrameTex.id(),
                             pos, {pos.x + size.x, pos.y + size.y});
            else
                dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, IM_COL32(40, 32, 20, 255));

            if (icon.ok()) {
                // The icons are medium-toned line-art on transparent bg; drawn
                // straight over the ornate frame they washed out. Give each a
                // dark disc + gold ring "medallion" so it reads clearly and
                // ties into the menu's gold theme.
                float iconSz = h * 0.66f;
                float cx = pos.x + h * 0.5f;
                float cy = pos.y + h * 0.5f;
                float r  = iconSz * 0.62f;
                dl->AddCircleFilled({cx, cy}, r, IM_COL32(15, 12, 8, 205));
                dl->AddCircle({cx, cy}, r, IM_COL32(205, 170, 95, 215), 0, 1.5f);
                float ix = cx - iconSz * 0.5f;
                float iy = cy - iconSz * 0.5f;
                dl->AddImage((ImTextureID)(uintptr_t)icon.id(), {ix, iy}, {ix + iconSz, iy + iconSz});
            }

            ImVec2 tsz = ImGui::CalcTextSize(label);
            float tx = pos.x + h * 1.05f;
            float ty = pos.y + (h - tsz.y) * 0.5f;
            dl->AddText({tx + 1, ty + 1}, IM_COL32(0, 0, 0, 180), label);
            dl->AddText({tx, ty}, IM_COL32(235, 205, 120, 255), label);

            ImGui::SetCursorScreenPos(pos);
            return ImGui::InvisibleButton(label, size);
        };

        if (fancyButton("New Game",   m_menuIconTex[0])) m_menuMode = 1;
        ImGui::Spacing();
        if (fancyButton("Conquest",   m_menuIconTex[2])) m_state = GameState::Conquest;
        ImGui::Spacing();
        if (fancyButton("Load Game",  m_menuIconTex[1])) m_menuMode = 2;
        ImGui::Spacing();
        if (fancyButton("Campaign",   m_menuIconTex[2])) m_menuMode = 4;
        ImGui::Spacing();
        if (fancyButton("Battle Sim", m_menuIconTex[3])) m_menuMode = 5;
        ImGui::Spacing();
        if (fancyButton("Watch AI vs AI", m_menuIconTex[4])) m_menuMode = 6;
        ImGui::Spacing();
        if (fancyButton("High Scores", m_menuIconTex[7])) m_menuMode = 7;
        ImGui::Spacing();
        if (fancyButton("How to Play", m_menuIconTex[2])) m_menuMode = 9;
        ImGui::Spacing();
        if (fancyButton("Settings",   m_menuIconTex[5])) m_menuMode = 3;
        ImGui::Spacing();
        if (fancyButton("Map Editor", m_menuIconTex[6])) { enterEditor(); }
        ImGui::Spacing();
        if (fancyButton("Credits",    m_menuIconTex[2])) m_menuMode = 8;
        ImGui::Spacing();
        if (fancyButton("Quit",       m_menuIconTex[7])) m_running = false;

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextColored({0.4f, 0.4f, 0.4f, 1.0f}, "F5 Save  F9 Load  F2 Editor");
    }
    // ── 1: New Game — setup + slot picker ────────────────────────────────────
    else if (m_menuMode == 1) {
        header("New Game");
        renderSetupBody(false);
    }
    // ── 2: Load Game — slot list ──────────────────────────────────────────────
    else if (m_menuMode == 2) {
        header("Load Game");

        // General saves (5 slots)
        // List all saves from DB, newest first, grouped by type
        auto allSaves = m_saveDB.listAll();
        auto generalSaves  = m_saveDB.list(false);
        auto campaignSaves = m_saveDB.list(true);

        static const char* kMissionNames[] = { "I. The Border Burns", "II. The Thornwood Passage", "III. The Convergence Point" };

        auto renderSaveList = [&](std::vector<SaveEntry>& entries, bool isCampaign) {
            if (entries.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled(isCampaign ? "No campaign saves found." : "No saves found.");
                ImGui::Spacing();
                return;
            }
            float delBtnW = 52.0f;
            for (auto& e : entries) {
                char lbl[256];
                if (isCampaign) {
                    const char* mname = (e.missionIdx >= 0 && e.missionIdx < 3) ? kMissionNames[e.missionIdx] : "?";
                    std::snprintf(lbl, sizeof(lbl), "%s  |  %s  |  %s  Day %d  Week %d##ldc%lld",
                        e.name.c_str(), mname, e.heroName.c_str(), e.day, e.week, (long long)e.id);
                } else {
                    std::snprintf(lbl, sizeof(lbl), "%s  |  %s  (%s)  Day %d  Week %d##ldg%lld",
                        e.name.c_str(), e.heroName.c_str(), e.factionName.c_str(), e.day, e.week, (long long)e.id);
                }
                if (ImGui::Button(lbl, ImVec2(bw - delBtnW - 4, 36))) {
                    if (loadGame(e.id)) {
                        m_state    = GameState::WorldMap;
                        m_menuMode = 0;
                    }
                }
                ImGui::SameLine(0, 4);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.1f, 0.1f, 1.0f));
                char delLbl[32]; std::snprintf(delLbl, sizeof(delLbl), "Del##del%lld", (long long)e.id);
                if (ImGui::Button(delLbl, ImVec2(delBtnW, 36)))
                    m_saveDB.del(e.id);
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
        };

        ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "General Saves");
        ImGui::Separator(); ImGui::Spacing();
        renderSaveList(generalSaves, false);

        ImGui::Spacing();
        ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "Campaign Saves");
        ImGui::Separator(); ImGui::Spacing();
        renderSaveList(campaignSaves, true);

        ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##ld", ImVec2(bw, 30))) m_menuMode = 0;
    }
    // ── 3: Settings ───────────────────────────────────────────────────────────
    else if (m_menuMode == 3) {
        header("Settings");

        ImGui::Text("Audio");
        ImGui::Separator();
        // Music applies on slider RELEASE: setMusicVolume re-queues the current
        // track (pre-mixed at the old volume), so applying per-drag-tick would
        // restart it dozens of times while dragging.
        ImGui::SliderFloat("Music Volume", &m_settingsMasVol, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            m_audio.setMusicVolume(m_settingsMasVol);
        if (ImGui::SliderFloat("SFX Volume",   &m_settingsSfxVol, 0.0f, 1.0f, "%.2f"))
            m_audio.setSfxVolume(m_settingsSfxVol);

        ImGui::Spacing();
        ImGui::Text("Display");
        ImGui::Separator();
        if (ImGui::Checkbox("Fullscreen", &m_settingsFullscreen))
            SDL_SetWindowFullscreen(m_window,
                m_settingsFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

        // Resolution picker (windowed mode only)
        if (!m_settingsFullscreen) {
            static const char* kResLabels[] = { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440" };
            static const int   kResW[]      = { 1280, 1600, 1920, 2560 };
            static const int   kResH[]      = { 720,  900,  1080, 1440 };
            // Determine current index
            int curW, curH;
            SDL_GetWindowSize(m_window, &curW, &curH);
            int resIdx = 0;
            for (int i = 0; i < 4; ++i)
                if (kResW[i] == curW && kResH[i] == curH) { resIdx = i; break; }
            if (ImGui::Combo("Resolution", &resIdx, kResLabels, 4)) {
                SDL_SetWindowSize(m_window, kResW[resIdx], kResH[resIdx]);
                SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
        }

        ImGui::Checkbox("Floating Combat Numbers", &m_settingsShowDmgNums);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show damage numbers floating above units during combat.");

        ImGui::Spacing();
        ImGui::Text("Gameplay");
        ImGui::Separator();
        ImGui::Checkbox("Auto-Save at Week End", &m_settingsAutoSave);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Automatically save to the active slot at the start of each new week.");
        ImGui::SliderFloat("Combat Anim Speed", &m_settingsAnimSpeed, 0.5f, 2.0f, "%.1fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adjust combat animation speed. 1.0 = normal, 2.0 = double speed.");

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        if (ImGui::Button("Save & Back", ImVec2(bw * 0.55f, 34))) {
            saveSettings();
            m_menuMode = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(-1, 34))) {
            loadSettings();   // reload from disk to undo in-session changes
            m_menuMode = 0;
        }
    }
    // ── 4: Campaign ───────────────────────────────────────────────────────────
    else if (m_menuMode == 4) {
        header("CAMPAIGN");

        ImGui::TextWrapped(
            "A three-chapter story spanning the fractured continent of Vael — "
            "forge alliances, betray old friends, and decide the fate of the Convergence."
        );
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Chapter list (informational)
        ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "Chapters:");
        ImGui::TextDisabled("  I.   The Border Burns");
        ImGui::TextDisabled("  II.  The Thornwood Passage");
        ImGui::TextDisabled("  III. The Convergence Point");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button("Start Campaign", ImVec2(bw, 42))) {
            m_menuMode              = 0;
            m_campaignTutorialSeen  = false;   // first time = show tutorial
            m_tutorialStep          = 0;
            enterCampaign();
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::TextColored({0.5f, 0.8f, 0.5f, 1.0f},
            m_campaignTutorialSeen ? "Tutorial already completed." : "First run: a short tutorial will play first.");

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##camp", ImVec2(bw, 30))) m_menuMode = 0;
    }
    // ── 5: Battle Simulator ───────────────────────────────────────────────────
    else if (m_menuMode == 5) {
        header("BATTLE SIMULATOR");

        static const char* kFacNames[] = {
            "Holy Order","Crimson Wardens","Thornkin","Eternal Empire",
            "Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"
        };

        // Week picker
        ImGui::Text("Week:");
        ImGui::SetNextItemWidth(bw);
        ImGui::SliderInt("##simweek", &m_simWeek, 1, 20, "Week %d");

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Faction 1
        ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "Side 1:");
        for (int i = 0; i < 9; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_simFaction1 == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.6f, 1.f));
            char lbl[40]; std::snprintf(lbl, sizeof(lbl), "%s##s1f%d", kFacNames[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 4) / 3.f, 26))) m_simFaction1 = i;
            if (sel) ImGui::PopStyleColor();
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Faction 2
        ImGui::TextColored({1.0f, 0.5f, 0.3f, 1.0f}, "Side 2:");
        for (int i = 0; i < 9; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_simFaction2 == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.2f, 0.1f, 1.f));
            char lbl[40]; std::snprintf(lbl, sizeof(lbl), "%s##s2f%d", kFacNames[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 4) / 3.f, 26))) m_simFaction2 = i;
            if (sel) ImGui::PopStyleColor();
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Siege setup — fight over a town instead of an open field
        ImGui::Text("Battlefield:");
        if (ImGui::RadioButton("Open field", m_simSiegeMode == 0)) m_simSiegeMode = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Side 2 defends a town", m_simSiegeMode == 1)) m_simSiegeMode = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("Side 1 defends a town", m_simSiegeMode == 2)) m_simSiegeMode = 2;
        if (m_simSiegeMode != 0) {
            ImGui::Checkbox("Defender has a Bastion (+25% walls, auto defense prep)",
                            &m_simBastion);
            ImGui::TextDisabled("Walls, gate, moat, faction towers and siege engines included.");
        }
        ImGui::Spacing();

        // Info line
        ImGui::TextDisabled("Side 1: %s  vs  Side 2: %s  (week %d)",
            kFacNames[m_simFaction1], kFacNames[m_simFaction2], m_simWeek);
        ImGui::Spacing();
        ImGui::Checkbox("Auto-play (watch mode — AI controls both sides)", &m_simAutoPlay);
        ImGui::Spacing();

        // Start button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.15f, 1.0f));
        if (ImGui::Button("Start Battle", ImVec2(bw, 42))) {
            FactionId f1 = static_cast<FactionId>(m_simFaction1);
            FactionId f2 = static_cast<FactionId>(m_simFaction2);
            Hero h1 = ArmyBuilder::buildHero(f1, m_simWeek);
            Hero h2 = ArmyBuilder::buildHero(f2, m_simWeek);
            h1.name = kFacNames[m_simFaction1];
            h2.name = kFacNames[m_simFaction2];
            auto army1 = ArmyBuilder::buildArmy(f1, m_simWeek);
            auto army2 = ArmyBuilder::buildArmy(f2, m_simWeek);
            // Tag units with faction hint so combat result display works
            for (auto& u : army1) { u.isPlayer = true;  u.factionHint = m_simFaction1; }
            for (auto& u : army2) { u.isPlayer = false; u.factionHint = m_simFaction2; }
            m_fromBattleSim = true;
            enterCombat(h1, army1, h2, army2);
        }
        ImGui::PopStyleColor();

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##sim", ImVec2(bw, 30))) m_menuMode = 0;
    }
    // ── 6: Watch AI vs AI ─────────────────────────────────────────────────────
    else if (m_menuMode == 6) {
        header("WATCH AI vs AI");
        renderSetupBody(true);
    }
    // ── 7: High Scores ────────────────────────────────────────────────────────
    else if (m_menuMode == 7) {
        header("HIGH SCORES");
        // Query lazily and cache while the view is open so we don't hit the DB
        // every frame; refresh on (re)entering the view.
        static std::vector<HighScore> scores;
        static bool loaded = false;
        if (!loaded) {
            ScoreDB db;
            if (db.open(metaDbPath())) { scores = db.topScores(15); db.close(); }
            loaded = true;
        }
        if (scores.empty()) {
            ImGui::TextDisabled("No games finished yet — win or lose one to set a score.");
        } else {
            if (ImGui::BeginTable("hs", 5,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH, ImVec2(bw, 0))) {
                ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed, 26);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Score",ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Days", ImGuiTableColumnFlags_WidthFixed, 44);
                ImGui::TableHeadersRow();
                int rank = 1;
                for (const auto& s : scores) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%d", rank++);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(s.won ? ImVec4(0.7f,1.0f,0.7f,1.0f)
                                             : ImVec4(1.0f,0.6f,0.6f,1.0f),
                                       "%s%s", s.name.c_str(), s.won ? "" : "  (lost)");
                    ImGui::TableNextColumn(); ImGui::TextColored({1.0f,0.85f,0.2f,1.0f}, "%d", s.score);
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", s.rank.c_str());
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%d", s.days);
                }
                ImGui::EndTable();
            }
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##hs", ImVec2(bw, 30))) { loaded = false; m_menuMode = 0; }
    }
    // ── 8: Credits ────────────────────────────────────────────────────────────
    else if (m_menuMode == 8) {
        header("CREDITS");
        auto head = [&](const char* t){
            ImGui::Spacing();
            ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "%s", t);
            ImGui::Separator();
        };

        head("Unnamed Strategy");
        ImGui::TextWrapped("A hex-grid turn-based strategy game of conquest, "
                           "town-building and heroic warfare.");
        ImGui::TextDisabled("Design, code & art direction: krabysoft");

        head("Built with");
        ImGui::BulletText("C++20  -  SDL2  -  OpenGL 3.3 Core");
        ImGui::BulletText("Dear ImGui  -  Lua 5.4  -  SQLite3  -  stb_image");

        head("Art");
        ImGui::TextWrapped("Some 2D art assets were generated with AI tools "
                           "(Google Gemini / Imagen) and edited by the developer.");

        head("Thanks");
        ImGui::TextWrapped("To everyone who played, tested, and gave feedback.");

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##credits", ImVec2(bw, 30))) m_menuMode = 0;
    }
    // ── 9: How to Play ────────────────────────────────────────────────────────
    else if (m_menuMode == 9) {
        header("HOW TO PLAY");
        auto sect = [&](const char* t){
            ImGui::Spacing();
            ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "%s", t);
            ImGui::Separator();
        };
        auto row = [&](const char* key, const char* what){
            ImGui::TextColored({0.8f, 0.9f, 1.0f, 1.0f}, "%-16s", key);
            ImGui::SameLine(150); ImGui::TextDisabled("%s", what);
        };

        sect("Goal");
        ImGui::TextWrapped("Explore the hex map, capture mines and towns for income, "
                           "build up your towns, recruit armies and defeat the enemy. "
                           "Winning faster on a higher difficulty scores more (see High Scores).");

        sect("World map");
        row("Left-click",   "Select a tile / move your hero there");
        row("Right-click hero", "Quick sheet (stats, army, artifacts)");
        row("Right-click mine", "Mine info & threat estimate");
        row("Mouse wheel",  "Zoom     -     Drag: pan the map");
        row("SPACE",        "End turn");
        row("TAB",          "Next hero");
        row("M",            "Toggle mini-map");
        row("G",            "Garrison (dig in / block passage)");

        sect("Panels");
        row("F7",           "Artifacts");
        row("F8",           "Hero details");
        row("Kingdom",      "Overview of all heroes, towns & income");
        row("ESC",          "Options / close a panel");

        sect("Combat");
        ImGui::TextWrapped("Battles are turn-based on a hex grid. Click a unit, then a tile "
                           "to move or an enemy to attack. Ranged units shoot; flyers cross "
                           "obstacles. Retreat is possible but costs your army.");

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##howto", ImVec2(bw, 30))) m_menuMode = 0;
    }

    ImGui::End();
    endImGuiFrame();
}
