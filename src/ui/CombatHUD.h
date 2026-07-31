#pragma once
#include "Widgets.h"
#include "../combat/CombatEngine.h"
#include <functional>
#include <tuple>

class CombatHUD
{
public:
    bool init(int screenW, int screenH);
    void resize(int screenW, int screenH);

    // Returns {textureId, numCols, isFlipped} for a unit's idle-frame icon, or
    // {0,0,false} if unavailable (falls back to the colored box). Supplied by
    // Game_Combat.cpp, which owns the actual textures/animator cache — keeps
    // CombatHUD decoupled from Game internals.
    using UnitIconLookup = std::function<std::tuple<unsigned int,int,bool>(uint32_t unitId)>;

    void draw(UIRenderer& rdr, const CombatEngine& engine, const UnitIconLookup& iconOf = nullptr);

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
    void drawTurnOrder(UIRenderer& rdr, const CombatEngine& engine, const UnitIconLookup& iconOf);
    void drawHeroInfo(UIRenderer& rdr, const CombatEngine& engine);
    void drawCombatLog(UIRenderer& rdr, const CombatEngine& engine);
    void drawActionBar(UIRenderer& rdr, const CombatUnit* active);

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
