// ── Conquest mode: node-map screen, hero setup, battle launch/return ─────────
// See CONQUEST_MODE.md. Phase 1: weekly node map, persistent hero, XP/gold.
#include "Game.h"
#include "DevLog.h"
#include "../sim/ArmyBuilder.h"
#include <imgui.h>
#include <cstdio>
#include <algorithm>
#include <cstdint>

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

// Draws the first animation frame of a unit's sprite sheet inline as an icon.
// Falls back to a coloured dot if the sheet is missing. Advances the ImGui
// cursor like a widget so callers can SameLine() after it.
void Game::conquestUnitIcon(int defId, float size)
{
    const UnitDef* d = m_registry.getUnitDef(defId);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool drawn = false;
    if (d) {
        int fi = (int)d->faction;
        int ti = std::max(0, std::min(NUM_UNIT_TIERS - 1, d->tier - 1));
        if (fi >= 0 && fi < NUM_FACTIONS && m_unitTex[fi][ti].ok()) {
            int cols = std::max(1, m_unitTexCols[fi][ti]);
            ImTextureID tid = (ImTextureID)(uintptr_t)m_unitTex[fi][ti].id();
            // First frame = left 1/cols of the sheet
            ImVec2 uv0(0.f, 0.f), uv1(1.f / cols, 1.f);
            dl->AddImage(tid, p, {p.x + size, p.y + size}, uv0, uv1);
            drawn = true;
        }
    }
    if (!drawn) {
        ImU32 col = d ? IM_COL32(120 + (d->tier * 20) % 135, 120, 200, 255)
                      : IM_COL32(100, 100, 100, 255);
        dl->AddRectFilled(p, {p.x + size, p.y + size}, col, 4.f);
    }
    ImGui::Dummy({size, size});
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
    m_conquest.refreshQuests();   // regenerate daily/weekly sets if elapsed

    // ── Hero setup gate ───────────────────────────────────────────────────────
    if (!m_conquest.hasHero()) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Always);
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
            // Faction icon (T1 unit sprite) + name in one clickable group.
            ImGui::BeginGroup();
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            if (ImGui::Button(kF[i], ImVec2(142, 34))) m_conquestSetupFaction = i;
            // Draw the faction's T1 sprite on the left edge of the button
            if (m_unitTex[i][0].ok()) {
                int cols = std::max(1, m_unitTexCols[i][0]);
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(uintptr_t)m_unitTex[i][0].id(),
                    {cursor.x + 3, cursor.y + 3}, {cursor.x + 31, cursor.y + 31},
                    {0.f, 0.f}, {1.f / cols, 1.f});
            }
            ImGui::EndGroup();
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        // Class picker for the chosen faction
        {
            FactionId f = static_cast<FactionId>(m_conquestSetupFaction);
            auto classes = m_classRegistry.getClassesForFaction(f);
            if (!classes.empty()) {
                bool valid = false;
                for (auto* c : classes) if (c->id == m_conquestSetupClassId) valid = true;
                if (!valid) m_conquestSetupClassId = classes[0]->id;
                ImGui::Text("Class:");
                for (int ci = 0; ci < (int)classes.size(); ++ci) {
                    const HeroClassDef* cls = classes[ci];
                    if (ci % 2 != 0) ImGui::SameLine();
                    bool csel = (m_conquestSetupClassId == cls->id);
                    if (csel) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 110, 60, 255));
                    char clbl[48]; std::snprintf(clbl, sizeof(clbl), "%s##ccl%d", cls->name.c_str(), cls->id);
                    if (ImGui::Button(clbl, ImVec2(220, 26))) m_conquestSetupClassId = cls->id;
                    if (ImGui::IsItemHovered() && !cls->specialtyDesc.empty())
                        ImGui::SetTooltip("Specialty: %s", cls->specialtyDesc.c_str());
                    if (csel) ImGui::PopStyleColor();
                }
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("Begin", ImVec2(-1, 36))) {
            FactionId f = static_cast<FactionId>(m_conquestSetupFaction);
            auto classes = m_classRegistry.getClassesForFaction(f);
            int classId = m_conquestSetupClassId;
            if (classId == 0 && !classes.empty()) classId = classes[0]->id;
            m_conquest.createHero(m_conquestSetupName, f, classId);
            m_conquest.grantChest(ConquestMode::ChestType::Wooden, 2); // starter units
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
    if (ImGui::IsItemClicked()) m_conquestShowGemShop = !m_conquestShowGemShop;
    ImGui::SameLine(830);
    ImGui::TextDisabled("(click gems to shop)");
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

    // ── Bottom bar: chests + army button (Phase 2) ────────────────────────────
    {
        ImGui::SetNextWindowPos({0, io.DisplaySize.y - 64}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x, 64}, ImGuiCond_Always);
        ImGui::Begin("##conqBottom", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

        if (ImGui::Button("Army / Collection", ImVec2(170, 40)))
            m_conquestShowArmy = !m_conquestShowArmy;
        ImGui::SameLine(0, 12);
        if (ImGui::Button("Quests", ImVec2(90, 40)))
            m_conquestShowQuests = !m_conquestShowQuests;
        ImGui::SameLine(0, 12);
        if (ImGui::Button("Upgrades", ImVec2(100, 40)))
            m_conquestShowUpgrades = !m_conquestShowUpgrades;
        ImGui::SameLine(0, 12);
        if (ImGui::Button("Arena", ImVec2(90, 40)))
            m_conquestShowArena = !m_conquestShowArena;
        ImGui::SameLine(0, 20);

        static const char* kChestNames[] = {"Wooden", "Iron", "Golden", "Grand"};
        for (int c = 0; c < 4; ++c) {
            auto ct = static_cast<ConquestMode::ChestType>(c);
            int n = m_conquest.chestCount(ct);
            char lbl[48];
            std::snprintf(lbl, sizeof(lbl), "%s Chest x%d##c%d", kChestNames[c], n, c);
            if (n <= 0) ImGui::BeginDisabled();
            if (ImGui::Button(lbl, ImVec2(150, 40))) {
                m_conquestChestResult = m_conquest.openChest(ct, m_registry);
                m_conquestShowChestResult = true;
            }
            if (n <= 0) ImGui::EndDisabled();
            if (c < 3) ImGui::SameLine();
        }
        ImGui::End();
    }

    // ── Chest result popup ────────────────────────────────────────────────────
    if (m_conquestShowChestResult) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
        ImGui::Begin("Chest Opened!", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        for (const auto& d : m_conquestChestResult.units) {
            conquestUnitIcon(d.defId, 24.f);
            ImGui::SameLine();
            ImGui::Text("+%d  %s (T%d)", d.count, d.name.c_str(), d.tier);
        }
        if (m_conquestChestResult.keysGained > 0) {
            static const char* kF[] = {"Holy Order","Crimson Wardens","Thornkin",
                "Eternal Empire","Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"};
            ImGui::TextColored({1.f, 0.8f, 0.2f, 1.f}, "+%d %s Key(s)",
                m_conquestChestResult.keysGained,
                kF[m_conquestChestResult.keysFaction % 9]);
        }
        if (m_conquestChestResult.gemsGained > 0)
            ImGui::TextColored({0.5f, 0.85f, 1.f, 1.f}, "+%d Gems",
                m_conquestChestResult.gemsGained);
        if (m_conquestChestResult.units.empty() &&
            m_conquestChestResult.keysGained == 0)
            ImGui::TextDisabled("(empty — no chest available)");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-1, 32)))
            m_conquestShowChestResult = false;
        ImGui::End();
    }

    // ── Army / collection screen ──────────────────────────────────────────────
    if (m_conquestShowArmy) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({760, 480}, ImGuiCond_Always);
        ImGui::Begin("Army & Collection", &m_conquestShowArmy,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        auto team = m_conquest.team();
        auto owned = m_conquest.collection();

        // Reserved counts (already in team) so the pool shows what's free
        auto reservedOf = [&](int defId) {
            int r = 0;
            for (auto& [d, c] : team) if (d == defId) r += c;
            return r;
        };

        ImGui::Columns(2, "##armyCols", true);

        // Left: collection pool
        ImGui::Text("Collection (open chests to gain units)");
        ImGui::Separator();
        ImGui::BeginChild("##pool", {0, 360});
        if (owned.empty())
            ImGui::TextDisabled("Empty. Clear Treasure/Elite/Boss nodes\nto earn chests, then open them below.");
        for (auto& [defId, count] : owned) {
            const UnitDef* d = m_registry.getUnitDef(defId);
            if (!d) continue;
            int freeCount = count - reservedOf(defId);
            conquestUnitIcon(defId, 28.f);
            ImGui::SameLine();
            char row[96];
            std::snprintf(row, sizeof(row), "%s (T%d)  x%d free##p%d",
                          d->name.c_str(), d->tier, freeCount, defId);
            bool canAdd = freeCount > 0 && (int)team.size() < 6;
            if (!canAdd) ImGui::BeginDisabled();
            if (ImGui::Button(row, ImVec2(-1, 28))) {
                // Merge into an existing slot of the same unit, else new slot
                bool merged = false;
                for (auto& [td, tc] : team)
                    if (td == defId) { tc += freeCount; merged = true; break; }
                if (!merged) team.emplace_back(defId, freeCount);
                m_conquest.setTeam(team);
            }
            if (!canAdd) ImGui::EndDisabled();
        }
        ImGui::EndChild();

        // Right: team slots
        ImGui::NextColumn();
        ImGui::Text("Battle Team (max 6 stacks)");
        ImGui::Separator();
        int removeIdx = -1;
        for (int i = 0; i < (int)team.size(); ++i) {
            const UnitDef* d = m_registry.getUnitDef(team[i].first);
            if (!d) continue;
            ImGui::PushID(i);
            conquestUnitIcon(team[i].first, 26.f);
            ImGui::SameLine();
            ImGui::Text("%d. %s (T%d)", i + 1, d->name.c_str(), d->tier);
            ImGui::SameLine(240);
            int c = team[i].second;
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputInt("##cnt", &c)) {
                int maxC = m_conquest.ownedCount(team[i].first);
                team[i].second = std::clamp(c, 1, maxC);
                m_conquest.setTeam(team);
            }
            ImGui::SameLine();
            if (ImGui::Button("X##rm", ImVec2(24, 22))) removeIdx = i;
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            team.erase(team.begin() + removeIdx);
            m_conquest.setTeam(team);
        }
        if (team.empty())
            ImGui::TextDisabled("No team set — battles will use a\ndefault army for your faction.");
        ImGui::Columns(1);

        ImGui::End();
    }

    // ── Quests panel (Phase 3) ────────────────────────────────────────────────
    if (m_conquestShowQuests) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({520, 440}, ImGuiCond_Always);
        ImGui::Begin("Quests", &m_conquestShowQuests,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        auto qs = m_conquest.quests();
        auto drawQuest = [&](const Quest& q) {
            ImGui::PushID(q.id);
            QuestReward rw = QuestRewards::forQuest(q);
            ImGui::Text("%s", q.text.c_str());
            ImGui::SameLine(300);
            ImGui::ProgressBar((float)q.progress / (float)std::max(1, q.target),
                               ImVec2(120, 16));
            ImGui::SameLine();
            if (q.claimed) {
                ImGui::TextDisabled("Claimed");
            } else if (q.complete()) {
                if (ImGui::Button("Claim", ImVec2(70, 20)))
                    m_conquest.claimQuest(q.id, m_registry);
            } else {
                ImGui::TextDisabled("%s", QuestRewards::describe(rw).c_str());
            }
            ImGui::PopID();
        };

        ImGui::TextColored({1.f, 0.85f, 0.3f, 1.f}, "Daily");
        ImGui::Separator();
        for (auto& q : qs) if (!q.weekly) drawQuest(q);
        ImGui::Spacing();
        ImGui::TextColored({0.6f, 0.8f, 1.f, 1.f}, "Weekly");
        ImGui::Separator();
        for (auto& q : qs) if (q.weekly) drawQuest(q);

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextDisabled("Daily resets at midnight; weekly every 7 days.");
        ImGui::End();
    }

    // ── Gem shop: buy chests (Phase 3) ────────────────────────────────────────
    if (m_conquestShowGemShop) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({320, 0}, ImGuiCond_Always);
        ImGui::Begin("Gem Shop", &m_conquestShowGemShop,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::Text("Gems: %d", m_conquest.gems());
        ImGui::Separator();
        static const char* kChestNames[] = {"Wooden", "Iron", "Golden", "Grand"};
        for (int c = 0; c < 4; ++c) {
            auto ct = static_cast<ConquestMode::ChestType>(c);
            int price = ConquestMode::chestGemPrice(ct);
            char lbl[64];
            std::snprintf(lbl, sizeof(lbl), "Buy %s Chest — %d gems##buy%d",
                          kChestNames[c], price, c);
            bool afford = m_conquest.gems() >= price;
            if (!afford) ImGui::BeginDisabled();
            if (ImGui::Button(lbl, ImVec2(-1, 30)))
                m_conquest.buyChestWithGems(ct);
            if (!afford) ImGui::EndDisabled();
        }
        ImGui::End();
    }

    // ── Unit path upgrade screen (Phase 4) ────────────────────────────────────
    if (m_conquestShowUpgrades) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({640, 520}, ImGuiCond_Always);
        ImGui::Begin("Unit Path Upgrades", &m_conquestShowUpgrades,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        static const char* kF[] = {"Holy Order","Crimson Wardens","Thornkin",
            "Eternal Empire","Bloodsworn","Voidkin","Iron Assembly","Amalgamate","Convergence"};
        ImGui::Text("Spend faction Keys to permanently pick Path A or B per tier.");
        ImGui::TextDisabled("Applies to every owned & future unit of that tier. Respec costs %d gems.",
                            ConquestMode::respecGemCost());
        ImGui::Separator();

        ImGui::SetNextItemWidth(200);
        ImGui::Combo("Faction", &m_conquestUpgradeFaction, kF, 9);
        int f = m_conquestUpgradeFaction;
        ImGui::SameLine(0, 30);
        ImGui::TextColored({1.f, 0.85f, 0.2f, 1.f}, "%s Keys: %d",
                           kF[f], m_conquest.keys(f));
        ImGui::Separator();

        if (ImGui::BeginTable("##upg", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Tier");
            ImGui::TableSetupColumn("Path A");
            ImGui::TableSetupColumn("Path B");
            ImGui::TableSetupColumn("Chosen");
            ImGui::TableSetupColumn("Cost");
            ImGui::TableHeadersRow();

            for (int tier = 1; tier <= 5; ++tier) {
                ImGui::TableNextRow();
                ImGui::PushID(tier);

                const UnitDef* a = m_registry.getUnitDef((FactionId)f, tier, UpgradePath::PathA);
                const UnitDef* b = m_registry.getUnitDef((FactionId)f, tier, UpgradePath::PathB);
                int chosen = m_conquest.pathChoice(f, tier);
                int cost   = ConquestMode::keyCostForTier(tier);
                bool canAfford = m_conquest.keys(f) >= cost;

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("T%d", tier);

                ImGui::TableSetColumnIndex(1);
                if (chosen == 0) {
                    if (!canAfford) ImGui::BeginDisabled();
                    char lbl[64]; std::snprintf(lbl, sizeof(lbl), "%s##a%d",
                        a ? a->name.c_str() : "Path A", tier);
                    if (ImGui::Button(lbl, ImVec2(-1, 0)))
                        m_conquest.chooseUnitPath(f, tier, 1);
                    if (!canAfford) ImGui::EndDisabled();
                } else {
                    ImGui::TextColored(chosen == 1 ? ImVec4(0.4f,1,0.4f,1) : ImVec4(0.5f,0.5f,0.5f,1),
                        "%s", a ? a->name.c_str() : "Path A");
                }

                ImGui::TableSetColumnIndex(2);
                if (chosen == 0) {
                    if (!canAfford) ImGui::BeginDisabled();
                    char lbl[64]; std::snprintf(lbl, sizeof(lbl), "%s##b%d",
                        b ? b->name.c_str() : "Path B", tier);
                    if (ImGui::Button(lbl, ImVec2(-1, 0)))
                        m_conquest.chooseUnitPath(f, tier, 2);
                    if (!canAfford) ImGui::EndDisabled();
                } else {
                    ImGui::TextColored(chosen == 2 ? ImVec4(0.4f,1,0.4f,1) : ImVec4(0.5f,0.5f,0.5f,1),
                        "%s", b ? b->name.c_str() : "Path B");
                }

                ImGui::TableSetColumnIndex(3);
                if (chosen == 0) {
                    ImGui::TextDisabled("—");
                } else {
                    ImGui::Text("Path %s", chosen == 1 ? "A" : "B");
                    // Respec button switches to the other path for gems
                    bool canRespec = m_conquest.gems() >= ConquestMode::respecGemCost();
                    if (!canRespec) ImGui::BeginDisabled();
                    if (ImGui::SmallButton("Swap"))
                        m_conquest.respecUnitPath(f, tier, chosen == 1 ? 2 : 1);
                    if (!canRespec) ImGui::EndDisabled();
                }

                ImGui::TableSetColumnIndex(4);
                if (chosen == 0) ImGui::Text("%d keys", cost);
                else             ImGui::TextDisabled("%d gems", ConquestMode::respecGemCost());

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }

    // ── Arena (Phase 5) ───────────────────────────────────────────────────────
    if (m_conquestShowArena) {
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({420, 0}, ImGuiCond_Always);
        ImGui::Begin("Arena", &m_conquestShowArena,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        int myPower  = m_conquest.teamPower(m_registry);
        int oppPower = m_conquest.arenaOpponentPower(m_registry);
        int left     = m_conquest.arenaEntriesLeft();

        ImGui::Text("Exhibition fights — no casualties, win or lose.");
        ImGui::Separator();
        ImGui::Text("Rank points: %d", m_conquest.arenaPoints());
        ImGui::Text("Your team power:   %d", myPower);
        ImGui::Text("Opponent power:    ~%d", oppPower);
        ImGui::Spacing();
        if (left > 0)
            ImGui::Text("Free entries today: %d / %d",
                        left, ConquestMode::ARENA_ENTRIES_PER_DAY);
        else
            ImGui::TextColored({1.f, 0.7f, 0.3f, 1.f},
                "No free entries left — extra fight costs %d gems.",
                ConquestMode::ARENA_EXTRA_ENTRY_GEMS);
        ImGui::Spacing();

        bool canEnter = m_conquest.arenaCanEnter() && myPower > 0;
        if (myPower <= 0) {
            ImGui::TextDisabled("Assemble a team first (Army / Collection).");
        }
        if (!canEnter) ImGui::BeginDisabled();
        if (ImGui::Button("Fight!", ImVec2(-1, 40))) {
            if (m_conquest.arenaConsumeEntry()) {
                m_conquestShowArena = false;
                startArenaBattle();
            }
        }
        if (!canEnter) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::TextDisabled("Win streaks grant bonus points; 3 wins = Golden chest.\n"
                            "Rank resets each week.");
        ImGui::End();
    }

    endImGuiFrame();
}

void Game::startConquestBattle(int nodeIndex)
{
    m_conquestActiveNode = nodeIndex;

    const ConquestHero& ch = m_conquest.hero();

    // Player: assembled team from the collection (Phase 2); falls back to a
    // generated army for the hero's faction if no team is set.
    Hero playerHero = ArmyBuilder::buildHero(ch.faction, std::max(1, ch.level));
    playerHero.name    = ch.name;
    playerHero.level   = ch.level;
    playerHero.classId = ch.classId;
    playerHero.attack  = ch.attack + (ch.level - 1) / 2;
    playerHero.defense = ch.defense + (ch.level - 1) / 2;

    std::vector<CombatUnit> playerUnits;
    m_conquestDeployed.clear();
    auto team = m_conquest.team();
    if (!team.empty()) {
        int slot = 0;
        for (auto& [defId, wantCount] : team) {
            int ownedNow = m_conquest.ownedCount(defId);
            int count = std::min(wantCount, ownedNow);
            if (count <= 0) continue;
            // Resolve the collection's base unit to the player's chosen A/B
            // variant (Phase 4). The pool always stores base defIds; the
            // upgrade is applied at deploy time.
            int variantId = m_conquest.resolveVariant(defId, m_registry);
            const UnitDef* d = m_registry.getUnitDef(variantId);
            if (!d) d = m_registry.getUnitDef(defId);
            if (!d) continue;
            playerUnits.push_back(ArmyBuilder::makeCombatUnit(*d, count, slot++));
            m_conquestDeployed.emplace_back(defId, count);   // track by BASE id
        }
    }
    if (playerUnits.empty())   // no team (or team invalid) → generated fallback
        playerUnits = ArmyBuilder::buildArmy(ch.faction, std::max(1, ch.level));

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

void Game::startArenaBattle()
{
    const ConquestHero& ch = m_conquest.hero();

    // Player: same team as map battles, path variants applied. No casualties
    // are tracked — arena is exhibition (see CONQUEST_MODE.md).
    Hero playerHero = ArmyBuilder::buildHero(ch.faction, std::max(1, ch.level));
    playerHero.name    = ch.name;
    playerHero.level   = ch.level;
    playerHero.classId = ch.classId;
    playerHero.attack  = ch.attack + (ch.level - 1) / 2;
    playerHero.defense = ch.defense + (ch.level - 1) / 2;

    std::vector<CombatUnit> playerUnits;
    auto team = m_conquest.team();
    int slot = 0;
    for (auto& [defId, wantCount] : team) {
        int owned = m_conquest.ownedCount(defId);
        int count = std::min(wantCount, owned);
        if (count <= 0) continue;
        int variantId = m_conquest.resolveVariant(defId, m_registry);
        const UnitDef* d = m_registry.getUnitDef(variantId);
        if (!d) d = m_registry.getUnitDef(defId);
        if (!d) continue;
        playerUnits.push_back(ArmyBuilder::makeCombatUnit(*d, count, slot++));
    }
    if (playerUnits.empty())
        playerUnits = ArmyBuilder::buildArmy(ch.faction, std::max(1, ch.level));

    // Opponent: a "ghost" army power-matched to the player. Approximate a
    // target power by scaling a random faction's generated army (weeks) until
    // its power is closest to arenaOpponentPower().
    int targetPower = m_conquest.arenaOpponentPower(m_registry);
    uint32_t seed = (uint32_t)m_conquest.week() * 40503u
                  ^ (uint32_t)m_conquest.arenaPoints() * 2654435761u;
    FactionId ef = static_cast<FactionId>(seed % 9);

    int bestWeeks = 1, bestDiff = 1 << 30;
    for (int w = 1; w <= 40; ++w) {
        int p = ArmyBuilder::armyPower(ef, w);
        int diff = std::abs(p - targetPower);
        if (diff < bestDiff) { bestDiff = diff; bestWeeks = w; }
    }
    Hero enemyHero = ArmyBuilder::buildHero(ef, bestWeeks);
    auto enemyUnits = ArmyBuilder::buildArmy(ef, bestWeeks);

    int pid = 1, eid = 50;
    for (auto& u : playerUnits) { u.id = pid++; u.isPlayer = true;  u.factionHint = (int)ch.faction; }
    for (auto& u : enemyUnits)  { u.id = eid++; u.isPlayer = false; u.factionHint = (int)ef; }

    m_conquestInArena = true;
    m_conquestDeployed.clear();   // arena = no casualties
    enterCombat(playerHero, playerUnits, enemyHero, enemyUnits);
}

void Game::onArenaBattleEnd(bool victory)
{
    // NO casualties — collection untouched. Only points/rewards change.
    int delta = m_conquest.arenaReportResult(victory);
    gLog("Arena %s (%+d pts, now %d)\n",
         victory ? "won" : "lost", delta, m_conquest.arenaPoints());
    m_conquestInArena = false;
    m_audio.playMusic("worldmap_music");
    m_state = GameState::Conquest;
}

void Game::onConquestBattleEnd(bool victory)
{
    // ── Casualties (Phase 2): units that didn't survive leave the collection.
    // Survivors (including after a retreat) return to the pool untouched.
    if (!m_conquestDeployed.empty()) {
        for (auto& [baseId, sent] : m_conquestDeployed) {
            // Survivors carry the resolved variant defId; map back to base.
            int variantId = m_conquest.resolveVariant(baseId, m_registry);
            int survived = 0;
            for (const auto& cu : m_combat.grid().units())
                if (cu.isPlayer && cu.alive &&
                    (cu.defId == baseId || cu.defId == variantId))
                    survived += cu.count;
            int losses = sent - std::min(survived, sent);
            if (losses > 0) m_conquest.addUnits(baseId, -losses);
        }
        m_conquestDeployed.clear();
        // Clamp team counts to what's still owned so the next fight is valid
        auto team = m_conquest.team();
        std::vector<std::pair<int,int>> pruned;
        for (auto& [defId, cnt] : team) {
            int ownedNow = m_conquest.ownedCount(defId);
            if (ownedNow > 0) pruned.emplace_back(defId, std::min(cnt, ownedNow));
        }
        m_conquest.setTeam(pruned);
    }

    if (victory && m_conquestActiveNode >= 0) {
        int levelsGained = m_conquest.grantVictoryRewards(m_conquestActiveNode);
        m_conquest.clearNode(m_conquestActiveNode);

        // Multi-faction quest: count distinct factions among deployed units
        {
            std::vector<int> seen;
            for (const auto& cu : m_combat.grid().units()) {
                if (!cu.isPlayer || cu.defId == 0) continue;
                const UnitDef* d = m_registry.getUnitDef(cu.defId);
                if (!d) continue;
                int f = (int)d->faction;
                if (std::find(seen.begin(), seen.end(), f) == seen.end())
                    seen.push_back(f);
            }
            if ((int)seen.size() >= 3)
                m_conquest.reportEvent(QuestEvent::MultiFactionWin);
        }

        if (levelsGained > 0)
            gLog("Conquest: level up! Now level %d\n", m_conquest.currentLevel());
    } else {
        m_conquest.onDefeat();
    }
    m_conquestActiveNode = -1;
    m_audio.playMusic("worldmap_music");
    m_state = GameState::Conquest;
}
