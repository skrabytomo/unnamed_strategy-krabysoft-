#include "CombatHUD.h"
#include <sstream>
#include <algorithm>

bool CombatHUD::init(int sw, int sh)
{
    buildLayout(sw, sh);
    return true;
}

void CombatHUD::resize(int sw, int sh)
{
    m_screenW = sw; m_screenH = sh;
    buildLayout(sw, sh);
}

void CombatHUD::buildLayout(int sw, int sh)
{
    float bh = 130.0f;  // bottom HUD height
    float by = sh - bh;

    m_bottomBar = {0, by, (float)sw, bh};

    // Active unit panel — left
    m_activeUnitPanel = Panel({0, by, 220.0f, bh});
    m_activeUnitPanel.title = "Active Unit";

    // Targeted unit panel
    m_targetUnitPanel = Panel({224.0f, by, 220.0f, bh});
    m_targetUnitPanel.title = "Target";

    // Log panel — center
    m_logPanel = Panel({448.0f, by, (float)sw - 448.0f - 180.0f, bh});
    m_logPanel.title = "Combat Log";

    // Action panel — right
    float ax = (float)sw - 176.0f;
    m_actionPanel = Panel({ax, by, 176.0f, bh});
    m_actionPanel.title = "Actions";

    float btnY = by + 30.0f;
    m_waitBtn    = Button("Wait",    {ax+8, btnY,      160.0f, 26.0f}, [this]{ if(onWait)     onWait();    });
    m_defendBtn  = Button("Defend",  {ax+8, btnY+30.0f,160.0f, 26.0f}, [this]{ if(onDefend)   onDefend();  });
    m_spellsBtn  = Button("Spells",  {ax+8, btnY+60.0f,160.0f, 26.0f}, [this]{ if(onSpells)   onSpells();  });
    m_retreatBtn = Button("Retreat", {ax+8, btnY+90.0f,160.0f, 26.0f}, [this]{ if(onEndCombat) onEndCombat(); });

    m_retreatBtn.colorBorder = UIColor::hex(UITheme::DANGER_RED, 0.7f);
    m_retreatBtn.colorText   = UIColor::hex(UITheme::DANGER_RED);

    // Turn order bar — top
    m_turnOrderBar = {0, 0, (float)sw, 44.0f};
}

void CombatHUD::draw(UIRenderer& rdr, const CombatEngine& engine)
{
    drawTurnOrder(rdr, engine);
    drawHeroInfo(rdr, engine);

    // Bottom HUD background
    rdr.drawRect(m_bottomBar,
        UIColor::hex(UITheme::BG_PANEL_DARK, 0.95f),
        UIColor::hex(UITheme::BORDER), 1.0f);

    auto* active = const_cast<CombatEngine&>(engine).activeUnit();
    drawUnitInfo(rdr, active, true);
    drawUnitInfo(rdr, m_hoveredUnit, false);
    drawCombatLog(rdr, engine);
    drawActionBar(rdr);

    // Phase display
    std::string phaseStr;
    switch (engine.phase()) {
        case CombatPhase::PlayerTurn: phaseStr = "YOUR TURN"; break;
        case CombatPhase::EnemyTurn:  phaseStr = "ENEMY TURN"; break;
        case CombatPhase::Victory:    phaseStr = "VICTORY!";   break;
        case CombatPhase::Defeat:     phaseStr = "DEFEAT";     break;
        default: phaseStr = "..."; break;
    }
    UIColor phaseColor = (engine.phase() == CombatPhase::Victory) ?
        UIColor::hex(UITheme::GOLD) :
        (engine.phase() == CombatPhase::Defeat) ?
        UIColor::hex(UITheme::DANGER_RED) :
        UIColor::hex(UITheme::TEXT_SECONDARY);

    float tx = (m_screenW - phaseStr.size() * 8.0f) * 0.5f;
    rdr.drawText(phaseStr, tx, 48.0f, phaseColor, 14.0f);

    m_tooltip.draw(rdr);
}

