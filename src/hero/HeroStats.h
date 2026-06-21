#pragma once

// ── Hero casting stats (one per magic school) ──────────────────────────────────
struct CastingStats
{
    int lightPower  = 0;
    int bloodPower  = 0;
    int deathPower  = 0;
    int naturePower = 0;
    int forgePower  = 0;
    int fleshPower  = 0;
};

// ── Full hero stat block ───────────────────────────────────────────────────────
struct HeroStats
{
    // Combat stats
    int attack      = 2;
    int defense     = 2;

    // Magic stats
    CastingStats casting;

    // Derived / modified
    int visionRange = 5;
    int moveBonus   = 0;    // flat bonus to movement pool

    // XP and level
    int  level = 1;
    int  xp    = 0;
    int  xpToNext = 100;    // recalculated on level up

    // HP (hero can take damage from targeted abilities)
    int  hp    = 100;
    int  maxHp = 100;

    // Mana pool (for spells)
    int  mana    = 10;
    int  maxMana = 10;

    static int xpRequired(int level) {
        // Exponential curve — doubles roughly every 5 levels
        return 100 * level * level;
    }

    bool addXp(int amount) {
        xp += amount;
        bool leveled = false;
        while (xp >= xpToNext) {
            xp -= xpToNext;
            level++;
            xpToNext = xpRequired(level);
            leveled = true;
        }
        return leveled;
    }
};
