#include "Game.h"
#include <imgui.h>
#include <stdio.h>

// ── Tutorial slide data ───────────────────────────────────────────────────────
struct TutorialSlide {
    const char* title;
    const char* body;
};

static const TutorialSlide kTutorial[] = {
    {
        "Welcome to the Campaign",
        "You are a commander in a fractured world on the brink of war.\n\n"
        "Three chapters lie ahead — each choice you make will shape the ending.\n\n"
        "This brief tutorial will walk you through the core mechanics."
    },
    {
        "Moving Your Hero",
        "Click any highlighted hex on the world map to move your hero there.\n\n"
        "Each hero has a limited number of movement points per turn (shown in the HUD).\n\n"
        "Standing on a ROAD tile costs half movement. Rough terrain costs more."
    },
    {
        "Resources",
        "Six resources drive your war machine:\n\n"
        "  Gold   — the universal currency, earned from towns and mines.\n"
        "  Iron   — needed for Forts and advanced constructions.\n"
        "  Faith Stones, Verdant Sap, Mercury, Blood Essence\n"
        "             — faction resources for advanced buildings and units.\n\n"
        "Capture resource mines on the map to gain weekly income."
    },
    {
        "Towns & Buildings",
        "Enter one of your towns (move your hero onto its hex) to manage it.\n\n"
        "Build dwellings to recruit new unit tiers each week.\n"
        "Build the Fort to train and garrison troops.\n"
        "Build the Mage Guild to learn spells.\n\n"
        "Each faction has unique buildings — explore them all."
    },
    {
        "Combat",
        "When your hero meets an enemy on the world map, battle begins.\n\n"
        "Combat is turn-based. Units act in initiative order (fastest first).\n"
        "Click a highlighted hex to MOVE your unit.\n"
        "Click an enemy unit to ATTACK it.\n\n"
        "Use the action bar at the bottom to Wait, Defend, or cast Spells."
    },
    {
        "Ending Your Turn",
        "Press SPACE or click the 'End Turn' button in the HUD to end your turn.\n\n"
        "Enemy AI heroes will then take their actions.\n"
        "After all factions have acted, a new day begins.\n"
        "After 7 days, a new week begins — your mines and towns pay out income."
    },
    {
        "The Utopia Ruins",
        "You will find a set of Utopia Ruins nearby — marked with a golden crown icon.\n\n"
        "Enter the tile to face its weakened guardians in your first real battle.\n"
        "Use the action bar to move, attack, and cast spells.\n\n"
        "Defeating the guardians unlocks a reward AND allows you to found a city there!"
    },
    {
        "Found City",
        "Once you have cleared a Utopia and reached Level 5, you gain the 'Found City' spell.\n\n"
        "Open 'World Spells' in the action bar, stand on the cleared Utopia tile,\n"
        "and cast Found City to build a new town of any faction you choose.\n\n"
        "This is your second city — treasure it. Good luck, Commander."
    },
};
static constexpr int kTutorialCount = static_cast<int>(sizeof(kTutorial) / sizeof(kTutorial[0]));

// ── Campaign update ───────────────────────────────────────────────────────────
void Game::updateCampaign(float dt)
{
    // Campaign runs on top of the world map — delegate all input and movement.
    // SPACE (end turn / week advance) and hero movement are handled there.
    updateWorldMap(dt);
}

