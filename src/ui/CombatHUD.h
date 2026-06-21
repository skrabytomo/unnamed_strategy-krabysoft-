#pragma once
#include "Widgets.h"
#include "../combat/CombatEngine.h"
#include <functional>

class CombatHUD
{
public:
    bool init(int screenW, int screenH);
    void resize(int screenW, int screenH);

    void draw(UIRenderer& rdr, const CombatEngine& engine);

    bool onMouseMove(float x, float y);
    bool onMouseDown(float x, float y);
    bool onMouseUp(float x, float y);

    void setHoveredUnit(const CombatUnit* u) { m_hoveredUnit = u; }

    // Action callbacks — wired to game logic
    UICallback onWait;
    UICallback onDefend;
    UICallback onEndCombat;  // retreat
    UICallback onSpells;     // open spell panel

private:
    void buildLayout(int sw, int sh);
    void drawUnitInfo(UIRenderer& rdr, const CombatUnit* unit, bool isActive);
    void drawTurnOrder(UIRenderer& rdr, const CombatEngine& engine);
    void drawHeroInfo(UIRenderer& rdr, const CombatEngine& engine);
    void drawCombatLog(UIRenderer& rdr, const CombatEngine& engine);
    void drawActionBar(UIRenderer& rdr);

    int m_screenW = 1280, m_screenH = 720;

    // Bottom panels
    Rect   m_bottomBar;
    Panel  m_activeUnitPanel;   // left — active unit info
    Panel  m_targetUnitPanel;   // center-left — hovered/targeted unit
    Panel  m_logPanel;          // center-right — combat log
    Panel  m_actionPanel;       // right — action buttons

    Button m_waitBtn;
    Button m_defendBtn;
    Button m_retreatBtn;
    Button m_spellsBtn;

    // Turn order bar — top
    Rect   m_turnOrderBar;

    TooltipWidget m_tooltip;

    const CombatUnit* m_hoveredUnit = nullptr;
};
