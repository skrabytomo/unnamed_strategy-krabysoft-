#include "SimulatorWindow.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include "../sim/SimTypes.h"

static const char* kFactionLabels[] = {
    "Holy Order", "Crimson Wardens", "Thornkin",
    "Eternal Empire", "Bloodsworn", "Voidkin",
    "Iron Assembly", "Amalgamate", "Convergence",
};
static constexpr int kFactionCount = 9;

const char* SimulatorWindow::factionLabel(int idx)
{
    if (idx < 0 || idx >= kFactionCount) return "?";
    return kFactionLabels[idx];
}

SimulatorWindow::~SimulatorWindow()
{
    stopSimulation();
}

void SimulatorWindow::stopSimulation()
{
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void SimulatorWindow::launchSimulation()
{
    if (m_running) return;

    stopSimulation();
    m_progressDone  = 0;
    m_progressTotal = 1;
    m_running       = true;

    SimConfig cfg;
    cfg.faction1   = static_cast<FactionId>(m_faction1);
    cfg.faction2   = static_cast<FactionId>(m_faction2);
    cfg.allVsAll   = m_allVsAll;
    cfg.weeks      = m_weeks;
    cfg.numBattles = m_numBattles;
    cfg.seed       = static_cast<uint32_t>(m_seed);
    cfg.side1AI    = static_cast<AIDifficulty>(m_ai1);
    cfg.side2AI    = static_cast<AIDifficulty>(m_ai2);

    m_thread = std::thread([this, cfg]() {
        SimResult r = Simulator::run(cfg, [this](int done, int total) {
            m_progressDone  = done;
            m_progressTotal = total;
        });
        std::lock_guard<std::mutex> lk(m_resultMutex);
        m_result    = std::move(r);
        m_hasResult = true;
        m_running   = false;
    });
}

void SimulatorWindow::render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Combat Simulator", &m_open, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("Run")) launchSimulation();
        if (ImGui::MenuItem("Stop", nullptr, false, m_running)) stopSimulation();
        ImGui::EndMenuBar();
    }

    ImGui::Columns(2, "sim_cols", true);
    ImGui::SetColumnWidth(0, 240.0f);

    drawConfigPanel();
    ImGui::NextColumn();
    drawResultsPanel();
    ImGui::Columns(1);

    ImGui::End();
}

void SimulatorWindow::drawConfigPanel()
{
    ImGui::TextDisabled("Configuration");
    ImGui::Separator();

    ImGui::Checkbox("All vs All (9x9)", &m_allVsAll);

    if (!m_allVsAll) {
        ImGui::Combo("Faction A", &m_faction1, kFactionLabels, kFactionCount);
        ImGui::Combo("Faction B", &m_faction2, kFactionLabels, kFactionCount);
    }

    ImGui::SliderInt("Weeks",    &m_weeks,      1, 20);
    ImGui::SliderInt("Battles",  &m_numBattles, 100, 5000);
    ImGui::InputInt("Seed",      &m_seed);

    static const char* kAILabels[] = {"Passive","Standard","Tactical"};
    ImGui::Combo("A.I. Side A", &m_ai1, kAILabels, 3);
    ImGui::Combo("A.I. Side B", &m_ai2, kAILabels, 3);
    if (m_ai1 != m_ai2) {
        ImGui::TextColored(ImVec4(1,0.85f,0.3f,1),
            "Asymmetric AI — tests difficulty gap");
    }

    // Hero level preview
    int level = 1 + static_cast<int>(std::sqrt(static_cast<float>(m_weeks) * 2.0f));
    ImGui::TextDisabled("Hero level: %d  (atk %d / def %d)", level,
        2 + (level - 1) / 2, 2 + (level - 1) / 2);

    ImGui::Spacing();

    if (m_running) {
        int done  = m_progressDone;
        int total = m_progressTotal;
        float frac = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
        ImGui::ProgressBar(frac, ImVec2(-1, 0));
        ImGui::TextUnformatted("Running...");
        if (ImGui::Button("Stop")) stopSimulation();
    } else {
        if (ImGui::Button("Run Simulation", ImVec2(-1, 0)))
            launchSimulation();
    }
}

