#include "TownScreen.h"
#include <sstream>
#include <algorithm>
#include <imgui.h>

bool TownScreen::init(int sw, int sh)
{
    buildLayout(sw, sh);
    return true;
}

void TownScreen::buildLayout(int sw, int sh)
{
    m_screenW = sw; m_screenH = sh;

    float pw = sw * 0.88f, ph = sh * 0.88f;
    float px = (sw - pw) * 0.5f, py = (sh - ph) * 0.5f;

    m_mainPanel = Panel({px, py, pw, ph});

    // Close button — larger and clearly labeled
    m_closeBtn = Button("EXIT TOWN", {px + pw - 110.0f, py + 4.0f, 104.0f, 28.0f},
                         [this]{ close(); if(onClose) onClose(); });
    m_closeBtn.colorBorder = UIColor::hex(UITheme::DANGER_RED, 0.7f);
    m_closeBtn.colorText   = UIColor::hex(UITheme::DANGER_RED);

    // Building tree — left 55%
    m_buildPanel = Panel({px + 4, py + 36, pw * 0.55f - 4, ph - 44});
    m_buildPanel.title = "Buildings";

    // Recruit panel — top right
    m_recruitPanel = Panel({px + pw*0.55f + 4, py + 36, pw*0.45f - 8, ph*0.55f});
    m_recruitPanel.title = "Recruit";

    // Income panel — bottom right
    m_incomePanel = Panel({px + pw*0.55f + 4, py + 36 + ph*0.55f + 4,
                           pw*0.45f - 8, ph*0.45f - 44});
    m_incomePanel.title = "Weekly Income";
}

void TownScreen::open(Town* town, Resources* playerRes, const BuildingRegistry* registry,
                      Hero* visitingHero, int currentWeek, int blueprintDiscount)
{
    m_town              = town;
    m_playerRes         = playerRes;
    m_registry          = registry;
    m_hero              = visitingHero;
    m_open              = true;
    m_currentWeek       = currentWeek;
    m_blueprintDiscount = blueprintDiscount;

    m_mainPanel.title = town->name + " — " + [town]{
        switch(town->faction) {
            case FactionId::HolyOrder:     return "Holy Order";
            case FactionId::Bloodsworn:    return "Bloodsworn";
            case FactionId::Thornkin:      return "Thornkin";
            case FactionId::EternalEmpire: return "Eternal Empire";
            case FactionId::CrimsonWardens:return "Crimson Wardens";
            case FactionId::Voidkin:       return "Voidkin";
            case FactionId::IronAssembly:  return "Iron Assembly";
            case FactionId::Amalgamate:    return "Amalgamate";
            case FactionId::Convergence:   return "Convergence";
            default: return "Unknown";
        }
    }();

    rebuildBuildingButtons();
    rebuildRecruitButtons();
}

void TownScreen::rebuildBuildingButtons()
{
    // Building buttons are now rendered inline via ImGui in drawBuildingTree().
    m_buildBtns.clear();
}

