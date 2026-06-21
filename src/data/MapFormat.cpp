#include "../core/DevLog.h"
#include "MapFormat.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

// ── Helpers ───────────────────────────────────────────────────────────────────
static const char* mapSizeName(MapSize s) {
    switch (s) {
        case MapSize::Small:  return "Small";
        case MapSize::Medium: return "Medium";
        case MapSize::Large:  return "Large";
        case MapSize::XLarge: return "XLarge";
    }
    return "Medium";
}
static MapSize mapSizeFromName(const std::string& s) {
    if (s == "Small")  return MapSize::Small;
    if (s == "Large")  return MapSize::Large;
    if (s == "XLarge") return MapSize::XLarge;
    return MapSize::Medium;
}

// ── Save ──────────────────────────────────────────────────────────────────────
bool MapFormat::save(const std::string& path, const MapFile& mf)
{
    try {
        json j;

        // Metadata
        j["name"]        = mf.meta.name;
        j["author"]      = mf.meta.author;
        j["description"] = mf.meta.description;
        j["playerCount"] = mf.meta.playerCount;
        j["mapSize"]     = mapSizeName(mf.meta.size);
        j["version"]     = mf.meta.version;

        // Tiles
        json tiles = json::array();
        for (auto& t : mf.tiles) {
            tiles.push_back({
                {"q", t.q}, {"r", t.r}, {"ter", t.terrain},
                {"tid", t.townId}, {"rid", t.resourceId}
            });
        }
        j["tiles"] = tiles;

        // Towns
        json towns = json::array();
        for (auto& t : mf.towns) {
            towns.push_back({
                {"id", t.id}, {"name", t.name},
                {"faction", static_cast<int>(t.faction)},
                {"q", t.pos.q}, {"r", t.pos.r},
                {"ownerId", t.ownerId}
            });
        }
        j["towns"] = towns;

        // Resources
        json res = json::array();
        for (auto& r : mf.resources) {
            res.push_back({
                {"id", r.id}, {"q", r.pos.q}, {"r", r.pos.r},
                {"type", static_cast<int>(r.type)}, {"amount", r.amount}
            });
        }
        j["resources"] = res;

        // Hero starts
        json starts = json::array();
        for (auto& s : mf.heroStarts)
            starts.push_back({{"q", s.q}, {"r", s.r}});
        j["heroStarts"] = starts;

        // Triggers
        json trigs = json::array();
        for (auto& t : mf.triggers) {
            json jt;
            jt["type"]     = t.type;
            jt["func"]     = t.funcName;
            jt["cond"]     = t.condFunc;
            jt["once"]     = t.once;
            jt["q"]        = t.q;
            jt["r"]        = t.r;
            jt["level"]    = t.levelThreshold;
            jt["body"]     = t.scriptBody;
            trigs.push_back(jt);
        }
        j["triggers"] = trigs;

        // World objects
        json wobjs = json::array();
        for (auto& wo : mf.worldObjects) {
            json jwo;
            jwo["id"]           = wo.id;
            jwo["type"]         = static_cast<int>(wo.type);
            jwo["q"]            = wo.pos.q;
            jwo["r"]            = wo.pos.r;
            jwo["value"]        = wo.value;
            jwo["resourceType"] = static_cast<int>(wo.resourceType);
            jwo["faction"]      = static_cast<int>(wo.faction);
            jwo["questState"]   = wo.questState;
            jwo["linkedId"]     = wo.linkedId;
            wobjs.push_back(jwo);
        }
        j["worldObjects"] = wobjs;

        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << j.dump(2);
        gLog("MapFormat: saved '%s' to %s\n", mf.meta.name.c_str(), path.c_str());
        return true;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "MapFormat save: %s\n", e.what());
        return false;
    }
}

