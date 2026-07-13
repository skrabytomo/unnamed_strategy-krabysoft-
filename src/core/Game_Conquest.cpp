// ── Conquest mode: node-map screen, hero setup, battle launch/return ─────────
// See CONQUEST_MODE.md. Phase 1: weekly node map, persistent hero, XP/gold.
#include "Game.h"
#include "DevLog.h"
#include "../sim/ArmyBuilder.h"
#include <imgui.h>
#include <cstdio>
#include <algorithm>

static const char* nodeTypeName(ConquestNodeType t)
{
    switch (t) {
    case ConquestNodeType::Battle:   return "Battle";
    case ConquestNodeType::Elite:    return "Elite";
    case ConquestNodeType::Treasure: return "Treasure";
    case ConquestNodeType::Boss:     return "Boss";
    }
    return "?";
}

static ImU32 nodeColor(const ConquestNode& n)
{
    if (n.state == 'C') return IM_COL32(90, 90, 90, 255);        // cleared: grey
    if (n.state == 'L') return IM_COL32(40, 40, 48, 255);        // locked: dark
    switch (n.type) {                                            // available:
    case ConquestNodeType::Battle:   return IM_COL32(70, 120, 200, 255);
    case ConquestNodeType::Elite:    return IM_COL32(190, 110, 30, 255);
    case ConquestNodeType::Treasure: return IM_COL32(200, 175, 40, 255);
    case ConquestNodeType::Boss:     return IM_COL32(190, 40, 40, 255);
    }
    return IM_COL32(120, 120, 120, 255);
}

void Game::updateConquest(float dt)
{
    (void)dt;
    if (m_input.keyDown(SDLK_ESCAPE)) {
        m_conquest.saveHero();
        m_state = GameState::MainMenu;
    }
}

