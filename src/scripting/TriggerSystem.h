#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../world/HexMap.h"   // HexCoord, HexCoordHash
#include "LuaEngine.h"

// ── Trigger types ──────────────────────────────────────────────────────────────
enum class TriggerType : uint8_t
{
    EnterTile,      // hero steps onto a specific tile
    LeaveTile,      // hero leaves a specific tile
    WeekStart,      // every week start
    BattleWon,      // player won a combat
    BattleLost,     // player lost a combat
    TownCaptured,   // player captured a town
    HeroLevel,      // hero leveled up to a threshold
    Custom,         // manually fired by script
};

// ── One trigger instance ──────────────────────────────────────────────────────
struct Trigger
{
    uint32_t    id        = 0;
    TriggerType type      = TriggerType::Custom;
    std::string funcName; // Lua global function to call

    // For tile triggers: specific hex (ignored for non-tile triggers)
    HexCoord    tilePos   = {0, 0};

    // For HeroLevel: minimum level to fire
    int         levelThreshold = 0;

    // Fire only once, then remove
    bool        once      = false;
    bool        fired     = false;

    // Optional condition Lua function name (empty = always fires)
    std::string condFunc;
};

// ── TriggerSystem ─────────────────────────────────────────────────────────────
class TriggerSystem
{
public:
    void setEngine(LuaEngine* engine) { m_lua = engine; }

    // Add a trigger — returns its assigned ID
    uint32_t addTrigger(Trigger t);

    // Remove trigger by id
    void removeTrigger(uint32_t id);

    // Fire all triggers matching type + context
    void fire(TriggerType type, const ScriptContext& ctx);

    // Convenience: fire tile-enter event
    void fireTileEnter(HexCoord tile, const ScriptContext& ctx);

    // Load triggers from a JSON array string (for map file integration)
    void loadFromJSON(const std::string& json);

    const std::vector<Trigger>& all() const { return m_triggers; }

private:
    bool checkCondition(const Trigger& t, const ScriptContext& ctx);

    LuaEngine*          m_lua = nullptr;
    std::vector<Trigger> m_triggers;
    uint32_t             m_nextId = 1;

    // Spatial index: tile → list of trigger ids
    std::unordered_map<HexCoord, std::vector<uint32_t>, HexCoordHash> m_tileIndex;
};
