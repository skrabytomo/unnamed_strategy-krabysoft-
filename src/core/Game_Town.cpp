#include "Game.h"
#include <cstdio>
#include <algorithm>
#include "../town/BuildingRegistry.h"
#include "../magic/SpellRegistry.h"
#include "../world/HexGrid.h"
#include "../world/FogOfWar.h"
#include "../hero/Artifacts.h"
#include "../hero/HeroClass.h"
#include "../hero/SkillRegistry.h"
#include <imgui.h>
#include <stdio.h>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>

// ── Town update ───────────────────────────────────────────────────────────────
void Game::updateTown(float dt)
{
    (void)dt;
    const auto& mouse = m_input.mouse();
    bool imguiWants = ImGui::GetIO().WantCaptureMouse;

    if (mouse.leftDown && !imguiWants)
        m_townScreen.onMouseDown(static_cast<float>(mouse.x),
                                 static_cast<float>(mouse.y));
    if (mouse.leftUp && !imguiWants)
        m_townScreen.onMouseUp(static_cast<float>(mouse.x),
                               static_cast<float>(mouse.y));
    m_townScreen.onMouseMove(static_cast<float>(mouse.x),
                             static_cast<float>(mouse.y));
}

// ── Town render ───────────────────────────────────────────────────────────────
void Game::renderTown()
{
    beginImGuiFrame();          // must come BEFORE any ImGui calls in draw()
    m_ui.beginFrame();
    m_townScreen.draw(m_ui);
    m_ui.endFrame();
    m_ui.flushText(ImGui::GetBackgroundDrawList());

    // Service buttons bar — top strip above the town panel
    {
        const Town* town = m_townScreen.currentTown();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)m_width, 0), ImGuiCond_Always);
        ImGui::Begin("##TownServices", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBackground);

        if (town && town->hasBuilding(BID::MAGE_GUILD)) {
            if (ImGui::Button(m_showMageGuildPanel ? "[Mage Guild X]" : "Mage Guild"))
                m_showMageGuildPanel = !m_showMageGuildPanel;
            ImGui::SameLine();
        }
        if (town && town->ownerId == currentPlayerId()) {
            if (ImGui::Button(m_showTavernPanel ? "[Tavern X]" : "Tavern"))
                m_showTavernPanel = !m_showTavernPanel;
            ImGui::SameLine();
            if (ImGui::Button(m_showGarrisonPanel ? "[Garrison X]" : "Garrison")) {
                m_showGarrisonPanel = !m_showGarrisonPanel;
                m_garrisonSelSlot = -1;
                m_garrisonSelSide = -1;
                // Default recruit to garrison when panel opens
                m_townScreen.setRecruitTarget(m_showGarrisonPanel);
            }
            ImGui::SameLine();
        }
        // Market: always visible, disabled if no player town has MARKET built
        {
            bool anyMarket = false;
            for (const auto& t : m_towns)
                if (t.ownerId == currentPlayerId() && t.hasBuilding(BID::MARKET)) { anyMarket = true; break; }
            if (!anyMarket) ImGui::BeginDisabled();
            if (ImGui::Button(m_showMarketPanel ? "[Market X]" : "Market"))
                m_showMarketPanel = !m_showMarketPanel;
            if (!anyMarket) {
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Build a Marketplace in one of your towns first.");
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
        }
        // Artifact Forge: only when current town has MARKET
        if (town && town->hasBuilding(BID::MARKET)) {
            if (ImGui::Button(m_showArtifactForgePanel ? "[Forge X]" : "Artifact Forge"))
                m_showArtifactForgePanel = !m_showArtifactForgePanel;
            ImGui::SameLine();
        }
        // Fortify — only when this town is under siege and not yet fortified this turn
        // Resolve mutable pointer so we can set fortify flags
        Town* mutableTown = nullptr;
        if (town) { for (auto& t : m_towns) if (t.id == town->id) { mutableTown = &t; break; } }
        if (mutableTown && mutableTown->underSiege && !mutableTown->siegeFortified) {
            // Cost: 500 Gold + 5 of faction primary resource
            auto factionPrimary = [](FactionId f) -> ResourceType {
                switch (f) {
                    case FactionId::HolyOrder:     return ResourceType::FaithStones;
                    case FactionId::CrimsonWardens:return ResourceType::FaithStones;
                    case FactionId::Thornkin:      return ResourceType::VerdantSap;
                    case FactionId::EternalEmpire: return ResourceType::Mercury;
                    case FactionId::Bloodsworn:    return ResourceType::BloodEssence;
                    case FactionId::Voidkin:       return ResourceType::VerdantSap;
                    case FactionId::IronAssembly:  return ResourceType::Iron;
                    case FactionId::Amalgamate:    return ResourceType::BloodEssence;
                    case FactionId::Convergence:   return ResourceType::Mercury;
                    default:                       return ResourceType::Gold;
                }
            };
            ResourceType primRes = factionPrimary(mutableTown->faction);
            Resources& res = (m_hotSeatMode && m_hotSeatP2Turn) ? m_player2Resources : m_playerResources;
            bool canAfford = res.get(ResourceType::Gold) >= 500 &&
                             res.get(primRes) >= 5;

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.7f, 0.4f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
            if (!canAfford) ImGui::BeginDisabled();
            if (ImGui::Button("Fortify!")) {
                res.add(ResourceType::Gold, -500);
                res.add(primRes, -5);
                mutableTown->siegeFortified    = true;
                mutableTown->fortifyDefBonus   = 4;
                mutableTown->fortifyWallBonus  = 2;
                mutableTown->fortifyTowerBonus = 3;
            }
            if (!canAfford) ImGui::EndDisabled();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (canAfford) {
                    ImGui::SetTooltip(
                        "Fortify\n\n"
                        "Rally defenders before the siege assault.\n"
                        "+4 DEF to all garrison units\n"
                        "+2 wall HP rounds\n"
                        "+3 tower damage per shot\n\n"
                        "Cost: 500 Gold + 5 %s\n"
                        "One use per siege turn.",
                        resourceName(primRes)
                    );
                } else {
                    ImGui::SetTooltip(
                        "Fortify\n\n"
                        "Insufficient resources.\n"
                        "Requires 500 Gold + 5 %s.",
                        resourceName(primRes)
                    );
                }
            }
            ImGui::SameLine();
        } else if (mutableTown && mutableTown->underSiege && mutableTown->siegeFortified) {
            ImGui::BeginDisabled();
            ImGui::Button("Fortified");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Already fortified this siege turn.");
            ImGui::SameLine();
        }

        // Spacer + exit button pushed to the right
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 130.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("EXIT TOWN  [ESC]", ImVec2(125.0f, 0)))
            exitTown();
        ImGui::PopStyleColor(2);
        ImGui::End();
    }

    if (m_showMageGuildPanel)    renderMageGuild();
    if (m_showTavernPanel)       renderTavern();
    if (m_showMarketPanel)       renderMarketplace();
    if (m_showArtifactForgePanel) renderArtifactForge();
    if (m_showGarrisonPanel)     renderGarrisonPanel();
    if (m_showCapturePopup) renderCapturePopup();
    if (m_showUpgradePathPopup) renderUpgradePathPopup();
    endImGuiFrame();
}

