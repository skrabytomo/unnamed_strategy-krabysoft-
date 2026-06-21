#include "HideoutScreen.h"
#include <imgui.h>
#include <cstdio>

// XP cost to unlock each tier, indexed by (tier - 1)
static constexpr int CASTLE_COSTS[]   = { 100, 300, 600 };
static constexpr int BARRACKS_COSTS[] = { 150, 400 };
static constexpr int VAULT_COSTS[]    = { 200, 500 };
static constexpr int SHRINE_COSTS[]   = { 250 };
static constexpr int SANCTUM_COSTS[]  = { 400 };

// Approximate XP needed for the next level (mirrors HideoutDB XP pool)
static constexpr int XP_DISPLAY_MAX = 1000;

void HideoutScreen::draw(HideoutDB& db, bool& open)
{
    if (!open) return;
    if (!db.isOpen()) {
        ImGui::Begin("Hideout", &open);
        ImGui::TextDisabled("Database not available.");
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Hideout  [F6]", &open)) { ImGui::End(); return; }

    drawXPBar(db);
    ImGui::Separator();

    if (ImGui::BeginTabBar("HideoutTabs")) {
        if (ImGui::BeginTabItem("Upgrades")) {
            drawBranch(db, HideoutBranch::CASTLE,   "Castle",   3, CASTLE_COSTS);
            drawBranch(db, HideoutBranch::BARRACKS, "Barracks", 2, BARRACKS_COSTS);
            drawBranch(db, HideoutBranch::VAULT,    "Vault",    2, VAULT_COSTS);
            drawBranch(db, HideoutBranch::SHRINE,   "Shrine",   1, SHRINE_COSTS);
            drawBranch(db, HideoutBranch::SANCTUM,  "Sanctum",  1, SANCTUM_COSTS);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Milestones")) {
            drawMilestones(db);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Convergence status
    ImGui::Separator();
    if (db.isConvergenceUnlocked()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                           "Convergence UNLOCKED — 9th faction accessible");
    } else {
        ImGui::TextDisabled("Convergence locked (need Castle T2 + Barracks T1 + Vault T1)");
    }

    // Active bonuses summary
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Active bonuses on next new game:");
    {
        static constexpr int CASTLE_GOLD[] = { 0, 200, 600, 1300 };
        int ct = db.getUpgradeLevel(HideoutBranch::CASTLE);
        int bt = db.getUpgradeLevel(HideoutBranch::BARRACKS);
        int vt = db.getUpgradeLevel(HideoutBranch::VAULT);
        int sh = db.getUpgradeLevel(HideoutBranch::SHRINE);
        int sa = db.getUpgradeLevel(HideoutBranch::SANCTUM);
        if (ct > 0) ImGui::BulletText("Castle T%d: +%d Gold", ct, CASTLE_GOLD[std::min(ct, 3)]);
        if (bt >= 1) ImGui::BulletText("Barracks T1: Hero +1 ATK");
        if (bt >= 2) ImGui::BulletText("Barracks T2: Hero +1 DEF");
        if (vt >= 1) ImGui::BulletText("Vault T1: +1 Iron, +1 Mercury");
        if (vt >= 2) ImGui::BulletText("Vault T2: +1 Verdant Sap, Blood Essence, Faith Stones");
        if (sh >= 1) ImGui::BulletText("Shrine T1: Extra starting spell");
        if (sa >= 1) ImGui::BulletText("Sanctum T1: +10 Max Mana");
        if (ct == 0 && bt == 0 && vt == 0 && sh == 0 && sa == 0)
            ImGui::TextDisabled("  None (unlock upgrades to gain permanent bonuses)");
    }

    ImGui::End();
}

void HideoutScreen::drawXPBar(HideoutDB& db)
{
    int xp = db.getXP();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "XP: %d", xp);
    float frac = static_cast<float>(xp % XP_DISPLAY_MAX) / static_cast<float>(XP_DISPLAY_MAX);
    ImGui::ProgressBar(frac, ImVec2(-1, 18), buf);
    ImGui::TextDisabled("Total accumulated XP: %d", xp);
}

// Returns a brief description for each branch tier (1-based tier)
static const char* branchTierDesc(const char* branch, int tier)
{
    if (branch == HideoutBranch::CASTLE) {
        static const char* d[] = { "Start with +200 Gold", "Start with +400 Gold", "Start with +700 Gold" };
        return (tier >= 1 && tier <= 3) ? d[tier-1] : "";
    }
    if (branch == HideoutBranch::BARRACKS) {
        static const char* d[] = { "Hero starts with +1 ATK", "Hero starts with +1 ATK and +1 DEF" };
        return (tier >= 1 && tier <= 2) ? d[tier-1] : "";
    }
    if (branch == HideoutBranch::VAULT) {
        static const char* d[] = { "Start with +1 Iron and +1 Mercury", "+1 to all rare resources at start" };
        return (tier >= 1 && tier <= 2) ? d[tier-1] : "";
    }
    if (branch == HideoutBranch::SHRINE) {
        return tier == 1 ? "Hero starts knowing one extra spell" : "";
    }
    if (branch == HideoutBranch::SANCTUM) {
        return tier == 1 ? "Hero starts with +10 max mana" : "";
    }
    return "";
}

void HideoutScreen::drawBranch(HideoutDB& db, const char* branch, const char* label,
                                int maxTiers, const int tierCosts[])
{
    int current = db.getUpgradeLevel(branch);
    ImGui::PushID(branch);

    ImGui::Text("%s  [%d / %d tiers]", label, current, maxTiers);

    // Show what each unlocked tier provides
    for (int t = 1; t <= current; t++) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "|T%d", t);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", branchTierDesc(branch, t));
    }

    if (current >= maxTiers) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), " MAX");
    } else {
        int cost = tierCosts[current]; // current is 0-based index into costs
        char btnLabel[64];
        std::snprintf(btnLabel, sizeof(btnLabel), "Unlock T%d  (%d XP)##%s", current + 1, cost, branch);
        bool canAfford = db.canUnlockNextTier(branch, cost);
        if (!canAfford) ImGui::BeginDisabled();
        if (ImGui::Button(btnLabel)) {
            db.unlockNextTier(branch, cost);
        }
        if (!canAfford) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("-> %s", branchTierDesc(branch, current + 1));
    }
    ImGui::PopID();
}

