#include "../core/DevLog.h"
#include "CampaignManager.h"
#include <cstdio>
#include <algorithm>

// ── Hardcoded campaign: "The Fracture" (3 missions) ───────────────────────────
std::vector<CampaignMission> CampaignManager::buildCampaign()
{
    std::vector<CampaignMission> missions;

    // ── Mission 1: The Border Burns ────────────────────────────────────────────
    {
        CampaignMission m;
        m.id            = 0;
        m.name          = "The Border Burns";
        m.briefing      = "Bloodsworn raiders have crossed the border and are advancing on "
                          "Sanctuary. You must capture the contested fort at Ashgate before "
                          "their main force arrives — but the path is not clean.";
        m.playerFaction = FactionId::HolyOrder;
        m.startWeeks    = 3;

        // Objectives
        // targetId=0 means "any enemy town" — works with procedural maps
        m.objectives.push_back({1, ObjectiveType::CaptureTown,
            "Capture the enemy stronghold", true, false, 0});
        // targetValue=6 = 6 weeks from mission start (relative)
        m.objectives.push_back({2, ObjectiveType::SurviveWeeks,
            "Survive 6 weeks", true, false, 0, 6});
        m.objectives.push_back({3, ObjectiveType::CollectResources,
            "Gather 500 Gold for relief supplies (bonus)", false, false, 0, 500,
            ResourceType::Gold});

        // Decision 1 — week 2: Civilian crossfire
        {
            CampaignDecision d;
            d.id          = 10;
            d.prompt      = "Crossfire";
            d.contextText = "Bloodsworn prisoners have been herding civilians as a shield. "
                            "Your siege will kill innocents unless you slow the assault.";
            d.choices.push_back({"Halt siege, evacuate civilians",
                "Takes 2 days. You lose tactical momentum but the people live.",
                {+1, +1}, "onDecision_evacuate"});
            d.choices.push_back({"Negotiate prisoner exchange",
                "Trade your Bloodsworn captives. Unpredictable outcome.",
                {0, +1}, "onDecision_negotiate"});
            d.choices.push_back({"Continue the assault",
                "The fort is the priority. Acceptable losses.",
                {+1, -1}, "onDecision_assault"});
            d.choices.push_back({"Arm the civilians, use them as a diversion",
                "Desperate and ruthless — but effective.",
                {-1, -1}, "onDecision_arm"});
            m.decisions.push_back(d);
        }

        // Decision 2 — week 4: Captured commander
        {
            CampaignDecision d;
            d.id          = 11;
            d.prompt      = "The Commander's Offer";
            d.contextText = "Their field commander, Vorka, offers to reveal Bloodsworn supply "
                            "routes in exchange for her freedom. She gives her word.";
            d.choices.push_back({"Release her on her word",
                "You gain the intelligence and lose a prisoner. She may honour it.",
                {-1, +1}, "onDecision_release"});
            d.choices.push_back({"Take the information, then imprison her",
                "Practical. She'll escape eventually.",
                {+1, -1}, "onDecision_imprison"});
            d.choices.push_back({"Refuse — no deals with raiders",
                "No intelligence gained, but your principles hold.",
                {+1, +1}, "onDecision_refuse"});
            m.decisions.push_back(d);
        }

        m.decisionWeekTriggers.push_back({2, 10});
        m.decisionWeekTriggers.push_back({4, 11});
        m.nextMissionWin  = 1;
        m.nextMissionLose = -1;  // game over

        missions.push_back(m);
    }

    // ── Mission 2: The Thornwood Passage ──────────────────────────────────────
    {
        CampaignMission m;
        m.id            = 1;
        m.name          = "The Thornwood Passage";
        m.briefing      = "The Void Rift is opening. Eternal Empire is racing you to it. "
                          "The fastest route cuts through Thornkin sacred land. "
                          "Their elders will speak with you — once.";
        m.playerFaction = FactionId::HolyOrder;
        m.startWeeks    = 6;

        // Defeat any enemy hero (id=0 = any) — the new enemy spawned at mission start
        m.objectives.push_back({5, ObjectiveType::DefeatHero,
            "Drive back the Empire vanguard", true, false, 0});
        // Survive 4 weeks from mission 2 start
        m.objectives.push_back({4, ObjectiveType::SurviveWeeks,
            "Hold the Thornwood passage for 4 weeks", true, false, 0, 4});
        m.objectives.push_back({6, ObjectiveType::CollectResources,
            "Carry 10 VerdantSap for Thornkin passage (bonus)", false, false,
            0, 10, ResourceType::VerdantSap});

        // Decision 3 — week 1: Thornkin entry terms
        {
            CampaignDecision d;
            d.id          = 20;
            d.prompt      = "The Elder's Price";
            d.contextText = "The Thornkin elder, Briarwynd, blocks the path. 'These roots have "
                            "stood ten thousand years. What are your years against ours?'";
            d.choices.push_back({"Accept: protect the grove in perpetuity",
                "You bind yourself to defend Thornkin lands forever. They will remember.",
                {-1, +1}, "onDecision_groveOath"});
            d.choices.push_back({"Negotiate a trade — VerdantSap for passage",
                "A fair exchange. No binding obligations.",
                {0, 0}, "onDecision_groveTrade"});
            d.choices.push_back({"Invoke Holy Order authority to pass",
                "You outrank a forest elder. Legal, cold, effective.",
                {+1, -1}, "onDecision_groveAuthority"});
            d.choices.push_back({"Cut through — time is critical",
                "The Rift doesn't wait. Neither do you.",
                {-1, -1}, "onDecision_groveForce"});
            m.decisions.push_back(d);
        }

        // Decision 4 — week 3: Defector
        {
            CampaignDecision d;
            d.id          = 21;
            d.prompt      = "The Cursed Defector";
            d.contextText = "An Eternal Empire officer deserts to your banner, carrying "
                            "battle plans — and a death-curse she cannot remove. "
                            "She has weeks to live, maybe less.";
            d.choices.push_back({"Accept her fully — curse and all",
                "Her knowledge is invaluable. You accept the risk.",
                {0, +1}, "onDecision_defectorAccept"});
            d.choices.push_back({"Accept her, attempt to cure the curse",
                "Costs time and FaithStones. May not work.",
                {+1, +1}, "onDecision_defectorCure"});
            d.choices.push_back({"Use her intelligence, then send her away",
                "Extract the plans. Let her die elsewhere.",
                {0, -1}, "onDecision_defectorUse"});
            d.choices.push_back({"Refuse entirely",
                "The risk isn't worth it.",
                {+1, 0}, "onDecision_defectorRefuse"});
            m.decisions.push_back(d);
        }

        m.decisionWeekTriggers.push_back({1, 20});
        m.decisionWeekTriggers.push_back({3, 21});
        m.nextMissionWin  = 2;
        m.nextMissionLose = -1;

        missions.push_back(m);
    }

    // ── Mission 3: The Convergence Point ──────────────────────────────────────
    {
        CampaignMission m;
        m.id            = 2;
        m.name          = "The Convergence Point";
        m.briefing      = "You stand at the Void Rift. All faction forces are converging. "
                          "The Rift can be sealed — or used as a weapon that ends this war "
                          "in a single moment. The choice defines the world you leave behind.";
        m.playerFaction = FactionId::HolyOrder;
        m.startWeeks    = 8;

        // Survive 4 weeks from mission 3 start (relative)
        m.objectives.push_back({7, ObjectiveType::SurviveWeeks,
            "Hold the Rift for 4 weeks while factions arrive", true, false, 0, 4});
        // Capture any remaining enemy town (targetId=0)
        m.objectives.push_back({8, ObjectiveType::CaptureTown,
            "Control the Convergence Citadel", true, false, 0});

        // Decision 5 — week 2: The Rift choice (the alignment-defining moment)
        {
            CampaignDecision d;
            d.id          = 30;
            d.prompt      = "What the Rift Offers";
            d.contextText = "Scholars and void-touched alike confirm it: the Rift can amplify "
                            "whatever is fed into it. The factions arrive expecting an answer. "
                            "What will you give the Rift?";
            d.choices.push_back({"Seal it — some power should not exist",
                "Costs everything you brought. The war ends but the Rift is gone forever.",
                {0, +2}, "onDecision_riftSeal"});
            d.choices.push_back({"Channel it through Holy Order doctrine — controlled",
                "Faith as the filter. Order shapes the power. Dangerous but directed.",
                {+2, +1}, "onDecision_riftOrder"});
            d.choices.push_back({"Distribute the power equally among all factions",
                "No faction dominates. Everyone gets something. Trust required.",
                {0, 0}, "onDecision_riftShare"});
            d.choices.push_back({"Weaponise it to end the war now",
                "Fast. Final. Thousands die in seconds instead of years.",
                {+1, -2}, "onDecision_riftWeapon"});
            m.decisions.push_back(d);
        }

        m.decisionWeekTriggers.push_back({2, 30});
        m.nextMissionWin  = -1;  // campaign end
        m.nextMissionLose = -1;

        missions.push_back(m);
    }

    return missions;
}

