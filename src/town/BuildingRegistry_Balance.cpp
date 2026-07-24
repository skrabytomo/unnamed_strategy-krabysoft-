// Data-driven balance overrides for BuildingRegistry.
//
// After init() has populated the hardcoded defaults, applyBalanceOverrides()
// reads assets/data/{units,buildings}.json (the same shape `--export-content`
// writes) and overrides matching entries BY ID. Anything missing — a file, an
// id, a field — is left at its compiled default, so a partial or malformed file
// can only ever change what it explicitly names; it can never brick the game.
//
// Workflow: `unnamed_strategy --export-content=assets/data` once to seed the
// files with current values, tweak the numbers, relaunch. No recompile.
#include "BuildingRegistry.h"
#include "../data/Resources.h"
#include "../core/DevLog.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

using json = nlohmann::json;

namespace {

// Resource cost object: { "gold":N, "iron":N, "faithStones":N, ... }. Only the
// keys present are applied; the rest keep their current amount.
void applyCost(const json& j, Resources& out) {
    if (!j.is_object()) return;
    static const char* kKeys[RESOURCE_COUNT] = {
        "gold", "iron", "faithStones", "bloodEssence", "verdantSap", "mercury"
    };
    for (int i = 0; i < RESOURCE_COUNT; ++i)
        if (j.contains(kKeys[i]) && j[kKeys[i]].is_number_integer())
            out.amounts[i] = j[kKeys[i]].get<int>();
}

// Load+parse a json array from <base>path, falling back to a base-less relative
// path (mirrors how the texture loaders probe). Returns false if neither opens
// or the content isn't a JSON array.
bool loadArray(const std::string& basePath, const char* rel, json& out) {
    for (const std::string& p : { basePath + rel, std::string(rel) }) {
        std::ifstream f(p);
        if (!f.good()) continue;
        try {
            out = json::parse(f);
        } catch (const std::exception& e) {
            gLog("[BALANCE] %s parse error: %s (ignored)\n", p.c_str(), e.what());
            return false;
        }
        return out.is_array();
    }
    return false;
}

} // namespace

void BuildingRegistry::applyBalanceOverrides(const std::string& basePath)
{
    int unitN = 0, bldN = 0;

    // ── units.json ──────────────────────────────────────────────────────────
    json units;
    if (loadArray(basePath, "assets/data/units.json", units)) {
        for (const auto& e : units) {
            if (!e.contains("id")) continue;
            int id = e["id"].get<int>();
            UnitDef* u = nullptr;
            for (auto& d : m_units) if (d.id == id) { u = &d; break; }
            if (!u) continue;
            u->hp          = e.value("hp",          u->hp);
            u->attack      = e.value("attack",      u->attack);
            u->defense     = e.value("defense",     u->defense);
            u->damage_min  = e.value("damageMin",   u->damage_min);
            u->damage_max  = e.value("damageMax",   u->damage_max);
            u->speed       = e.value("speed",       u->speed);
            u->range       = e.value("range",       u->range);
            u->shots       = e.value("shots",       u->shots);
            u->flying      = e.value("flying",      u->flying);
            u->vampiric    = e.value("vampiric",    u->vampiric);
            u->regenerates = e.value("regenerates", u->regenerates);
            if (e.contains("cost")) applyCost(e["cost"], u->cost);
            ++unitN;
        }
    }

    // ── buildings.json ──────────────────────────────────────────────────────
    json buildings;
    if (loadArray(basePath, "assets/data/buildings.json", buildings)) {
        for (const auto& e : buildings) {
            if (!e.contains("id")) continue;
            int id = e["id"].get<int>();
            BuildingDef* b = nullptr;
            for (auto& d : m_buildings) if (d.id == id) { b = &d; break; }
            if (!b) continue;
            b->weeklyGrowth  = e.value("weeklyGrowth",  b->weeklyGrowth);
            b->growthA       = e.value("growthA",       b->growthA);
            b->growthB       = e.value("growthB",       b->growthB);
            b->growthBonus   = e.value("growthBonus",   b->growthBonus);
            b->growthMultPct = e.value("growthMultPct", b->growthMultPct);
            b->minWeek       = e.value("minWeek",       b->minWeek);
            if (e.contains("cost"))         applyCost(e["cost"],         b->cost);
            if (e.contains("weeklyIncome")) applyCost(e["weeklyIncome"], b->weeklyIncome);
            ++bldN;
        }
    }

    if (unitN > 0 || bldN > 0)
        gLog("[BALANCE] applied overrides: %d units, %d buildings (from assets/data/)\n",
             unitN, bldN);
}
