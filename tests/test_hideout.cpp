// Focused check of the hideout progression fixes:
//  1. a gameplay milestone pays its XP exactly ONCE, however often it fires
//  2. buying an upgrade tier completes the matching tier milestone
//  3. upgrade-tier milestones pay no XP (no circular refund)
#include "../src/meta/HideoutDB.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
static void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main()
{
    const std::string path = "/tmp/hideout_test_tmp.db";
    std::remove(path.c_str());

    HideoutDB db;
    if (!db.open(path)) { printf("FAIL  could not open db\n"); return 1; }

    check(db.getXP() == 0, "fresh db starts at 0 XP");

    // 1. Repeated completion pays once.
    db.completeMilestone(Milestone::FIRST_BATTLE_WON);
    int afterFirst = db.getXP();
    check(afterFirst == 25, "FIRST_BATTLE_WON pays 25 XP on first completion");

    for (int i = 0; i < 10; ++i) db.completeMilestone(Milestone::FIRST_BATTLE_WON);
    check(db.getXP() == afterFirst,
          "repeating the same milestone 10x pays nothing extra");

    // 2. Several distinct gameplay milestones accumulate.
    db.completeMilestone(Milestone::HERO_LEVEL_5);     // +100
    db.completeMilestone(Milestone::WEEK_10_REACHED);  // +100
    check(db.getXP() == 225, "distinct milestones accumulate (25+100+100)");

    // 3. Buying a tier completes the matching milestone.
    check(!db.isMilestoneComplete(Milestone::CASTLE_T1),
          "castle T1 milestone not complete before purchase");
    bool bought = db.unlockNextTier(HideoutBranch::CASTLE, 100);
    check(bought, "castle T1 purchased with 100 XP");
    check(db.getUpgradeLevel(HideoutBranch::CASTLE) == 1, "castle level is 1");
    check(db.isMilestoneComplete(Milestone::CASTLE_T1),
          "castle T1 MILESTONE completed by the purchase");

    // 4. The tier milestone paid no XP back (225 - 100 spent = 125).
    check(db.getXP() == 125, "tier milestone refunds no XP (125 remains)");

    // 5. Second tier maps to its own milestone.
    db.completeMilestone(Milestone::HERO_LEVEL_10);    // +250 -> 375
    bool bought2 = db.unlockNextTier(HideoutBranch::CASTLE, 300);
    check(bought2, "castle T2 purchased");
    check(db.isMilestoneComplete(Milestone::CASTLE_T2), "castle T2 milestone completed");

    // 6. Can't buy what you can't afford.
    check(!db.unlockNextTier(HideoutBranch::SANCTUM, 999999),
          "unaffordable tier is refused");

    db.close();
    std::remove(path.c_str());
    printf("\n%s (%d failure(s))\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