void TownScreen::rebuildRecruitButtons()
{
    m_recruitBtns.clear();
    if (!m_town) return;

    float x = m_recruitPanel.bounds.x + 8;
    float y = m_recruitPanel.bounds.y + 28;
    float bw = m_recruitPanel.bounds.w - 16;

    for (auto& dw : m_town->dwellings) {
        if (dw.available <= 0) continue;

        // Look up unit name from registry
        std::string unitName = "T" + std::to_string(dw.tier);
        int costPerUnit = 0;
        if (m_registry) {
            for (const auto& ud : m_registry->units()) {
                if (ud.faction == m_town->faction && ud.tier == dw.tier
                    && ud.path == dw.path) {
                    unitName   = ud.name;
                    costPerUnit = ud.cost.get(ResourceType::Gold);
                    break;
                }
            }
        }
        bool efficient = m_hero && m_hero->efficientSpecialty;
        int effectiveCostPerUnit = efficient ? static_cast<int>(costPerUnit * 0.8f) : costPerUnit;

        RecruitBtn rb;
        rb.tier      = dw.tier;
        rb.available = dw.available;

        // Build stat tooltip
        if (m_registry) {
            for (const auto& ud : m_registry->units()) {
                if (ud.faction == m_town->faction && ud.tier == dw.tier && ud.path == dw.path) {
                    rb.defId = ud.id;
                    std::ostringstream ts;
                    ts << ud.name << "  |  ";
                    ts << "ATK " << ud.attack << "  DEF " << ud.defense;
                    ts << "  HP " << ud.hp << "  Dmg " << ud.damage_min << "-" << ud.damage_max;
                    ts << "  Spd " << ud.speed;
                    if (ud.range > 0) ts << "  Rng " << ud.range << " (" << ud.shots << " shots)";
                    if (ud.flying)     ts << "  [Flying]";
                    if (ud.vampiric)   ts << "  [Vampiric]";
                    if (ud.regenerates) ts << "  [Regenerates]";
                    if (efficient)
                        ts << "  |  " << effectiveCostPerUnit << "g each (-20% Efficient)";
                    else
                        ts << "  |  " << costPerUnit << "g each";
                    rb.statTip = ts.str();
                    break;
                }
            }
        }

        std::string label = unitName
            + "  x" + std::to_string(dw.available)
            + "  (" + std::to_string(effectiveCostPerUnit * dw.available) + "g)"
            + (efficient ? " [Efficient]" : "");
        rb.btn = Button(label, {x, y, bw, 26.0f});
        rb.btn.colorBorder = UIColor::hex(UITheme::NATURE_GREEN, 0.6f);

        int capturedTier = dw.tier;
        UpgradePath capturedPath = dw.path;
        rb.btn.onClick = [this, capturedTier, capturedPath]{
            if (!m_town || !m_playerRes || !m_registry) return;
            const UnitDef* matchedUd = nullptr;
            for (const auto& ud : m_registry->units()) {
                if (ud.faction == m_town->faction && ud.tier == capturedTier
                    && ud.path == capturedPath) {
                    matchedUd = &ud; break;
                }
            }
            if (!matchedUd) return;

            // Route to garrison or hero army based on toggle (hero required for army)
            bool useGarrison = m_recruitToGarrison || !m_hero;
            std::vector<UnitStack>& target = useGarrison ? m_town->garrison : m_hero->army;
            bool alreadyHasStack = false;
            for (const auto& s : target)
                if (s.defId == matchedUd->id) { alreadyHasStack = true; break; }
            if (!alreadyHasStack && target.size() >= 7) return;

            float costMult = (m_hero && m_hero->efficientSpecialty) ? 0.8f : 1.0f;
            int recruited = m_town->recruit(capturedTier, 999, *m_playerRes, m_registry->units(), costMult);
            if (recruited > 0) {
                bool merged = false;
                for (auto& s : target)
                    if (s.defId == matchedUd->id) { s.count += recruited; merged = true; break; }
                if (!merged)
                    target.push_back({matchedUd->id, recruited});
            }
            rebuildRecruitButtons();
        };

        m_recruitBtns.push_back(rb);
        y += 30.0f;
        if (y + 26 > m_recruitPanel.bounds.bottom() - 4) break;
    }
}

void TownScreen::draw(UIRenderer& rdr)
{
    if (!m_open) return;

    // Dim background
    rdr.drawRect({0,0,(float)m_screenW,(float)m_screenH},
                 UIColor::rgba(0,0,0,0.6f));

    // Faction artwork — fills the entire right column (recruit + income panels)
    if (m_townBannerTex) {
        float rx  = m_recruitPanel.bounds.x;
        float ry  = m_recruitPanel.bounds.y;
        float rw  = m_recruitPanel.bounds.w;
        // Span from top of recruit panel to bottom of income panel
        float rh  = (m_incomePanel.bounds.y + m_incomePanel.bounds.h) - ry;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddImageRounded(m_townBannerTex, {rx + 2, ry + 2}, {rx + rw - 2, ry + rh - 2},
                            {0,0}, {1,1}, IM_COL32(255, 255, 255, 215), 6.0f);
        // Dark gradient over the bottom 40% so income text stays readable
        float gradStart = ry + rh * 0.60f;
        dl->AddRectFilledMultiColor(
            {rx + 2, gradStart}, {rx + rw - 2, ry + rh - 2},
            IM_COL32(0,0,0,0), IM_COL32(0,0,0,0),
            IM_COL32(0,0,0,190), IM_COL32(0,0,0,190));
        // Semi-transparent panel header stripe at top so "Recruit" title reads
        dl->AddRectFilled({rx + 2, ry + 2}, {rx + rw - 2, ry + 24},
                          IM_COL32(0, 0, 0, 120));
    }

    m_mainPanel.draw(rdr);
    if (m_panoramaMode) drawPanorama(rdr);
    else                drawBuildingTree(rdr);
    drawRecruitPanel(rdr);
    drawIncomePanel(rdr);
    m_closeBtn.draw(rdr);
    // Draw tooltip via ImGui foreground draw list so it appears above all ImGui windows
    if (m_tooltip.visible && !m_tooltip.text.empty()) {
        ImDrawList* fdl = ImGui::GetForegroundDrawList();
        float tx = m_tooltip.bounds.x + 4.0f;
        float ty = m_tooltip.bounds.y - 4.0f;
        ImVec2 tsz = ImGui::CalcTextSize(m_tooltip.text.c_str(), nullptr, false, 320.0f);
        float pad = 6.0f;
        fdl->AddRectFilled({tx - pad, ty - pad},
                           {tx + tsz.x + pad, ty + tsz.y + pad},
                           IM_COL32(18, 14, 8, 228), 4.0f);
        fdl->AddRect({tx - pad, ty - pad},
                     {tx + tsz.x + pad, ty + tsz.y + pad},
                     IM_COL32(180, 145, 55, 200), 4.0f);
        fdl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {tx, ty},
                     IM_COL32(240, 225, 175, 255),
                     m_tooltip.text.c_str(), nullptr, 320.0f);
    }
}

