#include "WorldMapHUD.h"
#include <string>
#include <sstream>

bool WorldMapHUD::init(int screenW, int screenH)
{
    buildLayout(screenW, screenH);
    return true;
}

void WorldMapHUD::resize(int screenW, int screenH)
{
    m_screenW = screenW; m_screenH = screenH;
    buildLayout(screenW, screenH);
}

// Bottom bar is 100px tall (HoMM3-style action row).
// Buttons: 2 rows × 2 cols at right side, each 150×40.
static constexpr float BOT_H   = 100.0f;
static constexpr float BTN_W   = 150.0f;
static constexpr float BTN_H   = 40.0f;
static constexpr float BTN_GAP = 6.0f;

void WorldMapHUD::buildLayout(int sw, int sh)
{
    m_screenW = sw; m_screenH = sh;

    // Top resource bar
    m_topBar = {0, 0, (float)sw, 68.0f};

    // Bottom action bar — stone-panel style like HoMM3
    m_bottomBar = {0, (float)sh - BOT_H, (float)sw, BOT_H};

    // Button grid: 2 cols × 2 rows, anchored to right edge
    float col2X = (float)sw - BTN_W - 8.0f;
    float col1X = col2X - BTN_W - BTN_GAP;
    float row1Y = (float)sh - BOT_H + 8.0f;
    float row2Y = row1Y + BTN_H + BTN_GAP;

    // Row 1
    m_worldSpellsBtn = Button("World Spells",
        {col1X, row1Y, BTN_W, BTN_H},
        [this]{ if (onWorldSpells) onWorldSpells(); });
    m_worldSpellsBtn.colorBorder = UIColor::hex(0x8080C8);
    m_worldSpellsBtn.colorText   = UIColor::hex(0xC0C0FF);

    m_kingdomBtn = Button("Kingdom",
        {col2X, row1Y, BTN_W, BTN_H},
        [this]{ if (onKingdom) onKingdom(); });
    m_kingdomBtn.colorBorder = UIColor::hex(UITheme::GOLD);
    m_kingdomBtn.colorText   = UIColor::hex(UITheme::GOLD);

    // Row 2
    m_endTurnBtn = Button("End Turn",
        {col1X, row2Y, BTN_W, BTN_H},
        [this]{ if (onEndTurn) onEndTurn(); });
    m_endTurnBtn.colorBorder = UIColor::hex(UITheme::GOLD);
    m_endTurnBtn.colorText   = UIColor::hex(UITheme::GOLD);

    m_optionsBtn = Button("Options",
        {col2X, row2Y, BTN_W, BTN_H},
        [this]{ if (onOptions) onOptions(); });
    m_optionsBtn.colorBorder = UIColor::hex(UITheme::TEXT_SECONDARY);
    m_optionsBtn.colorText   = UIColor::hex(UITheme::TEXT_SECONDARY);

    // Hero panel — right side, below top bar
    m_heroPanel = Panel({(float)sw - 180.0f, 68.0f, 176.0f, 230.0f});
    m_heroPanel.title = "Heroes";

    // Town panel — below hero panel
    m_townPanel = Panel({(float)sw - 180.0f, 306.0f, 176.0f, 160.0f});
    m_townPanel.title = "Towns";
}

void WorldMapHUD::draw(UIRenderer& rdr,
                        const Resources& playerRes,
                        const Resources& weeklyIncome,
                        const TurnManager& turns,
                        const std::vector<Hero>& heroes,
                        int selectedHeroIdx,
                        const std::vector<Town>& towns)
{
    // Sync end-turn label with day counter
    m_endTurnBtn.text = "End Turn [D" + std::to_string(turns.day()) + "]";

    drawResourceBar(rdr, playerRes, weeklyIncome);
    drawDatePanel(rdr, turns);
    drawHeroPanel(rdr, heroes, selectedHeroIdx);
    drawTownPanel(rdr, towns);

    // Bottom bar background — stone dark
    rdr.drawRect(m_bottomBar,
        UIColor::hex(0x0E0E16, 0.97f),
        UIColor::hex(UITheme::GOLD, 1.0f), 1.5f);

    // Gold separator line at top of bottom bar
    float sepY = m_bottomBar.y + 0.5f;
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddLine({m_bottomBar.x, sepY}, {m_bottomBar.x + m_bottomBar.w, sepY},
                IM_COL32(200, 160, 50, 200), 1.5f);

    m_worldSpellsBtn.draw(rdr);
    m_kingdomBtn.draw(rdr);
    m_endTurnBtn.draw(rdr);
    m_optionsBtn.draw(rdr);

    m_tooltip.draw(rdr);
}

