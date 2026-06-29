#include "Game.h"
#include "../hero/SkillRegistry.h"
#include "../hero/HeroClass.h"
#include "../magic/SpellRegistry.h"
#include "../combat/DamageCalc.h"
#include <imgui.h>
#include <stdio.h>
#include <sstream>
#include <algorithm>
#include <cmath>

// ── Helper: resolve (faction, tier) for a CombatUnit ─────────────────────────
static std::pair<int,int> unitFactionTier(const CombatUnit& u,
                                          const std::vector<UnitDef>& defs)
{
    if (u.defId != 0) {
        for (const auto& d : defs)
            if (d.id == u.defId)
                return { static_cast<int>(d.faction), d.tier };
    }
    if (u.factionHint >= 0 && u.factionHint < 9)
        return { u.factionHint, u.stackSlot + 1 };  // stackSlot = tier-1 set by ArmyBuilder
    return { -1, 1 };  // truly unknown — no sprite lookup
}

// ── Combat update ─────────────────────────────────────────────────────────────
void Game::updateCombat(float dt)
{
    const auto& mouse = m_input.mouse();
    float mx = static_cast<float>(mouse.x);
    float my = static_cast<float>(mouse.y);

    if (mouse.leftDown && !ImGui::GetIO().WantCaptureMouse) {
        bool consumed = m_combatHUD.onMouseDown(mx, my);
        if (!consumed && m_combat.phase() == CombatPhase::PlayerTurn) {
            // Convert mouse → world → hex
            float wx = (mx - m_combatBoardOffX) / m_combatBoardScale;
            float wy = (my - m_combatBoardOffY) / m_combatBoardScale;
            HexCoord clicked = m_combat.grid().hexGrid().worldToHex(wx, wy);

            if (m_combat.grid().inBounds(clicked)) {
                CombatUnit* active = m_combat.activeUnit();
                if (active && active->isPlayer) {
                    const CombatUnit* tgt = m_combat.grid().getUnitAt(clicked);
                    if (tgt && !tgt->isPlayer && tgt->alive) {
                        // Attack or shoot
                        CombatAction act;
                        act.type         = (active->shotsLeft > 0 && active->range > 0)
                                           ? ActionType::Shoot
                                           : ActionType::Attack;
                        act.targetUnitId = tgt->id;
                        act.target       = clicked;
                        m_combat.submitAction(act);
                    } else if (!tgt && m_combat.isSiege() &&
                               m_combat.grid().isWallTile(clicked)) {
                        // Siege: attack the wall / gate
                        m_combat.attackWall(clicked);
                    } else if (!tgt) {
                        // Move to empty reachable tile
                        auto reach = m_combat.grid().reachable(
                            active->pos, active->speed, active->flying);
                        for (const auto& h : reach) {
                            if (h == clicked) {
                                CombatAction act;
                                act.type   = ActionType::Move;
                                act.target = clicked;
                                m_combat.submitAction(act);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    // Right-click on a unit hex — show stat popup
    if (mouse.rightDown && !ImGui::GetIO().WantCaptureMouse) {
        float wx2 = (mx - m_combatBoardOffX) / m_combatBoardScale;
        float wy2 = (my - m_combatBoardOffY) / m_combatBoardScale;
        HexCoord rc = m_combat.grid().hexGrid().worldToHex(wx2, wy2);
        m_combatRightClickUnitId = 0;
        if (m_combat.grid().inBounds(rc)) {
            const CombatUnit* u = m_combat.grid().getUnitAt(rc);
            if (u && u->alive) {
                m_combatRightClickUnitId = u->id;
                ImGui::OpenPopup("##CombatUnitStats");
            }
        }
    }

    m_combatHUD.onMouseMove(mx, my);

    // Advance sprite animators
    {
        const CombatUnit* active = m_combat.activeUnit();
        for (const auto& u : m_combat.grid().units()) {
            auto it = m_combatAnimators.find(u.id);
            if (it == m_combatAnimators.end()) continue;
            SpriteAnimator& anim = it->second;
            if (!u.alive) {
                anim.setState(AnimState::Dead);
            } else if (active && u.id == active->id && u.isPlayer) {
                // Active player unit shows attack pose
                anim.setState(AnimState::Attack);
            } else {
                anim.setState(AnimState::Idle);
            }
            anim.update(dt * m_settingsAnimSpeed);
        }
    }

    if (m_fromBattleSim && m_simAutoPlay) {
        if (m_watchingAI) {
            // Watch AI: resolve entire combat instantly (no per-action delay)
            for (int guard = 0; guard < 2000; ++guard) {
                auto ph = m_combat.phase();
                if (ph == CombatPhase::Victory || ph == CombatPhase::Defeat) break;
                if (ph == CombatPhase::PlayerTurn || ph == CombatPhase::EnemyTurn)
                    m_combat.processOneAIAction();
                else break;
            }
        } else {
            // Battle-sim watch mode: fire one unit action per tick at a human-visible pace
            m_simAutoPlayTimer -= dt;
            if (m_simAutoPlayTimer <= 0.f) {
                m_simAutoPlayTimer = 0.4f;
                auto ph = m_combat.phase();
                if (ph == CombatPhase::PlayerTurn || ph == CombatPhase::EnemyTurn)
                    m_combat.processOneAIAction();
            }
        }
    } else {
        if (m_combat.phase() == CombatPhase::EnemyTurn)
            m_combat.processAITurn();
    }

    // Advance floating damage effect timers
    m_particles.update(dt);

    for (auto& ef : m_combatDmgEffects) ef.t -= dt;
    m_combatDmgEffects.erase(
        std::remove_if(m_combatDmgEffects.begin(), m_combatDmgEffects.end(),
            [](const CombatDmgEffect& e){ return e.t <= 0.f; }),
        m_combatDmgEffects.end());

    if (m_combat.phase() == CombatPhase::Victory)
        exitCombat(true);
    else if (m_combat.phase() == CombatPhase::Defeat)
        exitCombat(false);
}

// ── Combat render ─────────────────────────────────────────────────────────────
void Game::renderCombat()
{
    m_ui.beginFrame();
    m_combatHUD.draw(m_ui, m_combat);
    m_ui.endFrame();

    beginImGuiFrame();
    m_ui.flushText(ImGui::GetBackgroundDrawList());
    renderCombatBoard();
    if (m_showSpellPanel) renderSpellPanel();

    // ImGui action bar — always visible regardless of UIRenderer state
    {
        const char* phaseLabel =
            m_combat.phase() == CombatPhase::PlayerTurn ? "YOUR TURN" :
            m_combat.phase() == CombatPhase::EnemyTurn  ? "ENEMY TURN" :
            m_combat.phase() == CombatPhase::Victory    ? "VICTORY!"   :
            m_combat.phase() == CombatPhase::Defeat     ? "DEFEAT"     : "...";

        ImGui::SetNextWindowPos(ImVec2((float)m_width - 190.f, (float)m_height - 155.f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(186.f, 150.f), ImGuiCond_Always);
        ImGui::Begin("##CombatActions", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        ImGui::TextUnformatted(phaseLabel);
        ImGui::Separator();
        bool isPlayerTurn = m_combat.phase() == CombatPhase::PlayerTurn;
        if (!isPlayerTurn) ImGui::BeginDisabled();
        if (ImGui::Button("Wait (end turn)", ImVec2(170, 28)))
            m_combat.wait();
        if (ImGui::Button("Defend", ImVec2(170, 28))) {
            CombatAction act; act.type = ActionType::Defend;
            m_combat.submitAction(act);
        }
        if (ImGui::Button("Spells", ImVec2(170, 28)))
            m_showSpellPanel = !m_showSpellPanel;
        if (!isPlayerTurn) ImGui::EndDisabled();
        if (ImGui::Button("Retreat", ImVec2(170, 28)))
            exitCombat(false);

        ImGui::End();
    }

    // Right-click unit stat popup
    if (m_combatRightClickUnitId != 0) {
        const CombatUnit* u = m_combat.grid().getUnit(m_combatRightClickUnitId);
        if (!u || !u->alive) {
            m_combatRightClickUnitId = 0;
        } else if (ImGui::BeginPopup("##CombatUnitStats")) {
            ImGui::TextUnformatted(u->name.c_str());
            char cntBuf[32]; std::snprintf(cntBuf, sizeof(cntBuf), "Count: %d", u->count);
            ImGui::TextUnformatted(cntBuf);
            ImGui::Separator();
            char hpBuf[48]; std::snprintf(hpBuf, sizeof(hpBuf),
                "HP: %d/%d  (Total: %d)", u->hp, u->maxHp, u->totalHp());
            ImGui::TextUnformatted(hpBuf);
            char stBuf[64]; std::snprintf(stBuf, sizeof(stBuf),
                "ATK:%d  DEF:%d  SPD:%d", u->attack, u->defense, u->speed);
            ImGui::TextUnformatted(stBuf);
            char dmBuf[32]; std::snprintf(dmBuf, sizeof(dmBuf),
                "Damage: %d-%d", u->damageMin, u->damageMax);
            ImGui::TextUnformatted(dmBuf);
            if (u->range > 0) {
                char rBuf[32]; std::snprintf(rBuf, sizeof(rBuf),
                    "Range:%d  Shots:%d/%d", u->range, u->shotsLeft, u->shots);
                ImGui::TextUnformatted(rBuf);
            }
            if (u->morale != 50) {
                char mBuf[24]; std::snprintf(mBuf, sizeof(mBuf), "Morale: %d", u->morale);
                ImGui::TextUnformatted(mBuf);
            }
            if (u->luck > 0) {
                char lBuf[20]; std::snprintf(lBuf, sizeof(lBuf), "Luck: %d", u->luck);
                ImGui::TextUnformatted(lBuf);
            }
            if (u->poisonRounds > 0) {
                char pBuf[40]; std::snprintf(pBuf, sizeof(pBuf),
                    "Poison: %d dmg x%d rnd", u->poisonDamage, u->poisonRounds);
                ImGui::TextUnformatted(pBuf);
            }
            if (u->burnRounds > 0) {
                char bBuf[40]; std::snprintf(bBuf, sizeof(bBuf),
                    "Burn: %d dmg x%d rnd", u->burnDamage, u->burnRounds);
                ImGui::TextUnformatted(bBuf);
            }
            ImGui::Separator();
            if (u->flying)    ImGui::TextUnformatted("Flying");
            if (u->vampiric)  ImGui::TextUnformatted("Vampiric");
            if (u->regenerates) ImGui::TextUnformatted("Regenerates");
            if (u->hasSecondLife) ImGui::TextUnformatted("Second Life");
            if (u->moraleImmune) ImGui::TextUnformatted("Morale Immune");
            ImGui::EndPopup();
        }
    }

    endImGuiFrame();
}

// ── Combat board ──────────────────────────────────────────────────────────────
void Game::renderCombatBoard()
{
    if (!m_imguiReady) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const CombatGrid& grid = m_combat.grid();
    const HexGrid& hg = grid.hexGrid();
    const auto& coords = grid.allCoords();
    if (coords.empty()) return;

    // Compute world-space bounding box from hex corners
    float wxMin = 1e9f, wyMin = 1e9f, wxMax = -1e9f, wyMax = -1e9f;
    for (const auto& h : coords) {
        float corners[12];
        hg.hexCorners(h, corners);
        for (int i = 0; i < 6; ++i) {
            if (corners[i*2]   < wxMin) wxMin = corners[i*2];
            if (corners[i*2]   > wxMax) wxMax = corners[i*2];
            if (corners[i*2+1] < wyMin) wyMin = corners[i*2+1];
            if (corners[i*2+1] > wyMax) wyMax = corners[i*2+1];
        }
    }

    // Screen area between turn bar (44 px) and HUD (130 px)
    float areaX = 0.0f;
    float areaY = 44.0f;
    float areaW = static_cast<float>(m_width);
    float areaH = static_cast<float>(m_height) - 44.0f - 130.0f;

    float margin = 12.0f;
    float worldW = wxMax - wxMin;
    float worldH = wyMax - wyMin;
    if (worldW < 1.0f || worldH < 1.0f) return;

    float scale = std::min((areaW - 2.0f * margin) / worldW,
                           (areaH - 2.0f * margin) / worldH);

    float boardW = worldW * scale;
    float boardH = worldH * scale;
    m_combatBoardScale = scale;
    m_combatBoardOffX  = areaX + (areaW - boardW) * 0.5f - wxMin * scale;
    m_combatBoardOffY  = areaY + (areaH - boardH) * 0.5f - wyMin * scale;

    // Reachable tiles for active player unit
    std::vector<HexCoord> reach;
    const CombatUnit* active = const_cast<CombatEngine&>(m_combat).activeUnit();
    if (active && active->isPlayer && !active->hasMoved)
        reach = grid.reachable(active->pos, active->speed, active->flying);

    // Draw background — terrain image if loaded, else dark colour fallback
    int terrIdx = static_cast<int>(m_combatTerrain);
    if (terrIdx < 0 || terrIdx >= NUM_TERRAIN_TYPES) terrIdx = 0;
    if (m_combatBgTex[terrIdx].ok()) {
        ImTextureID bgTex = (ImTextureID)(uintptr_t)m_combatBgTex[terrIdx].id();
        dl->AddImage(bgTex, {areaX, areaY}, {areaX + areaW, areaY + areaH});
    } else {
        // Solid colour fallback per terrain
        ImU32 bgCol;
        switch (m_combatTerrain) {
            case Terrain::Forest:
            case Terrain::CorruptedForest: bgCol=IM_COL32(8,18,8,255);  break;
            case Terrain::Highland:        bgCol=IM_COL32(22,18,12,255); break;
            case Terrain::Rocky:           bgCol=IM_COL32(20,18,16,255); break;
            case Terrain::Volcanic:        bgCol=IM_COL32(20,8,4,255);   break;
            case Terrain::Swamp:           bgCol=IM_COL32(10,14,8,255);  break;
            case Terrain::Corrupted:       bgCol=IM_COL32(14,8,18,255);  break;
            case Terrain::FleshZone:       bgCol=IM_COL32(22,10,10,255); break;
            case Terrain::Toxic:           bgCol=IM_COL32(10,16,6,255);  break;
            case Terrain::Industrial:      bgCol=IM_COL32(16,16,18,255); break;
            case Terrain::Sacred:          bgCol=IM_COL32(8,10,20,255);  break;
            default:                       bgCol=IM_COL32(14,18,10,255); break;
        }
        dl->AddRectFilled({areaX, areaY}, {areaX + areaW, areaY + areaH}, bgCol);
    }

    // Semi-transparent tile overlay colours (background shows through)
    // Normal tiles: very slight dark tint so grid lines are visible
    ImU32 tileBase = IM_COL32(0,   0,   0,  40);
    ImU32 obstCol  = IM_COL32(10,  10,  10, 170);

    // Draw tiles
    for (const auto& h : coords) {
        float corners[12];
        hg.hexCorners(h, corners);

        ImVec2 pts[6];
        for (int i = 0; i < 6; ++i) {
            pts[i].x = corners[i*2]   * scale + m_combatBoardOffX;
            pts[i].y = corners[i*2+1] * scale + m_combatBoardOffY;
        }

        const CombatTile* tile = grid.getTile(h);
        ImU32 fill = tileBase;
        if (tile) {
            switch (tile->type) {
                case CombatTileType::Attack:       fill = IM_COL32(140, 30,  30,  130); break;
                case CombatTileType::Defense:      fill = IM_COL32(30,  30,  160, 130); break;
                case CombatTileType::Speed:        fill = IM_COL32(30,  130, 30,  120); break;
                case CombatTileType::SpeedPenalty: fill = IM_COL32(120, 80,  10,  120); break;
                case CombatTileType::Obstacle:     fill = obstCol; break;
                case CombatTileType::Wall:         fill = IM_COL32(90,  90,  90,  210); break;
                default: break;
            }
        }
        // Reachable highlight
        for (const auto& rh : reach)
            if (rh == h) { fill = IM_COL32(40, 180, 60, 130); break; }

        // Siege: highlight attackable wall tiles for the active unit
        if (tile && tile->type == CombatTileType::Wall && tile->wallHP > 0
            && active && active->isPlayer && m_combat.isSiege()) {
            bool canHit = false;
            if (active->isSiegeEngine && active->range > 0 && active->shotsLeft > 0) {
                canHit = (HexGrid::distance(active->pos, h) <= active->range);
                if (active->gateOnly) canHit = canHit && (h == grid.gateHex());
            } else {
                canHit = (HexGrid::distance(active->pos, h) == 1);
                if (active->gateOnly) canHit = canHit && (h == grid.gateHex());
            }
            if (canHit) fill = IM_COL32(200, 100, 20, 200);
        }

        // Active unit tile highlight
        if (active && h == active->pos)
            fill = IM_COL32(220, 190, 20, 140);

        // Expand fill 0.8px outward from centroid to close sub-pixel seams
        float pcx = 0, pcy = 0;
        for (int i = 0; i < 6; i++) { pcx += pts[i].x; pcy += pts[i].y; }
        pcx /= 6.0f; pcy /= 6.0f;
        ImVec2 fillPts[6];
        for (int i = 0; i < 6; i++) {
            float dx = pts[i].x - pcx, dy = pts[i].y - pcy;
            float len = sqrtf(dx*dx + dy*dy);
            float e = (len > 0.5f) ? 0.8f / len : 0.0f;
            fillPts[i] = {pts[i].x + dx*e, pts[i].y + dy*e};
        }
        dl->AddConvexPolyFilled(fillPts, 6, fill);
        dl->AddPolyline(pts, 6, IM_COL32(80, 85, 110, 210),
                        ImDrawFlags_Closed, 1.2f);

        // Wall HP bar — colour-coded by health fraction
        if (tile && tile->type == CombatTileType::Wall && tile->wallHP > 0) {
            float wx2, wy2;
            hg.hexToWorld(h, wx2, wy2);
            float sx2 = wx2 * scale + m_combatBoardOffX;
            float sy2 = wy2 * scale + m_combatBoardOffY;
            float bw  = hg.hexSize() * scale * 0.55f;
            float by2 = sy2 + hg.hexSize() * scale * 0.45f;
            // Derive max from original values: walls=40, gate=20
            bool isGate = (h == grid.gateHex());
            int  maxHp  = isGate ? 20 : 40;
            float frac  = static_cast<float>(tile->wallHP) / static_cast<float>(maxHp);
            frac = std::max(0.0f, std::min(1.0f, frac));
            ImU32 barCol = frac > 0.5f ? IM_COL32(180, 180, 60, 230)
                         : frac > 0.25f ? IM_COL32(220, 120, 30, 230)
                                        : IM_COL32(220, 50,  50, 230);
            dl->AddRectFilled({sx2 - bw, by2}, {sx2 + bw, by2 + 4}, IM_COL32(30, 30, 30, 200));
            dl->AddRectFilled({sx2 - bw, by2}, {sx2 - bw + 2.0f * bw * frac, by2 + 4}, barCol);
            char hpBuf[8]; std::snprintf(hpBuf, sizeof(hpBuf), "%d", tile->wallHP);
            ImVec2 hts = ImGui::CalcTextSize(hpBuf);
            dl->AddText({sx2 - hts.x * 0.5f, by2 - 12.0f}, IM_COL32(255, 240, 160, 220), hpBuf);
        }
    }

    // Range ring for active ranged unit
    if (active && active->isPlayer && active->range > 0 && active->shotsLeft > 0) {
        float awx, awy;
        hg.hexToWorld(active->pos, awx, awy);
        float asx = awx * scale + m_combatBoardOffX;
        float asy = awy * scale + m_combatBoardOffY;
        float hexW = hg.hexSize() * scale;
        float rangeR = hexW * active->range * 1.05f;
        dl->AddCircle({asx, asy}, rangeR, IM_COL32(180, 200, 255, 80), 48, 1.5f);
    }

    // Unit rendering
    float hexR = hg.hexSize() * scale * 0.38f;
    float sprW = hexR * 1.8f;
    float sprH = hexR * 2.8f;

    static const ImU32 kFacFill[] = {
        IM_COL32(210, 175, 55,  235),
        IM_COL32(175, 28,  28,  235),
        IM_COL32(35,  120, 35,  235),
        IM_COL32(95,  45,  155, 235),
        IM_COL32(155, 35,  35,  235),
        IM_COL32(75,  35,  175, 235),
        IM_COL32(70,  120, 175, 235),
        IM_COL32(155, 75,  35,  235),
        IM_COL32(35,  75,  155, 235),
    };

    // Dead units first (behind living)
    for (const auto& u : grid.units()) {
        if (u.alive) continue;
        float wx, wy;
        hg.hexToWorld(u.pos, wx, wy);
        float sx = wx * scale + m_combatBoardOffX;
        float sy = wy * scale + m_combatBoardOffY;
        auto it = m_combatAnimators.find(u.id);
        if (it != m_combatAnimators.end()) {
            int fi   = it->second.faction;
            int tidx = std::max(0, std::min(NUM_UNIT_TIERS - 1, it->second.tier - 1));
            if (fi >= 0 && fi < NUM_FACTIONS && m_unitTex[fi][tidx].ok()) {
                float u0, v0, u1, v1;
                it->second.getUV(u0, v0, u1, v1);
                ImTextureID tid = (ImTextureID)(uintptr_t)m_unitTex[fi][tidx].id();
                dl->AddImage(tid,
                    {sx - sprW, sy - sprH * 0.85f},
                    {sx + sprW, sy + sprH * 0.15f},
                    {u0, v0}, {u1, v1}, IM_COL32(255,255,255,90));
            }
        }
    }

    // CoordinatedStrike marked target ID
    uint32_t csTarget = m_combat.coordinatedStrikeTarget();

    // Alive units
    for (const auto& u : grid.units()) {
        if (!u.alive) continue;
        float wx, wy;
        hg.hexToWorld(u.pos, wx, wy);
        float sx = wx * scale + m_combatBoardOffX;
        float sy = wy * scale + m_combatBoardOffY;

        bool  isActive = (active && u.id == active->id);
        bool  isGhost  = (u.name.rfind("Ghost ", 0) == 0);
        ImU32 rimCol   = isActive ? IM_COL32(255, 205, 50, 255)
                                  : IM_COL32(210, 210, 210, 160);

        // Reset per-unit sprW/sprH (overridden below if sprite not available)
        sprW = hexR * 1.8f;
        sprH = hexR * 2.8f;

        auto it = m_combatAnimators.find(u.id);
        bool drewSprite = false;
        if (it != m_combatAnimators.end()) {
            int fi   = it->second.faction;
            int tidx = std::max(0, std::min(NUM_UNIT_TIERS - 1, it->second.tier - 1));
            if (fi >= 0 && fi < NUM_FACTIONS && m_unitTex[fi][tidx].ok()) {
                float u0, v0, u1, v1;
                it->second.getUV(u0, v0, u1, v1);
                ImTextureID tid = (ImTextureID)(uintptr_t)m_unitTex[fi][tidx].id();
                ImU32 tint = isGhost ? IM_COL32(200,230,255,110) : IM_COL32(255,255,255,255);
                dl->AddImage(tid,
                    {sx - sprW, sy - sprH * 0.85f},
                    {sx + sprW, sy + sprH * 0.15f},
                    {u0, v0}, {u1, v1}, tint);
                drewSprite = true;
            }
        }
        if (!drewSprite) {
            int facIdx = (it != m_combatAnimators.end())
                         ? std::max(0, std::min(8, it->second.faction))
                         : (u.isPlayer ? 0 : 1);
            ImU32 fillCol = kFacFill[facIdx];
            if (isGhost) fillCol = (fillCol & 0x00FFFFFF) | 0x60000000u;

            float tr = hexR * 0.82f;
            ImVec2 tkPts[6];
            for (int vi = 0; vi < 6; vi++) {
                float ang = (vi * 60.0f) * 3.14159265f / 180.0f;
                tkPts[vi] = {sx + tr * cosf(ang), sy + tr * sinf(ang)};
            }
            dl->AddConvexPolyFilled(tkPts, 6, fillCol);
            ImU32 edgeCol = isActive ? IM_COL32(255,220,80,240)
                          : u.isPlayer ? IM_COL32(100,180,255,220)
                                       : IM_COL32(255,80,80,220);
            dl->AddPolyline(tkPts, 6, edgeCol, ImDrawFlags_Closed, isActive ? 3.0f : 2.0f);

            char abbrevBuf[4] = {0};
            int nc = 0;
            for (const char* p = u.name.c_str(); *p && nc < 3; p++)
                if (*p >= 32 && *p < 127) abbrevBuf[nc++] = *p;
            ImVec2 ats = ImGui::CalcTextSize(abbrevBuf);
            dl->AddText({sx - ats.x*0.5f + 1, sy - ats.y*0.5f + 1}, IM_COL32(0,0,0,160), abbrevBuf);
            dl->AddText({sx - ats.x*0.5f,     sy - ats.y*0.5f    }, IM_COL32(255,255,255,230), abbrevBuf);

            sprW = tr * 0.9f;
            sprH = tr * 1.0f;
        }

        // Activity ring
        dl->AddCircle({sx, sy}, hexR * 1.05f, rimCol, 0, isActive ? 2.5f : 1.2f);

        // CoordinatedStrike reticle: orange double-ring on marked enemy
        if (!u.isPlayer && u.id == csTarget) {
            dl->AddCircle({sx, sy}, hexR * 1.28f, IM_COL32(255, 140, 0, 230), 0, 2.5f);
            dl->AddCircle({sx, sy}, hexR * 1.42f, IM_COL32(255, 200, 50, 110), 0, 1.5f);
            // Small crosshair lines
            float ch = hexR * 0.22f;
            dl->AddLine({sx - hexR*1.42f, sy}, {sx - hexR*1.15f, sy}, IM_COL32(255, 160, 30, 200), 1.5f);
            dl->AddLine({sx + hexR*1.15f, sy}, {sx + hexR*1.42f, sy}, IM_COL32(255, 160, 30, 200), 1.5f);
            dl->AddLine({sx, sy - hexR*1.42f}, {sx, sy - hexR*1.15f}, IM_COL32(255, 160, 30, 200), 1.5f);
            dl->AddLine({sx, sy + hexR*1.15f}, {sx, sy + hexR*1.42f}, IM_COL32(255, 160, 30, 200), 1.5f);
            (void)ch;
        }

        // HP bar — shows total stack HP as a fraction of starting HP for this stack
        int   stackMaxHp = u.count * u.maxHp + (u.maxHp - u.hp);  // approximate starting max
        float hpFrac     = (stackMaxHp > 0)
                            ? static_cast<float>(u.totalHp()) / static_cast<float>(u.count * u.maxHp + (u.maxHp - u.hp))
                            : 0.0f;
        // Simpler: just show top-unit fraction (more useful feedback per-unit)
        hpFrac = (u.maxHp > 0) ? static_cast<float>(u.hp) / static_cast<float>(u.maxHp) : 0.0f;
        float barW = hexR * 1.4f;
        float barY = sy + sprH * 0.15f + 2.0f;
        dl->AddRectFilled({sx - barW, barY}, {sx + barW, barY + 4}, IM_COL32(70, 10, 10, 200));
        ImU32 hpCol = hpFrac > 0.5f ? IM_COL32(50, 200, 50, 220)
                    : hpFrac > 0.25f ? IM_COL32(220, 180, 30, 220)
                                     : IM_COL32(220, 50, 50, 220);
        dl->AddRectFilled({sx - barW, barY}, {sx - barW + 2.0f*barW*hpFrac, barY + 4}, hpCol);

        // Buff/debuff indicators — small colored dots above the unit
        float dotY  = sy - sprH * 0.85f - 6.0f;
        float dotX  = sx - 6.0f;
        if (u.roundAttackBonus > 0) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(255, 140, 20, 220));  // orange = atk buff
            dotX += 10.0f;
        } else if (u.roundAttackBonus < 0) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(200, 60, 60, 220));   // red = atk debuff
            dotX += 10.0f;
        }
        if (u.roundDefenseBonus > 0) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(60, 140, 255, 220));  // blue = def buff
            dotX += 10.0f;
        } else if (u.roundDefenseBonus < 0) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(160, 60, 200, 220));  // purple = def debuff
            dotX += 10.0f;
        }
        if (u.poisonRounds > 0) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(80, 220, 80, 220));   // green = poisoned
            dotX += 10.0f;
        }
        if (u.burnRounds > 0) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(255, 120, 40, 220));  // orange = burning
            dotX += 10.0f;
        }
        if (u.vampiric) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(160, 0, 210, 220));   // purple = vampiric
            dotX += 10.0f;
        }
        if (u.regenerates) {
            dl->AddCircleFilled({dotX, dotY}, 4.0f, IM_COL32(0, 200, 140, 220));   // teal = regenerates
            dotX += 10.0f;
        }
        if (u.hasSecondLife && !u.secondLifeUsed) {
            dl->AddCircle({sx, sy}, hexR * 0.6f, IM_COL32(255, 215, 0, 180), 0, 1.5f);  // gold inner ring
        }
        // Morale indicator: pulsing ring when morale is extreme
        if (!u.moraleImmune) {
            if (u.morale >= 80) {
                // High morale — bright gold ring
                dl->AddCircle({sx, sy}, hexR * 1.18f, IM_COL32(255, 200, 30, 120), 0, 1.5f);
            } else if (u.morale < 20) {
                // Fear — red jagged outline (double circle slightly offset)
                dl->AddCircle({sx, sy}, hexR * 1.18f, IM_COL32(200, 30, 30, 140), 0, 1.5f);
                dl->AddCircle({sx, sy}, hexR * 1.10f, IM_COL32(200, 30, 30,  80), 0, 1.0f);
            }
        }
        // OrganicMech adaptation indicator: small teal gems below the unit, one per 2 adaptations
        if (hasTag(u.tags, UnitTag::OrganicMech) && u.adaptationsGained > 0) {
            int gemCount = (u.adaptationsGained + 1) / 2;  // show 1 gem per 2 adaptations (max 3)
            gemCount = std::min(gemCount, 3);
            float gemY = barY + 8.0f;
            float gemStartX = sx - (gemCount - 1) * 5.0f;
            ImU32 gemCol = u.adaptationsGained >= 6
                ? IM_COL32(50, 255, 220, 240)   // fully adapted — bright teal
                : IM_COL32(80, 200, 160, 200);  // partially adapted — muted teal
            for (int g = 0; g < gemCount; g++) {
                float gx = gemStartX + g * 10.0f;
                dl->AddCircleFilled({gx, gemY}, 3.0f, gemCol);
            }
        }
        // Flying marker: small wing-like triangle above unit
        if (u.flying) {
            float wy = sy - sprH * 0.85f - 14.0f;
            dl->AddTriangleFilled({sx - 5, wy + 4}, {sx + 5, wy + 4}, {sx, wy},
                                   IM_COL32(180, 220, 255, 200));
        }

        // Stack count label (bottom-center of token)
        char buf[12];
        std::snprintf(buf, sizeof(buf), "%d", u.count);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float lx = sx - ts.x * 0.5f;
        float ly = sy + sprH * 0.02f;
        dl->AddText({lx + 1, ly + 1}, IM_COL32(0, 0, 0, 200), buf);
        dl->AddText({lx, ly}, IM_COL32(255, 255, 255, 255), buf);

        // Shots remaining indicator for ranged units (small cyan badge top-right)
        if (u.range > 0 && u.shots > 0) {
            char shotBuf[8];
            std::snprintf(shotBuf, sizeof(shotBuf), "%d", u.shotsLeft);
            ImVec2 sts = ImGui::CalcTextSize(shotBuf);
            float bx2 = sx + sprW * 0.7f;
            float by3 = sy - sprH * 0.7f;
            ImU32 shotCol = u.shotsLeft > 0 ? IM_COL32(80, 200, 255, 240)
                                             : IM_COL32(120, 120, 120, 200);
            dl->AddRectFilled({bx2 - 2, by3 - 2}, {bx2 + sts.x + 2, by3 + sts.y + 2},
                               IM_COL32(0, 0, 0, 160), 2.0f);
            dl->AddText({bx2, by3}, shotCol, shotBuf);
        }
    }

    // Coordinate hint for hovered hex + damage estimate tooltip
    const auto& mouse = m_input.mouse();
    float mwx = (mouse.x - m_combatBoardOffX) / m_combatBoardScale;
    float mwy = (mouse.y - m_combatBoardOffY) / m_combatBoardScale;
    HexCoord mh = hg.worldToHex(mwx, mwy);
    if (grid.inBounds(mh)) {
        float corners[12];
        hg.hexCorners(mh, corners);
        ImVec2 pts[6];
        for (int i = 0; i < 6; ++i) {
            pts[i].x = corners[i*2]   * scale + m_combatBoardOffX;
            pts[i].y = corners[i*2+1] * scale + m_combatBoardOffY;
        }
        dl->AddPolyline(pts, 6, IM_COL32(220, 220, 120, 180),
                        ImDrawFlags_Closed, 1.5f);

        // Damage preview: active player unit vs enemy unit on hovered hex
        const CombatUnit* hovered = grid.getUnitAt(mh);
        if (hovered && !hovered->isPlayer && hovered->alive) {
            m_combatHUD.setHoveredUnit(hovered);
            const CombatUnit* act = const_cast<CombatEngine&>(m_combat).activeUnit();
            if (act && act->isPlayer && act->alive) {
                auto est = DamageCalc::estimate(*act, *hovered, grid);
                if (est.maxDmg > 0) {
                    // Also compute retaliation estimate if target can retaliate
                    char retBuf[64] = {};
                    if (hovered->canRetaliate && !hovered->hasActed) {
                        auto retEst = DamageCalc::estimate(*hovered, *act, grid);
                        if (retEst.maxDmg > 0)
                            std::snprintf(retBuf, sizeof(retBuf),
                                "\nRetal: %d-%d  Kills: %d-%d",
                                retEst.minDmg, retEst.maxDmg, retEst.minKills, retEst.maxKills);
                    }
                    char tipBuf[160];
                    std::snprintf(tipBuf, sizeof(tipBuf),
                        "Damage: %d-%d  Kills: %d-%d%s",
                        est.minDmg, est.maxDmg, est.minKills, est.maxKills, retBuf);
                    // Draw tooltip near mouse
                    float tx = mouse.x + 12.0f;
                    float ty = mouse.y - 36.0f;
                    ImVec2 ts2 = ImGui::CalcTextSize(tipBuf);
                    dl->AddRectFilled({tx - 4, ty - 3}, {tx + ts2.x + 4, ty + ts2.y + 3},
                                      IM_COL32(15, 15, 30, 220), 3.0f);
                    dl->AddRect({tx - 4, ty - 3}, {tx + ts2.x + 4, ty + ts2.y + 3},
                                IM_COL32(180, 100, 60, 180), 3.0f);
                    dl->AddText({tx, ty}, IM_COL32(255, 200, 100, 255), tipBuf);
                }
            }
        } else if (hovered && hovered->isPlayer) {
            m_combatHUD.setHoveredUnit(hovered);
        } else {
            m_combatHUD.setHoveredUnit(nullptr);
        }

        // Tile-type tooltip for special terrain (shown on empty tiles)
        if (!hovered) {
            const CombatTile* mt = grid.getTile(mh);
            char wallTipBuf[80] = {};
            const char* tileTip = nullptr;
            if (mt) {
                switch (mt->type) {
                case CombatTileType::Attack:
                    tileTip = "Power Ground\n+2 ATK when a unit enters this tile";
                    break;
                case CombatTileType::Defense:
                    tileTip = "Fortified Ground\n+2 DEF when a unit enters this tile";
                    break;
                case CombatTileType::Speed:
                    tileTip = "Sacred Ground\n+5 Morale when a unit enters (bonus action threshold)";
                    break;
                case CombatTileType::SpeedPenalty:
                    tileTip = "Hazard Ground\n-5 Morale when a unit enters this tile";
                    break;
                case CombatTileType::Wall: {
                    bool isGate = (mh == grid.gateHex());
                    std::snprintf(wallTipBuf, sizeof(wallTipBuf),
                        "%s  HP: %d\nClick to attack — melee or siege engines",
                        isGate ? "Gate" : "Fort Wall", mt->wallHP);
                    tileTip = wallTipBuf;
                    break;
                }
                default: break;
                }
            }
            if (tileTip) {
                float tx = mouse.x + 12.0f;
                float ty = mouse.y - 24.0f;
                ImVec2 ts2 = ImGui::CalcTextSize(tileTip);
                dl->AddRectFilled({tx - 4, ty - 3}, {tx + ts2.x + 4, ty + ts2.y + 3},
                                   IM_COL32(15, 20, 35, 220), 3.0f);
                dl->AddRect({tx - 4, ty - 3}, {tx + ts2.x + 4, ty + ts2.y + 3},
                             IM_COL32(100, 160, 200, 180), 3.0f);
                dl->AddText({tx, ty}, IM_COL32(200, 230, 255, 255), tileTip);
            }
        }
    }

    // Particles
    m_particles.render(dl);

    // Floating damage numbers
    for (const auto& ef : m_combatDmgEffects) {
        float alpha = std::min(1.0f, ef.t);
        if (alpha <= 0.f) continue;
        float rise = (1.5f - ef.t) * 35.0f;
        char dmgBuf[16];
        std::snprintf(dmgBuf, sizeof(dmgBuf), "%d", ef.dmg);
        float fx = ef.bx - ImGui::CalcTextSize(dmgBuf).x * 0.5f;
        float fy = ef.by - rise;
        int   a  = static_cast<int>(alpha * 255);
        ImU32 shadow = IM_COL32(0, 0, 0, a);
        ImU32 col    = ef.isHeal ? IM_COL32(80, 255, 100, a) : IM_COL32(255, 80, 60, a);
        dl->AddText({fx + 1, fy + 1}, shadow, dmgBuf);
        dl->AddText({fx, fy}, col, dmgBuf);
    }
}