void TownScreen::drawBuildingTree(UIRenderer& rdr)
{
    m_buildPanel.draw(rdr);
    if (!m_town || !m_registry || !m_playerRes) return;

    ImGui::SetNextWindowPos({m_buildPanel.bounds.x, m_buildPanel.bounds.y});
    ImGui::SetNextWindowSize({m_buildPanel.bounds.w, m_buildPanel.bounds.h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 28));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4, 3));

    ImGui::Begin("##build_tree", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float bw  = (ImGui::GetContentRegionAvail().x - 4.0f) * 0.5f - 2.0f;
    const float bh  = 36.0f;
    const float iSz = 28.0f;  // icon size drawn inside each button
    // UV column width per category in the 6-column icon atlas
    static constexpr float kIconUvW = 1.0f / 6.0f;

    auto costStr = [](const Resources& cost) -> std::string {
        std::string s;
        if (cost.get(ResourceType::Gold) > 0)
            s += std::to_string(cost.get(ResourceType::Gold)) + "g";
        for (int ri = 1; ri < RESOURCE_COUNT; ++ri) {
            auto rt = static_cast<ResourceType>(ri);
            int v = cost.get(rt);
            if (v > 0) {
                if (!s.empty()) s += " ";
                s += std::to_string(v) + resourceName(rt)[0];
            }
        }
        return s.empty() ? "free" : s;
    };

    // View toggle
    if (ImGui::Button("Panorama View")) { m_panoramaMode = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("— click to build");

    int  col        = 0;
    bool needRebuild = false;

    for (const auto& def : m_registry->buildings()) {
        if (def.faction != FactionId::None && def.faction != m_town->faction) continue;

        bool built      = m_town->hasBuilding(def.id);
        bool limitReach = m_town->builtToday >= 1;
        bool prereqMet  = m_town->canBuild(def.id, m_registry->buildings(),
                                            m_currentWeek, m_blueprintDiscount);
        bool affordable = m_playerRes->canAfford(def.cost);

        std::string label;
        if (built) {
            label = "[BUILT] " + def.name;
        } else if (limitReach && prereqMet) {
            label = "[1/day] " + def.name + "  [" + costStr(def.cost) + "]";
        } else if (!prereqMet && m_currentWeek > 0 && def.minWeek > 0) {
            int effectiveMin = std::max(1, def.minWeek - m_blueprintDiscount);
            if (m_currentWeek < effectiveMin)
                label = "[Wk" + std::to_string(effectiveMin) + "] " + def.name + " " + costStr(def.cost);
            else
                label = def.name + "  [" + costStr(def.cost) + "]";
        } else {
            label = def.name + (built ? "" : "  [" + costStr(def.cost) + "]");
        }

        if (built) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.08f,0.20f,0.08f,0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f,0.25f,0.10f,0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.45f,0.62f,0.45f,1.0f));
        } else if (!prereqMet || limitReach) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f,0.14f,0.14f,0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f,0.18f,0.18f,0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.38f,0.38f,0.38f,1.0f));
        } else if (!affordable) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.28f,0.08f,0.08f,0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f,0.10f,0.10f,0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f,0.30f,0.30f,1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f,0.18f,0.06f,0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f,0.24f,0.08f,0.9f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.90f,0.80f,0.30f,1.0f));
        }

        if (col == 1) ImGui::SameLine(0, 4);

        // Look up per-building art first, fall back to category icon atlas
        auto artIt = m_buildingArt.find(def.id);
        ImTextureID artTex = (artIt != m_buildingArt.end()) ? artIt->second : nullptr;
        bool hasIcon = artTex || m_buildingIconTex;

        bool clicked = false;
        if (built || limitReach || !prereqMet) ImGui::BeginDisabled();

        // Pad label so icon doesn't overlap text
        std::string btnId = (hasIcon ? "      " : "") + label + "##b" + std::to_string(def.id);
        if (ImGui::Button(btnId.c_str(), {bw, bh})) clicked = true;

        // Overlay icon on button — per-building art takes priority over category atlas
        if (hasIcon) {
            ImVec2 btnMin = ImGui::GetItemRectMin();
            float  iX = btnMin.x + 3.0f;
            float  iY = btnMin.y + (bh - iSz) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 tint = built ? IM_COL32(180,220,180,200) : IM_COL32(255,255,255,220);
            if (artTex) {
                dl->AddImage(artTex, {iX, iY}, {iX + iSz, iY + iSz}, {0,0}, {1,1}, tint);
            } else {
                float u0 = kIconUvW * static_cast<int>(def.category);
                dl->AddImage(m_buildingIconTex, {iX, iY}, {iX + iSz, iY + iSz},
                             {u0, 0.0f}, {u0 + kIconUvW, 1.0f}, tint);
            }
        }

        if (built || limitReach || !prereqMet) ImGui::EndDisabled();

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            // Show building art preview if available
            if (artTex) {
                ImGui::Image(artTex, {128, 128});
                ImGui::SameLine();
                ImGui::BeginGroup();
            }
            ImGui::Text("%s", def.name.c_str());
            if (!def.description.empty())
                ImGui::TextDisabled("%s", def.description.c_str());
            if (!built) {
                ImGui::Separator();
                ImGui::Text("Cost: %s", costStr(def.cost).c_str());
            }
            if (artTex) ImGui::EndGroup();
            ImGui::EndTooltip();
        }

        col = 1 - col;

        if (clicked) {
            m_town->build(def.id, m_registry->buildings(), *m_playerRes);
            needRebuild = true;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    if (needRebuild) rebuildRecruitButtons();
}

