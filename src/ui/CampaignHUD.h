#pragma once
#include "../campaign/CampaignManager.h"

// ImGui-based Campaign HUD.
// Shows: mission briefing (on start), objective tracker, alignment compass,
// decision modal (blocks input when a decision is pending), and end-campaign screen.
class CampaignHUD
{
public:
    void render(CampaignManager& mgr, LuaEngine& lua);

    bool wantsReturnToMenu() const { return m_returnToMenu; }
    void resetReturnToMenu()       { m_returnToMenu = false; }

private:
    void drawObjectives(const CampaignMission& mission);
    void drawAlignmentCompass(const AlignmentSystem& align);
    void drawDecisionModal(CampaignManager& mgr, LuaEngine& lua);
    void drawEndScreen(CampaignManager& mgr);

    bool m_showBriefing   = false;
    int  m_lastMissionId  = -1;
    bool m_returnToMenu   = false;
};