// ── Spell panel (ImGui) ───────────────────────────────────────────────────────
void Game::renderSpellPanel()
{
    if (m_heroes.empty()) return;
    const Hero& hero = m_heroes[m_activeHeroIdx];

    // Large spellbook window — fills most of the screen
    ImGuiIO& spio = ImGui::GetIO();
    float winW = std::min(720.0f, spio.DisplaySize.x - 40.0f);
    float winH = std::min(560.0f, spio.DisplaySize.y - 120.0f);
    ImGui::SetNextWindowPos({spio.DisplaySize.x * 0.5f, spio.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({winW, winH}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGuiWindowFlags sbFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoSavedSettings;

    if (hero.knownSpells.empty()) {
        if (!ImGui::Begin("Spellbook", &m_showSpellPanel, sbFlags)) { ImGui::End(); return; }
        ImGui::TextDisabled("No spells known.");
        ImGui::TextDisabled("Find spellbooks on the world map or visit a Mage Guild.");
        ImGui::End();
        return;
    }

    if (!ImGui::Begin("Spellbook", &m_showSpellPanel, sbFlags)) { ImGui::End(); return; }

    // Header: mana + target picker
    ImGui::TextColored({1.0f, 0.82f, 0.2f, 1.0f}, "Mana: %d / %d", hero.mana, hero.maxMana);
    ImGui::SameLine(0, 20);
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("Target", m_spellTargetId == 0 ? "-- pick target --" : [&]() -> const char* {
        auto* u = m_combat.grid().getUnit(m_spellTargetId);
        return u ? u->name.c_str() : "--";
    }())) {
        for (auto& u : m_combat.grid().units()) {
            if (!u.alive) continue;
            bool sel = (u.id == m_spellTargetId);
            std::string lbl = u.name + (u.isPlayer ? " [ally]" : " [enemy]");
            if (ImGui::Selectable(lbl.c_str(), sel))
                m_spellTargetId = u.id;
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    CombatUnit* active = m_combat.activeUnit();
    bool isPlayerTurn  = active && active->isPlayer;

    bool hasFreeByCast  = hero.exsanguinate && !hero.exsanguinateUsed;
    bool hasFreeByMirror= hero.predatorMirrorSpecialty && !hero.predatorMirrorUsed;

    auto schoolPow = [&](SpellSchool school) -> int {
        switch (school) {
            case SpellSchool::Light:  return hero.lightPower;
            case SpellSchool::Blood:  return hero.bloodPower;
            case SpellSchool::Death:  return hero.deathPower;
            case SpellSchool::Nature: return hero.naturePower;
            case SpellSchool::Forge:  return hero.forgePower;
            case SpellSchool::Flesh:  return hero.fleshPower;
            default: return 0;
        }
    };

    static constexpr ImVec4 kSchoolCol[] = {
        {1.0f, 0.95f, 0.6f, 1.0f},  // Light
        {0.9f, 0.25f, 0.25f, 1.0f}, // Blood
        {0.3f, 0.85f, 0.75f, 1.0f}, // Death
        {0.4f, 0.85f, 0.35f, 1.0f}, // Nature
        {0.7f, 0.75f, 1.0f, 1.0f},  // Forge
        {0.8f, 0.5f,  0.2f, 1.0f},  // Flesh
    };
    static const char* kSchoolName[] = { "Light","Blood","Death","Nature","Forge","Flesh","Neutral" };

    // Spell cards — 2 columns, big icon + name + cost + desc
    const float ICON_SZ  = 56.0f;
    const float CARD_H   = ICON_SZ + 16.0f;
    const float CARD_W   = (winW - 24.0f) * 0.5f - 4.0f;
    ImGui::BeginChild("##spellcards", ImVec2(-1, -1), false);

    int col = 0;
    for (int sid : hero.knownSpells) {
        const SpellDef* spell = findSpell(sid);
        if (!spell || spell->target == SpellTarget::WorldMap) continue;

        bool freeBlood  = hasFreeByCast  && spell->school == SpellSchool::Blood;
        bool freeMirror = hasFreeByMirror;
        bool isFree     = freeBlood || freeMirror;
        bool canAfford  = isFree || hero.mana >= spell->manaCost;
        bool canCast    = isPlayerTurn && canAfford;

        if (col == 1) ImGui::SameLine(0, 8);

        int si = static_cast<int>(spell->school);
        if (si < 0 || si >= static_cast<int>(std::size(kSchoolCol))) si = 0;
        ImVec4 sc = kSchoolCol[si];

        // Card background
        ImVec2 cardPos = ImGui::GetCursorScreenPos();
        ImDrawList* dl2 = ImGui::GetWindowDrawList();
        ImU32 bgCol = canCast ? IM_COL32(30, 28, 40, 230) : IM_COL32(18, 18, 24, 180);
        ImU32 brdCol = canCast
            ? IM_COL32((int)(sc.x*200),(int)(sc.y*200),(int)(sc.z*200),200)
            : IM_COL32(50, 50, 60, 200);
        dl2->AddRectFilled({cardPos.x, cardPos.y},
                           {cardPos.x + CARD_W, cardPos.y + CARD_H}, bgCol, 6.0f);
        dl2->AddRect({cardPos.x, cardPos.y},
                     {cardPos.x + CARD_W, cardPos.y + CARD_H}, brdCol, 6.0f, 0, 1.5f);

        ImGui::PushID(sid);

        // Invisible button covers the card
        if (!canCast) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##card", ImVec2(CARD_W, CARD_H))) {
            CombatAction act;
            act.type         = ActionType::UseAbility;
            act.spellId      = sid;
            act.targetUnitId = m_spellTargetId;
            m_audio.playSound("spell");
            // Emit spell particles at the target unit's board position
            if (const CombatUnit* tgt = m_combat.grid().getUnit(m_spellTargetId)) {
                float wx, wy;
                m_combat.grid().hexGrid().hexToWorld(tgt->pos, wx, wy);
                float psx = wx * m_combatBoardScale + m_combatBoardOffX;
                float psy = wy * m_combatBoardScale + m_combatBoardOffY;
                const SpellDef* sd = findSpell(sid);
                ParticlePreset pp = ParticlePreset::SpellLight;
                if (sd) {
                    switch (sd->school) {
                    case SpellSchool::Blood:   pp = ParticlePreset::SpellBlood;  break;
                    case SpellSchool::Death:   pp = ParticlePreset::SpellDark;   break;
                    case SpellSchool::Nature:  pp = ParticlePreset::SpellNature; break;
                    case SpellSchool::Forge:   pp = ParticlePreset::SpellRune;   break;
                    case SpellSchool::Flesh:   pp = ParticlePreset::SpellFlesh;  break;
                    default:                   pp = ParticlePreset::SpellLight;  break;
                    }
                }
                m_particles.emit(psx, psy, pp);
            }
            m_combat.submitAction(act);
            m_showSpellPanel = false;
        }
        if (!canCast) ImGui::EndDisabled();

        // Hover glow
        // Buffs/debuffs use spell->power only; damage/heal/DoT/morale scale with school power.
        bool scalesWithSchool = (spell->effect == SpellEffect::Damage    ||
                                 spell->effect == SpellEffect::Heal      ||
                                 spell->effect == SpellEffect::Poison    ||
                                 spell->effect == SpellEffect::Burn      ||
                                 spell->effect == SpellEffect::MoraleBoost ||
                                 spell->effect == SpellEffect::MoraleDrain);
        int displayPow = scalesWithSchool
            ? spell->power + schoolPow(spell->school)
            : spell->power;

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            dl2->AddRect({cardPos.x, cardPos.y},
                         {cardPos.x + CARD_W, cardPos.y + CARD_H},
                         IM_COL32(255, 220, 80, 120), 6.0f, 0, 3.0f);
            char tipBuf[256];
            if (scalesWithSchool)
                std::snprintf(tipBuf, sizeof(tipBuf), "%s\nPower: %d + %s %d = %d",
                    spell->desc, spell->power, kSchoolName[si], schoolPow(spell->school),
                    displayPow);
            else
                std::snprintf(tipBuf, sizeof(tipBuf), "%s\nPower: %d (buffs don't scale with school)",
                    spell->desc, displayPow);
            ImGui::SetTooltip("%s", tipBuf);
        }

        // Draw icon over the card
        ImVec2 icoPos = {cardPos.x + 6.0f, cardPos.y + (CARD_H - ICON_SZ) * 0.5f};
        if (m_spellIconTex.ok()) {
            // Atlas layout: 5 cols × N rows, each row = one school, 5 spells per row.
            // Row 0: Light  (IDs  1- 4)   → atlas col = slot within school (0-3)
            // Row 1: Blood  (IDs 11-14)
            // Row 2: Death  (IDs 21-24, +Venomous Cloud 24 col 4)
            // Row 3: Nature (IDs 30-34)   ← need row 3 in atlas
            // Row 4: Forge  (IDs 40-44)   ← need row 4
            // Row 5: Flesh  (IDs 50-54)   ← need row 5
            // Row 6: Neutral(IDs 60-62)   ← need row 6
            // Current atlas is 5×5 (rows 0-4 exist; rows 5-6 missing).
            int row = 0, col = 0;
            if      (sid >= 1  && sid <= 4)  { row = 0; col = sid - 1; }
            else if (sid >= 11 && sid <= 14) { row = 1; col = sid - 11; }
            else if (sid >= 21 && sid <= 25) { row = 2; col = sid - 21; }
            else if (sid >= 30 && sid <= 34) { row = 3; col = sid - 30; }
            else if (sid >= 40 && sid <= 44) { row = 4; col = sid - 40; }
            else if (sid >= 50 && sid <= 54) { row = 5; col = sid - 50; }
            else if (sid >= 60 && sid <= 62) { row = 6; col = sid - 60; }
            // Clamp to atlas bounds (7 rows now exist; all schools covered)
            static const int kAtlasRows = 7;
            if (row >= kAtlasRows) row = kAtlasRows - 1;
            float u0 = col * 0.2f, v0 = row * (1.0f / kAtlasRows);
            float u1 = u0 + 0.2f,  v1 = v0 + (1.0f / kAtlasRows);
            dl2->AddImageRounded((ImTextureID)(uintptr_t)m_spellIconTex.id(),
                icoPos, {icoPos.x + ICON_SZ, icoPos.y + ICON_SZ},
                {u0, v0}, {u1, v1}, IM_COL32_WHITE, ICON_SZ * 0.5f);
        } else {
            dl2->AddCircleFilled({icoPos.x + ICON_SZ*0.5f, icoPos.y + ICON_SZ*0.5f},
                ICON_SZ*0.5f, IM_COL32((int)(sc.x*200),(int)(sc.y*200),(int)(sc.z*200),200));
        }

        // Text: name + cost + school
        float tx = cardPos.x + ICON_SZ + 14.0f;
        float ty = cardPos.y + 8.0f;
        dl2->AddText(ImGui::GetFont(), 15.0f, {tx, ty},
                     canCast ? IM_COL32(240,225,180,255) : IM_COL32(120,115,100,255),
                     spell->name);
        char costLine[80];
        if (isFree)
            std::snprintf(costLine, sizeof(costLine), "FREE  [pow %d]", displayPow);
        else
            std::snprintf(costLine, sizeof(costLine), "%d mana  [pow %d]", spell->manaCost, displayPow);
        dl2->AddText(ImGui::GetFont(), 12.0f, {tx, ty + 19.0f},
                     canCast ? IM_COL32(180,220,180,255) : IM_COL32(90,110,90,200),
                     costLine);
        dl2->AddText(ImGui::GetFont(), 11.0f, {tx, ty + 35.0f},
                     IM_COL32((int)(sc.x*200),(int)(sc.y*200),(int)(sc.z*200),200),
                     kSchoolName[si]);

        ImGui::PopID();

        col = 1 - col;
        if (col == 0) ImGui::Dummy(ImVec2(0, 4)); // row gap
    }

    ImGui::EndChild();
    ImGui::End();
}

// ── State transitions ─────────────────────────────────────────────────────────
void Game::enterCombat(Hero& playerHero,
                       const std::vector<CombatUnit>& playerUnits,
                       const Hero& enemyHero,
                       const std::vector<CombatUnit>& enemyUnits)
{
    m_prevState = m_state;  // remember Campaign vs WorldMap for return after battle
    m_state = GameState::Combat;

    // Capture terrain at the combat site
    if (const HexTile* tile = m_map.getTile(playerHero.pos))
        m_pendingCombatTerrain = tile->terrain;
    m_combatTerrain = m_pendingCombatTerrain;

    // Snapshot hero army for FIRST_AID post-combat calculation
    m_battleStartArmy = playerHero.army;

    // Snapshot enemy units for battle result display
    m_combatEnemiesDefeated.clear();
    for (const auto& cu : enemyUnits) {
        if (cu.count <= 0) continue;
        BattleUnitRecord rec;
        rec.name  = cu.name;
        rec.defId = cu.defId;
        rec.count = cu.count;
        if (cu.defId != 0) {
            const UnitDef* ud = m_registry.getUnitDef(cu.defId);
            if (ud) { rec.faction = static_cast<int>(ud->faction); rec.tier = ud->tier; }
        } else if (cu.factionHint >= 0) {
            rec.faction = cu.factionHint;
        }
        m_combatEnemiesDefeated.push_back(rec);
    }

    // Set per-battle specialty flags from class registry
    playerHero.feastSpecialty        = false;
    playerHero.witherSpecialty       = false;
    playerHero.ironDiscipline        = false;
    playerHero.exsanguinate          = false;
    playerHero.exsanguinateUsed      = false;
    playerHero.heresyDetection       = false;
    playerHero.heresyDetectionUsed   = false;
    playerHero.lightningRodSpecialty      = false;
    playerHero.lightningRodUsed           = false;
    playerHero.harmonySpecialty           = false;
    playerHero.elixirSpecialty            = false;
    playerHero.elixirUsed                 = false;
    playerHero.coordinatedStrikeSpecialty  = false;
    playerHero.bloodPenanceSpecialty       = false;
    playerHero.negotiatedWeaknessSpecialty = false;
    playerHero.wildGrowthSpecialty         = false;
    playerHero.overgrowthSpecialty         = false;
    playerHero.swarmSpecialty              = false;
    playerHero.livingRuneSpecialty         = false;
    playerHero.efficientSpecialty          = false;
    playerHero.bloodWebSpecialty           = false;
    playerHero.phylacterySpecialty         = false;
    playerHero.bloodScentSpecialty         = false;
    playerHero.lastRitesSpecialty          = false;
    playerHero.voidLinkSpecialty           = false;
    playerHero.infestationSpecialty        = false;
    playerHero.eternalLegionSpecialty      = false;
    playerHero.rapidEvolutionSpecialty     = false;
    playerHero.radianceSpecialty           = false;
    playerHero.predatorMirrorSpecialty     = false;
    playerHero.predatorMirrorUsed          = false;
    playerHero.covenantSpecialty           = false;
    playerHero.collectiveSpecialty         = false;
    playerHero.soulHarvestSpecialty        = false;
    playerHero.recyclerSpecialty           = false;
    playerHero.apexSpecialty               = false;
    playerHero.corruptionSpecialty         = false;
    playerHero.synthesisSpecialty          = false;
    playerHero.adaptationMirrorSpecialty   = false;
    if (const HeroClassDef* cls = m_classRegistry.getClass(playerHero.classId)) {
        playerHero.feastSpecialty              = (cls->specialty == SpecialtyType::Feast);
        playerHero.witherSpecialty             = (cls->specialty == SpecialtyType::Wither);
        playerHero.ironDiscipline              = (cls->specialty == SpecialtyType::IronDiscipline);
        playerHero.exsanguinate                = (cls->specialty == SpecialtyType::Exsanguinate);
        playerHero.heresyDetection             = (cls->specialty == SpecialtyType::HeresyDetection);
        playerHero.lightningRodSpecialty       = (cls->specialty == SpecialtyType::LightningRod);
        playerHero.harmonySpecialty            = (cls->specialty == SpecialtyType::Harmony);
        playerHero.elixirSpecialty             = (cls->specialty == SpecialtyType::Elixir);
        playerHero.coordinatedStrikeSpecialty  = (cls->specialty == SpecialtyType::CoordinatedStrike);
        playerHero.bloodPenanceSpecialty       = (cls->specialty == SpecialtyType::BloodPenance);
        playerHero.negotiatedWeaknessSpecialty = (cls->specialty == SpecialtyType::NegotiatedWeakness);
        playerHero.wildGrowthSpecialty         = (cls->specialty == SpecialtyType::WildGrowth);
        playerHero.overgrowthSpecialty         = (cls->specialty == SpecialtyType::Overgrowth);
        playerHero.swarmSpecialty              = (cls->specialty == SpecialtyType::Swarm);
        playerHero.livingRuneSpecialty         = (cls->specialty == SpecialtyType::LivingRune);
        playerHero.efficientSpecialty          = (cls->specialty == SpecialtyType::Efficient);
        playerHero.bloodWebSpecialty           = (cls->specialty == SpecialtyType::BloodWeb);
        playerHero.phylacterySpecialty         = (cls->specialty == SpecialtyType::Phylactery);
        playerHero.bloodScentSpecialty         = (cls->specialty == SpecialtyType::BloodScent);
        playerHero.lastRitesSpecialty          = (cls->specialty == SpecialtyType::LastRites);
        playerHero.voidLinkSpecialty           = (cls->specialty == SpecialtyType::VoidLink);
        playerHero.infestationSpecialty        = (cls->specialty == SpecialtyType::Infestation);
        playerHero.eternalLegionSpecialty      = (cls->specialty == SpecialtyType::EternalLegion);
        playerHero.rapidEvolutionSpecialty     = (cls->specialty == SpecialtyType::RapidEvolution);
        playerHero.radianceSpecialty           = (cls->specialty == SpecialtyType::Radiance);
        playerHero.predatorMirrorSpecialty     = (cls->specialty == SpecialtyType::PredatorMirror);
        playerHero.covenantSpecialty           = (cls->specialty == SpecialtyType::Covenant);
        playerHero.collectiveSpecialty         = (cls->specialty == SpecialtyType::Collective);
        playerHero.soulHarvestSpecialty        = (cls->specialty == SpecialtyType::SoulHarvest);
        playerHero.recyclerSpecialty           = (cls->specialty == SpecialtyType::Recycler);
        playerHero.apexSpecialty               = (cls->specialty == SpecialtyType::Apex);
        playerHero.corruptionSpecialty         = (cls->specialty == SpecialtyType::Corruption);
        playerHero.synthesisSpecialty          = (cls->specialty == SpecialtyType::Synthesis);
        playerHero.adaptationMirrorSpecialty   = (cls->specialty == SpecialtyType::AdaptationMirror);
    }

    // Garrison bonus: garrisoned hero grants +2 defense to all their units
    std::vector<CombatUnit> pUnitsGarr = playerUnits;
    if (playerHero.isGarrisoned)
        for (auto& u : pUnitsGarr) u.defense += 2;

    // Recycler bonus: salvaged ATK is applied to all player units each battle
    if (playerHero.recyclerBonus > 0)
        for (auto& u : pUnitsGarr) u.attack += playerHero.recyclerBonus;

    // Siege mode: triggered when attacking a garrisoned town
    bool isSiege = (m_pendingTownCaptureId != 0);
    if (isSiege) {
        auto makeSiegeEngines = [&](const Hero& hero) {
            // Helper to build a siege engine CombatUnit
            auto mkEng = [&](const char* nm, int atk, int def, int hp, int spd,
                             int rng, int shots, int wallDmg, bool gateOnly) -> CombatUnit {
                CombatUnit e;
                e.name          = nm;
                e.attack        = atk; e.defense = def;
                e.hp            = e.maxHp = hp;
                e.count         = 1;
                e.speed         = spd;
                e.range         = rng;
                e.shots         = shots; e.shotsLeft = shots;
                e.alive         = true;
                e.isSiegeEngine = true;
                e.wallDamage    = wallDmg;
                e.gateOnly      = gateOnly;
                e.isPlayer      = true;
                e.stackSlot     = 0; // assigned below
                return e;
            };
            // Base engine archetypes
            // Catapult: ranged, hits walls
            auto baseCat = [&]{ return mkEng("Catapult",        8,  4, 30, 3, 4, 5, 12, false); };
            // Battering Ram: melee, gate specialist
            auto baseRam = [&]{ return mkEng("Battering Ram",  14,  6, 50, 5, 0, 0, 20, true);  };
            // Trebuchet: long-range, high wall damage
            auto baseTre = [&]{ return mkEng("Trebuchet",      10,  3, 25, 2, 6, 4, 18, false); };
            // Siege Tower: durable, moderate gate
            auto baseTow = [&]{ return mkEng("Siege Tower",    12,  8, 70, 3, 0, 0, 15, true);  };

            std::vector<CombatUnit> engines;
            switch (hero.faction) {
            case FactionId::HolyOrder:
                // Divine Trebuchet (+20% wall damage, holy aura)
                engines.push_back(baseRam());
                {   auto dt = mkEng("Divine Trebuchet", 12, 4, 30, 2, 6, 5, 22, false);
                    engines.push_back(dt); }
                engines.push_back(baseCat());
                break;
            case FactionId::Bloodsworn:
                // Blood Catapult (+15% dmg; future: blood pool on hit)
                engines.push_back(baseRam());
                {   auto bc = mkEng("Blood Catapult", 10, 4, 32, 3, 4, 6, 14, false);
                    engines.push_back(bc); }
                engines.push_back(baseTow());
                break;
            case FactionId::Thornkin:
                // Living Tower (+30 HP, regenerates on its turn)
                engines.push_back(baseCat());
                {   auto lt = mkEng("Living Tower", 13, 9, 100, 3, 0, 0, 17, true);
                    engines.push_back(lt); }
                engines.push_back(baseRam());
                break;
            case FactionId::EternalEmpire:
                // Bone Crusher Ram (future: spawns skeletons on destroy)
                {   auto bc = mkEng("Bone Crusher", 17, 7, 55, 5, 0, 0, 24, true);
                    engines.push_back(bc); }
                engines.push_back(baseTre());
                engines.push_back(baseCat());
                break;
            case FactionId::CrimsonWardens:
                // Silver Trebuchet (+25% dmg vs Bloodsworn fortifications)
                engines.push_back(baseCat());
                {   auto st = mkEng("Silver Trebuchet", 13, 4, 28, 2, 6, 5, 23, false);
                    engines.push_back(st); }
                engines.push_back(baseRam());
                break;
            case FactionId::Voidkin:
                // Void Caster (future: creates Void terrain tile at impact)
                engines.push_back(baseRam());
                {   auto vc = mkEng("Void Caster", 11, 4, 35, 3, 4, 5, 13, false);
                    engines.push_back(vc); }
                engines.push_back(baseTow());
                break;
            case FactionId::IronAssembly:
                // All 3 engines upgraded: +30% HP, +15% damage
                {   auto ir = mkEng("Iron Ram",      16,  7, 65, 5, 0, 0, 23, true);  engines.push_back(ir); }
                {   auto ic = mkEng("Iron Catapult",  9,  5, 39, 3, 4, 6, 14, false); engines.push_back(ic); }
                {   auto it = mkEng("Iron Trebuchet",12,  4, 33, 2, 6, 5, 21, false); engines.push_back(it); }
                break;
            case FactionId::Amalgamate:
                // Flesh Drill (future: spawns FleshZone tiles on wall breach)
                engines.push_back(baseCat());
                engines.push_back(baseTre());
                {   auto fd = mkEng("Flesh Drill",  15,  7, 60, 4, 0, 0, 22, true);  engines.push_back(fd); }
                break;
            default: // Convergence + fallback: mirrors a standard 3-engine set
                engines.push_back(baseRam());
                engines.push_back(baseCat());
                engines.push_back(baseTre());
                break;
            }
            return engines;
        };

        for (auto& eng : makeSiegeEngines(playerHero)) {
            eng.stackSlot = static_cast<int>(pUnitsGarr.size());
            pUnitsGarr.push_back(eng);
        }
    }

    m_combat.startBattle(playerHero, pUnitsGarr, enemyHero, enemyUnits, isSiege, m_combatTerrain);

    // Terrain-driven obstacle tiles (non-siege only; siege already has walls)
    if (!isSiege) {
        int obstCount = 3;
        switch (m_combatTerrain) {
            case Terrain::Forest:         obstCount = 7; break;
            case Terrain::CorruptedForest:obstCount = 7; break;
            case Terrain::Highland:       obstCount = 6; break;
            case Terrain::Rocky:          obstCount = 6; break;
            case Terrain::Volcanic:       obstCount = 6; break;
            case Terrain::Swamp:          obstCount = 5; break;
            case Terrain::Corrupted:      obstCount = 5; break;
            case Terrain::Wasteland:      obstCount = 5; break;
            case Terrain::FleshZone:      obstCount = 4; break;
            case Terrain::Toxic:          obstCount = 4; break;
            case Terrain::Industrial:     obstCount = 4; break;
            case Terrain::Barren:         obstCount = 3; break;
            case Terrain::Plains:         obstCount = 2; break;
            case Terrain::Sacred:         obstCount = 2; break;
            case Terrain::Water:          obstCount = 1; break;
            default:                      obstCount = 3; break;
        }
        m_combat.applyTerrainObstacles(obstCount);
    }

    // Scale enemy AI difficulty with game difficulty and enemy hero level
    {
        AIDifficulty aiDiff = AIDifficulty::Standard;
        if (m_newGameDifficulty == 0) {
            aiDiff = AIDifficulty::Passive;
        } else if (m_newGameDifficulty >= 2 || enemyHero.level >= 5) {
            aiDiff = AIDifficulty::Tactical;
        }
        m_combat.setEnemyAI(aiDiff);
    }

    // Apply equipped artifact bonuses to the engine's internal hero/unit copies
    ArtifactBonus pb = m_artifactRegistry.totalBonus(playerHero.artifacts);
    ArtifactBonus eb = m_artifactRegistry.totalBonus(enemyHero.artifacts);
    m_combat.applyArtifactBonuses(pb, eb);

    // Apply support building bonuses when player hero is at or garrisoned in a town
    {
        int lightP = 0, bloodP = 0, deathP = 0, natureP = 0, forgeP = 0, fleshP = 0;
        int mechSpeed = 0, holyDesp = 0;
        bool eternalMonument = false;
        bool wardenBrand = false, symbiosisWeb = false, warShrine = false;
        bool voidLens = false, mergeChamber = false;
        bool resonanceWell = false, mirrorChamber = false;
        for (const auto& town : m_towns) {
            if (town.ownerId != static_cast<uint32_t>(currentPlayerId()) || town.pos != playerHero.pos) continue;
            // Magic-power support buildings
            if (town.hasBuilding(BID::HO_LIGHT_SHRINE))    lightP  += 2;
            if (town.hasBuilding(BID::CW_DEATH_ALTAR))     deathP  += 3;
            if (town.hasBuilding(BID::TK_ANCIENT_CIRCLE))  natureP += 3;
            if (town.hasBuilding(BID::EE_NECROPOLIS))      deathP  += 3;
            if (town.hasBuilding(BID::BS_BLOOD_ALTAR))     bloodP  += 3;
            if (town.hasBuilding(BID::VK_RIFT_GATE))       natureP += 3;
            if (town.hasBuilding(BID::IA_BLUEPRINT_VAULT)) forgeP  += 3;
            if (town.hasBuilding(BID::AM_FLESH_VAULT))     fleshP  += 3;
            // Unit-effect support buildings
            if (town.hasBuilding(BID::IA_OVERCLOCK))       mechSpeed    += 1;
            if (town.hasBuilding(BID::HO_RELIQUARY))       holyDesp     += 20;
            if (town.hasBuilding(BID::EE_MONUMENT))        eternalMonument = true;
            if (town.hasBuilding(BID::CW_WARDEN_BRAND))    wardenBrand  = true;
            if (town.hasBuilding(BID::TK_SYMBIOSIS_WEB))   symbiosisWeb = true;
            if (town.hasBuilding(BID::BS_WAR_SHRINE))      warShrine    = true;
            if (town.hasBuilding(BID::VK_VOID_LENS))       voidLens     = true;
            if (town.hasBuilding(BID::AM_MERGE_CHAMBER))   mergeChamber = true;
            if (town.hasBuilding(BID::CV_RESONANCE_WELL))  resonanceWell  = true;
            if (town.hasBuilding(BID::CV_MIRROR_CHAMBER))  mirrorChamber  = true;
            break;
        }
        if (lightP || bloodP || deathP || natureP || forgeP || fleshP
            || mechSpeed || holyDesp || eternalMonument
            || wardenBrand || symbiosisWeb || warShrine || voidLens
            || mergeChamber || resonanceWell || mirrorChamber)
            m_combat.applyPlayerTownBonus(lightP, bloodP, deathP, natureP, forgeP, fleshP,
                                          mechSpeed, holyDesp, eternalMonument,
                                          wardenBrand, symbiosisWeb, warShrine, voidLens,
                                          mergeChamber, resonanceWell, mirrorChamber);
    }

    m_combat.setLogCallback([](const std::string& msg) {
        gLog("[Combat] %s\n", msg.c_str());
    });

    // NegotiatedWeakness: Grave Diplomat reveals enemy specialty at battle start
    if (playerHero.negotiatedWeaknessSpecialty) {
        if (const HeroClassDef* eCls = m_classRegistry.getClass(enemyHero.classId)) {
            m_combat.pushLog("[Intel] Enemy is a " + eCls->name +
                             " — " + eCls->specialtyDesc);
        }
    }

    m_combatDmgEffects.clear();
    m_combat.setDamageCallback([this](uint32_t targetId, int dmg, HexCoord pos) {
        m_audio.playSound("hit");
        // Convert hex pos to board pixel pos
        float wx, wy;
        m_combat.grid().hexGrid().hexToWorld(pos, wx, wy);
        float sx = wx * m_combatBoardScale + m_combatBoardOffX;
        float sy = wy * m_combatBoardScale + m_combatBoardOffY;
        m_particles.emit(sx, sy, ParticlePreset::Hit);
        if (!m_settingsShowDmgNums) { (void)targetId; return; }
        m_combatDmgEffects.push_back({sx, sy, 1.5f, dmg, false});
        (void)targetId;
    });
    m_combat.setHealCallback([this](uint32_t targetId, int amount, HexCoord pos) {
        float wx, wy;
        m_combat.grid().hexGrid().hexToWorld(pos, wx, wy);
        float sx = wx * m_combatBoardScale + m_combatBoardOffX;
        float sy = wy * m_combatBoardScale + m_combatBoardOffY;
        m_particles.emit(sx, sy, ParticlePreset::Heal);
        if (m_settingsShowDmgNums)
            m_combatDmgEffects.push_back({sx, sy, 1.5f, amount, true});
        (void)targetId;
    });
    m_combat.setMoraleCallback([this](uint32_t unitId, HexCoord pos) {
        float wx, wy;
        m_combat.grid().hexGrid().hexToWorld(pos, wx, wy);
        float sx = wx * m_combatBoardScale + m_combatBoardOffX;
        float sy = wy * m_combatBoardScale + m_combatBoardOffY;
        m_particles.emit(sx, sy, ParticlePreset::Morale, 10);
        (void)unitId;
    });
    // Build sprite animators for every unit
    m_combatAnimators.clear();
    const auto& unitDefs = m_registry.units();
    for (const auto& u : m_combat.grid().units()) {
        SpriteAnimator anim;
        auto [fac, tier] = unitFactionTier(u, unitDefs);
        anim.faction = fac;
        anim.tier    = std::max(1, std::min(6, tier));
        anim.mirror  = !u.isPlayer;  // enemy faces left
        m_combatAnimators[u.id] = anim;
    }

    static int s_combatTrack = 0;
    s_combatTrack = (s_combatTrack % 4) + 1;
    char trackKey[20];
    std::snprintf(trackKey, sizeof(trackKey), "combat_music_%d", s_combatTrack);
    m_audio.playMusic(trackKey);
    gLog("Entered combat\n");
}

void Game::exitCombat(bool playerWon)
{
    gLog("Combat ended — %s\n", playerWon ? "Victory" : "Defeat/Retreat");
    ScriptContext ctx; ctx.heroId = m_heroes.empty() ? 0 : (int)m_heroes[m_activeHeroIdx].id;

    // Sync surviving units back to their hero armies
    if (!m_heroes.empty()) {
        Hero& hero = m_heroes[m_activeHeroIdx];
        hero.army.clear();
        for (const auto& cu : m_combat.grid().units()) {
            if (!cu.alive || cu.isPlayer == false || cu.count <= 0) continue;
            bool merged = false;
            for (auto& s : hero.army)
                if (s.defId == cu.defId) { s.count += cu.count; merged = true; break; }
            if (!merged) hero.army.push_back({cu.defId, cu.count});
        }
        gLog("Hero survivors: %zu stacks\n", hero.army.size());

        // Sync hero HP and mana from the combat engine's internal copy
        // (modified during battle by BloodPenance, Feast, SoulHarvest in-combat, Synthesis, etc.)
        const Hero& ch = m_combat.playerHero();
        hero.heroHp  = std::clamp(ch.heroHp,  1, hero.heroMaxHp);
        hero.mana    = std::clamp(ch.mana,     0, hero.maxMana);

        // Compute per-stack player losses for battle result display (before FIRST_AID inflates counts)
        m_combatUnitsLost.clear();
        for (const auto& startStack : m_battleStartArmy) {
            int survived = 0;
            for (const auto& s : hero.army)
                if (s.defId == startStack.defId) { survived = s.count; break; }
            int lost = startStack.count - survived;
            if (lost > 0) {
                BattleUnitRecord rec;
                rec.defId = startStack.defId;
                rec.count = lost;
                const UnitDef* ud = m_registry.getUnitDef(startStack.defId);
                if (ud) { rec.name = ud->name; rec.faction = static_cast<int>(ud->faction); rec.tier = ud->tier; }
                else    { rec.name = "Unknown"; }
                m_combatUnitsLost.push_back(rec);
            }
        }

        // Apply FIRST_AID: restore % of casualties from each stack
        if (playerWon) {
            if (const SkillInstance* s = hero.skills.getSkill(SkillID::FIRST_AID)) {
                if (const SkillDef* def = findSkillDef(SkillID::FIRST_AID)) {
                    int healPct = def->values[static_cast<int>(s->tier)];
                    for (auto& stack : hero.army) {
                        int startCount = 0;
                        for (const auto& bs : m_battleStartArmy)
                            if (bs.defId == stack.defId) { startCount = bs.count; break; }
                        int lost   = std::max(0, startCount - stack.count);
                        int healed = lost * healPct / 100;
                        stack.count += healed;
                        if (healed > 0)
                            gLog("First Aid: restored %d %s\n", healed, "units");
                    }
                }
            }

            // Apply NECROMANCY: raise % of killed enemies as Skeletons (defId 2001)
            if (const SkillInstance* s = hero.skills.getSkill(SkillID::NECROMANCY)) {
                if (const SkillDef* def = findSkillDef(SkillID::NECROMANCY)) {
                    int necroRaise = m_combat.enemyStartCount()
                                   * def->values[static_cast<int>(s->tier)] / 100;
                    if (necroRaise > 0) {
                        constexpr int SKELETON_DEF_ID = 2001;
                        bool merged = false;
                        for (auto& stack : hero.army)
                            if (stack.defId == SKELETON_DEF_ID) {
                                stack.count += necroRaise; merged = true; break;
                            }
                        if (!merged && hero.army.size() < 7)
                            hero.army.push_back({SKELETON_DEF_ID, necroRaise});
                        char necroBuf[48];
                        std::snprintf(necroBuf, sizeof(necroBuf),
                            "+%d Skeletons (Necromancy)", necroRaise);
                        pushPickupEffect(hero.pos, necroBuf, IM_COL32(180, 220, 255, 255));
                        gLog("Necromancy: raised %d skeletons\n", necroRaise);
                    }
                }
            }

            // Necropolis Gate (EE_NECROPOLIS building): 15% of killed enemies rise as Conscripts
            for (const auto& town : m_towns) {
                if (town.ownerId != static_cast<uint32_t>(currentPlayerId()) || town.pos != hero.pos) continue;
                if (!town.hasBuilding(BID::EE_NECROPOLIS)) break;
                int risen = m_combat.enemyStartCount() * 15 / 100;
                if (risen > 0) {
                    constexpr int CONSCRIPT_DEF_ID = 4001;
                    bool merged = false;
                    for (auto& stack : hero.army)
                        if (stack.defId == CONSCRIPT_DEF_ID) {
                            stack.count += risen; merged = true; break;
                        }
                    if (!merged && hero.army.size() < 7)
                        hero.army.push_back({CONSCRIPT_DEF_ID, risen});
                    char riseBuf[52];
                    std::snprintf(riseBuf, sizeof(riseBuf), "+%d Conscripts (Necropolis)", risen);
                    pushPickupEffect(hero.pos, riseBuf, IM_COL32(180, 220, 255, 255));
                }
                break;
            }
        }
    }

    if (playerWon) {
        // Record before any clearing — used by Predator specialty below
        const bool killedEnemyHero = (m_lastCombatEnemyId != 0);

        // Capture town if this was a garrison fight
        Town* captured = nullptr;
        if (m_pendingTownCaptureId != 0) {
            for (auto& t : m_towns)
                if (t.id == m_pendingTownCaptureId) { captured = &t; break; }
            if (captured) {
                uint32_t prevOwner = captured->ownerId;
                captured->ownerId = 1;
                captured->garrison.clear();
                m_capturedTownName = captured->name;
                m_showCapturePopup = true;
                m_hideout.completeMilestone(Milestone::FIRST_TOWN_CAPTURED);
                gLog("Captured town after garrison fight: %s\n", m_capturedTownName.c_str());
                ScriptContext townCtx;
                townCtx.townId = captured->id;
                m_triggers.fire(TriggerType::TownCaptured, townCtx);
                m_campaign.onTownCaptured(captured->id, prevOwner);
            }
            m_pendingTownCaptureId = 0;
        }

        // Bandit camp / chokepoint guard reward
        if (m_lastBanditCampId != 0) {
            for (auto& obj : m_worldObjects) {
                if (obj.id != m_lastBanditCampId) continue;
                int reward = 0;
                if (obj.type == WorldObjectType::BanditCamp)
                    reward = 200 * obj.value;
                else if (obj.type == WorldObjectType::ChokeGuard)
                    reward = 3000;
                if (reward > 0) {
                    m_playerResources.add(ResourceType::Gold, reward);
                    char campBuf[48];
                    std::snprintf(campBuf, sizeof(campBuf), "+%d Gold!", reward);
                    pushPickupEffect(obj.pos, campBuf, IM_COL32(255, 215, 50, 255));
                    gLog("Guard cleared! Reward: %d gold\n", reward);
                }
                obj.collected = true;
                break;
            }
            m_lastBanditCampId = 0;
        }

        // Crypt reward popup — do NOT zero m_pendingCryptId here; the popup render
        // function needs it to find the WorldObject. It is cleared on popup close.
        if (m_pendingCryptId != 0) {
            for (auto& o : m_worldObjects) {
                if (o.id == m_pendingCryptId && !o.collected) {
                    // Pick a bonus spell (prefer one the hero doesn't know yet)
                    if (!m_heroes.empty()) {
                        const Hero& h = m_heroes[m_activeHeroIdx];
                        int bonus = 1 + static_cast<int>(o.value % 8);
                        for (int sid : h.knownSpells)
                            if (sid == bonus) { bonus = (bonus % 8) + 1; break; }
                        o.questState = bonus;
                    } else {
                        o.questState = 1 + static_cast<int>(o.value % 8);
                    }
                    m_showCryptPopup = true;
                    break;
                }
            }
            // Only clear if no popup was shown (object already collected or missing)
            if (!m_showCryptPopup) m_pendingCryptId = 0;
        }

        // Utopia reward popup — do NOT zero m_pendingUtopiaId here; cleared on popup close.
        if (m_pendingUtopiaId != 0) {
            m_showUtopiaPopup = true;
        }

        // Mine guard beaten — mark and immediately capture
        if (m_pendingMineId != 0) {
            for (auto& r : m_resources) {
                if (r.id == m_pendingMineId) {
                    r.guardBeaten = true;
                    r.ownedBy = static_cast<uint32_t>(currentPlayerId());
                    m_playerResources.add(r.type, r.amount);
                    m_cachedWeeklyIncome.add(r.type, r.amount);
                    char mineBuf[48];
                    std::snprintf(mineBuf, sizeof(mineBuf), "+%d %s/week", r.amount, resourceName(r.type));
                    if (!m_heroes.empty())
                        pushPickupEffect(m_heroes[m_activeHeroIdx].pos, mineBuf, IM_COL32(255, 220, 80, 255));
                    m_audio.playSound("buy");
                    gLog("Captured mine after combat: +%d %s/week\n", r.amount, resourceName(r.type));
                    break;
                }
            }
            m_pendingMineId = 0;
        }

        // Neutral Outpost captured — only mark collected on player win
        if (m_pendingNeutralOutpostId != 0) {
            for (auto& o : m_worldObjects)
                if (o.id == m_pendingNeutralOutpostId) {
                    o.collected = true;
                    o.linkedId  = static_cast<uint32_t>(currentPlayerId());
                    break;
                }
            m_pendingNeutralOutpostId = 0;
        }

        // Remove defeated enemy hero from the world
        if (m_lastCombatEnemyId != 0) {
            // Loot the defeated hero — gold scales with enemy army strength
            {
                int lootGold = 100 + m_combat.xpEarned() * 3;
                m_playerResources.add(ResourceType::Gold, lootGold);
                // Find the hero position for the pickup effect
                for (const auto& eh : m_enemyHeroes) {
                    if (eh.id == m_lastCombatEnemyId) {
                        char lootBuf[32];
                        std::snprintf(lootBuf, sizeof(lootBuf), "+%d Gold (loot)", lootGold);
                        pushPickupEffect(eh.pos, lootBuf, IM_COL32(255, 215, 50, 255));
                        break;
                    }
                }
                gLog("Enemy hero looted: %d gold\n", lootGold);
            }
            // Release all mines owned by the defeated hero
            for (auto& r : m_resources)
                if (r.ownedBy == m_lastCombatEnemyId) r.ownedBy = 0;
            uint32_t defeatedId = m_lastCombatEnemyId;
            m_enemyHeroes.erase(
                std::remove_if(m_enemyHeroes.begin(), m_enemyHeroes.end(),
                    [&](const Hero& e){ return e.id == defeatedId; }),
                m_enemyHeroes.end());
            m_map.forEach([&](HexTile& t){
                if (t.heroId == defeatedId) t.heroId = 0;
            });
            m_campaign.onHeroDefeated(defeatedId);
            m_lastCombatEnemyId = 0;
        }

        m_hideout.addXP(50);
        m_hideout.completeMilestone(Milestone::FIRST_BATTLE_WON);
        m_triggers.fire(TriggerType::BattleWon, ctx);
        {
            bool noEnemyHeroes = m_enemyHeroes.empty();
            bool noEnemyTowns  = true;
            for (const auto& t : m_towns)
                if (t.ownerId > static_cast<uint32_t>(m_numHumanPlayers)) { noEnemyTowns = false; break; }
            if (noEnemyHeroes && noEnemyTowns) {
                if (m_numHumanPlayers == 2)
                    m_victoryMessage = "Player " + std::to_string(currentPlayerId())
                                     + " wins! All AI enemies defeated.";
                else
                    m_victoryMessage = "Victory! All enemies have been defeated.";
                m_showVictory = true;
                m_audio.playSound("victory");
            }
        }
        // Check if any other human player was just eliminated
        if (!m_showVictory && m_numHumanPlayers >= 2) {
            for (int pi = 0; pi < m_numHumanPlayers && !m_showVictory; ++pi) {
                if (pi == m_currentPlayerIdx) continue;
                uint32_t otherId = static_cast<uint32_t>(pi + 1);
                bool otherHasHeroes = !m_players[pi].heroes.empty();
                bool otherHasTowns  = false;
                for (const auto& t : m_towns)
                    if (t.ownerId == otherId) { otherHasTowns = true; break; }
                if (!otherHasHeroes && !otherHasTowns) {
                    m_victoryMessage = "Player " + std::to_string(currentPlayerId())
                                     + " wins! Player " + std::to_string(pi + 1)
                                     + " has been eliminated.";
                    m_showVictory = true;
                    m_audio.playSound("victory");
                }
            }
        }

        // Award hero XP
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            int xp = m_combat.xpEarned();
            if (xp > 0) {
                char xpBuf[32];
                std::snprintf(xpBuf, sizeof(xpBuf), "+%d XP", xp);
                pushPickupEffect(hero.pos, xpBuf, IM_COL32(160, 255, 160, 255));
            }
            gLog("Hero earns %d XP\n", xp);
            int oldLevel = hero.level;
            if (hero.addXp(xp)) {
                int levelsGained = hero.level - oldLevel;
                gLog("Hero leveled up to %d! (%d levels gained)\n", hero.level, levelsGained);
                if (hero.level >= 5)  m_hideout.completeMilestone(Milestone::HERO_LEVEL_5);
                if (hero.level >= 10) m_hideout.completeMilestone(Milestone::HERO_LEVEL_10);
                ScriptContext lvlCtx; lvlCtx.heroId = hero.id;
                m_triggers.fire(TriggerType::HeroLevel, lvlCtx);
                const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
                if (cls) {
                    std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
                    m_levelUpOffers = LevelUpSystem::generateOffers(
                        *cls, hero.skills, hero.level, allSkills, hero.faction);
                }
                if (m_levelUpOffers.empty())
                    m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
                m_pendingLevelUps = levelsGained;
                m_showLevelUpModal = true;
            }
        }

        // Apply hero specialties on victory
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
            if (cls) {
                // SoulHarvest (Death Herald): restore hero HP = enemies_killed × 3
                if (cls->specialty == SpecialtyType::SoulHarvest) {
                    int heal = m_combat.enemyStartCount() * 3;
                    hero.heroHp = std::min(hero.heroMaxHp, hero.heroHp + heal);
                    if (heal > 0) {
                        char buf[48];
                        std::snprintf(buf, sizeof(buf), "+%d Hero HP (Soul Harvest)", heal);
                        pushPickupEffect(hero.pos, buf, IM_COL32(180, 255, 200, 255));
                    }
                }
                // Veteran (Crusader): +1 ATK+DEF per battle won, max +5 each
                if (cls->specialty == SpecialtyType::Veteran) {
                    hero.battlesWon++;
                    if (hero.specialtyAtk < 5) {
                        hero.specialtyAtk++;
                        hero.attack++;
                        hero.defense++;
                        char buf[48];
                        std::snprintf(buf, sizeof(buf), "+1 ATK+DEF (Veteran, total %d)", hero.specialtyAtk);
                        pushPickupEffect(hero.pos, buf, IM_COL32(255, 200, 80, 255));
                    }
                }
                // Predator (Assassin Lord): permanent +1 attack for each enemy hero killed
                if (cls->specialty == SpecialtyType::Predator && killedEnemyHero) {
                    if (hero.specialtyAtk < 10) {
                        hero.specialtyAtk++;
                        hero.attack++;
                        char buf[44];
                        std::snprintf(buf, sizeof(buf), "+1 ATK (Predator, total %d)", hero.specialtyAtk);
                        pushPickupEffect(hero.pos, buf, IM_COL32(255, 100, 100, 255));
                    }
                }
                // Recycler (Salvage Lord): army gains permanent +1 ATK per battle won (max 5)
                if (cls->specialty == SpecialtyType::Recycler) {
                    if (hero.recyclerBonus < 5) {
                        hero.recyclerBonus++;
                        char buf[52];
                        std::snprintf(buf, sizeof(buf), "+1 ATK to all units (Recycler, total %d)",
                                      hero.recyclerBonus);
                        pushPickupEffect(hero.pos, buf, IM_COL32(180, 200, 100, 255));
                    }
                }
                // LivingRune (Runesmith): hero gains permanent +1 ATK and +1 DEF per battle won (max 5)
                if (cls->specialty == SpecialtyType::LivingRune) {
                    if (hero.livingRuneBonus < 5) {
                        hero.livingRuneBonus++;
                        hero.attack++;
                        hero.defense++;
                        char buf[56];
                        std::snprintf(buf, sizeof(buf), "+1 ATK/DEF (Living Rune, tier %d)",
                                      hero.livingRuneBonus);
                        pushPickupEffect(hero.pos, buf, IM_COL32(255, 210, 80, 255));
                    }
                }
            }
        }

        // Combat result summary — collect stats before transitioning
        if (!captured) {
            int startCount = 0, survivingCount = 0;
            for (const auto& bs : m_battleStartArmy) startCount += bs.count;
            if (!m_heroes.empty())
                for (const auto& s : m_heroes[m_activeHeroIdx].army) survivingCount += s.count;
            m_combatResultWon   = true;
            m_combatResultXp    = m_combat.xpEarned();
            m_combatResultKills = m_combat.enemyStartCount();
            m_combatResultLost  = std::max(0, startCount - survivingCount);
            m_combatResultGold  = killedEnemyHero ? 100 + m_combat.xpEarned() * 3 : 0;
            m_showCombatResult  = true;
        }

        // After a garrison victory, drop the player into the captured town
        if (captured) {
            enterTown(captured);
            return;
        }
    } else {
        // Sync surviving enemy army too (they won, they keep what's left)
        for (auto& eh : m_enemyHeroes) {
            if (eh.id != m_lastCombatEnemyId) continue;
            eh.army.clear();
            for (const auto& cu : m_combat.grid().units()) {
                if (!cu.alive || cu.isPlayer || cu.count <= 0) continue;
                bool merged = false;
                for (auto& s : eh.army)
                    if (s.defId == cu.defId) { s.count += cu.count; merged = true; break; }
                if (!merged) eh.army.push_back({cu.defId, cu.count});
            }
            break;
        }
        m_pendingTownCaptureId      = 0;
        m_lastBanditCampId          = 0;
        m_pendingCryptId            = 0;
        m_pendingUtopiaId           = 0;
        m_pendingMineId             = 0;
        m_pendingNeutralOutpostId   = 0;
        m_triggers.fire(TriggerType::BattleLost, ctx);

        // Phylactery (Lich): escape one defeat — hero returns at half stats
        bool phylacteryEscape = false;
        if (!m_heroes.empty()) {
            Hero& hero = m_heroes[m_activeHeroIdx];
            if (hero.phylacterySpecialty && !hero.phylacteryUsed) {
                hero.phylacteryUsed = true;
                hero.attack  = std::max(1, hero.attack  / 2);
                hero.defense = std::max(1, hero.defense / 2);
                hero.mana    = hero.maxMana / 2;
                hero.heroHp  = std::max(1, hero.heroMaxHp / 2);
                pushPickupEffect(hero.pos, "Phylactery: escaped death at half stats!",
                                 IM_COL32(180, 140, 255, 255));
                gLog("Phylactery: hero survived defeat at half stats\n");
                phylacteryEscape = true;
            }
        }
        if (!phylacteryEscape) {
            // Remove the defeated hero from the world map and make them hireable in the tavern
            if (!m_heroes.empty()) {
                Hero& defeated = m_heroes[m_activeHeroIdx];
                defeated.army.clear(); // no army after defeat
                m_map.forEach([&](HexTile& t){
                    if (t.heroId == defeated.id) t.heroId = 0;
                });
                m_players[m_currentPlayerIdx].defeatedPool.push_back(defeated);
                m_heroes.erase(m_heroes.begin() + m_activeHeroIdx);
                m_activeHeroIdx = m_heroes.empty() ? 0 : std::min(m_activeHeroIdx, (int)m_heroes.size() - 1);
            }
            // Show defeat combat result summary
            m_combatResultWon   = false;
            m_combatResultXp    = 0;
            m_combatResultKills = m_combat.enemyStartCount() - m_combat.enemiesAlive();
            m_combatResultLost  = m_battleStartArmy.empty() ? 0
                : [&](){
                    int start = 0;
                    for (const auto& bs : m_battleStartArmy) start += bs.count;
                    return start;
                }();
            m_combatResultGold  = 0;
            m_showCombatResult  = true;
            if (m_state == GameState::Campaign)
                m_campaign.onHeroDefeated(0); // 0 = player hero (triggers DefeatHero check)
            // Check for unrecoverable defeat: no heroes with armies, no player towns
            {
                bool anyUnit = false;
                for (const auto& h : m_heroes)
                    if (!h.army.empty()) { anyUnit = true; break; }
                bool anyTown = false;
                for (const auto& t : m_towns)
                    if (t.ownerId == static_cast<uint32_t>(currentPlayerId())) { anyTown = true; break; }
                m_finalDefeat = !anyUnit && !anyTown;
            }
            // Only show the defeat screen when it's truly unrecoverable
            m_showDefeat = m_finalDefeat;
            if (m_state == GameState::Campaign && m_finalDefeat)
                m_campaign.triggerMissionLoss();
            m_audio.playSound("hit");
        }
    }
    m_audio.playMusic("worldmap_music");
    if (m_fromBattleSim && m_watchingAI) {
        m_fromBattleSim = false;
        m_simAutoPlay   = false;
        enterWorldMap();
    } else if (m_fromBattleSim) {
        m_fromBattleSim = false;
        m_state         = GameState::MainMenu;
        m_menuMode      = 5;
    } else if (m_prevState == GameState::Campaign) {
        m_state = GameState::Campaign;
    } else {
        enterWorldMap();
    }
}