// ── Garrison management panel ─────────────────────────────────────────────────
void Game::renderGarrisonPanel()
{
    // Resolve mutable town pointer
    const Town* ct = m_townScreen.currentTown();
    if (!ct) return;
    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == ct->id) { town = &t; break; }
    if (!town) return;
    Hero* hero = m_heroes.empty() ? nullptr : &m_heroes[m_activeHeroIdx];

    const auto& unitDefs = m_registry.units();

    // Look up UnitDef and its texture by defId
    auto getUd = [&](int defId) -> const UnitDef* {
        for (const auto& u : unitDefs) if (u.id == defId) return &u;
        return nullptr;
    };
    auto getUnitTex = [&](const UnitDef* ud) -> ImTextureID {
        if (!ud) return nullptr;
        int fid = std::clamp(static_cast<int>(ud->faction), 0, NUM_FACTIONS - 1);
        int tid = std::clamp(ud->tier - 1, 0, NUM_UNIT_TIERS - 1);
        return m_unitTex[fid][tid].ok() ? (ImTextureID)(uintptr_t)m_unitTex[fid][tid].id() : nullptr;
    };

    const float SW = 58.0f, SH = 80.0f, GAP = 4.0f;
    const int   NS = 7;

    ImGuiIO& io = ImGui::GetIO();
    float panW = NS * (SW + GAP) + 24.0f;
    float panH = 240.0f;
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.55f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({panW, panH}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (!ImGui::Begin("Garrison##mgmt", &m_showGarrisonPanel,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End(); return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Draw one 7-slot row for either hero army (side=0) or town garrison (side=1)
    auto drawRow = [&](int side, std::vector<UnitStack>& army) {
        float rowStartX = ImGui::GetCursorScreenPos().x;
        float rowStartY = ImGui::GetCursorScreenPos().y;

        for (int i = 0; i < NS; ++i) {
            if (i > 0) ImGui::SameLine(0, GAP);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool hasUnit = (i < (int)army.size() && army[i].count > 0);
            bool selected = (m_garrisonSelSide == side && m_garrisonSelSlot == i);

            // Slot background
            ImU32 bg  = hasUnit ? IM_COL32(20, 22, 35, 235) : IM_COL32(12, 14, 22, 180);
            ImU32 brd = selected ? IM_COL32(255, 195, 40, 255)
                      : hasUnit  ? IM_COL32(90, 100, 130, 200)
                                 : IM_COL32(40, 45, 65, 150);
            dl->AddRectFilled(pos, {pos.x + SW, pos.y + SH}, bg, 4.0f);
            dl->AddRect(pos, {pos.x + SW, pos.y + SH}, brd, 4.0f, 0, selected ? 2.5f : 1.5f);

            if (hasUnit) {
                const UnitStack& s = army[i];
                const UnitDef* ud = getUd(s.defId);
                ImTextureID tex = getUnitTex(ud);

                float sprW2 = SW - 4.0f, sprH2 = SH - 20.0f;
                float sprX = pos.x + 2.0f, sprY = pos.y + 2.0f;
                if (tex) {
                    dl->AddImage(tex, {sprX, sprY}, {sprX + sprW2, sprY + sprH2},
                                 {0.0f, 0.0f}, {0.125f, 1.0f}); // frame 0 of 8
                } else {
                    dl->AddRectFilled({sprX, sprY}, {sprX + sprW2, sprY + sprH2},
                                      IM_COL32(28, 32, 48, 200), 3.0f);
                    if (ud) {
                        char t2[4]; std::snprintf(t2, sizeof(t2), "T%d", ud->tier);
                        dl->AddText({sprX + sprW2 * 0.5f - 8, sprY + sprH2 * 0.5f - 7},
                                    IM_COL32(140, 150, 180, 200), t2);
                    }
                }
                // Count badge
                char cnt[12]; std::snprintf(cnt, sizeof(cnt), "x%d", s.count);
                ImVec2 sz = ImGui::CalcTextSize(cnt);
                float cx = pos.x + (SW - sz.x) * 0.5f;
                float cy = pos.y + SH - 16.0f;
                dl->AddText({cx + 1, cy + 1}, IM_COL32(0, 0, 0, 200), cnt);
                dl->AddText({cx, cy},          IM_COL32(225, 225, 255, 255), cnt);
            } else {
                dl->AddText({pos.x + SW * 0.5f - 4, pos.y + SH * 0.5f - 7},
                            IM_COL32(45, 50, 72, 160), "--");
            }

            // Invisible button for click + hover
            char bid[24]; std::snprintf(bid, sizeof(bid), "##gs%d_%d", side, i);
            ImGui::InvisibleButton(bid, {SW, SH});

            if (ImGui::IsItemHovered() && hasUnit) {
                const UnitDef* ud = getUd(army[i].defId);
                if (ud) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s  x%d", ud->name.c_str(), army[i].count);
                    ImGui::TextDisabled("ATK %d  DEF %d  HP %d  SPD %d",
                                       ud->attack, ud->defense, ud->hp, ud->speed);
                    if (ud->range > 0) ImGui::TextDisabled("Ranged (shots %d)", ud->shots);
                    if (ud->flying)    ImGui::TextDisabled("Flying");
                    if (ud->vampiric)  ImGui::TextDisabled("Vampiric");
                    ImGui::EndTooltip();
                }
            }

            if (ImGui::IsItemClicked()) {
                if (m_garrisonSelSide >= 0) {
                    // Transfer unless clicking own selected slot (deselect)
                    if (m_garrisonSelSide == side && m_garrisonSelSlot == i) {
                        m_garrisonSelSide = -1; m_garrisonSelSlot = -1;
                    } else {
                        // Perform transfer
                        auto& fromArmy = (m_garrisonSelSide == 0 && hero) ? hero->army : town->garrison;
                        auto& toArmy   = (side == 0 && hero)              ? hero->army : town->garrison;
                        if (m_garrisonSelSlot < (int)fromArmy.size()) {
                            if (i >= 7) { m_garrisonSelSide = -1; m_garrisonSelSlot = -1; return; }
                            while ((int)toArmy.size() <= i) toArmy.push_back({0, 0});
                            UnitStack& from = fromArmy[m_garrisonSelSlot];
                            UnitStack& to   = toArmy[i];
                            if (to.count == 0) {
                                to = from; from = {0, 0};
                            } else if (to.defId == from.defId) {
                                to.count += from.count; from = {0, 0};
                            } else {
                                std::swap(from, to);
                            }
                            // Trim trailing empty slots
                            while (fromArmy.size() > 1 && fromArmy.back().count == 0)
                                fromArmy.pop_back();
                            while (toArmy.size() > 1 && toArmy.back().count == 0)
                                toArmy.pop_back();
                        }
                        m_garrisonSelSide = -1; m_garrisonSelSlot = -1;
                    }
                } else if (hasUnit) {
                    m_garrisonSelSide = side;
                    m_garrisonSelSlot = i;
                }
            }
        }
        (void)rowStartX; (void)rowStartY;
        ImGui::Spacing();
    };

    // ── Hero army row ─────────────────────────────────────────────────────────
    if (hero) {
        ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "Hero: %s", hero->name.c_str());
        drawRow(0, hero->army);
    } else {
        ImGui::TextDisabled("No hero in town");
        static std::vector<UnitStack> empty;
        drawRow(0, empty);
    }

    ImGui::Separator();

    // ── Town garrison row ─────────────────────────────────────────────────────
    ImGui::TextColored({0.65f, 0.75f, 0.95f, 1.0f}, "Garrison: %s", town->name.c_str());
    drawRow(1, town->garrison);

    ImGui::Separator();
    ImGui::TextDisabled("Click unit to select  |  Click target slot to move / merge / swap");

    ImGui::End();
}

