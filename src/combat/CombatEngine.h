#pragma once
#include <vector>
#include <functional>
#include <unordered_set>
#include "CombatPhase.h"
#include "CombatGrid.h"
#include "DamageCalc.h"
#include "../hero/Hero.h"
#include "../magic/SpellDef.h"

// ── AI difficulty / personality ────────────────────────────────────────────────
enum class AIDifficulty : uint8_t
{
    Passive,    // random movement, no targeting priority — baseline / easy
    Standard,   // nearest enemy, attack if adjacent — current default
    Tactical,   // focus weakest stack, protect own ranged, kite
};

// ── Action types ───────────────────────────────────────────────────────────────
enum class ActionType { Move, Attack, Wait, Defend, Shoot, UseAbility };

struct CombatAction
{
    ActionType type         = ActionType::Move;
    uint32_t   unitId       = 0;
    HexCoord   target       = {0, 0};   // move target or attack position
    uint32_t   targetUnitId = 0;        // for attacks
    int        spellId      = 0;        // for UseAbility (cast spell)
};

// ── Combat log entry ───────────────────────────────────────────────────────────
struct CombatLog
{
    std::string message;
};

// ── CombatEngine ──────────────────────────────────────────────────────────────
class CombatEngine
{
public:
    using LogCallback    = std::function<void(const std::string&)>;
    using DamageCallback = std::function<void(uint32_t /*targetId*/, int /*damage*/, HexCoord /*pos*/)>;

    CombatEngine() = default;

    // Setup a battle — populates grid with units from both sides
    void startBattle(const Hero& playerHero, const std::vector<CombatUnit>& playerUnits,
                     const Hero& enemyHero,  const std::vector<CombatUnit>& enemyUnits,
                     bool isSiege = false,
                     Terrain terrain = Terrain::Plains);

    // Apply equipped-artifact bonuses to stored hero copies and their units
    // (call once after startBattle, before first turn)
    void applyArtifactBonuses(const ArtifactBonus& playerBonus,
                              const ArtifactBonus& enemyBonus);

    // Apply magic-power bonuses from town support buildings to the player hero
    // and optional speed buff to matching unit tags (call after startBattle)
    void applyPlayerTownBonus(int lightP, int bloodP, int deathP,
                              int natureP, int forgeP, int fleshP,
                              int mechSpeedBonus = 0,
                              int holyDespBonus = 0,
                              bool eternalMonument  = false,
                              bool wardenBrand       = false,
                              bool symbiosisWeb      = false,
                              bool warShrine         = false,
                              bool voidLens          = false,
                              bool mergeChamber      = false,
                              bool resonanceWell     = false,
                              bool mirrorChamber     = false);

    // Process one player action — returns true if action was valid
    bool submitAction(const CombatAction& action);

    // Advance AI turn — processes all enemy units
    void processAITurn();

    // Auto-play the player side with AI logic (for watch mode)
    void processPlayerAITurn();

    // Step exactly one unit with AI logic regardless of side (for timer-driven watch mode)
    void processOneAIAction();

    // Called when current unit clicks Wait
    void wait();

    // End current unit's turn without acting
    void skipUnit();

    // State queries
    CombatPhase    phase()          const { return m_phase; }
    CombatGrid&    grid()                 { return m_grid; }
    const CombatGrid& grid()        const { return m_grid; }
    CombatUnit*    activeUnit();
    int            round()          const { return m_round; }
    bool           isPlayerTurn()   const { return m_phase == CombatPhase::PlayerTurn; }
    bool           isSiege()        const { return m_isSiege; }

    // Siege: attack a wall tile with the active unit
    bool attackWall(HexCoord wallHex);

    // Turn order queue (sorted by speed, rebuilt each round)
    const std::vector<uint32_t>& turnOrder() const { return m_turnOrder; }
    int turnIndex() const { return m_turnIndex; }

    // Log
    const std::vector<CombatLog>& log() const { return m_log; }
    void setLogCallback(LogCallback cb) { m_logCb = cb; }
    void setDamageCallback(DamageCallback cb) { m_dmgCb = cb; }

    // XP earned this battle (enemy unit-count × 5, awarded on victory)
    int xpEarned() const { return m_enemyStartCount * 5; }
    int enemyStartCount() const { return m_enemyStartCount; }
    int enemiesAlive() const {
        int n = 0;
        for (const auto& u : m_grid.units())
            if (!u.isPlayer && u.alive) n += u.count;
        return n;
    }

