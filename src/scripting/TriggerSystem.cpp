#include "../core/DevLog.h"
#include "TriggerSystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
extern "C" {
#include <lua.h>
}

using json = nlohmann::json;

uint32_t TriggerSystem::addTrigger(Trigger t)
{
    t.id = m_nextId++;
    if (t.type == TriggerType::EnterTile || t.type == TriggerType::LeaveTile)
        m_tileIndex[t.tilePos].push_back(t.id);
    m_triggers.push_back(std::move(t));
    return m_triggers.back().id;
}

void TriggerSystem::removeTrigger(uint32_t id)
{
    m_triggers.erase(
        std::remove_if(m_triggers.begin(), m_triggers.end(),
                       [id](const Trigger& t){ return t.id == id; }),
        m_triggers.end());
    // Rebuild tile index
    m_tileIndex.clear();
    for (auto& t : m_triggers)
        if (t.type == TriggerType::EnterTile || t.type == TriggerType::LeaveTile)
            m_tileIndex[t.tilePos].push_back(t.id);
}

void TriggerSystem::fire(TriggerType type, const ScriptContext& ctx)
{
    if (!m_lua) return;
    for (auto& t : m_triggers) {
        if (t.fired && t.once) continue;
        if (t.type != type) continue;
        if (!checkCondition(t, ctx)) continue;
        m_lua->callFunction(t.funcName, ctx);
        if (t.once) t.fired = true;
    }
}

void TriggerSystem::fireTileEnter(HexCoord tile, const ScriptContext& ctx)
{
    if (!m_lua) return;
    auto it = m_tileIndex.find(tile);
    if (it == m_tileIndex.end()) return;

    for (uint32_t id : it->second) {
        auto tIt = std::find_if(m_triggers.begin(), m_triggers.end(),
                                [id](const Trigger& t){ return t.id == id; });
        if (tIt == m_triggers.end()) continue;
        if (tIt->fired && tIt->once) continue;
        if (tIt->type != TriggerType::EnterTile) continue;
        if (!checkCondition(*tIt, ctx)) continue;
        m_lua->callFunction(tIt->funcName, ctx);
        if (tIt->once) tIt->fired = true;
    }
}

bool TriggerSystem::checkCondition(const Trigger& t, const ScriptContext& ctx)
{
    if (t.condFunc.empty()) return true;
    if (!m_lua) return true;

    if (!m_lua->state()) return true;
    lua_State* L = m_lua->state();

    lua_getglobal(L, t.condFunc.c_str());
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return true; }

    lua_newtable(L);
    lua_pushinteger(L, ctx.heroId); lua_setfield(L, -2, "heroId");
    lua_pushinteger(L, ctx.tileQ);  lua_setfield(L, -2, "q");
    lua_pushinteger(L, ctx.tileR);  lua_setfield(L, -2, "r");

    int rc = lua_pcall(L, 1, 1, 0);
    if (rc != LUA_OK) { lua_pop(L, 1); return true; }

    bool result = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return result;
}

void TriggerSystem::loadFromJSON(const std::string& jsonStr)
{
    try {
        auto arr = json::parse(jsonStr);
        for (auto& jt : arr) {
            Trigger t;
            std::string typeStr = jt.value("type", "custom");
            if      (typeStr == "enterTile")    t.type = TriggerType::EnterTile;
            else if (typeStr == "leaveTile")    t.type = TriggerType::LeaveTile;
            else if (typeStr == "weekStart")    t.type = TriggerType::WeekStart;
            else if (typeStr == "battleWon")    t.type = TriggerType::BattleWon;
            else if (typeStr == "battleLost")   t.type = TriggerType::BattleLost;
            else if (typeStr == "townCaptured") t.type = TriggerType::TownCaptured;
            else if (typeStr == "heroLevel")    t.type = TriggerType::HeroLevel;
            else                                t.type = TriggerType::Custom;

            t.funcName        = jt.value("func", "");
            t.condFunc        = jt.value("cond", "");
            t.once            = jt.value("once", false);
            t.levelThreshold  = jt.value("level", 0);
            if (jt.contains("q")) t.tilePos.q = jt["q"].get<int>();
            if (jt.contains("r")) t.tilePos.r = jt["r"].get<int>();

            if (!t.funcName.empty()) addTrigger(t);
        }
        gLog("TriggerSystem: loaded %zu triggers\n", m_triggers.size());
    }
    catch (const std::exception& e) {
        fprintf(stderr, "TriggerSystem loadFromJSON: %s\n", e.what());
    }
}
