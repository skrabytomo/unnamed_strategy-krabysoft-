#include "ContentExport.h"
#include "../core/DevLog.h"
#include "../town/BuildingRegistry.h"
#include "../world/HexMap.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Enum → string ─────────────────────────────────────────────────────────────
static const char* categoryName(BuildingCategory c) {
    switch (c) {
        case BuildingCategory::UnitDwelling: return "UnitDwelling";
        case BuildingCategory::Support:      return "Support";
        case BuildingCategory::Economy:      return "Economy";
        case BuildingCategory::Special:      return "Special";
        case BuildingCategory::Fort:         return "Fort";
        case BuildingCategory::MageGuild:    return "MageGuild";
    }
    return "Economy";
}

static const char* pathName(UpgradePath p) {
    switch (p) {
        case UpgradePath::PathA: return "A";
        case UpgradePath::PathB: return "B";
        default:                 return "base";
    }
}

static const char* terrainName(Terrain t) {
    switch (t) {
        case Terrain::Plains:          return "Plains";
        case Terrain::Forest:          return "Forest";
        case Terrain::Highland:        return "Highland";
        case Terrain::Corrupted:       return "Corrupted";
        case Terrain::Toxic:           return "Toxic";
        case Terrain::Sacred:          return "Sacred";
        case Terrain::Industrial:      return "Industrial";
        case Terrain::Rocky:           return "Rocky";
        case Terrain::Swamp:           return "Swamp";
        case Terrain::Water:           return "Water";
        case Terrain::Volcanic:        return "Volcanic";
        case Terrain::Barren:          return "Barren";
        case Terrain::Wasteland:       return "Wasteland";
        case Terrain::CorruptedForest: return "CorruptedForest";
        case Terrain::FleshZone:       return "FleshZone";
        case Terrain::Mountain:        return "Mountain";
        default:                       return "Unknown";
    }
}

// Unit tags are a bitfield — emit the set names so consumers don't re-derive
// the bit layout.
static json tagList(UnitTag tags) {
    static const struct { UnitTag bit; const char* name; } kTags[] = {
        { UnitTag::Undead,      "Undead"      },
        { UnitTag::Construct,   "Construct"   },
        { UnitTag::Beast,       "Beast"       },
        { UnitTag::Humanoid,    "Humanoid"    },
        { UnitTag::Flying,      "Flying"      },
        { UnitTag::Mechanical,  "Mechanical"  },
        { UnitTag::OrganicMech, "OrganicMech" },
        { UnitTag::Holy,        "Holy"        },
        { UnitTag::BloodBound,  "BloodBound"  },
        { UnitTag::Void,        "Void"        },
    };
    json arr = json::array();
    for (const auto& t : kTags)
        if (hasTag(tags, t.bit)) arr.push_back(t.name);
    return arr;
}

// Resources as a sparse object — only nonzero entries, keyed by enum name so a
// consumer never has to know the array order.
static json resourceObj(const Resources& r) {
    static const char* kKeys[RESOURCE_COUNT] = {
        "gold", "iron", "faithStones", "bloodEssence", "verdantSap", "mercury"
    };
    json o = json::object();
    for (int i = 0; i < RESOURCE_COUNT; ++i)
        if (r.amounts[i] != 0) o[kKeys[i]] = r.amounts[i];
    return o;
}

// ── Asset inventory ───────────────────────────────────────────────────────────
// Walks assets/ and records every file as a forward-slashed path relative to the
// repo root, grouped by its top-level folder. Front-ends use this to build
// atlases without hardcoding filenames.
static json scanAssets(const fs::path& assetsRoot)
{
    json groups = json::object();
    if (!fs::exists(assetsRoot)) return groups;

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(assetsRoot, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;

        fs::path rel = fs::relative(it->path(), assetsRoot, ec);
        if (ec) continue;

        std::string relStr = rel.generic_string();
        auto slash = relStr.find('/');
        std::string group = (slash == std::string::npos) ? "_root" : relStr.substr(0, slash);
        groups[group].push_back("assets/" + relStr);
    }

    // Stable output — directory iteration order is not guaranteed, and a
    // reordering diff on every export would be noise.
    for (auto& [key, list] : groups.items()) {
        auto v = list.get<std::vector<std::string>>();
        std::sort(v.begin(), v.end());
        list = v;
    }
    return groups;
}

// ── Export ────────────────────────────────────────────────────────────────────
static bool writeJson(const fs::path& file, const json& j)
{
    std::ofstream out(file);
    if (!out) {
        gLog("[EXPORT] cannot write %s\n", file.string().c_str());
        return false;
    }
    out << j.dump(2) << "\n";
    return true;
}

