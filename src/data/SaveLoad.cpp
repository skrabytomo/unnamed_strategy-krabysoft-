#include "SaveLoad.h"
#include "../world/WorldObject.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

// ── HexCoord serialization ────────────────────────────────────────────────────
static json coordToJson(int q, int r)  { return {{"q", q}, {"r", r}}; }
static void jsonToCoord(const json& j, int& q, int& r)
{
    q = j.at("q").get<int>();
    r = j.at("r").get<int>();
}

// ── TileSave ──────────────────────────────────────────────────────────────────
static json tileToJson(const TileSave& t)
{
    return {
        {"q", t.q}, {"r", t.r},
        {"ter", t.terrain},
        {"exp", t.explored},
        {"vis", t.visible},
        {"hid", t.heroId},
        {"tid", t.townId},
        {"rid", t.resourceId},
    };
}
static TileSave tileFromJson(const json& j)
{
    TileSave t;
    t.q          = j.at("q").get<int>();
    t.r          = j.at("r").get<int>();
    t.terrain    = j.at("ter").get<int>();
    t.explored   = j.at("exp").get<bool>();
    t.visible    = j.at("vis").get<bool>();
    t.heroId     = j.value("hid", 0u);
    t.townId     = j.value("tid", 0u);
    t.resourceId = j.value("rid", 0u);
    return t;
}

// ── DwellingSave ──────────────────────────────────────────────────────────────
static json dwellingToJson(const DwellingSave& d)
{
    return {{"bid", d.buildingId}, {"tier", d.tier}, {"path", d.path},
            {"avail", d.available}, {"accum", d.accumulated}};
}
static DwellingSave dwellingFromJson(const json& j)
{
    DwellingSave d;
    d.buildingId  = j.at("bid").get<int>();
    d.tier        = j.at("tier").get<int>();
    d.path        = j.at("path").get<int>();
    d.available   = j.at("avail").get<int>();
    d.accumulated = j.at("accum").get<int>();
    return d;
}

// ── ResourceNodeSave ──────────────────────────────────────────────────────────
static json resNodeToJson(const ResourceNodeSave& n)
{
    return {{"id",n.id},{"q",n.posQ},{"r",n.posR},
            {"type",n.type},{"amt",n.amount},{"dep",n.depleted},{"own",n.ownedBy}};
}
static ResourceNodeSave resNodeFromJson(const json& j)
{
    ResourceNodeSave n;
    n.id       = j.at("id").get<uint32_t>();
    n.posQ     = j.at("q").get<int>();
    n.posR     = j.at("r").get<int>();
    n.type     = j.at("type").get<int>();
    n.amount   = j.at("amt").get<int>();
    n.depleted = j.value("dep", false);
    n.ownedBy  = j.value("own", 0u);
    return n;
}

// ── WorldObjectSave ───────────────────────────────────────────────────────────
static json worldObjToJson(const WorldObjectSave& o)
{
    return {{"id",o.id},{"type",o.type},{"q",o.posQ},{"r",o.posR},
            {"val",o.value},{"res",o.resType},{"col",o.collected},
            {"qs",o.questState},{"lid",o.linkedId},{"avail",o.available},{"fac",o.faction}};
}
static WorldObjectSave worldObjFromJson(const json& j)
{
    WorldObjectSave o;
    o.id         = j.at("id").get<uint32_t>();
    o.type       = j.at("type").get<int>();
    o.posQ       = j.at("q").get<int>();
    o.posR       = j.at("r").get<int>();
    o.value      = j.at("val").get<int>();
    o.resType    = j.value("res", 0);
    o.collected  = j.value("col", false);
    o.questState = j.value("qs", 0);
    o.linkedId   = j.value("lid", 0u);
    o.available  = j.value("avail", 0);
    o.faction    = j.value("fac", 0);
    return o;
}