    // Hero state accessors (for HUD display)
    const Hero& playerHero() const { return m_playerHero; }
    const Hero& enemyHero()  const { return m_enemyHero; }

    // CoordinatedStrike: current marked enemy unit ID (0 = none)
    uint32_t coordinatedStrikeTarget() const { return m_coordinatedStrikeTarget; }

    // Push an extra log entry externally (e.g. pre-battle intel)
    void pushLog(const std::string& msg) { addLog(msg); }

    // Headless batch simulation — both sides use AI, returns final phase
    void setSilent(bool s) { m_silent = s; }
    CombatPhase runHeadless(int maxRounds = 60);

    // Place terrain-driven obstacle tiles (call after startBattle, non-siege only)
    void applyTerrainObstacles(int count);

    // Seed the per-battle turn-order RNG (call alongside DamageCalc::seedRng)
    static void seedTurnRng(uint32_t seed);

    // AI difficulty — affects both processAITurn() and the player side in runHeadless()
    void setPlayerAI(AIDifficulty d) { m_playerAI = d; }
    void setEnemyAI(AIDifficulty d)  { m_enemyAI  = d; }
    AIDifficulty playerAI() const { return m_playerAI; }
    AIDifficulty enemyAI()  const { return m_enemyAI; }

private:
    void buildTurnOrder();
    void advanceTurn();
    void checkVictory();
    void applyTileEffect(CombatUnit& unit);
    void addLog(const std::string& msg);
    void applySymbiosisRound(); // Thornkin bond bonus — called at round start
    void processRoundStartEffects(); // DoT tick, mana regen — called at round start
    void applyTerrainBonuses(); // faction home/penalty terrain stat modifiers at battle start

    // AI dispatch — delegates to difficulty-specific implementation
    void aiActUnit(CombatUnit& unit);
    void aiActPassive(CombatUnit& unit);
    void aiActStandard(CombatUnit& unit);
    void aiActTactical(CombatUnit& unit);
    void tryEnemyHeroSpell();
    void spawnWildGrowthGhosts();
    // Fire all on-death specialty effects (LastRites, VoidLink, BloodWeb, SoulHarvest,
    // AdaptationMirror) when target is killed; also cleans up dead units.
    void processKillEvents(CombatUnit& attacker, CombatUnit& target, const DamageResult& result);
    // Apply Warden's Mark melee cleave splash for the attacker's hero (both sides).
    void applyWardenMarkSplash(CombatUnit& attacker, HexCoord targetPos, uint32_t targetId, int damage);

    CombatGrid  m_grid;
    CombatPhase m_phase     = CombatPhase::Setup;
    bool        m_isSiege   = false;
    int         m_round     = 1;
    int         m_turnIndex = 0;
    int         m_maxRounds = 60;

    std::vector<uint32_t>  m_turnOrder;   // unit IDs in speed order
    std::vector<uint32_t>  m_waitQueue;   // units that used Wait
    int                    m_enemyStartCount = 0; // total enemy units at battle start
    bool                   m_enemyHeroSpellUsed = false; // one cast per round

    std::vector<CombatLog> m_log;
    LogCallback            m_logCb;
    DamageCallback         m_dmgCb;
    bool                   m_silent  = false;
    AIDifficulty           m_playerAI = AIDifficulty::Standard;
    AIDifficulty           m_enemyAI  = AIDifficulty::Standard;

    uint32_t m_coordinatedStrikeTarget = 0;
    std::unordered_set<uint32_t> m_wildGrowthGhosted;  // unit IDs already ghosted
    bool     m_wardenBrand   = false;  // CW_WARDEN_BRAND: +1 splash target for Warden's Mark
    bool     m_symbiosisWeb  = false;  // TK_SYMBIOSIS_WEB: Symbiosis cap raised to 2

    Terrain  m_battleTerrain = Terrain::Plains;  // terrain where the battle takes place
    int      m_bloodPool     = 0;    // Bloodsworn: BloodBound kills toward Ascension
    bool     m_ascended      = false; // true once Ascension triggers (once per battle)

    Hero m_playerHero;
    Hero m_enemyHero;
};