// ── CampaignManager ────────────────────────────────────────────────────────────
void CampaignManager::init()
{
    m_missions = buildCampaign();
    m_over     = false;
    m_won      = false;
    m_alignment.reset();
    startMission(0);
}

void CampaignManager::startMission(int id)
{
    m_currentIdx        = id;
    m_pendingDecIdx     = -1;
    m_missionStartWeek  = 1;  // Game will call setMissionStartWeek() to correct this
    gLog("[Campaign] Mission %d: %s\n",
           m_missions[id].id, m_missions[id].name.c_str());
    fireEvent(CampaignEvent::MissionStarted);
}

void CampaignManager::onWeekStart(int week, LuaEngine& lua)
{
    if (m_over) return;
    if (m_pendingDecIdx >= 0) return;  // unresolved decision — wait for player
    auto& mission = m_missions[m_currentIdx];

    // Check if a decision should trigger this week (relative to mission start week)
    int relWeek = week - m_missionStartWeek + 1;
    for (auto& [trigWeek, decId] : mission.decisionWeekTriggers) {
        if (trigWeek == relWeek) {
            for (int i = 0; i < static_cast<int>(mission.decisions.size()); ++i) {
                if (mission.decisions[i].id == static_cast<uint32_t>(decId) &&
                    !mission.decisions[i].resolved)
                {
                    m_pendingDecIdx = i;
                    fireEvent(CampaignEvent::DecisionPresented);
                    break;  // Present one at a time — but continue to check objectives
                }
            }
        }
    }

    // Check SurviveWeeks objectives (relative to mission start week)
    for (auto& obj : mission.objectives) {
        if (obj.type == ObjectiveType::SurviveWeeks && !obj.completed) {
            int weeksElapsed = week - m_missionStartWeek;
            if (weeksElapsed >= obj.targetValue) {
                tryCompleteObjective(obj, true);
            }
        }
    }

    checkAllObjectives();
}