// ── HeroSave ──────────────────────────────────────────────────────────────────
static json heroToJson(const HeroSave& h)
{
    json spellArr = json::array();
    for (int s : h.knownSpells) spellArr.push_back(s);

    json skillArr = json::array();
    for (auto& sk : h.skillSlots)
        skillArr.push_back({{"d", sk.defId}, {"t", sk.tier}});

    json artEqArr = json::array();
    for (int a : h.artifactEquipped) artEqArr.push_back(a);

    json artInvArr = json::array();
    for (int a : h.artifactInventory) artInvArr.push_back(a);

    json armyArr = json::array();
    for (auto& [did, cnt] : h.army) armyArr.push_back({{"d", did}, {"c", cnt}});

    return {
        {"id", h.id}, {"name", h.name},
        {"faction", h.faction}, {"classId", h.classId},
        {"posQ", h.posQ}, {"posR", h.posR},
        {"movePool", h.movePool}, {"maxMove", h.maxMove},
        {"level", h.level}, {"attack", h.attack}, {"defense", h.defense},
        {"visionRange", h.visionRange},
        {"xp", h.xp}, {"xpToNext", h.xpToNext},
        {"hp", h.hp}, {"maxHp", h.maxHp},
        {"mana", h.mana}, {"maxMana", h.maxMana},
        {"lightPower", h.lightPower}, {"bloodPower", h.bloodPower},
        {"deathPower", h.deathPower}, {"naturePower", h.naturePower},
        {"forgePower", h.forgePower}, {"fleshPower", h.fleshPower},
        {"spells", spellArr}, {"skills", skillArr},
        {"artEq", artEqArr}, {"artInv", artInvArr},
        {"army", armyArr},
        {"battlesWon", h.battlesWon}, {"specialtyAtk", h.specialtyAtk},
        {"recyclerBonus", h.recyclerBonus}, {"livingRuneBonus", h.livingRuneBonus},
        {"phylacteryUsed", h.phylacteryUsed},
        {"ghostWalk", h.ghostWalkSpecialty}, {"blightAura", h.blightAuraSpecialty},
        {"infestation", h.infestationSpecialty}, {"efficient", h.efficientSpecialty},
        {"bloodScent", h.bloodScentSpecialty},
        {"garrisoned", h.isGarrisoned},
        {"onBoat", h.onBoat}, {"boatCount", h.boatCount},
    };
}
static HeroSave heroFromJson(const json& j)
{
    HeroSave h;
    h.id          = j.at("id").get<uint32_t>();
    h.name        = j.at("name").get<std::string>();
    h.faction     = j.at("faction").get<int>();
    h.classId     = j.value("classId", 0);
    h.posQ        = j.at("posQ").get<int>();
    h.posR        = j.at("posR").get<int>();
    h.movePool    = j.at("movePool").get<int>();
    h.maxMove     = j.at("maxMove").get<int>();
    h.level       = j.value("level", 1);
    h.attack      = j.value("attack", 1);
    h.defense     = j.value("defense", 1);
    h.visionRange = j.value("visionRange", 2);
    h.xp          = j.value("xp", 0);
    h.xpToNext    = j.value("xpToNext", 100);
    h.hp          = j.value("hp", 100);
    h.maxHp       = j.value("maxHp", 100);
    h.mana        = j.value("mana", 10);
    h.maxMana     = j.value("maxMana", 10);
    h.lightPower  = j.value("lightPower", 0);
    h.bloodPower  = j.value("bloodPower", 0);
    h.deathPower  = j.value("deathPower", 0);
    h.naturePower = j.value("naturePower", 0);
    h.forgePower  = j.value("forgePower", 0);
    h.fleshPower  = j.value("fleshPower", 0);

    if (j.contains("spells"))
        for (auto& s : j.at("spells")) h.knownSpells.push_back(s.get<int>());
    if (j.contains("skills"))
        for (auto& s : j.at("skills"))
            h.skillSlots.push_back({s.at("d").get<int>(), s.at("t").get<int>()});
    h.artifactEquipped.fill(0);
    if (j.contains("artEq")) {
        int i = 0;
        for (auto& a : j.at("artEq")) if (i < 8) h.artifactEquipped[i++] = a.get<int>();
    }
    if (j.contains("artInv"))
        for (auto& a : j.at("artInv")) h.artifactInventory.push_back(a.get<int>());
    if (j.contains("army"))
        for (auto& a : j.at("army"))
            h.army.push_back({a.at("d").get<int>(), a.at("c").get<int>()});
    h.battlesWon      = j.value("battlesWon",      0);
    h.specialtyAtk    = j.value("specialtyAtk",   0);
    h.recyclerBonus   = j.value("recyclerBonus",   0);
    h.livingRuneBonus = j.value("livingRuneBonus", 0);
    h.phylacteryUsed     = j.value("phylacteryUsed",  false);
    h.ghostWalkSpecialty = j.value("ghostWalk",        false);
    h.blightAuraSpecialty = j.value("blightAura",      false);
    h.infestationSpecialty = j.value("infestation",    false);
    h.efficientSpecialty   = j.value("efficient",      false);
    h.bloodScentSpecialty  = j.value("bloodScent",     false);
    h.isGarrisoned         = j.value("garrisoned",     false);
    h.onBoat               = j.value("onBoat",         false);
    h.boatCount            = j.value("boatCount",      0);
    return h;
}