void CombatHUD::drawUnitInfo(UIRenderer& rdr, const CombatUnit* unit, bool isActive)
{
    Panel& panel = isActive ? m_activeUnitPanel : m_targetUnitPanel;
    panel.draw(rdr);
    if (!unit) return;

    float x = panel.bounds.x + 8.0f;
    float y = panel.bounds.y + 28.0f;
    float w = panel.bounds.w - 16.0f;

    // Unit name + count
    std::string header = unit->name + " x" + std::to_string(unit->count);
    UIColor nameColor = unit->isPlayer ?
        UIColor::hex(UITheme::NATURE_GREEN) :
        UIColor::hex(UITheme::BLOOD_RED);
    rdr.drawText(header, x, y, nameColor, 12.0f);
    y += 16.0f;

    // HP bar
    float hpFrac = unit->maxHp > 0 ?
        static_cast<float>(unit->hp) / unit->maxHp : 0.0f;
    UIColor hpColor = hpFrac > 0.5f ? UIColor::hex(UITheme::HP_GREEN) :
                      hpFrac > 0.25f ? UIColor::hex(UITheme::MORALE_GOLD) :
                                       UIColor::hex(UITheme::HP_LOW);
    rdr.drawText("HP: " + std::to_string(unit->hp) + "/" + std::to_string(unit->maxHp),
                 x, y, UIColor::hex(UITheme::TEXT_SECONDARY), 11.0f);
    y += 13.0f;
    rdr.drawBar({x, y, w, 6.0f}, hpFrac, hpColor,
                UIColor::hex(UITheme::BG_DARK), UIColor::hex(UITheme::BORDER));
    y += 10.0f;

    // Morale bar (if not immune)
    if (!unit->moraleImmune) {
        float moraleFrac = unit->morale / 100.0f;
        UIColor moraleColor = unit->morale >= 80 ? UIColor::rgba(1.0f, 0.85f, 0.2f)   // gold — high
                            : unit->morale < 20  ? UIColor::rgba(0.9f, 0.25f, 0.25f)  // red — fear
                                                 : UIColor::hex(UITheme::MORALE_GOLD);
        std::string moraleLabel = "Morale " + std::to_string(unit->morale);
        if (unit->morale >= 80) moraleLabel += " [+surge]";
        else if (unit->morale < 20) moraleLabel += " [!fear]";
        rdr.drawText(moraleLabel, x, y, moraleColor, 11.0f);
        y += 13.0f;
        rdr.drawBar({x, y, w, 5.0f}, moraleFrac, moraleColor,
                    UIColor::hex(UITheme::BG_DARK),
                    UIColor::hex(UITheme::BORDER));
        y += 9.0f;
    }

    // Desperation meter for Holy units
    if (hasTag(unit->tags, UnitTag::Holy) && unit->desperationMeter > 0) {
        float despFrac = unit->desperationMeter / 100.0f;
        std::string despStr = "Desperation: " + std::to_string(unit->desperationMeter) + "/100";
        UIColor despCol = unit->desperationMeter >= 100
            ? UIColor::rgba(1.0f, 0.5f, 0.1f)   // full — orange glow
            : UIColor::rgba(0.9f, 0.8f, 0.3f);   // charging — gold
        rdr.drawText(despStr, x, y, despCol, 11.0f);
        y += 13.0f;
        rdr.drawBar({x, y, w, 5.0f}, despFrac,
                    despCol, UIColor::hex(UITheme::BG_DARK), UIColor::hex(UITheme::BORDER));
        y += 9.0f;
    }

    // Stats
    std::string stats = "ATK:" + std::to_string(unit->attack) +
                        " DEF:" + std::to_string(unit->defense) +
                        " SPD:" + std::to_string(unit->speed);
    rdr.drawText(stats, x, y, UIColor::hex(UITheme::TEXT_SECONDARY), 11.0f);
    y += 14.0f;

    // Active buff/debuff status
    bool hasBuff = unit->roundAttackBonus != 0 || unit->roundDefenseBonus != 0;
    if (hasBuff) {
        std::string buffStr;
        if (unit->roundAttackBonus > 0)       buffStr += "ATK+" + std::to_string(unit->roundAttackBonus) + " ";
        else if (unit->roundAttackBonus < 0)   buffStr += "ATK" + std::to_string(unit->roundAttackBonus) + " ";
        if (unit->roundDefenseBonus > 0)       buffStr += "DEF+" + std::to_string(unit->roundDefenseBonus);
        else if (unit->roundDefenseBonus < 0)  buffStr += "DEF" + std::to_string(unit->roundDefenseBonus);
        UIColor buffCol = (unit->roundAttackBonus > 0 || unit->roundDefenseBonus > 0)
                          ? UIColor::hex(UITheme::MORALE_GOLD) : UIColor::hex(UITheme::BLOOD_RED);
        rdr.drawText(buffStr, x, y, buffCol, 11.0f);
        y += 13.0f;
    }

    // DoT status
    if (unit->poisonRounds > 0) {
        std::string ps = "Poison: " + std::to_string(unit->poisonDamage)
                       + "/rnd (" + std::to_string(unit->poisonRounds) + ")";
        rdr.drawText(ps, x, y, UIColor::rgba(0.31f, 0.86f, 0.31f), 11.0f);
        y += 13.0f;
    }
    if (unit->burnRounds > 0) {
        std::string bs = "Burn: " + std::to_string(unit->burnDamage)
                       + "/rnd (" + std::to_string(unit->burnRounds) + ")";
        rdr.drawText(bs, x, y, UIColor::rgba(1.0f, 0.47f, 0.16f), 11.0f);
        y += 13.0f;
    }

    // Special abilities
    if (unit->vampiric)
        rdr.drawText("Vampiric", x, y, UIColor::rgba(0.6f, 0.0f, 0.9f), 11.0f), y += 13.0f;
    if (unit->regenerates)
        rdr.drawText("Regenerates", x, y, UIColor::rgba(0.2f, 0.9f, 0.5f), 11.0f), y += 13.0f;
    if (unit->luck > 0)
        rdr.drawText("Luck: " + std::to_string(unit->luck),
                     x, y, UIColor::rgba(1.0f, 0.9f, 0.0f), 11.0f), y += 13.0f;

    // OrganicMech adaptation count
    if (hasTag(unit->tags, UnitTag::OrganicMech) && unit->adaptationsGained > 0) {
        std::string adaptStr = "Adapted: " + std::to_string(unit->adaptationsGained) + "/6";
        UIColor adaptCol = unit->adaptationsGained >= 6
            ? UIColor::rgba(0.3f, 1.0f, 0.8f)   // max — teal glow
            : UIColor::rgba(0.5f, 0.9f, 0.6f);   // growing — mint
        rdr.drawText(adaptStr, x, y, adaptCol, 11.0f);
        float adaptFrac = unit->adaptationsGained / 6.0f;
        y += 13.0f;
        rdr.drawBar({x, y, w, 5.0f}, adaptFrac,
                    adaptCol, UIColor::hex(UITheme::BG_DARK), UIColor::hex(UITheme::BORDER));
        y += 9.0f;
    }

    // Shots remaining
    if (unit->range > 0) {
        rdr.drawText("Shots: " + std::to_string(unit->shotsLeft),
                     x, y, UIColor::hex(UITheme::MANA_BLUE), 11.0f);
    }
}