void TownScreen::drawPanorama(UIRenderer& rdr)
{
    m_buildPanel.draw(rdr);
    if (!m_town || !m_registry || !m_playerRes) return;

    ImGui::SetNextWindowPos({m_buildPanel.bounds.x, m_buildPanel.bounds.y});
    ImGui::SetNextWindowSize({m_buildPanel.bounds.w, m_buildPanel.bounds.h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 28));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(5, 5));

    ImGui::Begin("##panorama_view", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Town banner as subtle tinted background
    if (m_townBannerTex) {
        ImVec2 wMin = ImGui::GetWindowPos();
        ImVec2 wSz  = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddImage(
            m_townBannerTex, wMin, {wMin.x + wSz.x, wMin.y + wSz.y},
            {0,0}, {1,1}, IM_COL32(255,255,255,55));
        // Darken top strip for buttons to remain readable
        ImGui::GetWindowDrawList()->AddRectFilled(
            wMin, {wMin.x + wSz.x, wMin.y + 24}, IM_COL32(0,0,0,140));
    }

    if (ImGui::Button("List View")) m_panoramaMode = false;
    ImGui::SameLine();
    ImGui::TextDisabled("— click a building to construct");

    auto costStr = [](const Resources& cost) -> std::string {
        std::string s;
        if (cost.get(ResourceType::Gold) > 0)
            s += std::to_string(cost.get(ResourceType::Gold)) + "g";
        for (int ri = 1; ri < RESOURCE_COUNT; ++ri) {
            auto rt = static_cast<ResourceType>(ri);
            int v = cost.get(rt);
            if (v > 0) {
                if (!s.empty()) s += " ";
                s += std::to_string(v) + resourceName(rt)[0];
            }
        }
        return s.empty() ? "free" : s;
    };

    const float avail  = ImGui::GetContentRegionAvail().x;
    const float cardW  = (avail - 10.0f) / 3.0f;
    const float artSz  = cardW - 8.0f;
    const float nameH  = 30.0f;
    const float cardH  = artSz + nameH;

    int col = 0;
    bool needRebuild = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (const auto& def : m_registry->buildings()) {
        if (def.faction != FactionId::None && def.faction != m_town->faction) continue;

        bool built      = m_town->hasBuilding(def.id);
        bool limitReach = m_town->builtToday >= 1;
        bool prereqMet  = m_town->canBuild(def.id, m_registry->buildings(),
                                            m_currentWeek, m_blueprintDiscount);
        bool affordable = m_playerRes->canAfford(def.cost);
        bool canBuild   = prereqMet && affordable && !built && !limitReach;

        auto artIt     = m_buildingArt.find(def.id);
        ImTextureID artTex = (artIt != m_buildingArt.end()) ? artIt->second : nullptr;

        if (col > 0) ImGui::SameLine(0, 5);

        ImGui::BeginGroup();
        ImVec2 cMin = ImGui::GetCursorScreenPos();
        ImVec2 cMax = {cMin.x + cardW, cMin.y + cardH};

        // Card background
        ImU32 bgCol = built      ? IM_COL32(15, 40, 15, 220)
                    : canBuild   ? IM_COL32(40, 32, 8,  220)
                    : prereqMet  ? IM_COL32(45, 10, 10, 200)
                                 : IM_COL32(12, 12, 18, 210);
        dl->AddRectFilled(cMin, cMax, bgCol, 5.0f);

        // Art or category icon
        ImU32 artTint = built      ? IM_COL32(180,255,180,240)
                      : canBuild   ? IM_COL32(255,255,255,255)
                      : prereqMet  ? IM_COL32(200,100,100,180)
                                   : IM_COL32(90, 90, 90,  160);
        ImVec2 artMin = {cMin.x + 4, cMin.y + 4};
        ImVec2 artMax = {cMin.x + 4 + artSz, cMin.y + 4 + artSz};

        if (artTex) {
            dl->AddImageRounded(artTex, artMin, artMax, {0,0},{1,1}, artTint, 4.0f);
        } else if (m_buildingIconTex) {
            static constexpr float kUvW = 1.0f / 6.0f;
            float u0 = kUvW * static_cast<int>(def.category);
            dl->AddImageRounded(m_buildingIconTex, artMin, artMax,
                                {u0,0},{u0+kUvW,1}, artTint, 4.0f);
        } else {
            dl->AddRectFilled(artMin, artMax, IM_COL32(30,30,40,200), 4.0f);
        }

        // Status badge overlay (top-right corner of art)
        if (built) {
            dl->AddRectFilled({artMax.x-34, artMin.y+2},{artMax.x-2, artMin.y+14},
                              IM_COL32(20,120,20,220), 3.0f);
            dl->AddText({artMax.x-32, artMin.y+3}, IM_COL32(150,255,150,255), "BUILT");
        } else if (!prereqMet) {
            dl->AddRectFilled({artMax.x-34, artMin.y+2},{artMax.x-2, artMin.y+14},
                              IM_COL32(30,30,30,220), 3.0f);
            dl->AddText({artMax.x-32, artMin.y+3}, IM_COL32(100,100,100,255), "LOCK");
        } else if (!affordable) {
            dl->AddRectFilled({artMax.x-34, artMin.y+2},{artMax.x-2, artMin.y+14},
                              IM_COL32(100,20,20,220), 3.0f);
            dl->AddText({artMax.x-32, artMin.y+3}, IM_COL32(255,100,100,255), "COST");
        }

        // Name + cost below art
        ImGui::SetCursorScreenPos({cMin.x + 4, cMin.y + 4 + artSz + 3});
        ImGui::PushStyleColor(ImGuiCol_Text,
            built ? ImVec4(0.45f,0.75f,0.45f,1.f)
          : canBuild ? ImVec4(0.92f,0.82f,0.30f,1.f)
          : ImVec4(0.45f,0.45f,0.45f,1.f));
        ImGui::TextUnformatted(def.name.c_str());
        ImGui::PopStyleColor();

        if (!built) {
            ImGui::SetCursorScreenPos({cMin.x + 4, cMin.y + 4 + artSz + 16});
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f,0.55f,0.3f,0.9f));
            std::string cs = costStr(def.cost);
            ImGui::TextUnformatted(cs.c_str());
            ImGui::PopStyleColor();
        }

        // Invisible clickable overlay on art area
        ImGui::SetCursorScreenPos(artMin);
        std::string btnId = "##pan" + std::to_string(def.id);
        ImGui::InvisibleButton(btnId.c_str(), {artSz, artSz});

        bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImU32 hlCol = canBuild ? IM_COL32(220,190,60,220) : IM_COL32(140,140,140,180);
            dl->AddRect(cMin, cMax, hlCol, 5.0f, 0, 2.0f);
            ImGui::BeginTooltip();
            if (artTex) { ImGui::Image(artTex, {96,96}); ImGui::SameLine(); ImGui::BeginGroup(); }
            ImGui::Text("%s", def.name.c_str());
            if (!def.description.empty()) ImGui::TextDisabled("%s", def.description.c_str());
            ImGui::Separator();
            if (built)
                ImGui::TextColored({0.4f,0.8f,0.4f,1.f}, "Already built");
            else {
                ImGui::Text("Cost: %s", costStr(def.cost).c_str());
                if (!prereqMet)  ImGui::TextColored({0.6f,0.4f,0.4f,1.f}, "Prerequisites not met");
                else if (limitReach) ImGui::TextColored({0.6f,0.6f,0.2f,1.f}, "Already built today");
                else if (!affordable) ImGui::TextColored({0.8f,0.3f,0.3f,1.f}, "Cannot afford");
                else ImGui::TextColored({0.4f,0.9f,0.4f,1.f}, "Click to build");
            }
            if (artTex) ImGui::EndGroup();
            ImGui::EndTooltip();
        }

        if (ImGui::IsItemClicked() && canBuild) {
            m_town->build(def.id, m_registry->buildings(), *m_playerRes);
            needRebuild = true;
        }

        ImGui::SetCursorScreenPos({cMin.x, cMax.y + 5});
        ImGui::EndGroup();

        col = (col + 1) % 3;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    if (needRebuild) rebuildRecruitButtons();
}