void SimulatorWindow::drawResultsPanel()
{
    std::lock_guard<std::mutex> lk(m_resultMutex);

    if (!m_hasResult) {
        ImGui::TextDisabled("No results yet. Configure and click Run.");
        return;
    }

    if (ImGui::BeginTabBar("ResultTabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            drawOverview();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Win Rate Grid")) {
            drawMatchupGrid();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Balance Report")) {
            drawBalanceReport();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void SimulatorWindow::drawOverview()
{
    const SimResult& r = m_result;

    if (r.battlesPerMatchup <= 0) {
        ImGui::TextDisabled("No data.");
        return;
    }

    if (m_allVsAll) {
        // All-vs-all: show summary stats across all matchups
        ImGui::TextUnformatted("All vs All — Summary");
        ImGui::Separator();
        ImGui::Spacing();

        int imbalanceCount = 0;
        float totalLossA = 0.0f, totalLossB = 0.0f;
        float totalRounds = 0.0f;
        int matchupCount = 0;
        for (int i = 0; i < kFactionCount; ++i) {
            for (int j = i + 1; j < kFactionCount; ++j) {
                const auto& m = r.matchups[i][j];
                if (m.imbalanced) ++imbalanceCount;
                totalLossA  += m.avgF1LossRate;
                totalLossB  += m.avgF2LossRate;
                totalRounds += m.avgRounds;
                ++matchupCount;
            }
        }
        if (matchupCount > 0) {
            ImGui::Text("Matchups run:       36  (%d battles each)", r.battlesPerMatchup);
            ImGui::Text("Imbalanced:         %d / 36", imbalanceCount);
            ImGui::Text("Avg battle length:  %.1f rounds", totalRounds / matchupCount);
            float avgLoss = (totalLossA + totalLossB) * 0.5f / matchupCount;
            ImGui::Text("Avg army losses:    %.1f%%", avgLoss * 100.0f);
        }
        return;
    }

    // Single matchup
    int i = m_faction1;
    int j = m_faction2;
    const auto& m = (i != j) ? r.matchups[i][j] : r.matchups[i][j];
    if (m.battles <= 0) {
        ImGui::TextDisabled("No data for this matchup.");
        return;
    }

    const char* nameA = factionLabel(m_faction1);
    const char* nameB = factionLabel(m_faction2);
    int winsA = static_cast<int>(std::round(m.winRate1 * m.battles));
    int winsB = m.battles - winsA;

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "  %s  vs  %s", nameA, nameB);
    ImGui::TextDisabled("  %d battles  |  %d weeks  |  %s vs %s AI",
        m.battles, r.weeksSimulated,
        m_ai1 == 0 ? "Passive" : m_ai1 == 2 ? "Tactical" : "Standard",
        m_ai2 == 0 ? "Passive" : m_ai2 == 2 ? "Tactical" : "Standard");
    ImGui::Separator();
    ImGui::Spacing();

    // Winner block
    ImGui::TextUnformatted("Results:");
    ImGui::Spacing();

    auto winCol = [](float wr) {
        return wr > 0.5f ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    };

    ImGui::TextColored(winCol(m.winRate1),
        "  %-22s  won %4d / %d  (%.1f%%)", nameA, winsA, m.battles, m.winRate1 * 100.0f);
    ImGui::TextColored(winCol(1.0f - m.winRate1),
        "  %-22s  won %4d / %d  (%.1f%%)", nameB, winsB, m.battles, (1.0f - m.winRate1) * 100.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Average losses per battle:");
    ImGui::Spacing();

    // Loss bars
    auto drawLossBar = [&](const char* name, float lossRate) {
        ImGui::Text("  %-22s  lost %.1f%% of army", name, lossRate * 100.0f);
        float barW = ImGui::GetContentRegionAvail().x - 10.0f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float barH = 8.0f;
        dl->AddRectFilled(pos, {pos.x + barW, pos.y + barH},
            IM_COL32(60, 60, 60, 200));
        dl->AddRectFilled(pos, {pos.x + barW * lossRate, pos.y + barH},
            IM_COL32(220, 80, 60, 220));
        ImGui::Dummy({barW, barH + 4.0f});
    };

    drawLossBar(nameA, m.avgF1LossRate);
    ImGui::Spacing();
    drawLossBar(nameB, m.avgF2LossRate);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("  Avg battle length:    %.1f rounds", m.avgRounds);
    ImGui::Text("  Winner survival (A):  %.1f%%", m.avgF1Survival * 100.0f);
    ImGui::Text("  Winner survival (B):  %.1f%%", m.avgF2Survival * 100.0f);
    ImGui::Text("  Army cost A:          %dg", m.armyCostF1);
    ImGui::Text("  Army cost B:          %dg", m.armyCostF2);
    ImGui::Spacing();
    if (m.imbalanced)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "  IMBALANCED — win rate >15%% from 50%%");
    else
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
            "  Balanced — within 15%% of 50%%");
}