void WorldMapHUD::drawResourceBar(UIRenderer& rdr, const Resources& res,
                                   const Resources& income)
{
    rdr.drawRect(m_topBar,
        UIColor::hex(UITheme::BG_PANEL_DARK, 0.97f),
        UIColor::hex(UITheme::BORDER), 1.0f);

    float x = 10.0f;
    float y = 5.0f;
    float spacing = 185.0f;

    struct ResDisplay { ResourceType type; unsigned color; };
    static const ResDisplay displays[] = {
        {ResourceType::Gold,         UITheme::GOLD},
        {ResourceType::Iron,         UITheme::IRON_GREY},
        {ResourceType::FaithStones,  0xE8E4FF},
        {ResourceType::BloodEssence, UITheme::BLOOD_RED},
        {ResourceType::VerdantSap,   UITheme::NATURE_GREEN},
        {ResourceType::Mercury,      UITheme::DEATH_TEAL},
    };

    static const int resIconIdx[] = { 32, 33, 34, 35, 36, 37 };
    auto drawResIcon = [&](int atlasIdx, float ix, float iy, float sz) {
        if (!m_iconTex) return;
        float col = static_cast<float>(atlasIdx % 8);
        float row = static_cast<float>(atlasIdx / 8);
        ImVec2 uv0 = { col / 8.0f,          row / 6.0f };
        ImVec2 uv1 = { (col + 1.0f) / 8.0f, (row + 1.0f) / 6.0f };
        ImGui::GetBackgroundDrawList()->AddImage(m_iconTex, {ix, iy}, {ix+sz, iy+sz}, uv0, uv1);
    };

    int iconCount = 0;
    for (auto& d : displays) {
        int val = res.get(d.type);
        int inc = income.get(d.type);
        float iconSz = 40.0f;
        float iconY  = y + (46.0f - iconSz) * 0.5f;
        if (m_iconTex)
            drawResIcon(resIconIdx[iconCount], x, iconY, iconSz);
        else
            rdr.drawRect({x, y+2, 12.0f, 14.0f}, UIColor::hex(d.color));
        float textX = x + (m_iconTex ? iconSz + 4.0f : 16.0f);
        std::string label = std::string(resourceName(d.type)) + ": " + std::to_string(val);
        rdr.drawText(label, textX, y + 4.0f, UIColor::hex(d.color), 15.0f);
        if (inc > 0) {
            std::string incStr = "+" + std::to_string(inc) + "/wk";
            rdr.drawText(incStr, textX, y + 22.0f,
                         UIColor::rgba(0.55f, 0.85f, 0.55f), 12.0f);
        }
        x += spacing;
        ++iconCount;
        if (x + spacing > m_screenW - 200.0f) break;
    }
}

void WorldMapHUD::drawDatePanel(UIRenderer& rdr, const TurnManager& turns)
{
    float barTop = static_cast<float>(m_screenH) - BOT_H;

    if (m_numHumanPlayers >= 2) {
        // Player indicator — large, color-coded
        const char* pLabel = (m_currentPlayerId == 1) ? "PLAYER 1" : "PLAYER 2";
        UIColor     pColor = (m_currentPlayerId == 1)
                             ? UIColor::hex(0xFFDD44)   // gold for P1
                             : UIColor::hex(0x66AAFF);  // blue for P2
        rdr.drawText(pLabel, 14.0f, barTop + 6.0f, pColor, 20.0f);

        std::string date = "Week " + std::to_string(turns.week()) +
                           "  Day "  + std::to_string(turns.day());
        rdr.drawText(date, 14.0f, barTop + 32.0f,
                     UIColor::hex(UITheme::GOLD), 14.0f);
        rdr.drawText("SPACE = End Turn   TAB = Next Hero",
                     14.0f, barTop + 52.0f,
                     UIColor::hex(UITheme::TEXT_SECONDARY), 11.0f);
        rdr.drawText("F7 = Artifacts   F8 = Details   ESC = Options",
                     14.0f, barTop + 66.0f,
                     UIColor::hex(UITheme::TEXT_SECONDARY), 11.0f);
    } else {
        std::string date = "Week " + std::to_string(turns.week()) +
                           "  Day "  + std::to_string(turns.day());
        rdr.drawText(date, 14.0f, barTop + 14.0f,
                     UIColor::hex(UITheme::GOLD), 18.0f);
        rdr.drawText("SPACE = End Turn   TAB = Next Hero   F2 = Editor",
                     14.0f, barTop + 38.0f,
                     UIColor::hex(UITheme::TEXT_SECONDARY), 11.0f);
        rdr.drawText("F7 = Artifacts   F8 = Hero Details   M = Mini-Map",
                     14.0f, barTop + 54.0f,
                     UIColor::hex(UITheme::TEXT_SECONDARY), 11.0f);
        rdr.drawText("ESC = Options",
                     14.0f, barTop + 70.0f,
                     UIColor::hex(UITheme::TEXT_SECONDARY), 11.0f);
    }
}

