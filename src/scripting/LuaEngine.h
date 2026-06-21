#pragma once
#include <string>
#include <functional>
#include <memory>

// Forward declare so callers don't need Lua headers
struct lua_State;

// ── Script context passed to every trigger callback ───────────────────────────
struct ScriptContext
{
    int  heroId   = 0;    // triggering hero (0 = none)
    int  townId   = 0;    // involved town (0 = none)
    int  tileQ    = 0;
    int  tileR    = 0;
    bool playerSide = true;
};

// ── LuaEngine ─────────────────────────────────────────────────────────────────
// Wraps a Lua 5.4 state. Provides:
//   - Script loading from file or string
//   - A `game` global table with C++ API exposed to scripts
//   - Safe pcall wrapper — errors print to stderr, never crash the game
class LuaEngine
{
public:
    LuaEngine();
    ~LuaEngine();

    // Returns false if Lua library failed to load
    bool init();
    void shutdown();
    bool isReady() const { return m_L != nullptr; }

    // Execute a Lua file; returns false on error
    bool execFile(const std::string& path);

    // Execute a Lua string snippet; returns false on error
    bool execString(const std::string& code);

    // Call a named global Lua function with a ScriptContext arg.
    // Returns false if the function doesn't exist or errors.
    bool callFunction(const std::string& funcName, const ScriptContext& ctx);

    // Register a C callback under game.<name> in Lua
    // fn receives (lua_State*) and returns int (result count)
    using CFunc = int(*)(lua_State*);
    void registerGameFunc(const std::string& name, CFunc fn);

    lua_State* state() const { return m_L; }

private:
    void openGameTable();
    void bindCoreAPI();

    lua_State* m_L = nullptr;
};