// ── Load ──────────────────────────────────────────────────────────────────────
bool MapFormat::load(const std::string& path, MapFile& out)
{
    try {
        std::ifstream f(path);
        if (!f.is_open()) { fprintf(stderr, "MapFormat: cannot open '%s'\n", path.c_str()); return false; }
        json j; f >> j;

        out.meta.name        = j.value("name", "Unnamed Map");
        out.meta.author      = j.value("author", "");
        out.meta.description = j.value("description", "");
        out.meta.playerCount = j.value("playerCount", 2);
        out.meta.size        = mapSizeFromName(j.value("mapSize", "Medium"));
        out.meta.version     = j.value("version", 1);

        out.tiles.clear();
        for (auto& jt : j.value("tiles", json::array())) {
            MapFile::TileEntry te;
            te.q          = jt.at("q").get<int>();
            te.r          = jt.at("r").get<int>();
            te.terrain    = jt.at("ter").get<int>();
            te.townId     = jt.value("tid", 0u);
            te.resourceId = jt.value("rid", 0u);
            out.tiles.push_back(te);
        }

        out.towns.clear();
        for (auto& jt : j.value("towns", json::array())) {
            Town t;
            t.id      = jt.at("id").get<uint32_t>();
            t.name    = jt.at("name").get<std::string>();
            t.faction = static_cast<FactionId>(jt.at("faction").get<int>());
            t.pos     = {jt.at("q").get<int>(), jt.at("r").get<int>()};
            t.ownerId = jt.value("ownerId", 0u);
            out.towns.push_back(t);
        }

        out.resources.clear();
        for (auto& jr : j.value("resources", json::array())) {
            ResourceNode r;
            r.id     = jr.at("id").get<uint32_t>();
            r.pos    = {jr.at("q").get<int>(), jr.at("r").get<int>()};
            r.type   = static_cast<ResourceType>(jr.at("type").get<int>());
            r.amount = jr.value("amount", 2);
            out.resources.push_back(r);
        }

        out.heroStarts.clear();
        for (auto& js : j.value("heroStarts", json::array()))
            out.heroStarts.push_back({js.at("q").get<int>(), js.at("r").get<int>()});

        out.triggers.clear();
        for (auto& jt : j.value("triggers", json::array())) {
            MapTriggerEntry t;
            t.type           = jt.value("type", "custom");
            t.funcName       = jt.value("func", "");
            t.condFunc       = jt.value("cond", "");
            t.once           = jt.value("once", false);
            t.q              = jt.value("q", 0);
            t.r              = jt.value("r", 0);
            t.levelThreshold = jt.value("level", 0);
            t.scriptBody     = jt.value("body", "");
            out.triggers.push_back(t);
        }

        out.worldObjects.clear();
        for (auto& jwo : j.value("worldObjects", json::array())) {
            WorldObject wo;
            wo.id           = jwo.value("id", 0u);
            wo.type         = static_cast<WorldObjectType>(jwo.value("type", 0));
            wo.pos          = {jwo.value("q", 0), jwo.value("r", 0)};
            wo.value        = jwo.value("value", 0);
            wo.resourceType = static_cast<ResourceType>(jwo.value("resourceType", 0));
            wo.faction      = static_cast<uint8_t>(jwo.value("faction", 0));
            wo.questState   = jwo.value("questState", 0);
            wo.linkedId     = jwo.value("linkedId", 0u);
            out.worldObjects.push_back(wo);
        }

        gLog("MapFormat: loaded '%s' (%zu tiles, %zu towns, %zu triggers, %zu world objects)\n",
               out.meta.name.c_str(), out.tiles.size(),
               out.towns.size(), out.triggers.size(), out.worldObjects.size());
        return true;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "MapFormat load: %s\n", e.what());
        return false;
    }
}

// ── Apply to live map ─────────────────────────────────────────────────────────
void MapFormat::applyToMap(HexMap& map, const MapFile& mf,
                           std::vector<Town>& towns,
                           std::vector<ResourceNode>& resources)
{
    map.create(mf.meta.size);

    for (auto& te : mf.tiles) {
        HexTile* tile = map.getTile({te.q, te.r});
        if (!tile) continue;
        tile->terrain    = static_cast<Terrain>(te.terrain);
        tile->townId     = te.townId;
        tile->resourceId = te.resourceId;
    }

    towns     = mf.towns;
    resources = mf.resources;
}
