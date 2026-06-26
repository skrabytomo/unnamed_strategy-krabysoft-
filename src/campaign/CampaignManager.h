#pragma once
#include <vector>
#include <functional>
#include "CampaignDef.h"
#include "AlignmentSystem.h"
#include "../scripting/LuaEngine.h"
#include "../meta/HideoutDB.h"
#include "../data/SaveLoad.h"

enum class CampaignEvent : uint8_t
{
    MissionStarted,
    ObjectiveCompleted,
    DecisionPresented,
    DecisionResolved,
    MissionCompleted,
    MissionFailed,
    CampaignEnded,
};

class CampaignManager
{
public:
    using EventCallback = std::function<void(CampaignEvent)>;

    // Populate the hardcoded 3-mission campaign arc
    void init();

    // Called once per week from Game (campaign mode)
    void onWeekStart(int week, LuaEngine& lua);

    // Called by game systems when world events happen
    void onTownCaptured(uint32_t townId);
    void onHeroDefeated(uint32_t heroId);
    void onResourcesChecked(ResourceType type, int amount);
    void onTileReached(HexCoord pos);
    void triggerMissionLoss();  // called when player is finally defeated with no recovery

    // Decision flow
    bool                    hasPendingDecision() const;
    const CampaignDecision* pendingDecision() const;
    void                    resolveDecision(int choiceIdx, LuaEngine& lua);

    // State queries
    const CampaignMission*  currentMission()  const;
    const AlignmentSystem&  alignment()       const { return m_alignment; }
    bool isCampaignOver()  const { return m_over; }
    bool playerWon()       const { return m_won; }
    FactionId unlockedFaction(bool convergenceEligible) const;

    // All missions (for end-screen decision summary)
    const std::vector<CampaignMission>& allMissions() const { return m_missions; }

    // Convergence eligibility — set once at campaign start from HideoutDB
    void setConvergenceEligible(bool v) { m_convergenceEligible = v; }
    bool convergenceEligible() const    { return m_convergenceEligible; }

    // Objectives progress for HUD
    int completedObjectives() const;
    int requiredObjectives()  const;

    void setEventCallback(EventCallback cb) { m_onEvent = cb; }

    // Called by Game when new week fires (needed for relative SurviveWeeks)
    void setMissionStartWeek(int w) { m_missionStartWeek = w; }
    int  missionStartWeek() const   { return m_missionStartWeek; }

    // Persistence
    CampaignSaveState toSaveState() const;
    void fromSaveState(const CampaignSaveState& s);

private:
    void startMission(int id);
    void tryCompleteObjective(CampaignObjective& obj, bool success);
    void checkAllObjectives();
    void completeMission(bool won);
    void fireEvent(CampaignEvent e);

    static std::vector<CampaignMission> buildCampaign();

    std::vector<CampaignMission> m_missions;
    int              m_currentIdx        = 0;
    bool             m_over              = false;
    bool             m_won               = false;
    bool             m_convergenceEligible = false;
    int              m_pendingDecIdx     = -1;
    int              m_missionStartWeek  = 1;  // global week when current mission began
    AlignmentSystem  m_alignment;
    EventCallback    m_onEvent;
};
