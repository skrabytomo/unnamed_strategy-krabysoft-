#pragma once
#include "Widgets.h"
#include "../data/Resources.h"
#include "../hero/Hero.h"
#include "../core/TurnManager.h"
#include "../town/Town.h"
#include <vector>
#include <functional>
#include <imgui.h>

class WorldMapHUD
{
public:
    bool init(int screenW, int screenH);
    void resize(int screenW, int screenH);

    void draw(UIRenderer& rdr,
              const Resources& playerRes,
              const Resources& weeklyIncome,
              const TurnManager& turns,
              const std::vector<Hero>& heroes,
              int  selectedHeroIdx,
              const std::vector<Town>& towns);

    bool onMouseMove(float x, float y);
    bool onMouseDown(float x, float y);
    bool onMouseUp(float x, float y);

    UICallback onEndTurn;
    UIIntCallback onHeroClicked;  // index into heroes list
    UIIntCallback onTownClicked;  // index into player-towns list
    UICallback onWorldSpells;     // toggle world-map spell panel
    UICallback onKingdom;         // toggle kingdom overview
    UICallback onOptions;         // toggle options / pause menu

    void setIconTex(ImTextureID tex) { m_iconTex = tex; }
    void setPortraitTex(int factionIdx, ImTextureID tex) {
        if (factionIdx >= 0 && factionIdx < 9) m_portraitTex[factionIdx] = tex;
    }

private:
    void buildLayout(int sw, int sh);
    void drawResourceBar(UIRenderer& rdr, const Resources& res, const Resources& income);
    void drawHeroPanel(UIRenderer& rdr, const std::vector<Hero>& heroes, int sel);
    void drawTownPanel(UIRenderer& rdr, const std::vector<Town>& towns);
    void drawDatePanel(UIRenderer& rdr, const TurnManager& turns);

    int m_screenW = 1280, m_screenH = 720;

    // Top bar
    Rect m_topBar;

    // Bottom bar
    Rect   m_bottomBar;
    Button m_endTurnBtn;
    Button m_worldSpellsBtn;
    Button m_kingdomBtn;
    Button m_optionsBtn;

    // Hero list (right side)
    Panel m_heroPanel;
    std::vector<Button> m_heroBtns;
    int   m_heroCount = 0;  // updated each draw(); used for click detection

    // Town list (below hero panel)
    Panel m_townPanel;
    int   m_townCount = 0;  // player-owned towns, updated each draw()

    TooltipWidget m_tooltip;
    ImTextureID m_iconTex = nullptr;
    ImTextureID m_portraitTex[9] = {};
};