void HideoutScreen::drawMilestones(HideoutDB& db)
{
    struct MilestoneEntry { const char* key; const char* desc; };
    static const MilestoneEntry ENTRIES[] = {
        // Gameplay achievements
        { Milestone::FIRST_BATTLE_WON,   "First battle won" },
        { Milestone::FIRST_TOWN_CAPTURED,"First town captured" },
        { Milestone::HERO_LEVEL_5,       "Hero reached level 5" },
        { Milestone::HERO_LEVEL_10,      "Hero reached level 10" },
        { Milestone::WEEK_10_REACHED,    "Survived to week 10" },
        { Milestone::CAMPAIGN_WON,       "The Fracture campaign completed" },
        // Hideout upgrades
        { Milestone::CASTLE_T1,          "Castle Tier 1 unlocked" },
        { Milestone::CASTLE_T2,          "Castle Tier 2 unlocked" },
        { Milestone::CASTLE_T3,          "Castle Tier 3 unlocked" },
        { Milestone::BARRACKS_T1,        "Barracks Tier 1 unlocked" },
        { Milestone::BARRACKS_T2,        "Barracks Tier 2 unlocked" },
        { Milestone::VAULT_T1,           "Vault Tier 1 unlocked" },
        { Milestone::VAULT_T2,           "Vault Tier 2 unlocked" },
        { Milestone::CONVERGENCE_UNLOCK, "Convergence faction unlocked" },
    };

    int completed = 0, total = static_cast<int>(std::size(ENTRIES));
    for (auto& e : ENTRIES) if (db.isMilestoneComplete(e.key)) ++completed;

    ImGui::TextDisabled("Completed: %d / %d", completed, total);
    ImGui::Spacing();

    for (auto& e : ENTRIES) {
        bool done = db.isMilestoneComplete(e.key);
        if (done)
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[x] %s", e.desc);
        else
            ImGui::TextDisabled("[ ] %s", e.desc);
    }
}
