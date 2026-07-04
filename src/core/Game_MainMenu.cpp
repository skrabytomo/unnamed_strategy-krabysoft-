#include "Game.h"
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

static SlotMeta readSlotMeta(const std::string& path)
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

    // Full-screen menu backdrop (behind the panel). Falls back silently to the
    // dark theme background if the art is missing.
    {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        const float W = io.DisplaySize.x, H = io.DisplaySize.y;
        if (m_menuBgTex.ok())
            bg->AddImage((ImTextureID)(uintptr_t)m_menuBgTex.id(), ImVec2(0, 0), ImVec2(W, H));
        else
            bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H),
                IM_COL32(20, 16, 34, 255), IM_COL32(20, 16, 34, 255),
                IM_COL32(40, 20, 14, 255), IM_COL32(40, 20, 14, 255));
        // gentle scrim so the menu panel stays readable over bright art
        bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 70));
    }

    ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
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

    // ── 0: Main ──────────────────────────────────────────────────────────────
    if (m_menuMode == 0) {
        header("UNNAMED STRATEGY");

        if (ImGui::Button("New Game",   ImVec2(bw, 40))) m_menuMode = 1;
        ImGui::Spacing();
        if (ImGui::Button("Load Game",  ImVec2(bw, 40))) m_menuMode = 2;
        ImGui::Spacing();
        if (ImGui::Button("Campaign",   ImVec2(bw, 40))) m_menuMode = 4;
        ImGui::Spacing();
        if (ImGui::Button("Battle Sim", ImVec2(bw, 40))) m_menuMode = 5;
        ImGui::Spacing();
        if (ImGui::Button("Watch AI vs AI", ImVec2(bw, 40))) m_menuMode = 6;
        ImGui::Spacing();
        if (ImGui::Button("Settings",   ImVec2(bw, 40))) m_menuMode = 3;
        ImGui::Spacing();
        if (ImGui::Button("Map Editor", ImVec2(bw, 40))) { enterEditor(); }
        ImGui::Spacing();
        if (ImGui::Button("Quit",       ImVec2(bw, 40))) m_running = false;

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextColored({0.4f, 0.4f, 0.4f, 1.0f}, "F5 Save  F9 Load  F2 Editor");
    }
    // ── 1: New Game — setup + slot picker ────────────────────────────────────
    else if (m_menuMode == 1) {
        header("New Game");

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

        // Faction
        ImGui::Text("Faction:");
        static const char* kFacNames[] = {
            "Holy Order","Crimson Wardens","Thornkin","Eternal Empire",
            "Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"
        };
        for (int i = 0; i < 9; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_newGameFaction == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.3f, 0.1f, 1.f));
            char fLbl[40]; std::snprintf(fLbl, sizeof(fLbl), "%s##fc%d", kFacNames[i], i);
            if (ImGui::Button(fLbl, ImVec2((bw - 4) / 3.f, 26))) {
                m_newGameFaction = i;
                m_newGameClassId = 0;  // reset class selection on faction change
            }
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        // Hero class selection for chosen faction
        {
            FactionId f = static_cast<FactionId>(m_newGameFaction);
            auto classes = m_classRegistry.getClassesForFaction(f);
            if (!classes.empty()) {
                ImGui::Text("Hero Class:");
                // Ensure m_newGameClassId is valid
                bool classValid = false;
                for (auto* c : classes) if (c->id == m_newGameClassId) { classValid = true; break; }
                if (!classValid) m_newGameClassId = classes[0]->id;

                for (int ci = 0; ci < static_cast<int>(classes.size()); ++ci) {
                    const HeroClassDef* cls = classes[ci];
                    if (ci % 2 != 0) ImGui::SameLine();
                    bool sel = (m_newGameClassId == cls->id);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.f));
                    char clbl[48]; std::snprintf(clbl, sizeof(clbl), "%s##cl%d", cls->name.c_str(), cls->id);
                    if (ImGui::Button(clbl, ImVec2((bw - 4) / 2.f, 26)))
                        m_newGameClassId = cls->id;
                    if (ImGui::IsItemHovered() && !cls->specialtyDesc.empty())
                        ImGui::SetTooltip("Specialty: %s", cls->specialtyDesc.c_str());
                    if (sel) ImGui::PopStyleColor();
                }
            }
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

        // ── Player slots (HoMM-style lobby) ─────────────────────────────────
        ImGui::Text("Players:");
        for (int pc = 2; pc <= 4; ++pc) {
            if (pc > 2) ImGui::SameLine(0, 4);
            bool sel = (m_setupPlayerCount == pc);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.f));
            char pcl[16]; std::snprintf(pcl, sizeof(pcl), "%d##pcnt%d", pc, pc);
            if (ImGui::Button(pcl, ImVec2((bw - 8) / 3.f, 26))) m_setupPlayerCount = pc;
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
            m_slotType[0]    = 0;                    // slot 0 is always you
            m_slotFaction[0] = m_newGameFaction;     // driven by the picker above
            float thirdW = (bw - 8) / 3.f;
            for (int s = 0; s < m_setupPlayerCount; ++s) {
                ImGui::PushID(s);
                // Human / Bot toggle (slot 0 fixed)
                if (s == 0) {
                    ImGui::Button("You", ImVec2(thirdW, 26));
                } else {
                    bool isHuman = (m_slotType[s] == 0);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        isHuman ? ImVec4(0.1f, 0.3f, 0.6f, 1.f) : ImVec4(0.45f, 0.2f, 0.1f, 1.f));
                    if (ImGui::Button(isHuman ? "Human" : "Bot", ImVec2(thirdW, 26)))
                        m_slotType[s] = isHuman ? 1 : 0;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to switch. Humans play hot-seat on this screen.");
                    ImGui::PopStyleColor();
                }
                // Faction cycle (slot 0 shown read-only — set by the picker above)
                ImGui::SameLine(0, 4);
                if (s == 0) {
                    char flbl[40];
                    std::snprintf(flbl, sizeof(flbl), "%s##fac0", kSlotFacNames[std::clamp(m_newGameFaction, 0, 8)]);
                    ImGui::Button(flbl, ImVec2(thirdW, 26));
                } else {
                    char flbl[40];
                    std::snprintf(flbl, sizeof(flbl), "%s##fac", kSlotFacNames[std::clamp(m_slotFaction[s], 0, 9)]);
                    if (ImGui::Button(flbl, ImVec2(thirdW, 26)))
                        m_slotFaction[s] = (m_slotFaction[s] + 1) % 10;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to cycle faction (Random rolls at start).");
                }
                // Starting bonus cycle
                ImGui::SameLine(0, 4);
                char blbl[32];
                std::snprintf(blbl, sizeof(blbl), "%s##bon", kBonusNames[std::clamp(m_slotBonus[s], 0, 2)]);
                if (ImGui::Button(blbl, ImVec2(thirdW, 26)))
                    m_slotBonus[s] = (m_slotBonus[s] + 1) % 3;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Starting bonus: random artifact, +5 of the faction's key resource, or +1500 gold.");
                ImGui::PopID();
            }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button("Start New Game", ImVec2(bw, 42))) {
            m_activeSaveId = 0; // will create a new row on first save
            startNewGame();
            m_state    = GameState::WorldMap;
            m_menuMode = 0;
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##ng", ImVec2(bw, 30))) m_menuMode = 0;
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

        static const char* kFacNames[] = {
            "Holy Order","Crimson Wardens","Thornkin","Eternal Empire",
            "Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence",
            "Random"
        };
        static const char* kBonusNames[] = { "Artifact", "+5 Resource", "+1500 Gold" };

        // Both sides use the SAME picking system as a normal game: faction
        // (incl. Random), starting bonus, map size and difficulty. Watch is a
        // full fair game with every slot a bot.
        ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "Side 1 (Blue):");
        for (int i = 0; i < 10; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_watchAIFaction1 == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.6f, 1.f));
            char lbl[40]; std::snprintf(lbl, sizeof(lbl), "%s##w1f%d", kFacNames[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 8) / 3.f, 24))) m_watchAIFaction1 = i;
            if (sel) ImGui::PopStyleColor();
        }
        {
            char bl[24]; std::snprintf(bl, sizeof(bl), "Bonus: %s##w1b", kBonusNames[std::clamp(m_slotBonus[0],0,2)]);
            if (ImGui::Button(bl, ImVec2(bw, 22))) m_slotBonus[0] = (m_slotBonus[0] + 1) % 3;
        }
        ImGui::Spacing();

        ImGui::TextColored({1.0f, 0.5f, 0.3f, 1.0f}, "Side 2 (Red):");
        for (int i = 0; i < 10; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_watchAIFaction2 == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.2f, 0.1f, 1.f));
            char lbl[40]; std::snprintf(lbl, sizeof(lbl), "%s##w2f%d", kFacNames[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 8) / 3.f, 24))) m_watchAIFaction2 = i;
            if (sel) ImGui::PopStyleColor();
        }
        {
            char bl[24]; std::snprintf(bl, sizeof(bl), "Bonus: %s##w2b", kBonusNames[std::clamp(m_slotBonus[1],0,2)]);
            if (ImGui::Button(bl, ImVec2(bw, 22))) m_slotBonus[1] = (m_slotBonus[1] + 1) % 3;
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Map size
        ImGui::Text("Map size:");
        static const char* kSizeNames[] = { "Small", "Medium", "Large", "XLarge" };
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine();
            bool sel = (m_newGameMapSize == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.15f, 1.f));
            char lbl[24]; std::snprintf(lbl, sizeof(lbl), "%s##wsz%d", kSizeNames[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 12) / 4.f, 24))) m_newGameMapSize = i;
            if (sel) ImGui::PopStyleColor();
        }
        // Difficulty (controls AI hero cap + aggression on BOTH sides)
        ImGui::Text("Difficulty:");
        static const char* kDiffN[] = { "Easy", "Normal", "Hard" };
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            bool sel = (m_newGameDifficulty == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.2f, 0.1f, 1.f));
            char lbl[24]; std::snprintf(lbl, sizeof(lbl), "%s##wdf%d", kDiffN[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 8) / 3.f, 24))) m_newGameDifficulty = i;
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        ImGui::Text("Auto-advance speed:");
        ImGui::SetNextItemWidth(bw);
        ImGui::SliderFloat("##waisp", &m_watchAISpeed, 0.25f, 4.0f, "%.2fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("1.0 = 1 end-turn per second. Higher = faster.");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button("Start Watching", ImVec2(bw, 42))) {
            m_newGameClassId = 0;
            // A stale hot-seat toggle from the New Game menu would set
            // m_numHumanPlayers=2 in startNewGame and freeze all AI here.
            m_newGameHotSeat = false;
            // Watch = a normal 2-player game with BOTH slots as bots.
            m_setupPlayerCount = 2;
            m_slotType[0]    = 0;   // watched side (spectated, AI-driven)
            m_slotType[1]    = 1;   // bot
            m_slotFaction[0] = m_watchAIFaction1;   // 9 = Random, resolved in startNewGame
            m_slotFaction[1] = m_watchAIFaction2;
            m_newGameFaction = (m_watchAIFaction1 <= 8) ? m_watchAIFaction1 : 0;
            startNewGame();
            // startNewGame already set both sides' factions from the slots
            // (Random resolved) — no post-hoc override needed.
            m_watchingAI  = true;
            m_fogDisabled = true;
            m_watchAITimer= 1.0f / m_watchAISpeed;
            m_state    = GameState::WorldMap;
            m_menuMode = 0;
        }
        ImGui::PopStyleColor();
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##wai", ImVec2(bw, 30))) m_menuMode = 0;
    }

    ImGui::End();
    endImGuiFrame();
}
