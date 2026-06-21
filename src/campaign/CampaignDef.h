#pragma once
#include <string>
#include <vector>
#include "../world/HexMap.h"   // HexCoord
#include "../hero/Hero.h"      // FactionId
#include "../data/Resources.h" // ResourceType

// ── Alignment delta applied by a decision choice ───────────────────────────────
struct AlignmentDelta
{
    int orderDelta = 0;   // negative = Chaos shift, positive = Order shift
    int lightDelta = 0;   // negative = Dark shift,  positive = Light shift
};

// ── One option in a branching decision ────────────────────────────────────────
struct CampaignChoice
{
    std::string    label;
    std::string    tooltip;        // shown on hover — hint at consequence
    AlignmentDelta alignment;
    std::string    luaCallback;    // optional: function name in scripts/campaign.lua
};

// ── A decision panel presented to the player during a mission ─────────────────
struct CampaignDecision
{
    uint32_t    id;
    std::string prompt;
    std::string contextText;   // flavour paragraph
    std::vector<CampaignChoice> choices;   // 2-4 options

    int  chosenIdx = -1;
    bool resolved  = false;
};

// ── Mission objective types ────────────────────────────────────────────────────
enum class ObjectiveType : uint8_t
{
    CaptureTown,
    DefeatHero,
    SurviveWeeks,
    CollectResources,
    ReachTile,
};

// ── One checkable objective ───────────────────────────────────────────────────
struct CampaignObjective
{
    uint32_t      id;
    ObjectiveType type;
    std::string   description;
    bool          required    = true;    // false = bonus
    bool          completed   = false;

    // Payload — which field is active depends on type
    uint32_t     targetId    = 0;       // CaptureTown / DefeatHero
    int          targetValue = 0;       // SurviveWeeks count / resource amount
    ResourceType resourceType = ResourceType::Gold;
    HexCoord     targetTile  = {0,0};
};

// ── One campaign mission ───────────────────────────────────────────────────────
struct CampaignMission
{
    int         id;
    std::string name;
    std::string briefing;
    FactionId   playerFaction = FactionId::HolyOrder;
    int         startWeeks    = 4;    // army size seed for ArmyBuilder

    std::vector<CampaignObjective> objectives;
    std::vector<CampaignDecision>  decisions;

    // Decision trigger weeks (fire decision N at this week)
    std::vector<std::pair<int,int>> decisionWeekTriggers; // {week, decisionId}

    int nextMissionWin  = -1;   // -1 = campaign end
    int nextMissionLose = -1;   // -1 = game over screen
};