void CampaignManager::onTownCaptured(uint32_t townId)
{
    if (m_over) return;
    for (auto& obj : m_missions[m_currentIdx].objectives) {
        if (obj.type == ObjectiveType::CaptureTown && !obj.completed) {
            // targetId==0 means "any enemy town"; non-zero requires exact match
            if (obj.targetId == 0 || obj.targetId == townId)
                tryCompleteObjective(obj, true);
        }
    }
    checkAllObjectives();
}

void CampaignManager::onHeroDefeated(uint32_t heroId)
{
    if (m_over) return;
    // heroId == 0 means the PLAYER hero was defeated — don't complete any DefeatHero objective
    if (heroId == 0) { checkAllObjectives(); return; }
    for (auto& obj : m_missions[m_currentIdx].objectives) {
        if (obj.type == ObjectiveType::DefeatHero && !obj.completed) {
            // targetId == 0 means "defeat any enemy hero"; non-zero matches specific hero ID
            if (obj.targetId == 0 || obj.targetId == heroId)
                tryCompleteObjective(obj, true);
        }
    }
    checkAllObjectives();
}

void CampaignManager::onTileReached(HexCoord pos)
{
    if (m_over) return;
    for (auto& obj : m_missions[m_currentIdx].objectives) {
        if (obj.type == ObjectiveType::ReachTile &&
            obj.targetTile == pos && !obj.completed)
        {
            tryCompleteObjective(obj, true);
        }
    }
    checkAllObjectives();
}

void CampaignManager::onResourcesChecked(ResourceType type, int amount)
{
    if (m_over) return;
    for (auto& obj : m_missions[m_currentIdx].objectives) {
        if (obj.type == ObjectiveType::CollectResources &&
            obj.resourceType == type && !obj.completed &&
            amount >= obj.targetValue)
        {
            tryCompleteObjective(obj, true);
        }
    }
    checkAllObjectives();
}

bool CampaignManager::hasPendingDecision() const
{
    return m_pendingDecIdx >= 0;
}

const CampaignDecision* CampaignManager::pendingDecision() const
{
    if (m_pendingDecIdx < 0) return nullptr;
    const auto& decisions = m_missions[m_currentIdx].decisions;
    if (m_pendingDecIdx >= static_cast<int>(decisions.size())) return nullptr;
    return &decisions[m_pendingDecIdx];
}

