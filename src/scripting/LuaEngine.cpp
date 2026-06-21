#include "../core/DevLog.h"
#include "LuaEngine.h"
#include <lua.hpp>
#include <cstdio>
#include <cstring>

LuaEngine::LuaEngine()  = default;
LuaEngine::~LuaEngine() { shutdown(); }

bool LuaEngine::init()
{
    m_L = luaL_newstate();
    if (!m_L) {
        fprintf(stderr, "LuaEngine: failed to create Lua state\n");
        return false;
    }
    luaL_openlibs(m_L);
    openGameTable();
    bindCoreAPI();
    gLog("LuaEngine: Lua %s ready\n", LUA_VERSION);
    return true;
}

void LuaEngine::shutdown()
{
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
}

// ── Script execution ──────────────────────────────────────────────────────────
bool LuaEngine::execFile(const std::string& path)
{
    if (!m_L) return false;
    int rc = luaL_dofile(m_L, path.c_str());
    if (rc != LUA_OK) {
        fprintf(stderr, "LuaEngine execFile '%s': %s\n",
                path.c_str(), lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

bool LuaEngine::execString(const std::string& code)
{
    if (!m_L) return false;
    int rc = luaL_dostring(m_L, code.c_str());
    if (rc != LUA_OK) {
        fprintf(stderr, "LuaEngine execString: %s\n", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

// ── Trigger function call ──────────────────────────────────────────────────────
bool LuaEngine::callFunction(const std::string& funcName, const ScriptContext& ctx)
{
    if (!m_L) return false;

    lua_getglobal(m_L, funcName.c_str());
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return false;
    }

    // Push context as a Lua table
    lua_newtable(m_L);
    lua_pushinteger(m_L, ctx.heroId); lua_setfield(m_L, -2, "heroId");
    lua_pushinteger(m_L, ctx.townId); lua_setfield(m_L, -2, "townId");
    lua_pushinteger(m_L, ctx.tileQ);  lua_setfield(m_L, -2, "q");
    lua_pushinteger(m_L, ctx.tileR);  lua_setfield(m_L, -2, "r");
    lua_pushboolean(m_L, ctx.playerSide); lua_setfield(m_L, -2, "playerSide");

    int rc = lua_pcall(m_L, 1, 0, 0);
    if (rc != LUA_OK) {
        fprintf(stderr, "LuaEngine callFunction '%s': %s\n",
                funcName.c_str(), lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

// ── Register a C function under game.<name> ───────────────────────────────────
void LuaEngine::registerGameFunc(const std::string& name, CFunc fn)
{
    if (!m_L) return;
    lua_getglobal(m_L, "game");
    lua_pushcfunction(m_L, fn);
    lua_setfield(m_L, -2, name.c_str());
    lua_pop(m_L, 1);
}

// ── Internal: create empty game table ────────────────────────────────────────
void LuaEngine::openGameTable()
{
    lua_newtable(m_L);
    lua_setglobal(m_L, "game");
}

// ── Internal: bind core C++ API to Lua ───────────────────────────────────────
void LuaEngine::bindCoreAPI()
{
    // game.print(msg) — prints to stdout (supplements Lua's print)
    registerGameFunc("print", [](lua_State* L) -> int {
        const char* msg = luaL_checkstring(L, 1);
        gLog("[Lua] %s\n", msg);
        return 0;
    });

    // game.version() → string
    registerGameFunc("version", [](lua_State* L) -> int {
        lua_pushstring(L, "0.1.0");
        return 1;
    });

    // game.getDay() and game.getWeek() — placeholders until Game is wired
    // (will be replaced by real bindings in Game::init via registerGameFunc)
    registerGameFunc("getDay",  [](lua_State* L) -> int { lua_pushinteger(L, 1); return 1; });
    registerGameFunc("getWeek", [](lua_State* L) -> int { lua_pushinteger(L, 1); return 1; });
}