// ── TownSave ──────────────────────────────────────────────────────────────────
static json townToJson(const TownSave& t)
{
    json dwArr = json::array();
    for (auto& d : t.dwellings) dwArr.push_back(dwellingToJson(d));

    json bldArr = json::array();
    for (int b : t.builtBuildings) bldArr.push_back(b);

    json incArr = json::array();
    for (int a : t.weeklyIncomeAmounts) incArr.push_back(a);

    json garArr = json::array();
    for (auto& [did, cnt] : t.garrison) garArr.push_back({{"d", did}, {"c", cnt}});

    return {
        {"id", t.id}, {"name", t.name},
        {"faction", t.faction},
        {"posQ", t.posQ}, {"posR", t.posR},
        {"ownerId", t.ownerId},
        {"buildings", bldArr},
        {"dwellings", dwArr},
        {"fortHP", t.fortHP}, {"fortMaxHP", t.fortMaxHP},
        {"garrison", garArr},
        {"income", incArr},
    };
}
static TownSave townFromJson(const json& j)
{
    TownSave t;
    t.id      = j.at("id").get<uint32_t>();
    t.name    = j.at("name").get<std::string>();
    t.faction = j.at("faction").get<int>();
    t.posQ    = j.at("posQ").get<int>();
    t.posR    = j.at("posR").get<int>();
    t.ownerId = j.at("ownerId").get<uint32_t>();
    t.fortHP    = j.value("fortHP", 0);
    t.fortMaxHP = j.value("fortMaxHP", 0);

    for (auto& b : j.at("buildings")) t.builtBuildings.push_back(b.get<int>());
    for (auto& d : j.at("dwellings")) t.dwellings.push_back(dwellingFromJson(d));
    if (j.contains("garrison"))
        for (auto& g : j.at("garrison"))
            t.garrison.push_back({g.at("d").get<int>(), g.at("c").get<int>()});

    if (j.contains("income")) {
        int i = 0;
        for (auto& v : j.at("income")) {
            if (i < RESOURCE_COUNT) t.weeklyIncomeAmounts[i++] = v.get<int>();
        }
    }
    return t;
}