void CampaignManager::resolveDecision(int choiceIdx, LuaEngine& lua)
{
    if (m_pendingDecIdx < 0) return;
    auto& dec = m_missions[m_currentIdx].decisions[m_pendingDecIdx];

    if (choiceIdx < 0 || choiceIdx >= static_cast<int>(dec.choices.size())) return;

    dec.chosenIdx = choiceIdx;
    dec.resolved  = true;

    const auto& choice = dec.choices[choiceIdx];
    m_alignment.apply(choice.alignment.orderDelta, choice.alignment.lightDelta);

    gLog("[Campaign] Decision '%s' → '%s'  alignment now (%+d,%+d)\n",
        dec.prompt.c_str(), choice.label.c_str(),
        m_alignment.orderScore(), m_alignment.lightScore());

    // Fire Lua callback if present
    if (!choice.luaCallback.empty() && lua.isReady()) {
        ScriptContext ctx;
        lua.callFunction(choice.luaCallback, ctx);
    }

    m_pendingDecIdx = -1;
    fireEvent(CampaignEvent::DecisionResolved);
}

const CampaignMission* CampaignManager::currentMission() const
{
    if (m_currentIdx >= static_cast<int>(m_missions.size())) return nullptr;
    return &m_missions[m_currentIdx];
}

FactionId CampaignManager::unlockedFaction(bool convergenceEligible) const
{
    return m_alignment.resolveUnlock(convergenceEligible);
}

int CampaignManager::completedObjectives() const
{
    if (!currentMission()) return 0;
    int n = 0;
    for (auto& o : currentMission()->objectives) if (o.completed && o.required) ++n;
    return n;
}

int CampaignManager::requiredObjectives() const
{
    if (!currentMission()) return 0;
    int n = 0;
    for (auto& o : currentMission()->objectives) if (o.required) ++n;
    return n;
}

// ── Private helpers ────────────────────────────────────────────────────────────
void CampaignManager::tryCompleteObjective(CampaignObjective& obj, bool success)
{
    obj.completed = success;
    if (success) {
        gLog("[Campaign] Objective complete: %s\n", obj.description.c_str());
        fireEvent(CampaignEvent::ObjectiveCompleted);
    }
}

void CampaignManager::checkAllObjectives()
{
    if (m_over) return;
    const auto& mission = m_missions[m_currentIdx];

    bool allRequired = true;
    for (const auto& obj : mission.objectives)
        if (obj.required && !obj.completed) { allRequired = false; break; }

    if (allRequired) {
        completeMission(true);
    }
}

void CampaignManager::triggerMissionLoss()
{
    if (!m_over)
        completeMission(false);
}

void CampaignManager::completeMission(bool won)
{
    fireEvent(won ? CampaignEvent::MissionCompleted : CampaignEvent::MissionFailed);
    const auto& mission = m_missions[m_currentIdx];

    int next = won ? mission.nextMissionWin : mission.nextMissionLose;

    if (next < 0 || next >= static_cast<int>(m_missions.size())) {
        m_over = true;
        m_won  = won;
        fireEvent(CampaignEvent::CampaignEnded);
        gLog("[Campaign] Campaign %s! Final alignment: %s\n",
               won ? "COMPLETE" : "FAILED", m_alignment.getTitle());
    } else {
        startMission(next);
    }
}

void CampaignManager::fireEvent(CampaignEvent e)
{
    if (m_onEvent) m_onEvent(e);
}

// ── Persistence ────────────────────────────────────────────────────────────────
CampaignSaveState CampaignManager::toSaveState() const
{
    CampaignSaveState s;
    s.active     = !m_missions.empty();
    s.missionIdx = m_currentIdx;
    s.orderScore = m_alignment.orderScore();
    s.lightScore = m_alignment.lightScore();
    if (m_currentIdx < static_cast<int>(m_missions.size())) {
        for (auto& dec : m_missions[m_currentIdx].decisions) {
            if (dec.resolved)
                s.decisions.push_back({dec.id, dec.chosenIdx});
        }
    }
    return s;
}

void CampaignManager::fromSaveState(const CampaignSaveState& s)
{
    if (!s.active || m_missions.empty()) return;
    m_currentIdx = std::clamp(s.missionIdx, 0, static_cast<int>(m_missions.size()) - 1);
    m_alignment.reset();
    m_alignment.apply(s.orderScore, s.lightScore);
    if (m_currentIdx < static_cast<int>(m_missions.size())) {
        for (auto& [id, choice] : s.decisions) {
            for (auto& dec : m_missions[m_currentIdx].decisions) {
                if (dec.id == id) {
                    dec.resolved  = true;
                    dec.chosenIdx = choice;
                    break;
                }
            }
        }
    }
}
