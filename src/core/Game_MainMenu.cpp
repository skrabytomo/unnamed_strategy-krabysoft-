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
            "Easy: Player heroes gain +2 ATK/DEF, enemies are weaker. Good for learning.",
            "Normal: Balanced gameplay. Recommended for most players.",
            "Hard: Enemy heroes are stronger and more aggressive. For veterans."
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

        // Hot-Seat toggle
        ImGui::Text("Mode:");
        {
            bool sel1 = !m_newGameHotSeat;
            bool sel2 =  m_newGameHotSeat;
            if (sel1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.f));
            if (ImGui::Button("vs AI##hs0", ImVec2((bw - 4) / 2.f, 26))) m_newGameHotSeat = false;
            if (sel1) ImGui::PopStyleColor();
            ImGui::SameLine(0, 4);
            if (sel2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.6f, 1.f));
            if (ImGui::Button("2-Player Hot-Seat##hs1", ImVec2((bw - 4) / 2.f, 26))) m_newGameHotSeat = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Two human players take turns on the same screen.");
            if (sel2) ImGui::PopStyleColor();
        }

        if (m_newGameHotSeat) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f), "Player 2 Faction:");
            static const char* kFacNames2[] = {
                "Holy Order","Crimson Wardens","Thornkin","Eternal Empire",
                "Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"
            };
            for (int i = 0; i < 9; ++i) {
                if (i % 3 != 0) ImGui::SameLine();
                bool sel = (m_p2Faction == i);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.6f, 1.f));
                char f2Lbl[40]; std::snprintf(f2Lbl, sizeof(f2Lbl), "%s##p2f%d", kFacNames2[i], i);
                if (ImGui::Button(f2Lbl, ImVec2((bw - 4) / 3.f, 26))) {
                    m_p2Faction  = i;
                    m_p2ClassId  = 0;
                }
                if (sel) ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            FactionId p2f = static_cast<FactionId>(m_p2Faction);
            auto p2classes = m_classRegistry.getClassesForFaction(p2f);
            if (!p2classes.empty()) {
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f), "Player 2 Class:");
                bool p2Valid = false;
                for (auto* c : p2classes) if (c->id == m_p2ClassId) { p2Valid = true; break; }
                if (!p2Valid) m_p2ClassId = p2classes[0]->id;
                for (int ci = 0; ci < (int)p2classes.size(); ++ci) {
                    const HeroClassDef* cls = p2classes[ci];
                    if (ci % 2 != 0) ImGui::SameLine();
                    bool sel = (m_p2ClassId == cls->id);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.6f, 1.f));
                    char clbl[48]; std::snprintf(clbl, sizeof(clbl), "%s##p2cl%d", cls->name.c_str(), cls->id);
                    if (ImGui::Button(clbl, ImVec2((bw - 4) / 2.f, 26))) m_p2ClassId = cls->id;
                    if (ImGui::IsItemHovered() && !cls->specialtyDesc.empty())
                        ImGui::SetTooltip("Specialty: %s", cls->specialtyDesc.c_str());
                    if (sel) ImGui::PopStyleColor();
                }
            }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        ImGui::TextDisabled("Choose a slot. Existing save will be overwritten.");
        ImGui::Spacing();

        for (int s = 0; s < 5; ++s) {
            std::string path = "saves/save" + std::to_string(s) + ".json";
            SlotMeta meta = readSlotMeta(path);
            char lbl[200];
            if (meta.exists)
                std::snprintf(lbl, sizeof(lbl),
                    "Slot %d  |  %s  (%s)  Day %d  Week %d  [overwrite]##ng%d",
                    s + 1, meta.heroName.c_str(), meta.factionName.c_str(),
                    meta.day, meta.week, s);
            else
                std::snprintf(lbl, sizeof(lbl), "Slot %d  |  Empty##ng%d", s + 1, s);

            ImVec4 tc = meta.exists ? ImVec4(1.0f, 0.65f, 0.15f, 1.0f)
                                    : ImVec4(0.5f, 0.9f,  0.5f,  1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
            if (ImGui::Button(lbl, ImVec2(bw, 36))) {
                m_activeSlot = s;
                startNewGame();
                m_state    = GameState::WorldMap;
                m_menuMode = 0;
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##ng", ImVec2(bw, 30))) m_menuMode = 0;
    }
    // ── 2: Load Game — slot list ──────────────────────────────────────────────
    else if (m_menuMode == 2) {
        header("Load Game");

        // General saves (5 slots)
        ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "General Saves");
        ImGui::Separator();
        ImGui::Spacing();
        bool anyGeneral = false;
        for (int s = 0; s < 5; ++s) {
            std::string path = "saves/save" + std::to_string(s) + ".json";
            SlotMeta meta = readSlotMeta(path);
            if (!meta.exists) {
                ImGui::TextDisabled("Slot %d  |  Empty", s + 1);
                ImGui::Spacing();
                continue;
            }
            anyGeneral = true;
            char lbl[200];
            std::snprintf(lbl, sizeof(lbl),
                "Slot %d  |  %s  (%s)  Day %d  Week %d##ld%d",
                s + 1, meta.heroName.c_str(), meta.factionName.c_str(),
                meta.day, meta.week, s);
            float delBtnW = 52.0f;
            if (ImGui::Button(lbl, ImVec2(bw - delBtnW - 4, 36))) {
                m_activeSlot = s;
                if (loadGame(path)) {
                    m_state    = GameState::WorldMap;
                    m_menuMode = 0;
                }
            }
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.1f, 0.1f, 1.0f));
            char delLbl[24]; std::snprintf(delLbl, sizeof(delLbl), "Del##dg%d", s);
            if (ImGui::Button(delLbl, ImVec2(delBtnW, 36))) {
                std::remove(path.c_str());
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        if (!anyGeneral) { ImGui::Spacing(); ImGui::TextDisabled("No general saves found."); ImGui::Spacing(); }

        // Campaign saves (3 slots)
        ImGui::Spacing();
        ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "Campaign Saves");
        ImGui::Separator();
        ImGui::Spacing();
        static const char* kMissionNames[] = { "I. The Border Burns", "II. The Thornwood Passage", "III. The Convergence Point" };
        bool anyCampaign = false;
        for (int s = 0; s < 3; ++s) {
            std::string path = "saves/campaign" + std::to_string(s) + ".json";
            SlotMeta meta = readSlotMeta(path);
            if (!meta.exists) {
                ImGui::TextDisabled("Camp %d  |  Empty", s + 1);
                ImGui::Spacing();
                continue;
            }
            anyCampaign = true;
            const char* mname = (meta.missionIdx >= 0 && meta.missionIdx < 3) ? kMissionNames[meta.missionIdx] : "?";
            char lbl[200];
            std::snprintf(lbl, sizeof(lbl),
                "Camp %d  |  %s  |  %s  Day %d  Week %d##ldc%d",
                s + 1, mname, meta.heroName.c_str(), meta.day, meta.week, s);
            float delBtnW = 52.0f;
            if (ImGui::Button(lbl, ImVec2(bw - delBtnW - 4, 36))) {
                m_campaignActiveSlot = s;
                if (loadGame(path)) {
                    m_state    = GameState::WorldMap;
                    m_menuMode = 0;
                }
            }
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.1f, 0.1f, 1.0f));
            char delLbl[24]; std::snprintf(delLbl, sizeof(delLbl), "Del##dc%d", s);
            if (ImGui::Button(delLbl, ImVec2(delBtnW, 36))) {
                std::remove(path.c_str());
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        if (!anyCampaign) { ImGui::Spacing(); ImGui::TextDisabled("No campaign saves found."); ImGui::Spacing(); }

        ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Back##ld", ImVec2(bw, 30))) m_menuMode = 0;
    }
    // ── 3: Settings ───────────────────────────────────────────────────────────
    else if (m_menuMode == 3) {
        header("Settings");

        ImGui::Text("Audio");
        ImGui::Separator();
        if (ImGui::SliderFloat("Music Volume", &m_settingsMasVol, 0.0f, 1.0f, "%.2f"))
            m_audio.setMusicVolume(m_settingsMasVol);
        if (ImGui::SliderFloat("SFX Volume",   &m_settingsSfxVol, 0.0f, 1.0f, "%.2f"))
            m_audio.setSfxVolume(m_settingsSfxVol);

        ImGui::Spacing();
        ImGui::Text("Display");
        ImGui::Separator();
        if (ImGui::Checkbox("Fullscreen", &m_settingsFullscreen))
            SDL_SetWindowFullscreen(m_window,
                m_settingsFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
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
            "Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"
        };

        ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "Faction 1 (Blue):");
        for (int i = 0; i < 9; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_watchAIFaction1 == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.6f, 1.f));
            char lbl[40]; std::snprintf(lbl, sizeof(lbl), "%s##w1f%d", kFacNames[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 4) / 3.f, 26))) m_watchAIFaction1 = i;
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        ImGui::TextColored({1.0f, 0.5f, 0.3f, 1.0f}, "Faction 2 (Red):");
        for (int i = 0; i < 9; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_watchAIFaction2 == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.2f, 0.1f, 1.f));
            char lbl[40]; std::snprintf(lbl, sizeof(lbl), "%s##w2f%d", kFacNames[i], i);
            if (ImGui::Button(lbl, ImVec2((bw - 4) / 3.f, 26))) m_watchAIFaction2 = i;
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        ImGui::Text("Auto-advance speed:");
        ImGui::SetNextItemWidth(bw);
        ImGui::SliderFloat("##waisp", &m_watchAISpeed, 0.25f, 4.0f, "%.2fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("1.0 = 1 end-turn per second. Higher = faster.");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button("Start Watching", ImVec2(bw, 42))) {
            m_newGameFaction = m_watchAIFaction1;
            m_newGameMapSize = 1;  // Medium map
            m_newGameDifficulty = 1;
            m_newGameClassId = 0;
            startNewGame();
            // Override enemy faction to m_watchAIFaction2
            if (!m_enemyHeroes.empty())
                m_enemyHeroes[0].faction = static_cast<FactionId>(m_watchAIFaction2);
            for (auto& t : m_towns)
                if (t.ownerId > 1)
                    t.faction = static_cast<FactionId>(m_watchAIFaction2);
            m_watchingAI  = true;
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