void CombatHUD::drawHeroInfo(UIRenderer& rdr, const CombatEngine& engine)
{
    // Right side of the turn order bar — two hero stat strips
    const Hero& ph = engine.playerHero();
    const Hero& eh = engine.enemyHero();

    float panelW = 310.0f;
    float px = m_screenW - panelW - 4.0f;
    float py = 2.0f;
    float barW = 130.0f;

    auto drawHeroStrip = [&](const Hero& h, float oy, UIColor nameCol) {
        float tx = px;
        float ty = py + oy;

        // Name
        rdr.drawText(h.name, tx, ty, nameCol, 11.0f);
        tx += 90.0f;

        // HP bar
        float hpFrac = h.heroMaxHp > 0
            ? static_cast<float>(h.heroHp) / h.heroMaxHp : 1.0f;
        UIColor hpCol = hpFrac > 0.5f ? UIColor::hex(UITheme::HP_GREEN) :
                        hpFrac > 0.25f ? UIColor::hex(UITheme::MORALE_GOLD) :
                                         UIColor::hex(UITheme::HP_LOW);
        rdr.drawText("HP", tx, ty, UIColor::hex(UITheme::TEXT_SECONDARY), 10.0f);
        tx += 18.0f;
        rdr.drawBar({tx, ty+1.0f, barW * 0.45f, 8.0f}, hpFrac,
                    hpCol, UIColor::hex(UITheme::BG_DARK), UIColor::hex(UITheme::BORDER));
        tx += barW * 0.45f + 4.0f;

        // Mana bar
        float mpFrac = h.maxMana > 0
            ? static_cast<float>(h.mana) / h.maxMana : 1.0f;
        rdr.drawText("MP", tx, ty, UIColor::hex(UITheme::TEXT_SECONDARY), 10.0f);
        tx += 18.0f;
        rdr.drawBar({tx, ty+1.0f, barW * 0.45f, 8.0f}, mpFrac,
                    UIColor::hex(UITheme::MANA_BLUE),
                    UIColor::hex(UITheme::BG_DARK),
                    UIColor::hex(UITheme::BORDER));
        tx += barW * 0.45f + 4.0f;

        // Numeric mana
        std::string mpStr = std::to_string(h.mana) + "/" + std::to_string(h.maxMana);
        rdr.drawText(mpStr, tx, ty, UIColor::hex(UITheme::MANA_BLUE), 10.0f);
    };

    drawHeroStrip(ph, 5.0f,  UIColor::hex(UITheme::NATURE_GREEN));
    drawHeroStrip(eh, 23.0f, UIColor::hex(UITheme::BLOOD_RED));
}

