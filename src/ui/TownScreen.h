#pragma once
#include "Widgets.h"
#include "../town/Town.h"
#include "../town/BuildingRegistry.h"
#include "../data/Resources.h"
#include "../hero/Hero.h"
#include <functional>
#include <unordered_map>
#include <imgui.h>

class TownScreen
{
public:
    bool init(int screenW, int screenH);
    void open(Town* town, Resources* playerRes, const BuildingRegistry* registry,
              Hero* visitingHero = nullptr, int currentWeek = 0, int blueprintDiscount = 0);
    void close() { m_open = false; m_town = nullptr; }
    bool isOpen() const { return m_open; }
    const Town* currentTown() const { return m_town; }

    void draw(UIRenderer& rdr);
    bool onMouseMove(float x, float y);
    bool onMouseDown(float x, float y);
    bool onMouseUp(float x, float y);

    UICallback onClose;

private:
    void buildLayout(int sw, int sh);
    void rebuildBuildingButtons();
    void rebuildRecruitButtons();
    void drawBuildingTree(UIRenderer& rdr);
    void drawPanorama(UIRenderer& rdr);
    void drawRecruitPanel(UIRenderer& rdr);
    void drawIncomePanel(UIRenderer& rdr);

    Town*                   m_town              = nullptr;
    Resources*              m_playerRes         = nullptr;
    const BuildingRegistry* m_registry          = nullptr;
    Hero*                   m_hero              = nullptr;
    bool                    m_open              = false;
    int                     m_currentWeek       = 0;
    int                     m_blueprintDiscount = 0;

    int m_screenW = 1280, m_screenH = 720;

    Panel  m_mainPanel;
    Panel  m_buildPanel;
    Panel  m_recruitPanel;
    Panel  m_incomePanel;
    Button m_closeBtn;

    // Dynamic building buttons (rebuilt on open)
    struct BuildBtn {
        Button btn;
        int    buildingId = 0;
        bool   built      = false;
        bool   affordable = false;
        bool   prereqMet  = false;
    };
    std::vector<BuildBtn> m_buildBtns;

    struct RecruitBtn {
        Button btn;
        int    tier       = 0;
        int    available  = 0;
        int    defId      = 0;
        std::string statTip;  // prebuilt tooltip string with unit stats
    };
    std::vector<RecruitBtn> m_recruitBtns;

    TooltipWidget m_tooltip;

    // Faction art for the town screen banner (set from Game)
    ImTextureID m_townBannerTex = nullptr;

    // Building category icon atlas: 6 cols × 1 row (64×64 each)
    // Column order matches BuildingCategory enum: Dwelling/Support/Economy/Special/Fort/MageGuild
    ImTextureID m_buildingIconTex = nullptr;

    // Per-building art keyed by BID (optional — shown in tooltip preview + button thumbnail)
    std::unordered_map<int, ImTextureID> m_buildingArt;

    // Unit sprite textures [tier-1] for the recruit panel
    static constexpr int MAX_TIERS = 6;
    ImTextureID m_unitTex[MAX_TIERS] = {};

    // Recruit destination toggle
    bool m_recruitToGarrison = false;

    // Building view mode: true = panorama grid, false = list
    bool m_panoramaMode = true;

public:
    void setTownBannerTex(ImTextureID t)    { m_townBannerTex    = t; }
    void setBuildingIconTex(ImTextureID t)  { m_buildingIconTex  = t; }
    void setBuildingArt(int bid, ImTextureID t) { if (t) m_buildingArt[bid] = t; }
    void setUnitTex(int tierIdx, ImTextureID t) {
        if (tierIdx >= 0 && tierIdx < MAX_TIERS) m_unitTex[tierIdx] = t;
    }
    void setRecruitTarget(bool toGarrison) { m_recruitToGarrison = toGarrison; }
    bool recruitToGarrison() const { return m_recruitToGarrison; }

    // Called instead of building directly when a PathA upgrade is clicked.
    // Args: pathA building id, pathB building id.
    std::function<void(int, int)> onUpgradePathChoice;
};