void Game::renderConquest()
{
    beginImGuiFrame();
    ImGuiIO& io = ImGui::GetIO();

    // Lazy init on first entry
    if (!m_conquest.active())
        m_conquest.init("hideout.db");

    // ── Hero setup gate ───────────────────────────────────────────────────────
    if (!m_conquest.hasHero()) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({420, 0}, ImGuiCond_Always);
        ImGui::Begin("Create Conquest Hero", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextWrapped("Your Conquest hero is permanent: XP, levels, and "
                           "unlocks persist across every weekly map.");
        ImGui::Separator();
        ImGui::InputText("Name", m_conquestSetupName, sizeof(m_conquestSetupName));
        ImGui::Text("Faction:");
        static const char* kF[] = {"Holy Order","Crimson Wardens","Thornkin",
            "Eternal Empire","Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"};
        for (int i = 0; i < 9; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            bool sel = (m_conquestSetupFaction == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(160, 120, 20, 255));
            if (ImGui::Button(kF[i], ImVec2(128, 26))) m_conquestSetupFaction = i;
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        if (ImGui::Button("Begin", ImVec2(-1, 36))) {
            FactionId f = static_cast<FactionId>(m_conquestSetupFaction);
            auto classes = m_classRegistry.getClassesForFaction(f);
            int classId = classes.empty() ? 0 : classes[0]->id;
            m_conquest.createHero(m_conquestSetupName, f, classId);
        }
        if (ImGui::Button("Back", ImVec2(-1, 28)))
            m_state = GameState::MainMenu;
        ImGui::End();
        endImGuiFrame();
        return;
    }

    // ── Top bar: hero + currencies ────────────────────────────────────────────
    const ConquestHero& h = m_conquest.hero();
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({io.DisplaySize.x, 54}, ImGuiCond_Always);
    ImGui::Begin("##conqTop", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    int lvl = m_conquest.currentLevel();
    int xpCur = h.xp - ConquestMode::xpForLevel(lvl);
    int xpNext = ConquestMode::xpForLevel(lvl + 1) - ConquestMode::xpForLevel(lvl);
    ImGui::Text("%s  —  Level %d", h.name.c_str(), lvl);
    ImGui::SameLine(320);
    char xpBuf[48]; std::snprintf(xpBuf, sizeof(xpBuf), "XP %d / %d", xpCur, xpNext);
    ImGui::ProgressBar((float)xpCur / (float)std::max(1, xpNext), ImVec2(220, 18), xpBuf);
    ImGui::SameLine(580);
    ImGui::TextColored({1.0f, 0.85f, 0.2f, 1.f}, "Gold: %d", m_conquest.gold());
    ImGui::SameLine(720);
    ImGui::TextColored({0.5f, 0.85f, 1.0f, 1.f}, "Gems: %d", m_conquest.gems());
    ImGui::SameLine(io.DisplaySize.x - 190);
    ImGui::TextDisabled("Week map #%d  [Esc: menu]", m_conquest.week() % 100);
    ImGui::End();

    // ── Node map canvas ──────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({0, 54}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({io.DisplaySize.x, io.DisplaySize.y - 54}, ImGuiCond_Always);
    ImGui::Begin("##conqMap", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    float W = io.DisplaySize.x - 160.f;
    float H = io.DisplaySize.y - 200.f;
    auto nodePos = [&](const ConquestNode& n) -> ImVec2 {
        return { origin.x + 80.f + n.x * W, origin.y + 70.f + n.y * H };
    };

    const auto& nodes = m_conquest.nodes();

    // Edges first
    for (const auto& n : nodes) {
        ImVec2 a = nodePos(n);
        for (int nx : n.next) {
            ImVec2 b = nodePos(nodes[nx]);
            ImU32 col = (n.state == 'C') ? IM_COL32(150,150,150,160)
                                         : IM_COL32(80,80,90,120);
            dl->AddLine(a, b, col, 2.0f);
        }
    }

    // Nodes
    const float R = 22.f;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const ConquestNode& n = nodes[i];
        ImVec2 p = nodePos(n);
        dl->AddCircleFilled(p, R, nodeColor(n));
        dl->AddCircle(p, R, IM_COL32(220, 200, 150, 200), 0, 2.0f);
        const char* lbl = nodeTypeName(n.type);
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddText({p.x - ts.x * 0.5f, p.y + R + 4.f}, IM_COL32(210,210,210,255), lbl);

        // Click handling for available nodes
        ImGui::SetCursorScreenPos({p.x - R, p.y - R});
        ImGui::PushID((int)i);
        if (ImGui::InvisibleButton("##node", {R * 2, R * 2})) {
            if (m_conquest.isNodeAvailable((int)i)) {
                if (n.type == ConquestNodeType::Treasure) {
                    // No fight: collect gold immediately
                    m_conquest.grantVictoryRewards((int)i);
                    m_conquest.clearNode((int)i);
                } else {
                    startConquestBattle((int)i);
                }
            }
        }
        if (ImGui::IsItemHovered() && n.state == 'A') {
            if (n.type == ConquestNodeType::Treasure) {
                ImGui::SetTooltip("Treasure cache — claim gold");
            } else {
                FactionId ef = m_conquest.enemyFactionForNode((int)i);
                int ew = m_conquest.enemyWeeksForNode((int)i);
                static const char* kF[] = {"Holy Order","Crimson Wardens","Thornkin",
                    "Eternal Empire","Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"};
                ImGui::SetTooltip("%s vs %s (threat %d)\nClick to fight",
                    nodeTypeName(n.type), kF[(int)ef % 9], ew);
            }
        }
        ImGui::PopID();
    }

    ImGui::End();
    endImGuiFrame();
}

void Game::startConquestBattle(int nodeIndex)
{
    m_conquestActiveNode = nodeIndex;

    const ConquestHero& ch = m_conquest.hero();

    // Player: hero + army scaled by hero level (collection pool arrives Phase 2)
    Hero playerHero = ArmyBuilder::buildHero(ch.faction, std::max(1, ch.level));
    playerHero.name    = ch.name;
    playerHero.level   = ch.level;
    playerHero.classId = ch.classId;
    playerHero.attack  = ch.attack + (ch.level - 1) / 2;
    playerHero.defense = ch.defense + (ch.level - 1) / 2;
    auto playerUnits = ArmyBuilder::buildArmy(ch.faction, std::max(1, ch.level));

    // Enemy: scaled by node depth
    FactionId ef = m_conquest.enemyFactionForNode(nodeIndex);
    int ew = m_conquest.enemyWeeksForNode(nodeIndex);
    Hero enemyHero = ArmyBuilder::buildHero(ef, ew);
    auto enemyUnits = ArmyBuilder::buildArmy(ef, ew);

    int pid = 1, eid = 50;
    for (auto& u : playerUnits) { u.id = pid++; u.isPlayer = true;  u.factionHint = (int)ch.faction; }
    for (auto& u : enemyUnits)  { u.id = eid++; u.isPlayer = false; u.factionHint = (int)ef; }

    enterCombat(playerHero, playerUnits, enemyHero, enemyUnits);
    // enterCombat sets m_prevState = Conquest (we came from Conquest state),
    // which routes exitCombat back into onConquestBattleEnd.
}

void Game::onConquestBattleEnd(bool victory)
{
    if (victory && m_conquestActiveNode >= 0) {
        int levelsGained = m_conquest.grantVictoryRewards(m_conquestActiveNode);
        m_conquest.clearNode(m_conquestActiveNode);
        if (levelsGained > 0)
            gLog("Conquest: level up! Now level %d\n", m_conquest.currentLevel());
    } else {
        m_conquest.onDefeat();
    }
    m_conquestActiveNode = -1;
    m_audio.playMusic("worldmap_music");
    m_state = GameState::Conquest;
}