void WorldMapHUD::drawHeroPanel(UIRenderer& rdr,
                                  const std::vector<Hero>& heroes, int sel)
{
    m_heroCount = static_cast<int>(heroes.size());
    m_heroPanel.bounds.h = 28.0f + m_heroCount * 64.0f;
    m_heroPanel.draw(rdr);

    rdr.drawText("Click hero / F8 = details",
                 m_heroPanel.bounds.x + 4.0f, m_heroPanel.bounds.y + 13.0f,
                 UIColor::rgba(0.5f, 0.5f, 0.5f, 0.8f), 9.0f);

    float y = m_heroPanel.bounds.y + 28.0f;
    float x = m_heroPanel.bounds.x + 4.0f;
    float w = m_heroPanel.bounds.w - 8.0f;

    auto* dl = ImGui::GetBackgroundDrawList();
    for (int i = 0; i < static_cast<int>(heroes.size()); ++i) {
        auto& h = heroes[i];
        float portSz = 52.0f;
        Rect btn{x, y, w, portSz + 8.0f};

        UIColor bg  = (i == sel) ? UIColor::hex(UITheme::BG_HOVER) : UIColor::hex(UITheme::BG_PANEL_DARK);
        UIColor brd = (i == sel) ? UIColor::hex(UITheme::GOLD)     : UIColor::hex(UITheme::BORDER);
        rdr.drawRect(btn, bg, brd, 1.0f);

        float portX = x + 2.0f;
        float portY = y + 4.0f;
        int fid = static_cast<int>(h.faction);
        ImTextureID portTex = (fid >= 0 && fid < 9) ? m_portraitTex[fid] : nullptr;
        if (portTex) {
            dl->AddImage(portTex, {portX, portY}, {portX + portSz, portY + portSz});
            dl->AddRect({portX, portY}, {portX + portSz, portY + portSz},
                        IM_COL32(180, 150, 80, 200), 2.0f);
        } else {
            rdr.drawRect({portX, portY, portSz, portSz},
                         UIColor::hex(0x223344), UIColor::hex(UITheme::BORDER), 1.0f);
        }

        float tx = portX + portSz + 4.0f;
        float tw = btn.x + btn.w - tx - 2.0f;

        std::string label = h.name + " L" + std::to_string(h.level);
        rdr.drawText(label, tx, y + 2.0f, UIColor::hex(UITheme::TEXT_PRIMARY), 11.0f);

        // ATK / DEF
        std::string atk = "ATK " + std::to_string(h.attack) + "  DEF " + std::to_string(h.defense);
        rdr.drawText(atk, tx, y + 14.0f, UIColor::hex(0xFFCC88), 9.5f);

        // Mana
        std::string manaStr = "MP " + std::to_string(h.mana) + "/" + std::to_string(h.maxMana);
        rdr.drawText(manaStr, tx, y + 25.0f, UIColor::hex(0x88AAFF), 9.5f);

        // Move bar
        float moveFrac = h.maxMove > 0 ? static_cast<float>(h.movePool) / h.maxMove : 0.0f;
        Rect movebar{tx, y + 37.0f, tw, 5.0f};
        rdr.drawBar(movebar, moveFrac,
                    UIColor::hex(UITheme::NATURE_GREEN),
                    UIColor::hex(UITheme::BG_DARK),
                    UIColor::hex(UITheme::BORDER));

        // XP bar
        float xpFrac = h.xpToNext > 0 ? static_cast<float>(h.xp) / h.xpToNext : 1.0f;
        Rect xpbar{tx, y + 46.0f, tw, 5.0f};
        rdr.drawBar(xpbar, xpFrac,
                    UIColor::hex(0xAA55FF),
                    UIColor::hex(UITheme::BG_DARK),
                    UIColor::hex(UITheme::BORDER));

        y += portSz + 12.0f;
    }
}