// ── Tutorial modal ────────────────────────────────────────────────────────────
void Game::renderCampaignTutorial()
{
    ImGui::OpenPopup("Tutorial##camp");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Tutorial##camp", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

        // Progress indicator
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "  %d / %d", m_tutorialStep + 1, kTutorialCount);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.2f, 1.0f),
            "%s", kTutorial[m_tutorialStep].title);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 450.0f);
        ImGui::TextUnformatted(kTutorial[m_tutorialStep].body);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bw = ImGui::GetWindowWidth() - 32.0f;

        bool isLast = (m_tutorialStep == kTutorialCount - 1);
        if (isLast) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.15f, 1.0f));
            if (ImGui::Button("Begin Campaign", ImVec2(bw, 36))) {
                m_campaignTutorialSeen = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Next  >>", ImVec2(bw * 0.6f, 32))) {
                ++m_tutorialStep;
            }
            ImGui::SameLine();
            if (ImGui::Button("Skip Tutorial", ImVec2(-1, 32))) {
                m_campaignTutorialSeen = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

// ── Campaign render ───────────────────────────────────────────────────────────
void Game::renderCampaign()
{
    // Ocean-blue clear + world map hex grid (same as world map)
    glClearColor(0.04f, 0.12f, 0.30f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_hexRenderer.render(m_map, m_camera, m_hovered, m_selected, m_fogDisabled);

    // Hero sprites
    for (auto& hero : m_heroes)
        drawHero(hero);
    for (auto& hero : m_enemyHeroes)
        drawHero(hero);

    beginImGuiFrame();

    if (!m_campaignTutorialSeen) {
        renderCampaignTutorial();
    } else {
        // Full world-map HUD (resource bar, hero list, end-turn, all popups)
        renderWorldMapImGui();
        // Campaign overlay (objectives, alignment, decision modals) on top
        m_campaignHUD.render(m_campaign, m_lua);
    }

    endImGuiFrame();

    // End screen requested return to menu
    if (m_campaignHUD.wantsReturnToMenu()) {
        m_campaignHUD.resetReturnToMenu();
        m_state    = GameState::MainMenu;
        m_menuMode = 0;
    }
}

// ── State transitions ─────────────────────────────────────────────────────────
void Game::enterCampaign()
{
    // Mission 1 plays as Holy Order (index 0); generate a fresh medium map
    m_newGameFaction  = 0;   // HolyOrder
    m_newGameMapSize  = 1;   // Medium
    m_newGameClassId  = 0;   // auto-assign first class for faction
    startNewGame();
    m_state = GameState::Campaign;

    // Place a weakened tutorial Utopia 4 tiles east of the starting hero
    if (!m_heroes.empty()) {
        const Hero& ph = m_heroes[0];
        // Find a valid tile 4 tiles away (try east, SE, NE variants)
        HexCoord candidates[] = {
            {ph.pos.q + 4, ph.pos.r},
            {ph.pos.q + 3, ph.pos.r + 2},
            {ph.pos.q + 4, ph.pos.r - 2},
        };
        for (auto& cpos : candidates) {
            HexTile* ct = m_map.getTile(cpos);
            if (ct && ct->terrain != Terrain::Water && ct->townId == 0 && ct->resourceId == 0) {
                WorldObject uto;
                uto.id      = m_nextObjId++;
                uto.type    = WorldObjectType::Utopia;
                uto.pos     = cpos;
                uto.value   = -1;   // tutorial marker: weakened guards
                uto.faction = 0;    // HolyOrder guardian style
                uto.collected = false;
                m_worldObjects.push_back(uto);
                break;
            }
        }
    }
    m_campaign.init();

    // Lock in convergence eligibility at campaign start (HideoutDB state won't change mid-run)
    m_campaign.setConvergenceEligible(m_hideout.isConvergenceUnlocked());

    m_campaign.setEventCallback([this](CampaignEvent e) {
        if (e == CampaignEvent::MissionCompleted)
            gLog("[Campaign] Mission complete!\n");
        else if (e == CampaignEvent::MissionFailed)
            gLog("[Campaign] Mission failed.\n");
        else if (e == CampaignEvent::CampaignEnded) {
            bool convergenceOk = m_campaign.convergenceEligible();
            FactionId unlocked = m_campaign.unlockedFaction(convergenceOk);
            gLog("[Campaign] Ended — faction unlocked: %d\n",
                   static_cast<int>(unlocked));
            if (m_campaign.playerWon()) {
                m_hideout.completeMilestone(Milestone::CAMPAIGN_WON);
                m_hideout.addXP(200);   // bonus XP for completing the campaign
                if (convergenceOk)
                    m_hideout.completeMilestone(Milestone::CONVERGENCE_UNLOCK);
            }
        }
    });
    gLog("Entered Campaign\n");
}

void Game::exitCampaign()
{
    m_state    = GameState::MainMenu;
    m_menuMode = 0;
    gLog("Exited Campaign\n");
}