void CombatHUD::drawTurnOrder(UIRenderer& rdr, const CombatEngine& engine)
{
    rdr.drawRect(m_turnOrderBar,
        UIColor::hex(UITheme::BG_PANEL_DARK, 0.92f),
        UIColor::hex(UITheme::BORDER), 1.0f);

    auto& order = engine.turnOrder();
    float x     = 6.0f;
    float y     = 3.0f;
    float slotW = 52.0f;
    float slotH = 38.0f;
    float barH  = 4.0f;  // HP bar height at bottom of slot

    // "QUEUE" label
    rdr.drawText("QUEUE", 6.0f, y + 13.0f,
                 UIColor::hex(UITheme::TEXT_SECONDARY, 0.6f), 9.0f);
    x = 58.0f;

    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        if (x + slotW > static_cast<float>(m_screenW) - 200.0f) break;

        const CombatUnit* u = engine.grid().getUnit(order[i]);
        if (!u || !u->alive) continue;

        bool isActive = (i == engine.turnIndex());
        UIColor bg  = u->isPlayer
            ? UIColor::hex(UITheme::NATURE_GREEN, isActive ? 0.85f : 0.35f)
            : UIColor::hex(UITheme::BLOOD_RED,    isActive ? 0.85f : 0.35f);
        UIColor brd = isActive
            ? UIColor::hex(UITheme::GOLD)
            : UIColor::hex(UITheme::BORDER, 0.7f);

        rdr.drawRect({x, y, slotW, slotH}, bg, brd, isActive ? 2.0f : 1.0f);

        // Unit name — first 6 chars to fit
        std::string abbr = u->name.substr(0, 6);
        rdr.drawText(abbr, x + 2.0f, y + 2.0f,
                     UIColor::hex(UITheme::TEXT_PRIMARY, isActive ? 1.0f : 0.85f), 9.0f);

        // Count and speed on second row
        char info[20];
        std::snprintf(info, sizeof(info), "x%d S%d", u->count, u->speed);
        rdr.drawText(info, x + 2.0f, y + 14.0f,
                     UIColor::hex(UITheme::TEXT_SECONDARY), 9.0f);

        // HP bar across the bottom of the slot
        float hpFrac = (u->maxHp > 0)
            ? static_cast<float>(u->totalHp()) / static_cast<float>(u->count * u->maxHp)
            : 0.0f;
        hpFrac = std::max(0.0f, std::min(1.0f, hpFrac));
        float barY = y + slotH - barH - 1.0f;
        rdr.drawRect({x + 1.0f, barY, slotW - 2.0f, barH},
                     UIColor::rgba(0.2f, 0.2f, 0.2f, 0.8f),
                     UIColor::rgba(0,0,0,0), 0.0f);
        if (hpFrac > 0.0f)
            rdr.drawRect({x + 1.0f, barY, (slotW - 2.0f) * hpFrac, barH},
                         UIColor::rgba(0.2f, 0.85f, 0.3f, 0.9f),
                         UIColor::rgba(0,0,0,0), 0.0f);

        // Active arrow indicator above slot
        if (isActive)
            rdr.drawText("v", x + slotW * 0.5f - 3.0f, y - 2.0f,
                         UIColor::hex(UITheme::GOLD), 10.0f);

        x += slotW + 3.0f;
    }

    // Round counter — right side
    char rndBuf[24];
    std::snprintf(rndBuf, sizeof(rndBuf), "Round %d", engine.round());
    rdr.drawText(rndBuf, static_cast<float>(m_screenW) - 96.0f, 14.0f,
                 UIColor::hex(UITheme::TEXT_SECONDARY), 12.0f);
}