bool exportContentJson(const std::string& outDir)
{
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        gLog("[EXPORT] cannot create %s: %s\n", outDir.c_str(), ec.message().c_str());
        return false;
    }
    const fs::path dir(outDir);

    BuildingRegistry reg;
    reg.init();

    // ── factions ──────────────────────────────────────────────────────────────
    json factions = json::array();
    for (int i = 0; i < FACTION_COUNT; ++i) {
        factions.push_back({
            { "id",   i },
            { "name", factionName(static_cast<FactionId>(i)) },
        });
    }

    // ── resources ─────────────────────────────────────────────────────────────
    json resources = json::array();
    for (int i = 0; i < RESOURCE_COUNT; ++i) {
        resources.push_back({
            { "id",   i },
            { "name", resourceName(static_cast<ResourceType>(i)) },
        });
    }

    // ── terrain ───────────────────────────────────────────────────────────────
    json terrain = json::array();
    for (int i = 0; i < static_cast<int>(Terrain::COUNT); ++i) {
        terrain.push_back({
            { "id",   i },
            { "name", terrainName(static_cast<Terrain>(i)) },
        });
    }

    // ── buildings ─────────────────────────────────────────────────────────────
    json buildings = json::array();
    for (const BuildingDef& b : reg.buildings()) {
        json e = {
            { "id",            b.id },
            { "name",          b.name },
            { "description",   b.description },
            { "category",      categoryName(b.category) },
            { "faction",       static_cast<int>(b.faction) },
            { "cost",          resourceObj(b.cost) },
            { "weeklyIncome",  resourceObj(b.weeklyIncome) },
            { "tier",          b.tier },
            { "path",          pathName(b.path) },
            { "weeklyGrowth",  b.weeklyGrowth },
            { "growthA",       b.growthA },
            { "growthB",       b.growthB },
            { "growthBonus",   b.growthBonus },
            { "growthMultPct", b.growthMultPct },
            { "minWeek",       b.minWeek },
            { "prerequisites", b.prerequisites },
            { "upgradeA",      b.upgradeA },
            { "upgradeB",      b.upgradeB },
        };
        buildings.push_back(e);
    }

    // ── units ─────────────────────────────────────────────────────────────────
    json units = json::array();
    for (const UnitDef& u : reg.units()) {
        json e = {
            { "id",          u.id },
            { "name",        u.name },
            { "faction",     static_cast<int>(u.faction) },
            { "tier",        u.tier },
            { "path",        pathName(u.path) },
            { "hp",          u.hp },
            { "attack",      u.attack },
            { "defense",     u.defense },
            { "damageMin",   u.damage_min },
            { "damageMax",   u.damage_max },
            { "speed",       u.speed },
            { "range",       u.range },
            { "shots",       u.shots },
            { "flying",      u.flying },
            { "vampiric",    u.vampiric },
            { "regenerates", u.regenerates },
            { "tags",        tagList(u.tags) },
            { "cost",        resourceObj(u.cost) },
            { "abilities", {
                { "hasSecondLife",      u.hasSecondLife },
                { "secondLifeFullHeal", u.secondLifeFullHeal },
                { "moraleImmune",       u.moraleImmune },
                { "rapidEvolution",     u.rapidEvolution },
                { "adaptationDouble",   u.adaptationDouble },
            }},
        };
        if (u.isCrafted) {
            e["crafting"] = {
                { "batch", u.craftBatch },
                { "cost",  resourceObj(u.craftCost) },
            };
        }
        units.push_back(e);
    }

    // ── assets ────────────────────────────────────────────────────────────────
    json assets = scanAssets(fs::path("assets"));

    bool ok = true;
    ok &= writeJson(dir / "factions.json",  factions);
    ok &= writeJson(dir / "resources.json", resources);
    ok &= writeJson(dir / "terrain.json",   terrain);
    ok &= writeJson(dir / "buildings.json", buildings);
    ok &= writeJson(dir / "units.json",     units);
    ok &= writeJson(dir / "assets.json",    assets);

    int assetCount = 0;
    for (const auto& [key, list] : assets.items()) assetCount += static_cast<int>(list.size());

    gLog("[EXPORT] %s: %zu buildings, %zu units, %d factions, %d assets\n",
         outDir.c_str(), reg.buildings().size(), reg.units().size(),
         FACTION_COUNT, assetCount);
    return ok;
}