// ── Public API ────────────────────────────────────────────────────────────────
bool SaveLoad::saveGame(const std::string& path, const GameSaveData& data)
{
    try {
        json j;
        j["version"]      = data.version;
        j["day"]          = data.day;
        j["week"]         = data.week;
        j["difficulty"]   = data.difficulty;
        j["activeHero"]   = data.activeHeroIdx;
        j["mapRadius"]    = data.mapRadius;
        j["mapSizeEnum"]  = data.mapSizeEnum;

        // Resources
        json resArr = json::array();
        for (int v : data.resourceAmounts) resArr.push_back(v);
        j["resources"] = resArr;

        // Heroes
        json heroArr = json::array();
        for (auto& h : data.heroes) heroArr.push_back(heroToJson(h));
        j["heroes"] = heroArr;

        json eHeroArr = json::array();
        for (auto& h : data.enemyHeroes) eHeroArr.push_back(heroToJson(h));
        j["enemyHeroes"] = eHeroArr;

        json dHeroArr = json::array();
        for (auto& h : data.defeatedHeroes) dHeroArr.push_back(heroToJson(h));
        j["defeatedHeroes"] = dHeroArr;

        // Towns
        json townArr = json::array();
        for (auto& t : data.towns) townArr.push_back(townToJson(t));
        j["towns"] = townArr;

        // World objects
        json objArr = json::array();
        for (auto& o : data.worldObjects) objArr.push_back(worldObjToJson(o));
        j["worldObjects"] = objArr;
        j["nextObjId"] = data.nextObjId;

        // Resource nodes
        json rnArr = json::array();
        for (auto& n : data.resourceNodes) rnArr.push_back(resNodeToJson(n));
        j["resNodes"] = rnArr;

        // Tiles
        json tileArr = json::array();
        for (auto& t : data.tiles) tileArr.push_back(tileToJson(t));
        j["tiles"] = tileArr;

        // Campaign
        if (data.campaign.active) {
            json camp;
            camp["active"]  = data.campaign.active;
            camp["mission"] = data.campaign.missionIdx;
            camp["order"]   = data.campaign.orderScore;
            camp["light"]   = data.campaign.lightScore;
            json decArr = json::array();
            for (auto& [id, choice] : data.campaign.decisions)
                decArr.push_back({{"id", id}, {"choice", choice}});
            camp["decisions"] = decArr;
            j["campaign"] = camp;
        }

        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << j.dump(2);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool SaveLoad::loadGame(const std::string& path, GameSaveData& out)
{
    try {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        json j;
        f >> j;

        out.version       = j.value("version", 1);
        out.day           = j.value("day", 1);
        out.week          = j.value("week", 1);
        out.difficulty    = j.value("difficulty", 1);
        out.activeHeroIdx = j.value("activeHero", 0);
        out.mapRadius     = j.value("mapRadius", 16);
        out.mapSizeEnum   = j.value("mapSizeEnum", 0);

        out.resourceAmounts.fill(0);
        if (j.contains("resources")) {
            int i = 0;
            for (auto& v : j.at("resources")) {
                if (i < RESOURCE_COUNT) out.resourceAmounts[i++] = v.get<int>();
            }
        }

        out.heroes.clear();
        if (j.contains("heroes"))
            for (auto& jh : j.at("heroes")) out.heroes.push_back(heroFromJson(jh));

        out.enemyHeroes.clear();
        if (j.contains("enemyHeroes"))
            for (auto& jh : j.at("enemyHeroes")) out.enemyHeroes.push_back(heroFromJson(jh));

        out.defeatedHeroes.clear();
        if (j.contains("defeatedHeroes"))
            for (auto& jh : j.at("defeatedHeroes")) out.defeatedHeroes.push_back(heroFromJson(jh));

        out.towns.clear();
        if (j.contains("towns"))
            for (auto& jt : j.at("towns")) out.towns.push_back(townFromJson(jt));

        out.worldObjects.clear();
        if (j.contains("worldObjects"))
            for (auto& jo : j.at("worldObjects")) out.worldObjects.push_back(worldObjFromJson(jo));
        out.nextObjId = j.value("nextObjId", 1u);

        out.resourceNodes.clear();
        if (j.contains("resNodes"))
            for (auto& jn : j.at("resNodes")) out.resourceNodes.push_back(resNodeFromJson(jn));

        out.tiles.clear();
        if (j.contains("tiles"))
            for (auto& jt : j.at("tiles")) out.tiles.push_back(tileFromJson(jt));

        // Campaign
        if (j.contains("campaign")) {
            const auto& camp = j.at("campaign");
            out.campaign.active      = camp.value("active",  false);
            out.campaign.missionIdx  = camp.value("mission", 0);
            out.campaign.orderScore  = camp.value("order",   0);
            out.campaign.lightScore  = camp.value("light",   0);
            if (camp.contains("decisions")) {
                for (auto& d : camp.at("decisions")) {
                    out.campaign.decisions.push_back(
                        {d.at("id").get<uint32_t>(), d.at("choice").get<int>()});
                }
            }
        }

        return true;
    }
    catch (...) {
        return false;
    }
}

// ── Helper: pack one Hero into HeroSave ───────────────────────────────────────
static HeroSave packHero(const Hero& h)
{
    HeroSave hs;
    hs.id          = h.id;
    hs.name        = h.name;
    hs.faction     = static_cast<int>(h.faction);
    hs.classId     = h.classId;
    hs.posQ        = h.pos.q;
    hs.posR        = h.pos.r;
    hs.movePool    = h.movePool;
    hs.maxMove     = h.maxMove;
    hs.level       = h.level;
    hs.attack      = h.attack;
    hs.defense     = h.defense;
    hs.visionRange = h.visionRange;
    hs.xp          = h.xp;
    hs.xpToNext    = h.xpToNext;
    hs.hp          = h.heroHp;
    hs.maxHp       = h.heroMaxHp;
    hs.mana        = h.mana;
    hs.maxMana     = h.maxMana;
    hs.lightPower  = h.lightPower;
    hs.bloodPower  = h.bloodPower;
    hs.deathPower  = h.deathPower;
    hs.naturePower = h.naturePower;
    hs.forgePower  = h.forgePower;
    hs.fleshPower  = h.fleshPower;
    hs.knownSpells = h.knownSpells;
    for (auto& s : h.skills.slots)
        hs.skillSlots.push_back({s.defId, static_cast<int>(s.tier)});
    hs.artifactEquipped = {};
    for (int i = 0; i < 8; ++i) hs.artifactEquipped[i] = h.artifacts.equippedIds[i];
    hs.artifactInventory = h.artifactInventory;
    for (auto& s : h.army) hs.army.push_back({s.defId, s.count});
    hs.battlesWon      = h.battlesWon;
    hs.specialtyAtk    = h.specialtyAtk;
    hs.recyclerBonus   = h.recyclerBonus;
    hs.livingRuneBonus = h.livingRuneBonus;
    hs.phylacteryUsed     = h.phylacteryUsed;
    hs.ghostWalkSpecialty = h.ghostWalkSpecialty;
    hs.blightAuraSpecialty = h.blightAuraSpecialty;
    hs.infestationSpecialty = h.infestationSpecialty;
    hs.efficientSpecialty   = h.efficientSpecialty;
    hs.bloodScentSpecialty  = h.bloodScentSpecialty;
    hs.isGarrisoned         = h.isGarrisoned;
    hs.onBoat               = h.onBoat;
    hs.boatCount            = h.boatCount;
    return hs;
}

// ── Helper: unpack HeroSave into Hero ─────────────────────────────────────────
static Hero unpackHero(const HeroSave& hs)
{
    Hero h;
    h.id          = hs.id;
    h.name        = hs.name;
    h.faction     = static_cast<FactionId>(hs.faction);
    h.classId     = hs.classId;
    h.pos         = {hs.posQ, hs.posR};
    h.movePool    = hs.movePool;
    h.maxMove     = hs.maxMove;
    h.level       = hs.level;
    h.attack      = hs.attack;
    h.defense     = hs.defense;
    h.visionRange = hs.visionRange;
    h.xp          = hs.xp;
    h.xpToNext    = hs.xpToNext;
    h.heroHp      = hs.hp;
    h.heroMaxHp   = hs.maxHp;
    h.mana        = hs.mana;
    h.maxMana     = hs.maxMana;
    h.lightPower  = hs.lightPower;
    h.bloodPower  = hs.bloodPower;
    h.deathPower  = hs.deathPower;
    h.naturePower = hs.naturePower;
    h.forgePower  = hs.forgePower;
    h.fleshPower  = hs.fleshPower;
    h.knownSpells = hs.knownSpells;
    for (auto& s : hs.skillSlots) {
        SkillInstance si;
        si.defId = s.defId;
        si.tier  = static_cast<SkillTier>(s.tier);
        if (h.skills.slots.size() < HeroSkills::MAX_SLOTS)
            h.skills.slots.push_back(si);
    }
    for (int i = 0; i < 8; ++i) h.artifacts.equippedIds[i] = hs.artifactEquipped[i];
    h.artifactInventory = hs.artifactInventory;
    for (auto& [did, cnt] : hs.army) h.army.push_back({did, cnt});
    h.battlesWon      = hs.battlesWon;
    h.specialtyAtk    = hs.specialtyAtk;
    h.recyclerBonus   = hs.recyclerBonus;
    h.livingRuneBonus = hs.livingRuneBonus;
    h.phylacteryUsed  = hs.phylacteryUsed;
    h.ghostWalkSpecialty = hs.ghostWalkSpecialty;
    h.blightAuraSpecialty = hs.blightAuraSpecialty;
    h.infestationSpecialty = hs.infestationSpecialty;
    h.efficientSpecialty   = hs.efficientSpecialty;
    h.bloodScentSpecialty  = hs.bloodScentSpecialty;
    h.isGarrisoned         = hs.isGarrisoned;
    h.onBoat               = hs.onBoat;
    h.boatCount            = hs.boatCount;
    return h;
}

// ── Pack live game state into SaveData ────────────────────────────────────────
GameSaveData SaveLoad::packState(const HexMap& map,
                                 const std::vector<Hero>& heroes,
                                 const std::vector<Hero>& enemyHeroes,
                                 const std::vector<Hero>& defeatedHeroes,
                                 const std::vector<Town>& towns,
                                 const std::vector<WorldObject>& worldObjects,
                                 const std::vector<ResourceNode>& resourceNodes,
                                 uint32_t nextObjId,
                                 const Resources& playerRes,
                                 int day, int week,
                                 MapSize mapSize,
                                 int difficulty,
                                 int activeHeroIdx)
{
    GameSaveData save;
    save.day           = day;
    save.week          = week;
    save.difficulty    = difficulty;
    save.activeHeroIdx = activeHeroIdx;
    save.mapRadius     = map.radius();
    save.mapSizeEnum   = static_cast<int>(mapSize);
    save.resourceAmounts = playerRes.amounts;
    save.nextObjId     = nextObjId;

    // Heroes
    for (auto& h : heroes)         save.heroes.push_back(packHero(h));
    for (auto& h : enemyHeroes)    save.enemyHeroes.push_back(packHero(h));
    for (auto& h : defeatedHeroes) save.defeatedHeroes.push_back(packHero(h));

    // Towns
    for (auto& t : towns) {
        TownSave ts;
        ts.id         = t.id;
        ts.name       = t.name;
        ts.faction    = static_cast<int>(t.faction);
        ts.posQ       = t.pos.q;
        ts.posR       = t.pos.r;
        ts.ownerId    = t.ownerId;
        ts.builtBuildings = t.builtBuildings;
        ts.fortHP     = t.fortHP;
        ts.fortMaxHP  = t.fortMaxHP;
        for (auto& s : t.garrison) ts.garrison.push_back({s.defId, s.count});
        ts.weeklyIncomeAmounts = t.weeklyIncome.amounts;
        for (auto& d : t.dwellings) {
            DwellingSave ds;
            ds.buildingId  = d.buildingId;
            ds.tier        = d.tier;
            ds.path        = static_cast<int>(d.path);
            ds.available   = d.available;
            ds.accumulated = d.accumulated;
            ts.dwellings.push_back(ds);
        }
        save.towns.push_back(ts);
    }

    // World objects
    for (auto& obj : worldObjects) {
        WorldObjectSave os;
        os.id         = obj.id;
        os.type       = static_cast<int>(obj.type);
        os.posQ       = obj.pos.q;
        os.posR       = obj.pos.r;
        os.value      = obj.value;
        os.resType    = static_cast<int>(obj.resourceType);
        os.collected  = obj.collected;
        os.questState = obj.questState;
        os.linkedId   = obj.linkedId;
        os.available  = obj.available;
        os.faction    = obj.faction;
        save.worldObjects.push_back(os);
    }

    // Resource nodes
    for (const auto& rn : resourceNodes) {
        ResourceNodeSave ns;
        ns.id       = rn.id;
        ns.posQ     = rn.pos.q;
        ns.posR     = rn.pos.r;
        ns.type     = static_cast<int>(rn.type);
        ns.amount   = rn.amount;
        ns.depleted = rn.depleted;
        ns.ownedBy  = rn.ownedBy;
        save.resourceNodes.push_back(ns);
    }

    // Tiles (fog of war + entity references)
    for (auto c : map.coords()) {
        const HexTile* tile = map.getTile(c);
        if (!tile) continue;
        TileSave ts;
        ts.q          = c.q;
        ts.r          = c.r;
        ts.terrain    = static_cast<int>(tile->terrain);
        ts.explored   = tile->explored;
        ts.visible    = tile->visible;
        ts.heroId     = tile->heroId;
        ts.townId     = tile->townId;
        ts.resourceId = tile->resourceId;
        save.tiles.push_back(ts);
    }

    return save;
}

// ── Unpack SaveData into live game state ──────────────────────────────────────
void SaveLoad::unpackState(const GameSaveData& save,
                           HexMap& map,
                           std::vector<Hero>& heroes,
                           std::vector<Hero>& enemyHeroes,
                           std::vector<Hero>& defeatedHeroes,
                           std::vector<Town>& towns,
                           std::vector<WorldObject>& worldObjects,
                           std::vector<ResourceNode>& resourceNodes,
                           uint32_t& nextObjId,
                           Resources& playerRes,
                           int& day, int& week)
{
    day       = save.day;
    week      = save.week;
    nextObjId = save.nextObjId;
    playerRes.amounts = save.resourceAmounts;

    // Restore tile fog/entity state (map must already be created with correct size)
    for (auto& ts : save.tiles) {
        HexTile* tile = map.getTile({ts.q, ts.r});
        if (!tile) continue;
        tile->terrain    = static_cast<Terrain>(ts.terrain);
        tile->explored   = ts.explored;
        tile->visible    = ts.visible;
        tile->heroId     = ts.heroId;
        tile->townId     = ts.townId;
        tile->resourceId = ts.resourceId;
    }

    // Restore heroes
    heroes.clear();
    for (auto& hs : save.heroes) heroes.push_back(unpackHero(hs));

    enemyHeroes.clear();
    for (auto& hs : save.enemyHeroes) enemyHeroes.push_back(unpackHero(hs));

    defeatedHeroes.clear();
    for (auto& hs : save.defeatedHeroes) defeatedHeroes.push_back(unpackHero(hs));

    // Restore world objects
    worldObjects.clear();
    for (auto& os : save.worldObjects) {
        WorldObject obj;
        obj.id           = os.id;
        obj.type         = static_cast<WorldObjectType>(os.type);
        obj.pos          = {os.posQ, os.posR};
        obj.value        = os.value;
        obj.resourceType = static_cast<ResourceType>(os.resType);
        obj.collected    = os.collected;
        obj.questState   = os.questState;
        obj.linkedId     = os.linkedId;
        obj.available    = os.available;
        obj.faction      = os.faction;
        worldObjects.push_back(obj);
    }

    // Restore resource nodes
    resourceNodes.clear();
    for (const auto& ns : save.resourceNodes) {
        ResourceNode rn;
        rn.id       = ns.id;
        rn.pos      = {ns.posQ, ns.posR};
        rn.type     = static_cast<ResourceType>(ns.type);
        rn.amount   = ns.amount;
        rn.depleted = ns.depleted;
        rn.ownedBy  = ns.ownedBy;
        resourceNodes.push_back(rn);
    }

    // Restore towns
    towns.clear();
    for (auto& ts : save.towns) {
        Town t;
        t.id         = ts.id;
        t.name       = ts.name;
        t.faction    = static_cast<FactionId>(ts.faction);
        t.pos        = {ts.posQ, ts.posR};
        t.ownerId    = ts.ownerId;
        t.builtBuildings = ts.builtBuildings;
        t.fortHP     = ts.fortHP;
        t.fortMaxHP  = ts.fortMaxHP;
        for (auto& [did, cnt] : ts.garrison) t.garrison.push_back({did, cnt});
        t.weeklyIncome.amounts = ts.weeklyIncomeAmounts;
        for (auto& ds : ts.dwellings) {
            DwellingState d;
            d.buildingId  = ds.buildingId;
            d.tier        = ds.tier;
            d.path        = static_cast<UpgradePath>(ds.path);
            d.available   = ds.available;
            d.accumulated = ds.accumulated;
            t.dwellings.push_back(d);
        }
        towns.push_back(t);
    }
}