// ── Mage Guild overlay ────────────────────────────────────────────────────────
void Game::renderMageGuild()
{
    // Only show when the active town has a Mage Guild built
    if (m_heroes.empty() || m_towns.empty()) return;
    const Town* town = m_townScreen.currentTown();
    if (!town || !town->hasBuilding(BID::MAGE_GUILD)) return;

    Hero& hero = m_heroes[m_activeHeroIdx];

    // Faction spell offerings — 4 spells per faction, guild T1 shows first 2
    struct GuildEntry { int spellId; int goldCost; };
    using GE = GuildEntry;

    auto getEntries = [](FactionId f) -> std::vector<GuildEntry> {
        switch (f) {
        case FactionId::HolyOrder:     return {{SPL::BLESS,4000},{SPL::SMITE,5000},{SPL::DIVINE_SHIELD,4000},{SPL::RADIANCE,8000}};
        case FactionId::Bloodsworn:    return {{SPL::BLOOD_FRENZY,4000},{SPL::DRAIN_LIFE,5000},{SPL::ENERVATE,4500},{SPL::HEMORRHAGE,7000}};
        case FactionId::Thornkin:      return {{SPL::BARKSKIN,4000},{SPL::ENTANGLE,4500},{SPL::SERPENT_VENOM,5500},{SPL::CALL_LIGHTNING,6000}};
        case FactionId::EternalEmpire: return {{SPL::CURSE,4000},{SPL::WITHER,5000},{SPL::DEATH_COIL,6000},{SPL::VENOMOUS_CLOUD,8000}};
        case FactionId::CrimsonWardens:return {{SPL::WITHER,4000},{SPL::VENOMOUS_CLOUD,6000},{SPL::DEATH_COIL,5000},{SPL::PLAGUE,8000}};
        case FactionId::Voidkin:       return {{SPL::CURSE,4000},{SPL::ENTANGLE,4500},{SPL::WITHER,5000},{SPL::VENOMOUS_CLOUD,7000}};
        case FactionId::IronAssembly:  return {{SPL::REINFORCE,4000},{SPL::OVERCLOCK,4500},{SPL::SHRAPNEL,6000},{SPL::NAPALM,7500}};
        case FactionId::Amalgamate:    return {{SPL::MEND_FLESH,4000},{SPL::FESTER,5000},{SPL::ACID_SPRAY,5500},{SPL::GROWTH,7000}};
        case FactionId::Convergence:   return {{SPL::BLESS,4000},{SPL::CURSE,4000},{SPL::REINFORCE,4500},{SPL::REGROWTH,5500}};
        default:                       return {};
        }
    };
    auto entries = getEntries(town->faction);
    if (entries.empty()) return;

    const auto& entries_ref = entries;

    // Tier determines spell access and cost discount
    int  available = 2;
    float costMult  = 1.0f;
    int  tierLevel  = 1;
    if      (town->hasBuilding(BID::MAGE_GUILD_T4)) { available=4; costMult=0.5f;  tierLevel=4; }
    else if (town->hasBuilding(BID::MAGE_GUILD_T3)) { available=4; costMult=0.70f; tierLevel=3; }
    else if (town->hasBuilding(BID::MAGE_GUILD_T2)) { available=4; costMult=1.0f;  tierLevel=2; }

    // T4 bonus: grant hero +5 max mana on visit (one-time per visit)
    if (tierLevel == 4) {
        if (m_mageGuildT4BonusGiven.find(hero.id) == m_mageGuildT4BonusGiven.end()) {
            hero.maxMana += 5;
            hero.mana     = std::min(hero.mana + 5, hero.maxMana);
            m_mageGuildT4BonusGiven.insert(hero.id);
        }
    }

    char titleBuf[48];
    std::snprintf(titleBuf, sizeof(titleBuf), "Mage Guild (Tier %d)###MageGuild", tierLevel);
    ImGui::SetNextWindowPos(ImVec2(20, 32), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);
    if (!ImGui::Begin(titleBuf, nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    ImGui::Text("Gold: %d", currentResources().get(ResourceType::Gold));
    if (costMult < 1.0f)
        ImGui::TextColored(ImVec4(0.4f,1.f,0.6f,1.f), "Tier %d discount: %.0f%% off",
                           tierLevel, (1.0f - costMult) * 100.0f);
    ImGui::Separator();

    for (int i = 0; i < available && i < static_cast<int>(entries_ref.size()); ++i) {
        const SpellDef* sp = findSpell(entries_ref[i].spellId);
        if (!sp) continue;

        bool alreadyKnown = false;
        for (int sid : hero.knownSpells) if (sid == sp->id) { alreadyKnown = true; break; }

        ImGui::PushID(i);
        if (alreadyKnown) {
            ImGui::TextColored(ImVec4(0.4f,1.f,0.4f,1.f), "[known] %s", sp->name);
        } else {
            int cost = static_cast<int>(entries_ref[i].goldCost * costMult);
            bool canAfford = currentResources().get(ResourceType::Gold) >= cost;
            if (!canAfford) ImGui::BeginDisabled();
            char btn[80];
            if (costMult < 1.0f)
                std::snprintf(btn, sizeof(btn), "Learn %s  (%dg, was %dg)",
                              sp->name, cost, entries_ref[i].goldCost);
            else
                std::snprintf(btn, sizeof(btn), "Learn %s  (%dg)", sp->name, cost);
            if (ImGui::Button(btn, ImVec2(-1,0))) {
                hero.knownSpells.push_back(sp->id);
                currentResources().add(ResourceType::Gold, -cost);
            }
            if (!canAfford) ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", sp->desc);
        ImGui::PopID();
    }

    // Neutral spells — always available regardless of faction
    // Visions: T1 guild. Town Portal: T2+ guild.
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.75f, 0.75f, 1.0f, 1.0f), "Neutral (Universal)");
    struct NeutralEntry { int spellId; int goldCost; int minTier; };
    static constexpr NeutralEntry kNeutral[] = {
        { SPL::VISIONS,     2000, 1 },
        { SPL::TOWN_PORTAL, 4000, 2 },
    };
    for (const auto& ne : kNeutral) {
        if (tierLevel < ne.minTier) continue;
        const SpellDef* sp = findSpell(ne.spellId);
        if (!sp) continue;
        bool alreadyKnown = false;
        for (int sid : hero.knownSpells) if (sid == sp->id) { alreadyKnown = true; break; }
        ImGui::PushID(ne.spellId + 1000);
        if (alreadyKnown) {
            ImGui::TextColored(ImVec4(0.4f,1.f,0.4f,1.f), "[known] %s", sp->name);
        } else {
            int cost = static_cast<int>(ne.goldCost * costMult);
            bool canAfford = currentResources().get(ResourceType::Gold) >= cost;
            if (!canAfford) ImGui::BeginDisabled();
            char btn[80];
            std::snprintf(btn, sizeof(btn), "Learn %s  (%dg)", sp->name, cost);
            if (ImGui::Button(btn, ImVec2(-1, 0))) {
                hero.knownSpells.push_back(sp->id);
                currentResources().add(ResourceType::Gold, -cost);
            }
            if (!canAfford) ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", sp->desc);
        ImGui::PopID();
    }

    ImGui::End();
}

// ── Artifact Forge — craft Basic artifacts ────────────────────────────────────
void Game::renderArtifactForge()
{
    // Require MARKET to be built — the marketplace enables trading of goods/materials
    const Town* town = m_townScreen.currentTown();
    if (!town || !town->hasBuilding(BID::MARKET)) return;
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];

    ImGui::SetNextWindowPos(ImVec2(680, 32), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);
    if (!ImGui::Begin("Artifact Forge", nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    ImGui::TextDisabled("Craft Basic artifacts (requires MARKET)");
    ImGui::Separator();

    // Show current resources
    ImGui::Text("Gold: %d", currentResources().get(ResourceType::Gold));
    ImGui::SameLine(160.0f);
    ImGui::Text("Iron: %d", currentResources().get(ResourceType::Iron));
    ImGui::Separator();

    auto craftables = m_artifactRegistry.getCraftable();
    if (craftables.empty()) {
        ImGui::TextDisabled("No craftable artifacts registered.");
        ImGui::End(); return;
    }

    static const char* kSlotNames[] = {
        "Helm","Armor","Weapon","Shield","Ring","Boots","Cloak","Misc"
    };

    for (const ArtifactDef* art : craftables) {
        // Check if hero already has it equipped
        int slotIdx = static_cast<int>(art->slot);
        bool alreadyEquipped = (hero.artifacts.equippedIds[slotIdx] == art->id);
        bool inInventory = false;
        for (int inv : hero.artifactInventory) if (inv == art->id) { inInventory = true; break; }

        ImGui::PushID(art->id);

        // Cost check
        bool canAfford = true;
        for (int rt = 0; rt < RESOURCE_COUNT && canAfford; ++rt) {
            int needed = art->craftCost.get(static_cast<ResourceType>(rt));
            if (needed > 0 && currentResources().get(static_cast<ResourceType>(rt)) < needed)
                canAfford = false;
        }

        if (alreadyEquipped) {
            ImGui::TextColored(ImVec4(0.4f,1.f,0.4f,1.f), "[equipped] %s", art->name.c_str());
        } else if (inInventory) {
            // Can equip from inventory
            char btnLabel[80];
            std::snprintf(btnLabel, sizeof(btnLabel), "Equip %s  [in bag]", art->name.c_str());
            if (ImGui::Button(btnLabel, ImVec2(-1, 0))) {
                hero.artifacts.equip(art->id, art->slot);
                hero.artifactInventory.erase(
                    std::remove(hero.artifactInventory.begin(),
                                hero.artifactInventory.end(), art->id),
                    hero.artifactInventory.end());
            }
        } else {
            if (!canAfford) ImGui::BeginDisabled();
            // Build cost label
            char costStr[128] = {};
            int off = 0;
            for (int rt = 0; rt < RESOURCE_COUNT; ++rt) {
                int needed = art->craftCost.get(static_cast<ResourceType>(rt));
                if (needed <= 0) continue;
                static const char* kRNames[] = {"g","Fe","Fa","Bl","Sap","Hg"};
                off += std::snprintf(costStr + off, sizeof(costStr) - off,
                                     "%d%s ", needed, (rt < 6 ? kRNames[rt] : "?"));
            }
            char btnLabel[192];
            std::snprintf(btnLabel, sizeof(btnLabel), "Craft %s  [%s]  (%s)",
                          art->name.c_str(), kSlotNames[slotIdx], costStr);
            if (ImGui::Button(btnLabel, ImVec2(-1, 0))) {
                // Deduct cost
                for (int rt = 0; rt < RESOURCE_COUNT; ++rt) {
                    int needed = art->craftCost.get(static_cast<ResourceType>(rt));
                    if (needed > 0) currentResources().add(static_cast<ResourceType>(rt), -needed);
                }
                // Auto-equip if slot is free, else add to inventory
                if (hero.artifacts.getEquipped(art->slot) == 0) {
                    hero.artifacts.equip(art->id, art->slot);
                } else {
                    hero.artifactInventory.push_back(art->id);
                }
            }
            if (!canAfford) ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", art->description.c_str());

        ImGui::PopID();
    }
    ImGui::End();
}

// ── Tavern — hire a hero from candidates ────────────────────────────────────
void Game::renderTavern()
{
    const Town* town = m_townScreen.currentTown();
    if (!town || town->ownerId != currentPlayerId()) return;  // only in owned towns

    static constexpr int HIRE_COST  = 2500;
    static constexpr int MAX_HEROES = 3;
    static constexpr int NUM_CANDIDATES = 3;

    ImGui::SetNextWindowPos(ImVec2(350, 32), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);
    if (!ImGui::Begin("Tavern", nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    ImGui::Text("Gold: %d", currentResources().get(ResourceType::Gold));
    ImGui::Separator();

    if (static_cast<int>(m_heroes.size()) >= MAX_HEROES) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "Hero roster full (%d/%d).", MAX_HEROES, MAX_HEROES);
        ImGui::End(); return;
    }

    // Name pool
    static const char* kNames[] = {
        "Alara", "Dren", "Korvas", "Mira", "Seld", "Thayne",
        "Vex", "Lyra", "Brant", "Cael", "Essen", "Fynn",
        "Orin", "Yasha", "Wick", "Seren", "Gael", "Petra"
    };
    static const int kStartSpell[] = {
        SPL::BLESS,        // HolyOrder
        SPL::BLOOD_FRENZY, // CrimsonWardens
        SPL::ENTANGLE,     // Thornkin
        SPL::CURSE,        // EternalEmpire
        SPL::BLOOD_FRENZY, // Bloodsworn
        SPL::ENTANGLE,     // Voidkin
        SPL::REINFORCE,    // IronAssembly
        SPL::MEND_FLESH,   // Amalgamate
        SPL::BLESS,        // Convergence
    };

    auto buildHero = [&](int candidateSlot) -> Hero {
        // Deterministic seed per town + week + candidate slot
        uint32_t seed = static_cast<uint32_t>(town->pos.q * 997u + town->pos.r * 491u
                                              + m_turns.week() * 6271u + candidateSlot * 1013u);
        auto classes = m_classRegistry.getClassesForFaction(town->faction);

        Hero h;
        h.id      = 300u + static_cast<uint32_t>(candidateSlot);
        h.faction = town->faction;
        h.pos     = town->pos;
        h.movePool = h.maxMove;
        h.name    = kNames[seed % 18];

        if (!classes.empty()) {
            const HeroClassDef* cls = classes[(seed / 18) % classes.size()];
            h.classId = cls->id;
            if (!cls->skillPool.empty())
                h.skills.learn(cls->skillPool[0]);
        }
        int fi = static_cast<int>(town->faction);
        if (fi >= 0 && fi < 9) h.knownSpells.push_back(kStartSpell[fi]);

        for (int tier : {1, 2}) {
            int growth = 0;
            for (const auto& bd : m_registry.buildings()) {
                if (bd.faction == h.faction && bd.tier == tier
                    && bd.category == BuildingCategory::UnitDwelling
                    && bd.path == UpgradePath::None) {
                    growth = (tier == 1) ? bd.weeklyGrowth : bd.weeklyGrowth / 3;
                    break;
                }
            }
            if (growth <= 0) continue;
            for (const auto& ud : m_registry.units()) {
                if (ud.faction == h.faction && ud.tier == tier
                    && ud.path == UpgradePath::None) {
                    h.army.push_back({ud.id, growth});
                    break;
                }
            }
        }
        return h;
    };

    static constexpr int REHIRE_COST = 1000; // cheaper to rehire a defeated hero

    auto spawnHero = [&](Hero& h) {
        HexCoord spawnPos = town->pos;
        for (auto& nb : HexGrid::neighbors(town->pos)) {
            const HexTile* t2 = m_map.getTile(nb);
            if (t2 && t2->terrain != Terrain::Water && t2->heroId == 0) {
                spawnPos = nb; break;
            }
        }
        h.pos      = spawnPos;
        h.movePool = h.maxMove;
        m_heroes.push_back(h);
        if (HexTile* ht = m_map.getTile(spawnPos)) ht->heroId = h.id;
        FogOfWar::updateVision(m_map, h);
    };

    // ── Defeated heroes first (cheaper rehire) ────────────────────────────────
    // In 2P mode each player only sees their own defeated heroes
    auto& activeDefeatedPool = m_players[m_currentPlayerIdx].defeatedPool;
    if (!activeDefeatedPool.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                           "Defeated Heroes  (%dg to rehire):", REHIRE_COST);
        bool canRehire = currentResources().get(ResourceType::Gold) >= REHIRE_COST;
        for (int i = 0; i < (int)activeDefeatedPool.size(); ++i) {
            Hero& dh = activeDefeatedPool[i];
            ImGui::PushID(1000 + i);
            ImGui::Separator();
            const HeroClassDef* cls = m_classRegistry.getClass(dh.classId);
            char hdr[64];
            std::snprintf(hdr, sizeof(hdr), "%s  L%d  [%s]",
                          dh.name.c_str(), dh.level, cls ? cls->name.c_str() : "?");
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "%s", hdr);
            ImGui::TextDisabled("  XP: %d  ATK: %d  DEF: %d",
                                dh.xp, dh.attack, dh.defense);
            if (!canRehire) ImGui::BeginDisabled();
            char btn[48];
            std::snprintf(btn, sizeof(btn), "Rehire %s", dh.name.c_str());
            if (ImGui::Button(btn, ImVec2(-1, 0))) {
                currentResources().add(ResourceType::Gold, -REHIRE_COST);
                spawnHero(dh);
                gLog("Rehired defeated hero: %s\n", dh.name.c_str());
                activeDefeatedPool.erase(activeDefeatedPool.begin() + i);
            }
            if (!canRehire) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    // ── Fresh candidates ──────────────────────────────────────────────────────
    ImGui::TextDisabled("Choose a hero to hire  (%dg each):", HIRE_COST);
    bool canAfford = currentResources().get(ResourceType::Gold) >= HIRE_COST;

    for (int c = 0; c < NUM_CANDIDATES; ++c) {
        Hero cand = buildHero(c);
        const HeroClassDef* cls = m_classRegistry.getClass(cand.classId);

        ImGui::PushID(c);
        ImGui::Separator();

        // Candidate header
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "%s  [%s]", cand.name.c_str(),
                      cls ? cls->name.c_str() : "Unknown");
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", hdr);

        // Specialty description
        if (cls) ImGui::TextDisabled("  %s", cls->specialtyDesc.c_str());

        // Army preview
        char armyBuf[64] = {};
        int off2 = 0;
        for (const auto& s : cand.army) {
            for (const auto& ud : m_registry.units()) {
                if (ud.id == s.defId) {
                    off2 += std::snprintf(armyBuf + off2, sizeof(armyBuf) - off2,
                                          "%dx%s ", s.count, ud.name.c_str());
                    break;
                }
            }
        }
        if (off2 > 0) ImGui::TextDisabled("  Army: %s", armyBuf);

        // Hire button
        if (!canAfford) ImGui::BeginDisabled();
        char btnLabel[48];
        std::snprintf(btnLabel, sizeof(btnLabel), "Hire %s", cand.name.c_str());
        if (ImGui::Button(btnLabel, ImVec2(-1, 0))) {
            currentResources().add(ResourceType::Gold, -HIRE_COST);
            cand.id  = 200u + static_cast<uint32_t>(m_heroes.size());
            spawnHero(cand);
            gLog("Hired hero: %s (%s)\n", cand.name.c_str(), cls ? cls->name : "?");
        }
        if (!canAfford) ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Roster: %d/%d heroes", (int)m_heroes.size(), MAX_HEROES);

    // ── Artifact Wares (3 rotating Special artifacts for sale each week) ──────
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Wares  (restocks weekly)");

    auto specials = m_artifactRegistry.getByRarity(ArtifactRarity::Special);
    std::vector<const ArtifactDef*> forSale;
    for (auto* a : specials)
        if (a->shopPrice > 0) forSale.push_back(a);

    uint32_t shopSeed = (uint32_t)(town->pos.q * 1031u + town->pos.r * 7919u + m_turns.week() * 3313u);
    int wareGold = currentResources().get(ResourceType::Gold);
    for (int i = 0; i < 3 && !forSale.empty(); ++i) {
        const ArtifactDef* art = forSale[shopSeed % forSale.size()];
        shopSeed = shopSeed * 1664525u + 1013904223u;
        forSale.erase(std::remove(forSale.begin(), forSale.end(), art), forSale.end());

        bool alreadyOwn = false;
        if (m_activeHeroIdx >= 0 && m_activeHeroIdx < (int)m_heroes.size()) {
            const Hero& h = m_heroes[m_activeHeroIdx];
            for (int eq : h.artifacts.equippedIds)
                if (eq == art->id) { alreadyOwn = true; break; }
            if (!alreadyOwn)
                for (int inv : h.artifactInventory)
                    if (inv == art->id) { alreadyOwn = true; break; }
        }
        bool canAffordWare = wareGold >= art->shopPrice;

        ImGui::PushID(2000 + i);
        if (!canAffordWare || alreadyOwn) ImGui::BeginDisabled();
        char wareLbl[96];
        std::snprintf(wareLbl, sizeof(wareLbl), "%s  (%dg)", art->name.c_str(), art->shopPrice);
        if (ImGui::Button(wareLbl, ImVec2(280, 0))) {
            currentResources().add(ResourceType::Gold, -art->shopPrice);
            if (m_activeHeroIdx >= 0 && m_activeHeroIdx < (int)m_heroes.size())
                m_heroes[m_activeHeroIdx].artifactInventory.push_back(art->id);
            wareGold = currentResources().get(ResourceType::Gold);
            gLog("Tavern: bought %s for %dg\n", art->name.c_str(), art->shopPrice);
        }
        if (!canAffordWare || alreadyOwn) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s\n%s", art->name.c_str(), art->description.c_str());
        ImGui::PopID();
    }

    ImGui::End();
}

// ── Traveling Artifact Merchant popup (world map object) ─────────────────────
void Game::renderArtifactMerchantPopup()
{
    if (!m_showMerchantPopup) return;
    ImGui::SetNextWindowPos(ImVec2(400, 200), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);
    if (!ImGui::Begin("Traveling Merchant", nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }
    ImGui::Text("Gold: %d", currentResources().get(ResourceType::Gold));
    ImGui::Separator();

    auto specials = m_artifactRegistry.getByRarity(ArtifactRarity::Special);
    std::vector<const ArtifactDef*> forSale;
    for (auto* a : specials)
        if (a->shopPrice > 0) forSale.push_back(a);

    uint32_t seed = static_cast<uint32_t>(m_merchantSeed);
    int gold = currentResources().get(ResourceType::Gold);
    for (int i = 0; i < 3 && !forSale.empty(); ++i) {
        const ArtifactDef* art = forSale[seed % forSale.size()];
        seed = seed * 1664525u + 1013904223u;
        forSale.erase(std::remove(forSale.begin(), forSale.end(), art), forSale.end());

        bool alreadyOwn = false;
        if (m_activeHeroIdx >= 0 && m_activeHeroIdx < (int)m_heroes.size()) {
            const Hero& h = m_heroes[m_activeHeroIdx];
            for (int eq : h.artifacts.equippedIds)
                if (eq == art->id) { alreadyOwn = true; break; }
            if (!alreadyOwn)
                for (int inv : h.artifactInventory)
                    if (inv == art->id) { alreadyOwn = true; break; }
        }
        bool canAfford = gold >= art->shopPrice;

        ImGui::PushID(3000 + i);
        if (!canAfford || alreadyOwn) ImGui::BeginDisabled();
        char lbl[96];
        std::snprintf(lbl, sizeof(lbl), "%s  (%dg)", art->name.c_str(), art->shopPrice);
        if (ImGui::Button(lbl, ImVec2(280, 0))) {
            currentResources().add(ResourceType::Gold, -art->shopPrice);
            if (m_activeHeroIdx >= 0 && m_activeHeroIdx < (int)m_heroes.size())
                m_heroes[m_activeHeroIdx].artifactInventory.push_back(art->id);
            gold = currentResources().get(ResourceType::Gold);
            gLog("Merchant: bought %s for %dg\n", art->name.c_str(), art->shopPrice);
        }
        if (!canAfford || alreadyOwn) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s\n%s", art->name.c_str(), art->description.c_str());
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button("Leave", ImVec2(280, 0)))
        m_showMerchantPopup = false;
    ImGui::End();
}

// ── Arena choice popup ────────────────────────────────────────────────────────
void Game::renderArenaPopup()
{
    if (!m_showArenaPopup) return;
    ImGui::SetNextWindowPos(ImVec2(400, 220), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Always);
    if (!ImGui::Begin("Arena", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
        { ImGui::End(); return; }
    ImGui::Text("A champion awaits. Choose your prize:");
    ImGui::Spacing();
    if (ImGui::Button("Fight for +1 Attack",  ImVec2(260, 0))) { m_arenaBonusChoice = 0; startArenaCombat(); }
    if (ImGui::Button("Fight for +1 Defense", ImVec2(260, 0))) { m_arenaBonusChoice = 1; startArenaCombat(); }
    ImGui::Separator();
    if (ImGui::Button("Leave", ImVec2(260, 0))) m_showArenaPopup = false;
    ImGui::End();
}

// ── Upgrade Path A/B choice popup ─────────────────────────────────────────────
void Game::renderUpgradePathPopup()
{
    const Town* ct = m_townScreen.currentTown();
    if (!ct) { m_showUpgradePathPopup = false; return; }
    Town* town = nullptr;
    for (auto& t : m_towns) if (t.id == ct->id) { town = &t; break; }
    if (!town) { m_showUpgradePathPopup = false; return; }

    const BuildingDef* defA = m_registry.getBuildingDef(m_upgradePathA);
    const BuildingDef* defB = m_registry.getBuildingDef(m_upgradePathB);
    if (!defA || !defB) { m_showUpgradePathPopup = false; return; }

    // Find unit defs for each path
    const UnitDef* unitA = nullptr;
    const UnitDef* unitB = nullptr;
    for (const auto& ud : m_registry.units()) {
        if (ud.faction == town->faction && ud.tier == defA->tier && ud.path == UpgradePath::PathA)
            unitA = &ud;
        if (ud.faction == town->faction && ud.tier == defB->tier && ud.path == UpgradePath::PathB)
            unitB = &ud;
    }

    ImGui::OpenPopup("Choose Upgrade Path");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0));

    if (ImGui::BeginPopupModal("Choose Upgrade Path", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "Choose an upgrade path for Tier %d:", defA->tier);
        ImGui::TextDisabled("This choice is permanent — the other path will be locked out.");
        ImGui::Spacing();

        auto drawPathCol = [&](const BuildingDef* def, const UnitDef* ud, const char* label) {
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", label);
            ImGui::Separator();
            ImGui::Text("%s", def->name.c_str());
            if (!def->description.empty())
                ImGui::TextDisabled("%s", def->description.c_str());
            ImGui::Spacing();
            if (ud) {
                ImGui::Text("Unit: %s", ud->name.c_str());
                ImGui::Text("HP %d  ATK %d  DEF %d", ud->hp, ud->attack, ud->defense);
                ImGui::Text("Dmg %d-%d  Spd %d", ud->damage_min, ud->damage_max, ud->speed);
                if (ud->range > 0) ImGui::Text("Range %d (%d shots)", ud->range, ud->shots);
                if (ud->flying)    ImGui::TextColored(ImVec4(0.7f,0.8f,1.0f,1.0f), "[Flying]");
                if (ud->vampiric)  ImGui::TextColored(ImVec4(0.9f,0.3f,0.3f,1.0f), "[Vampiric]");
            } else {
                ImGui::TextDisabled("(no unit data)");
            }
            ImGui::Spacing();
            ImGui::Text("Cost: %s", def->cost.get(ResourceType::Gold) > 0 ?
                        (std::to_string(def->cost.get(ResourceType::Gold)) + " Gold").c_str() : "?");
            ImGui::EndGroup();
        };

        ImGui::Columns(2, "##pathcols", true);
        drawPathCol(defA, unitA, "  PATH A  ");
        ImGui::NextColumn();
        drawPathCol(defB, unitB, "  PATH B  ");
        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();

        auto tryBuild = [&](int buildingId) {
            m_townScreen.currentTown();  // ensure valid
            town->build(buildingId, m_registry.buildings(), m_playerResources);
            m_showUpgradePathPopup = false;
            ImGui::CloseCurrentPopup();
        };

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.1f, 0.4f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
        if (ImGui::Button("Choose Path A", ImVec2(200, 0))) tryBuild(m_upgradePathA);
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, 20);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.2f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.9f, 1.0f));
        if (ImGui::Button("Choose Path B", ImVec2(200, 0))) tryBuild(m_upgradePathB);
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, 20);
        if (ImGui::Button("Cancel")) {
            m_showUpgradePathPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Capture notification popup ────────────────────────────────────────────────
void Game::renderCapturePopup()
{
    ImGui::OpenPopup("Town Captured!");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Town Captured!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                           "%s is now under your control!", m_capturedTownName.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Weekly income from this town will begin next week.");
        ImGui::Spacing();
        if (ImGui::Button("Continue", ImVec2(-1, 32))) {
            m_showCapturePopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Town lost notification (enemy captured player town) ───────────────────────
void Game::renderTownLostPopup()
{
    ImGui::OpenPopup("Town Lost!");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Town Lost!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.2f, 1.0f),
                           "%s has fallen to the enemy!", m_lostTownName.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Recapture it to restore your income.");
        ImGui::Spacing();
        if (ImGui::Button("Acknowledge", ImVec2(-1, 32))) {
            m_showTownLostPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Pause menu (Escape on world map) ─────────────────────────────────────────
void Game::renderPauseMenu()
{
    ImGui::OpenPopup("Paused");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(220, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Paused", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.6f, 1.0f), "Game Paused");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Resume  [Esc]", ImVec2(-1, 32))) {
            m_showPauseMenu = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Spacing();

        if (ImGui::Button("Save Game  [F5]", ImVec2(-1, 32))) {
            saveGame();
            m_showPauseMenu = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Spacing();

        if (ImGui::Button("Main Menu", ImVec2(-1, 32))) {
            m_showPauseMenu = false;
            m_state = GameState::MainMenu;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Developer Options");
        ImGui::Checkbox("Disable Fog of War", &m_fogDisabled);
        ImGui::Spacing();

        // ── Game Log ──────────────────────────────────────────────────────────
        bool logOpen = ImGui::CollapsingHeader("Game Log");
        if (logOpen) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) DevLog::clear();
            ImGui::BeginChild("##devlog", ImVec2(0, 220), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            const auto& logLines = DevLog::lines();
            for (const auto& line : logLines)
                ImGui::TextUnformatted(line.c_str());
            // Auto-scroll to bottom when new lines arrive
            if (DevLog::hasNewLines()) {
                ImGui::SetScrollHereY(1.0f);
                DevLog::markSeen();
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Quit to Desktop", ImVec2(-1, 28))) {
            m_running = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Week summary popup ────────────────────────────────────────────────────────
void Game::renderWeekSummary()
{
    ImGui::OpenPopup("Week Begins!");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Week Begins!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.15f, 1.0f), "Week %d", m_weekSummaryWeek);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Resources received this week:");
        ImGui::Spacing();

        static const struct { ResourceType type; const char* icon; ImVec4 col; } kRes[] = {
            { ResourceType::Gold,         "Gold",        {1.00f, 0.85f, 0.20f, 1.f} },
            { ResourceType::Iron,         "Iron",        {0.70f, 0.70f, 0.75f, 1.f} },
            { ResourceType::FaithStones,  "Faith",       {0.88f, 0.84f, 1.00f, 1.f} },
            { ResourceType::BloodEssence, "Blood",       {0.90f, 0.25f, 0.25f, 1.f} },
            { ResourceType::VerdantSap,   "Sap",         {0.35f, 0.80f, 0.35f, 1.f} },
            { ResourceType::Mercury,      "Mercury",     {0.30f, 0.80f, 0.75f, 1.f} },
        };
        bool anyIncome = false;
        for (const auto& rd : kRes) {
            int amt = m_weekSummaryIncome.get(rd.type);
            if (amt <= 0) continue;
            anyIncome = true;
            ImGui::TextColored(rd.col, "  %-12s  +%d", rd.icon, amt);
        }
        if (!anyIncome)
            ImGui::TextDisabled("  (no towns or mines owned)");

        // Weekly event section
        if (!m_weeklyEventHeadline.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "World Event:");
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", m_weeklyEventHeadline.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_weeklyEventBody.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!m_weekChoiceOptions.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.20f, 1.0f), "Make your choice:");
            ImGui::Spacing();
            float cbw = ImGui::GetWindowWidth() - 32.0f;
            for (int ci = 0; ci < (int)m_weekChoiceOptions.size(); ++ci) {
                const auto& opt = m_weekChoiceOptions[ci];
                char lbl[128];
                std::snprintf(lbl, sizeof(lbl), "%s##wc%d", opt.label.c_str(), ci);
                if (ImGui::Button(lbl, ImVec2(cbw, 30))) {
                    opt.onSelect();
                    m_weekChoiceOptions.clear();
                    m_showWeekSummary = false;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", opt.effectText.c_str());
            }
        } else {
            if (ImGui::Button("Continue", ImVec2(-1, 30))) {
                m_showWeekSummary = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

// ── Hot-seat handoff screen ───────────────────────────────────────────────────
void Game::renderHotSeatHandoff()
{
    ImGui::OpenPopup("##hotseat_handoff");
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);

    if (ImGui::BeginPopupModal("##hotseat_handoff", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        // Dim the title bar text via centering
        const char* title = m_hotSeatP2Turn ? "PLAYER 2's TURN" : "PLAYER 1's TURN";
        ImVec4 col = m_hotSeatP2Turn ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f)
                                      : ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
        float tw = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - tw) * 0.5f);
        ImGui::TextColored(col, "%s", title);
        ImGui::Separator();
        ImGui::Spacing();

        // Show whose resources
        const Resources& res = m_hotSeatP2Turn ? m_player2Resources : m_playerResources;
        ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1.f), "Resources:");
        ImGui::Text("  Gold: %d   Iron: %d",
            res.get(ResourceType::Gold), res.get(ResourceType::Iron));
        ImGui::Spacing();

        const char* hint = m_hotSeatP2Turn
            ? "Pass the device to Player 2, then click Continue."
            : "Pass the device to Player 1, then click Continue.";
        ImGui::TextWrapped("%s", hint);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bw = ImGui::GetWindowWidth() - 32.0f;
        if (ImGui::Button("Continue", ImVec2(bw, 36))) {
            m_hotSeatHandoff = false;
            ImGui::CloseCurrentPopup();
            // Snap camera to the active player's hero
            const std::vector<Hero>* activeHeroes = m_hotSeatP2Turn
                ? &m_enemyHeroes : &(const std::vector<Hero>&)m_heroes;
            if (!activeHeroes->empty()) {
                float cx, cy;
                m_hexRenderer.grid().hexToWorld((*activeHeroes)[0].pos, cx, cy);
                m_camera.setPosition(cx, cy);
            }
        }
        ImGui::EndPopup();
    }
}

// ── State transitions ─────────────────────────────────────────────────────────
void Game::enterTown(Town* town)
{
    if (!town) return;
    m_prevState = m_state;   // remember Campaign vs WorldMap so exitTown() returns correctly
    m_state = GameState::Town;
    // Only treat the hero as "in town" if they are physically on or adjacent to the town tile.
    // Remote access (via HUD panel) still opens the screen but routes recruits to the garrison.
    Hero* hero = nullptr;
    if (m_hotSeatMode && m_hotSeatP2Turn && !m_enemyHeroes.empty()) {
        // P2's turn: look in enemyHeroes
        int sel = (m_selectedEnemyHero >= 0 && m_selectedEnemyHero < (int)m_enemyHeroes.size())
                  ? m_selectedEnemyHero : 0;
        Hero& h = m_enemyHeroes[sel];
        if (h.pos == town->pos || HexGrid::distance(h.pos, town->pos) <= 1)
            hero = &h;
    } else if (!m_heroes.empty()) {
        Hero& h = m_heroes[m_activeHeroIdx];
        if (h.pos == town->pos || HexGrid::distance(h.pos, town->pos) <= 1)
            hero = &h;
    }
    // Entering an owned town restores hero HP fully
    int currentPlayerId = (m_hotSeatMode && m_hotSeatP2Turn) ? 2 : 1;
    if (hero && town->ownerId == currentPlayerId && hero->heroHp < hero->heroMaxHp) {
        hero->heroHp = hero->heroMaxHp;
        pushPickupEffect(town->pos, "Hero healed!", IM_COL32(180, 255, 180, 255));
    }
    // Compute BLUEPRINT discount for Iron Assembly heroes visiting their town
    int blueprintDiscount = 0;
    if (hero && town->faction == FactionId::IronAssembly) {
        if (const SkillInstance* s = hero->skills.getSkill(SkillID::BLUEPRINT))
            if (const SkillDef* def = findSkillDef(SkillID::BLUEPRINT))
                blueprintDiscount = def->values[static_cast<int>(s->tier)]; // 1/2/3
    }
    m_showMageGuildPanel     = false;
    m_showTavernPanel        = false;
    m_showArtifactForgePanel = false;
    m_townScreen.open(town, &m_playerResources, &m_registry, hero,
                      m_turns.week(), blueprintDiscount);
    // Wire faction art + unit textures for the recruit panel
    {
        int fid = std::clamp(static_cast<int>(town->faction), 0, NUM_FACTIONS - 1);
        m_townScreen.setTownBannerTex(m_portraitTex[fid].ok()
            ? (ImTextureID)(uintptr_t)m_portraitTex[fid].id() : nullptr);
        m_townScreen.setBuildingIconTex(m_buildingIconTex.ok()
            ? (ImTextureID)(uintptr_t)m_buildingIconTex.id() : nullptr);
        // Per-faction single-tier buildings
        auto setFA = [&](int bid, const Texture* tex) {
            m_townScreen.setBuildingArt(bid,
                tex[fid].ok() ? (ImTextureID)(uintptr_t)tex[fid].id() : nullptr);
        };
        setFA(BID::FORT,      m_fortTex);
        setFA(BID::MARKET,    m_marketTex);
        setFA(BID::TOWN_HALL, m_townHallTex);
        setFA(BID::CITY_HALL, m_cityHallTex);

        // Faction-specific mage guild art
        static const int kMageGuildBIDs[MAGE_GUILD_TIERS] = {
            BID::MAGE_GUILD, BID::MAGE_GUILD_T2, BID::MAGE_GUILD_T3, BID::MAGE_GUILD_T4
        };
        for (int t = 0; t < MAGE_GUILD_TIERS; ++t)
            m_townScreen.setBuildingArt(kMageGuildBIDs[t],
                m_mageGuildTex[fid][t].ok()
                    ? (ImTextureID)(uintptr_t)m_mageGuildTex[fid][t].id() : nullptr);

        // Faction-specific warehouse art
        static const int kWarehouseBIDs[WAREHOUSE_TIERS] = {
            BID::WAREHOUSE, BID::WAREHOUSE_T2, BID::WAREHOUSE_T3
        };
        for (int t = 0; t < WAREHOUSE_TIERS; ++t)
            m_townScreen.setBuildingArt(kWarehouseBIDs[t],
                m_warehouseTex[fid][t].ok()
                    ? (ImTextureID)(uintptr_t)m_warehouseTex[fid][t].id() : nullptr);
        for (int t = 0; t < NUM_UNIT_TIERS; ++t)
            m_townScreen.setUnitTex(t, m_unitTex[fid][t].ok()
                ? (ImTextureID)(uintptr_t)m_unitTex[fid][t].id() : nullptr);

        // HolyOrder dwelling art (base + A/B variants per tier)
        if (town->faction == FactionId::HolyOrder) {
            static const int kHOBIDs[HO_DWELLING_TIERS][HO_DWELLING_VARIANTS] = {
                { BID::HO_T1_BASE, BID::HO_T1_A, BID::HO_T1_B },
                { BID::HO_T2_BASE, BID::HO_T2_A, BID::HO_T2_B },
                { BID::HO_T3_BASE, BID::HO_T3_A, BID::HO_T3_B },
                { BID::HO_T4_BASE, BID::HO_T4_A, BID::HO_T4_B },
                { BID::HO_T5_BASE, BID::HO_T5_A, BID::HO_T5_B },
                { BID::HO_T6_BASE, BID::HO_T6_A, BID::HO_T6_B },
            };
            for (int t = 0; t < HO_DWELLING_TIERS; ++t)
                for (int v = 0; v < HO_DWELLING_VARIANTS; ++v)
                    m_townScreen.setBuildingArt(kHOBIDs[t][v],
                        m_hoDwellingTex[t][v].ok()
                            ? (ImTextureID)(uintptr_t)m_hoDwellingTex[t][v].id() : nullptr);
        }

        // CrimsonWardens dwelling art (base + A/B variants per tier)
        if (town->faction == FactionId::CrimsonWardens) {
            static const int kCWBIDs[CW_DWELLING_TIERS][CW_DWELLING_VARIANTS] = {
                { BID::CW_T1, BID::CW_T1_A, BID::CW_T1_B },
                { BID::CW_T2, BID::CW_T2_A, BID::CW_T2_B },
                { BID::CW_T3, BID::CW_T3_A, BID::CW_T3_B },
                { BID::CW_T4, BID::CW_T4_A, BID::CW_T4_B },
                { BID::CW_T5, BID::CW_T5_A, BID::CW_T5_B },
                { BID::CW_T6, BID::CW_T6_A, BID::CW_T6_B },
            };
            for (int t = 0; t < CW_DWELLING_TIERS; ++t)
                for (int v = 0; v < CW_DWELLING_VARIANTS; ++v)
                    m_townScreen.setBuildingArt(kCWBIDs[t][v],
                        m_cwDwellingTex[t][v].ok()
                            ? (ImTextureID)(uintptr_t)m_cwDwellingTex[t][v].id() : nullptr);
        }
    }
    // Play faction-specific theme; fall back to generic town_music
    int fid = static_cast<int>(town->faction);
    if (fid >= 0 && fid < 9) {
        char key[32]; std::snprintf(key, sizeof(key), "faction_music_%d", fid);
        m_audio.playMusic(key);
    } else {
        m_audio.playMusic("town_music");
    }
    gLog("Entered town: %s\n", town->name.c_str());
}

void Game::exitTown()
{
    m_townScreen.close();
    m_showMageGuildPanel     = false;
    m_showTavernPanel        = false;
    m_showArtifactForgePanel = false;
    m_showMarketPanel        = false;
    m_showGarrisonPanel      = false;
    m_garrisonSelSlot        = -1;
    m_garrisonSelSide        = -1;
    m_townScreen.setRecruitTarget(false);
    m_audio.playMusic("worldmap_music");
    if (m_prevState == GameState::Campaign)
        m_state = GameState::Campaign;
    else
        enterWorldMap();
}

// ── Marketplace — resource exchange ───────────────────────────────────────────
void Game::renderMarketplace()
{
    // Gate: need at least one player town with MARKET
    bool anyMarket = false;
    for (const auto& t : m_towns)
        if (t.ownerId == currentPlayerId() && t.hasBuilding(BID::MARKET)) { anyMarket = true; break; }
    if (!anyMarket) { m_showMarketPanel = false; return; }

    static const int SELL_RATE = 4;  // HoMM3-style 4:1 exchange
    static const int BUY_RATE  = 1;

    static const char* kResNames[] = {
        "Gold", "Iron", "Faith Stones", "Blood Essence", "Verdant Sap", "Mercury"
    };
    static const char* kResShort[] = { "Gold", "Iron", "Faith", "Blood", "Sap", "Mercury" };
    // icon atlas indices (8x6 atlas, icons 9-14 are the 6 resources)
    static const int kResIcon[] = { 9, 10, 11, 12, 13, 14 };

    const float CARD_W = 76.0f, CARD_H = 90.0f, CARD_GAP = 6.0f;
    const float PANEL_W = RESOURCE_COUNT * (CARD_W + CARD_GAP) + 20.0f;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Once, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({PANEL_W, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);
    if (!ImGui::Begin("Marketplace##market", &m_showMarketPanel,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                      ImGuiWindowFlags_NoMove)) {
        ImGui::End(); return;
    }

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    bool hasIcons   = m_iconTex.ok();
    ImTextureID tex = hasIcons ? (ImTextureID)(uintptr_t)m_iconTex.id() : nullptr;

    // ── Resource cards row ────────────────────────────────────────────────────
    ImGui::Text("Your Resources  (left-click = SELL, right-click = BUY):");
    ImGui::Spacing();

    float rowX = ImGui::GetCursorScreenPos().x;
    float rowY = ImGui::GetCursorScreenPos().y;

    for (int i = 0; i < RESOURCE_COUNT; ++i) {
        ImVec2 cp = { rowX + i * (CARD_W + CARD_GAP), rowY };
        int val   = currentResources().get(static_cast<ResourceType>(i));
        bool isSell = (m_marketSellType == i);
        bool isBuy  = (m_marketBuyType  == i);

        // Card background
        ImU32 bg  = isSell ? IM_COL32(70, 35, 10, 235)
                   : isBuy  ? IM_COL32(10, 40, 70, 235)
                            : IM_COL32(18, 22, 38, 220);
        ImU32 brd = isSell ? IM_COL32(255, 150, 30, 255)
                   : isBuy  ? IM_COL32(40, 150, 255, 255)
                            : IM_COL32(65, 75, 105, 200);
        dl->AddRectFilled(cp, {cp.x+CARD_W, cp.y+CARD_H}, bg, 6.0f);
        dl->AddRect(cp, {cp.x+CARD_W, cp.y+CARD_H}, brd, 6.0f, 0, isSell || isBuy ? 2.5f : 1.5f);

        // Icon
        if (hasIcons) {
            int idx   = kResIcon[i];
            float col = static_cast<float>(idx % 8);
            float row = static_cast<float>(idx / 8);
            ImVec2 u0 = { col / 8.0f, row / 6.0f };
            ImVec2 u1 = { (col+1.0f) / 8.0f, (row+1.0f) / 6.0f };
            float is  = 40.0f;
            float ix  = cp.x + (CARD_W - is) * 0.5f;
            float iy  = cp.y + 6.0f;
            dl->AddImage(tex, {ix, iy}, {ix+is, iy+is}, u0, u1);
        }

        // Amount
        char numBuf[16]; std::snprintf(numBuf, sizeof(numBuf), "%d", val);
        ImVec2 ns = ImGui::CalcTextSize(numBuf);
        ImU32 valCol = val > 0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 70, 70, 255);
        dl->AddText({cp.x + (CARD_W - ns.x) * 0.5f, cp.y + 52.0f}, valCol, numBuf);

        // Label
        ImVec2 ls = ImGui::CalcTextSize(kResShort[i]);
        dl->AddText({cp.x + (CARD_W - ls.x) * 0.5f, cp.y + 70.0f},
                    IM_COL32(150, 160, 185, 220), kResShort[i]);

        // Click area
        ImGui::SetCursorScreenPos(cp);
        ImGui::InvisibleButton(("##mrc" + std::to_string(i)).c_str(), {CARD_W, CARD_H});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !isSell) {
            m_marketSellType = i;
            if (m_marketBuyType == i) m_marketBuyType = (i + 1) % RESOURCE_COUNT;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !isBuy) {
            m_marketBuyType = i;
            if (m_marketSellType == i) m_marketSellType = (i + 1) % RESOURCE_COUNT;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: %d\n[L] Set as Sell  [R] Set as Buy", kResNames[i], val);
    }
    ImGui::SetCursorScreenPos({ rowX, rowY + CARD_H + 8.0f });

    // ── Trade direction row ───────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::Spacing();

    // Mini SELL card
    {
        ImVec2 sc = ImGui::GetCursorScreenPos();
        float mw = 60.0f, mh = 60.0f;
        int si    = m_marketSellType;
        dl->AddRectFilled(sc, {sc.x+mw, sc.y+mh}, IM_COL32(70,35,10,230), 5.0f);
        dl->AddRect(sc, {sc.x+mw, sc.y+mh}, IM_COL32(255,150,30,255), 5.0f, 0, 2.0f);
        if (hasIcons) {
            int idx = kResIcon[si]; float c = idx%8, r = idx/8;
            dl->AddImage(tex, {sc.x+4,sc.y+4},{sc.x+38,sc.y+38},
                         {c/8.0f,r/6.0f}, {(c+1)/8.0f,(r+1)/6.0f});
        }
        ImVec2 snl = ImGui::CalcTextSize(kResShort[si]);
        dl->AddText({sc.x+(mw-snl.x)*0.5f, sc.y+42.0f}, IM_COL32(220,220,220,255), kResShort[si]);
        ImGui::Dummy({mw, mh});
    }

    ImGui::SameLine(0, 10.0f);
    // Arrow + rate text
    {
        ImVec2 ap = ImGui::GetCursorScreenPos();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);
        ImGui::Text("x%d  ->  x%d", SELL_RATE, BUY_RATE);
        ImGui::SetCursorScreenPos({ap.x, ap.y});
        ImGui::Dummy({90.0f, 60.0f});
    }

    ImGui::SameLine(0, 10.0f);
    // Mini BUY card
    {
        ImVec2 bc = ImGui::GetCursorScreenPos();
        float mw = 60.0f, mh = 60.0f;
        int bi    = m_marketBuyType;
        dl->AddRectFilled(bc, {bc.x+mw, bc.y+mh}, IM_COL32(10,40,70,230), 5.0f);
        dl->AddRect(bc, {bc.x+mw, bc.y+mh}, IM_COL32(40,150,255,255), 5.0f, 0, 2.0f);
        if (hasIcons) {
            int idx = kResIcon[bi]; float c = idx%8, r = idx/8;
            dl->AddImage(tex, {bc.x+4,bc.y+4},{bc.x+38,bc.y+38},
                         {c/8.0f,r/6.0f}, {(c+1)/8.0f,(r+1)/6.0f});
        }
        ImVec2 bnl = ImGui::CalcTextSize(kResShort[bi]);
        dl->AddText({bc.x+(mw-bnl.x)*0.5f, bc.y+42.0f}, IM_COL32(220,220,220,255), kResShort[bi]);
        ImGui::Dummy({mw, mh});
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Trade buttons ─────────────────────────────────────────────────────────
    int have      = currentResources().get(static_cast<ResourceType>(m_marketSellType));
    int maxTrades = have / SELL_RATE;

    ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f),
        "You have: %d %s", have, kResNames[m_marketSellType]);
    ImGui::SameLine();
    ImGui::TextDisabled("(max %d trades)", maxTrades);

    if (maxTrades <= 0) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
            "Not enough %s to trade.", kResNames[m_marketSellType]);
        ImGui::End();
        return;
    }

    auto doTrade = [&](int count) {
        if (count <= 0 || count > maxTrades) return;
        currentResources().add(static_cast<ResourceType>(m_marketSellType), -(count * SELL_RATE));
        currentResources().add(static_cast<ResourceType>(m_marketBuyType),   count * BUY_RATE);
    };

    ImGui::Spacing();
    float bw = (PANEL_W - 20.0f - 3.0f * 8.0f) / 4.0f;
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.35f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.50f, 0.25f, 1.0f));
    if (ImGui::Button("Trade x1",  ImVec2(bw, 34))) doTrade(1);
    ImGui::SameLine(0, 8.0f);
    if (maxTrades >= 5) {
        if (ImGui::Button("Trade x5",  ImVec2(bw, 34))) doTrade(5);
    } else {
        ImGui::BeginDisabled(); ImGui::Button("Trade x5",  ImVec2(bw, 34)); ImGui::EndDisabled();
    }
    ImGui::SameLine(0, 8.0f);
    if (maxTrades >= 10) {
        if (ImGui::Button("Trade x10", ImVec2(bw, 34))) doTrade(10);
    } else {
        ImGui::BeginDisabled(); ImGui::Button("Trade x10", ImVec2(bw, 34)); ImGui::EndDisabled();
    }
    ImGui::SameLine(0, 8.0f);
    char maxLbl[32]; std::snprintf(maxLbl, sizeof(maxLbl), "Trade MAX (x%d)", maxTrades);
    if (ImGui::Button(maxLbl, ImVec2(bw, 34))) doTrade(maxTrades);
    ImGui::PopStyleColor(2);

    ImGui::Spacing();
    ImGui::TextDisabled("Receive %d %s per trade", BUY_RATE, kResNames[m_marketBuyType]);

    ImGui::End();
}