void SimulatorWindow::drawMatchupGrid()
{
    const SimResult& r = m_result;

    ImGui::TextUnformatted("Win rate for row faction vs column faction");
    ImGui::TextDisabled("Red = imbalanced (>15%% from 50%%)  |  Click cell for details");
    ImGui::Separator();

    // Header row
    ImGui::Dummy(ImVec2(110, 0)); ImGui::SameLine();
    for (int j = 0; j < kFactionCount; ++j) {
        // Abbreviated 3-char label
        char abbr[4] = {kFactionLabels[j][0],
                        kFactionLabels[j][1],
                        kFactionLabels[j][2], 0};
        ImGui::TextDisabled("%s", abbr); ImGui::SameLine();
        ImGui::Dummy(ImVec2(38, 0)); ImGui::SameLine();
    }
    ImGui::NewLine();

    for (int i = 0; i < kFactionCount; ++i) {
        // Row label
        char label[18];
        std::snprintf(label, sizeof(label), "%.14s", kFactionLabels[i]);
        ImGui::TextDisabled("%s", label); ImGui::SameLine();
        ImGui::Dummy(ImVec2(std::max(0.0f, 110.0f - ImGui::CalcTextSize(label).x - 4.0f), 0));
        ImGui::SameLine();

        for (int j = 0; j < kFactionCount; ++j) {
            if (i == j) {
                ImGui::TextDisabled(" -- "); ImGui::SameLine();
                continue;
            }
            const auto& m = r.matchups[i][j];
            float wr = m.winRate1;  // win rate for faction i when row=i

            bool selected = (m_selectedRow == i && m_selectedCol == j);
            bool bad = m.imbalanced;

            char cellLabel[16];
            std::snprintf(cellLabel, sizeof(cellLabel), "##cell%d%d", i, j);

            char txt[8];
            std::snprintf(txt, sizeof(txt), "%.0f%%", wr * 100.0f);

            ImVec4 col = bad
                ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                : ImVec4(0.7f, 1.0f, 0.7f, 1.0f);
            if (selected) col = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(txt);
            ImGui::PopStyleColor();

            if (ImGui::IsItemClicked()) {
                m_selectedRow = i;
                m_selectedCol = j;
            }
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(6, 0));
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    // Drilldown panel
    if (m_selectedRow >= 0 && m_selectedCol >= 0 &&
        m_selectedRow < kFactionCount && m_selectedCol < kFactionCount &&
        m_selectedRow != m_selectedCol)
    {
        ImGui::Separator();
        const auto& m = r.matchups[m_selectedRow][m_selectedCol];
        ImGui::Text("%s  vs  %s", factionLabel(m_selectedRow), factionLabel(m_selectedCol));
        ImGui::Text("Win rate:      %.1f%% / %.1f%%",
            m.winRate1 * 100.0f, (1.0f - m.winRate1) * 100.0f);
        ImGui::Text("Avg rounds:    %.1f", m.avgRounds);
        ImGui::Text("Avg losses:    A=%.1f%%  B=%.1f%%",
            m.avgF1LossRate * 100.0f, m.avgF2LossRate * 100.0f);
        ImGui::Text("Winner surviv: A=%.1f%%  B=%.1f%%",
            m.avgF1Survival * 100.0f, m.avgF2Survival * 100.0f);
        ImGui::Text("Balance grade: %s",
            m.imbalanced ? "IMBALANCED" : "OK");
    }
}

void SimulatorWindow::drawBalanceReport()
{
    const SimResult& r = m_result;
    ImGui::InputTextMultiline("##report",
        const_cast<char*>(r.balanceReport.c_str()),
        r.balanceReport.size() + 1,
        ImVec2(-1, -1),
        ImGuiInputTextFlags_ReadOnly);
}