void TownScreen::drawRecruitPanel(UIRenderer& rdr)
{
    m_recruitPanel.draw(rdr);

    // ── HoMM3-style creature recruitment cards (ImGui) ──────────────────────
    ImGui::SetNextWindowPos({m_recruitPanel.bounds.x, m_recruitPanel.bounds.y});
    ImGui::SetNextWindowSize({m_recruitPanel.bounds.w, m_recruitPanel.bounds.h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg,  ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border,    ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6,28));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4,4));

    ImGui::Begin("##recruit_cards", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Recruit destination toggle
    {
        float halfW = (m_recruitPanel.bounds.w - 20.0f) * 0.5f - 2.0f;
        bool toArmy = !m_recruitToGarrison;
        if (toArmy) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f,0.4f,0.2f,1.f));
        else        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f,0.18f,0.18f,1.f));
        if (ImGui::Button("-> Hero Army", {halfW, 18})) m_recruitToGarrison = false;
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4);
        if (!toArmy) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f,0.25f,0.1f,1.f));
        else         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f,0.18f,0.18f,1.f));
        if (ImGui::Button("-> Garrison", {halfW, 18})) m_recruitToGarrison = true;
        ImGui::PopStyleColor();
    }

    if (!m_town || !m_registry || m_town->dwellings.empty()) {
        ImGui::TextDisabled("No units available");
        ImGui::TextDisabled("Build a unit dwelling in Buildings");
    } else {
        // 2-column card grid
        const float cardW  = (m_recruitPanel.bounds.w - 20.0f) * 0.5f;
        const float cardH  = 130.0f;
        const float sprW   = 80.0f;

        // Faction color tint for card headers
        auto factionHdr = [](FactionId f) -> ImVec4 {
            switch (f) {
            case FactionId::HolyOrder:     return {0.85f,0.80f,0.35f,1.f};
            case FactionId::CrimsonWardens:return {0.80f,0.20f,0.20f,1.f};
            case FactionId::Thornkin:      return {0.25f,0.65f,0.25f,1.f};
            case FactionId::EternalEmpire: return {0.50f,0.30f,0.70f,1.f};
            case FactionId::Bloodsworn:    return {0.70f,0.10f,0.10f,1.f};
            case FactionId::Voidkin:       return {0.20f,0.55f,0.70f,1.f};
            case FactionId::IronAssembly:  return {0.55f,0.55f,0.60f,1.f};
            case FactionId::Amalgamate:    return {0.50f,0.35f,0.20f,1.f};
            case FactionId::Convergence:   return {0.60f,0.50f,0.30f,1.f};
            default:                       return {0.60f,0.60f,0.60f,1.f};
            }
        };
        ImVec4 hdrCol = factionHdr(m_town->faction);

        int col = 0;
        for (auto& dw : m_town->dwellings) {
            // Find matching unit definition
            const UnitDef* ud = nullptr;
            if (m_registry) {
                for (const auto& u : m_registry->units()) {
                    if (u.faction == m_town->faction && u.tier == dw.tier && u.path == dw.path) {
                        ud = &u; break;
                    }
                }
            }
            if (!ud) continue;

            if (col == 1) ImGui::SameLine();
            col = (col + 1) % 2;

            ImGui::PushID(dw.buildingId);
            ImGui::BeginGroup();

            // Card background
            ImVec2 cardMin = ImGui::GetCursorScreenPos();
            ImVec2 cardMax = {cardMin.x + cardW, cardMin.y + cardH};
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(cardMin, cardMax, IM_COL32(18,20,28,240), 4.0f);
            dl->AddRect(cardMin, cardMax,
                IM_COL32((int)(hdrCol.x*200),(int)(hdrCol.y*200),(int)(hdrCol.z*200),180), 4.0f, 0, 1.5f);

            // Unit sprite (first idle frame)
            ImTextureID spr = (dw.tier >= 1 && dw.tier <= MAX_TIERS)
                              ? m_unitTex[dw.tier - 1] : nullptr;
            if (spr) {
                float u0 = 0.0f, u1 = 1.0f / 8.0f; // first of 8 frames
                dl->AddImage(spr,
                    {cardMin.x + 2, cardMin.y + 18},
                    {cardMin.x + 2 + sprW, cardMin.y + 18 + sprW},
                    {u0, 0}, {u1, 1});
            } else {
                dl->AddRectFilled({cardMin.x+2, cardMin.y+18},
                                  {cardMin.x+2+sprW, cardMin.y+18+sprW},
                                  IM_COL32(30,30,40,200), 3.0f);
                dl->AddText({cardMin.x + 16, cardMin.y + 42},
                            IM_COL32((int)(hdrCol.x*255),(int)(hdrCol.y*255),(int)(hdrCol.z*255),180),
                            ("T" + std::to_string(dw.tier)).c_str());
            }

            // Unit name header
            ImGui::SetCursorScreenPos({cardMin.x + 4, cardMin.y + 2});
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(hdrCol.x, hdrCol.y, hdrCol.z, 1.0f));
            ImGui::TextUnformatted(ud->name.c_str());
            ImGui::PopStyleColor();

            // Stats column (right of sprite)
            float sx2 = cardMin.x + sprW + 8, sy2 = cardMin.y + 18;
            const float rowH = 13.0f;
            auto statRow = [&](const char* label, int val) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%s %d", label, val);
                dl->AddText({sx2, sy2}, IM_COL32(200,200,210,255), buf);
                sy2 += rowH;
            };
            statRow("ATK", ud->attack);
            statRow("DEF", ud->defense);
            char dmgBuf[24];
            std::snprintf(dmgBuf, sizeof(dmgBuf), "DMG %d-%d", ud->damage_min, ud->damage_max);
            dl->AddText({sx2, sy2}, IM_COL32(200,200,210,255), dmgBuf); sy2 += rowH;
            statRow("HP ", ud->hp);
            statRow("SPD", ud->speed);
            statRow("GRW", dw.available);

            // Available + cost
            ImGui::SetCursorScreenPos({cardMin.x + 4, cardMin.y + cardH - 32});
            bool canAfford = m_playerRes &&
                             m_playerRes->get(ResourceType::Gold) >= ud->cost.get(ResourceType::Gold) * dw.available;
            ImGui::TextDisabled("Avail: %d  (%dg)", dw.available,
                                ud->cost.get(ResourceType::Gold) * dw.available);

            // Recruit button
            ImGui::SetCursorScreenPos({cardMin.x + 4, cardMin.y + cardH - 16});
            bool hasUnits = (dw.available > 0);
            if (!hasUnits || !canAfford) ImGui::BeginDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(hdrCol.x*0.3f, hdrCol.y*0.3f, hdrCol.z*0.3f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(hdrCol.x*0.5f, hdrCol.y*0.5f, hdrCol.z*0.5f, 1.0f));

            if (ImGui::Button("Recruit All##btn", {cardW - 8.0f, 12.0f})) {
                const UnitDef* mu = ud;
                bool useGarrison = m_recruitToGarrison || !m_hero;
                std::vector<UnitStack>& target = useGarrison ? m_town->garrison : m_hero->army;
                bool alreadyHasStack = false;
                for (const auto& s : target)
                    if (s.defId == mu->id) { alreadyHasStack = true; break; }
                if (m_playerRes && (alreadyHasStack || target.size() < 7)) {
                    float costMult = (m_hero && m_hero->efficientSpecialty) ? 0.8f : 1.0f;
                    int recruited = m_town->recruit(dw.tier, 999, *m_playerRes,
                                                    m_registry->units(), costMult);
                    if (recruited > 0) {
                        bool merged = false;
                        for (auto& s : target)
                            if (s.defId == mu->id) { s.count += recruited; merged = true; break; }
                        if (!merged) target.push_back({mu->id, recruited});
                        rebuildRecruitButtons();
                    }
                }
            }

            ImGui::PopStyleColor(2);
            if (!hasUnits || !canAfford) ImGui::EndDisabled();

            // Dummy to consume the card area
            ImGui::SetCursorScreenPos({cardMin.x, cardMax.y + 4});
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void TownScreen::drawIncomePanel(UIRenderer& rdr)
{
    // Panel border only — no opaque background so faction art shows through
    m_incomePanel.draw(rdr);
    if (!m_town) return;

    float bh = m_incomePanel.bounds.h;
    float x  = m_incomePanel.bounds.x + 8;
    // Start text in the lower 45% where the dark gradient covers the art
    float y  = m_incomePanel.bounds.y + bh * 0.57f;

    for (int i = 0; i < RESOURCE_COUNT; ++i) {
        int income = m_town->weeklyIncome.amounts[i];
        if (income == 0) continue;
        std::string line = std::string(resourceName(static_cast<ResourceType>(i)))
                         + ": +" + std::to_string(income) + "/week";
        rdr.drawText(line, x, y, UIColor::hex(UITheme::GOLD), 12.0f);
        y += 16.0f;
    }
}