void WorldMapHUD::drawTownPanel(UIRenderer& rdr, const std::vector<Town>& towns)
{
    std::vector<const Town*> playerTowns;
    for (const auto& t : towns)
        if (t.ownerId == static_cast<uint32_t>(m_currentPlayerId)) playerTowns.push_back(&t);

    m_townCount = static_cast<int>(playerTowns.size());
    if (m_townCount == 0) return;

    float rowH = 28.0f;
    // Position town panel just below the hero panel
    m_townPanel.bounds.y = m_heroPanel.bounds.y + m_heroPanel.bounds.h + 4.0f;
    float panelH = 26.0f + m_townCount * (rowH + 4.0f);
    m_townPanel.bounds.h = panelH;
    m_townPanel.draw(rdr);

    float y = m_townPanel.bounds.y + 26.0f;
    float x = m_townPanel.bounds.x + 4.0f;
    float w = m_townPanel.bounds.w - 8.0f;

    auto* dl = ImGui::GetBackgroundDrawList();
    const float icoSz = 20.0f;
    for (int i = 0; i < m_townCount; ++i) {
        const Town* t = playerTowns[i];
        Rect btn{x, y, w, rowH};
        rdr.drawRect(btn,
            UIColor::hex(UITheme::BG_PANEL_DARK),
            UIColor::hex(UITheme::GOLD), 1.0f);

        float iy = y + (rowH - icoSz) * 0.5f;
        if (m_iconTex) {
            ImVec2 uv0 = { 2.0f / 8.0f, 0.0f / 6.0f };
            ImVec2 uv1 = { 3.0f / 8.0f, 1.0f / 6.0f };
            dl->AddImage(m_iconTex, {x + 2.0f, iy}, {x + 2.0f + icoSz, iy + icoSz}, uv0, uv1);
        } else {
            dl->AddRectFilled({x + 3.0f, iy + 3.0f}, {x + 3.0f + 14.0f, iy + 14.0f},
                              IM_COL32(120, 180, 255, 200), 2.0f);
        }

        // Town name + building count
        char townLine[80];
        std::snprintf(townLine, sizeof(townLine), "%s (%d bldgs)",
                      t->name.c_str(), static_cast<int>(t->builtBuildings.size()));
        rdr.drawText(townLine, x + icoSz + 6.0f, y + 6.0f,
                     UIColor::hex(UITheme::TEXT_PRIMARY), 10.0f);
        y += rowH + 4.0f;
    }
}

bool WorldMapHUD::onMouseMove(float x, float y) {
    m_endTurnBtn.onMouseMove(x, y);
    m_worldSpellsBtn.onMouseMove(x, y);
    m_kingdomBtn.onMouseMove(x, y);
    m_optionsBtn.onMouseMove(x, y);
    return false;
}

bool WorldMapHUD::onMouseDown(float x, float y) {
    if (m_endTurnBtn.onMouseDown(x, y))    return true;
    if (m_worldSpellsBtn.onMouseDown(x, y)) return true;
    if (m_kingdomBtn.onMouseDown(x, y))     return true;
    if (m_optionsBtn.onMouseDown(x, y))     return true;

    // Hero panel click
    if (onHeroClicked && m_heroCount > 0) {
        float px = m_heroPanel.bounds.x + 4.0f;
        float py = m_heroPanel.bounds.y + 28.0f;
        float pw = m_heroPanel.bounds.w - 8.0f;
        float ph = 60.0f;  // matches portSz(52) + 8 card height in drawHeroPanel
        float gap = 64.0f;
        if (x >= px && x <= px + pw) {
            for (int i = 0; i < m_heroCount; ++i) {
                float ey = py + i * gap;
                if (y >= ey && y <= ey + ph) {
                    onHeroClicked(i);
                    return true;
                }
            }
        }
    }

    // Town panel click
    if (onTownClicked && m_townCount > 0) {
        float px = m_townPanel.bounds.x + 4.0f;
        float py = m_townPanel.bounds.y + 26.0f;
        float pw = m_townPanel.bounds.w - 8.0f;
        float rowH = 28.0f;
        float gap  = rowH + 4.0f;
        if (x >= px && x <= px + pw) {
            for (int i = 0; i < m_townCount; ++i) {
                float ey = py + i * gap;
                if (y >= ey && y <= ey + rowH) {
                    onTownClicked(i);
                    return true;
                }
            }
        }
    }

    return false;
}

bool WorldMapHUD::onMouseUp(float x, float y) {
    bool hit = m_endTurnBtn.onMouseUp(x, y);
    hit |= m_worldSpellsBtn.onMouseUp(x, y);
    hit |= m_kingdomBtn.onMouseUp(x, y);
    hit |= m_optionsBtn.onMouseUp(x, y);
    return hit;
}