void CombatHUD::drawCombatLog(UIRenderer& rdr, const CombatEngine& engine)
{
    m_logPanel.draw(rdr);

    auto& log = engine.log();
    float x = m_logPanel.bounds.x + 6.0f;
    float y = m_logPanel.bounds.y + 28.0f;
    float maxY = m_logPanel.bounds.bottom() - 8.0f;

    // Show last N entries that fit
    int lineH = 14;
    int maxLines = static_cast<int>((maxY - y) / lineH);
    int start = std::max(0, static_cast<int>(log.size()) - maxLines);

    for (int i = start; i < static_cast<int>(log.size()) && y < maxY; ++i) {
        const auto& msg = log[i].message;
        UIColor c = UIColor::hex(UITheme::TEXT_SECONDARY);
        // Round separator
        if (msg.find("===") != std::string::npos || msg.find("Round") != std::string::npos)
            c = UIColor::rgba(0.7f, 0.7f, 0.9f);
        else if (msg.find("VICTORY") != std::string::npos)
            c = UIColor::hex(UITheme::GOLD);
        else if (msg.find("DEFEAT") != std::string::npos)
            c = UIColor::hex(UITheme::DANGER_RED);
        else if (msg.find("LUCKY") != std::string::npos)
            c = UIColor::rgba(1.0f, 0.95f, 0.2f);     // bright yellow for lucky hit
        else if (msg.find("spell") != std::string::npos ||
                 msg.find("casts") != std::string::npos ||
                 msg.find("Mana") != std::string::npos)
            c = UIColor::rgba(0.5f, 0.7f, 1.0f);      // blue for spell cast
        else if (msg.find("drains") != std::string::npos ||
                 msg.find("vampir") != std::string::npos ||
                 msg.find("Feast") != std::string::npos)
            c = UIColor::rgba(0.75f, 0.2f, 0.95f);    // purple for vampiric
        else if (msg.find("regenerat") != std::string::npos ||
                 msg.find("healed") != std::string::npos ||
                 msg.find("Regrowth") != std::string::npos)
            c = UIColor::rgba(0.3f, 0.95f, 0.5f);     // green for heal/regen
        else if (msg.find("poison") != std::string::npos)
            c = UIColor::rgba(0.3f, 0.9f, 0.3f);      // poison green
        else if (msg.find("burn") != std::string::npos ||
                 msg.find("incinerat") != std::string::npos)
            c = UIColor::rgba(1.0f, 0.5f, 0.1f);      // orange for burn
        else if (msg.find("Wither") != std::string::npos ||
                 msg.find("Symbiosis") != std::string::npos)
            c = UIColor::rgba(0.7f, 0.5f, 1.0f);      // lavender for auras
        else if (msg.find("killed") != std::string::npos ||
                 msg.find("destroyed") != std::string::npos ||
                 msg.find("perishes") != std::string::npos)
            c = UIColor::hex(UITheme::BLOOD_RED);
        else if (msg.find("morale surge") != std::string::npos)
            c = UIColor::rgba(1.0f, 0.8f, 0.2f);      // gold for morale
        else if (msg.find("hesitates") != std::string::npos)
            c = UIColor::rgba(0.85f, 0.35f, 0.35f);   // red for fear/hesitation
        else if (msg.find("Power tile") != std::string::npos || msg.find("+2 ATK") != std::string::npos)
            c = UIColor::rgba(1.0f, 0.55f, 0.1f);     // orange for attack buff
        else if (msg.find("Shield tile") != std::string::npos || msg.find("+2 DEF") != std::string::npos)
            c = UIColor::rgba(0.4f, 0.65f, 1.0f);     // sky blue for defense buff
        else if (msg.find("Symbiosis") != std::string::npos || msg.find("Wither") != std::string::npos
              || msg.find("Soul Harvest") != std::string::npos)
            c = UIColor::rgba(0.7f, 0.5f, 1.0f);      // lavender for aura/regen effects
        else if (msg.find("Intel") != std::string::npos)
            c = UIColor::rgba(0.7f, 1.0f, 0.7f);      // pale green for intel/info
        rdr.drawText(msg, x, y, c, 11.0f);
        y += lineH;
    }
}

void CombatHUD::drawActionBar(UIRenderer& rdr)
{
    m_actionPanel.draw(rdr);
    m_waitBtn.draw(rdr);
    m_defendBtn.draw(rdr);
    m_spellsBtn.draw(rdr);
    m_retreatBtn.draw(rdr);
}

bool CombatHUD::onMouseMove(float x, float y) {
    m_waitBtn.onMouseMove(x, y);
    m_defendBtn.onMouseMove(x, y);
    m_spellsBtn.onMouseMove(x, y);
    m_retreatBtn.onMouseMove(x, y);
    return false;
}
bool CombatHUD::onMouseDown(float x, float y) {
    if (m_waitBtn.onMouseDown(x, y))    return true;
    if (m_defendBtn.onMouseDown(x, y))  return true;
    if (m_spellsBtn.onMouseDown(x, y))  return true;
    if (m_retreatBtn.onMouseDown(x, y)) return true;
    return false;
}
bool CombatHUD::onMouseUp(float x, float y) {
    m_waitBtn.onMouseUp(x, y);
    m_defendBtn.onMouseUp(x, y);
    m_spellsBtn.onMouseUp(x, y);
    m_retreatBtn.onMouseUp(x, y);
    return false;
}