bool TownScreen::onMouseMove(float x, float y) {
    if (!m_open) return false;
    m_closeBtn.onMouseMove(x, y);

    m_tooltip.hide();
    for (auto& bb : m_buildBtns) {
        bb.btn.onMouseMove(x, y);
        if (!bb.built && m_registry && bb.btn.bounds.contains(x, y)) {
            const BuildingDef* bd = m_registry->getBuildingDef(bb.buildingId);
            if (bd) {
                std::string tip = bd->name + ": ";
                if (!bd->description.empty()) tip += bd->description + "  |  ";
                tip += "Cost: ";
                bool first = true;
                for (int i = 0; i < RESOURCE_COUNT; ++i) {
                    int v = bd->cost.amounts[i];
                    if (v <= 0) continue;
                    if (!first) tip += ", ";
                    tip += std::to_string(v) + " " + resourceName(static_cast<ResourceType>(i));
                    first = false;
                }
                if (first) tip += "free";
                m_tooltip.show(tip, x, y - 10.0f);
            }
        }
    }

    for (auto& rb : m_recruitBtns) {
        rb.btn.onMouseMove(x, y);
        if (!rb.statTip.empty() && rb.btn.bounds.contains(x, y))
            m_tooltip.show(rb.statTip, x, y - 10.0f);
    }
    return m_mainPanel.bounds.contains(x, y);
}

bool TownScreen::onMouseDown(float x, float y) {
    if (!m_open) return false;
    if (m_closeBtn.onMouseDown(x, y)) return true;
    for (auto& bb : m_buildBtns)   if (bb.btn.onMouseDown(x, y)) return true;
    for (auto& rb : m_recruitBtns) if (rb.btn.onMouseDown(x, y)) return true;
    return m_mainPanel.bounds.contains(x, y);
}

bool TownScreen::onMouseUp(float x, float y) {
    if (!m_open) return false;
    m_closeBtn.onMouseUp(x, y);
    // Snapshot vectors before iterating — onClick fires rebuildBuildingButtons()
    // which clears m_buildBtns mid-loop, invalidating range-for iterators.
    auto buildSnap   = m_buildBtns;
    auto recruitSnap = m_recruitBtns;
    for (auto& bb : buildSnap)   bb.btn.onMouseUp(x, y);
    for (auto& rb : recruitSnap) rb.btn.onMouseUp(x, y);
    return m_mainPanel.bounds.contains(x, y);
}
