#include "MapEditor.h"
#include "../world/WorldGen.h"
#include "../world/FogOfWar.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <cstdio>

// WorldObjectType names in enum order
static const char* kWorldObjNames[] = {
    "SpellScroll",
    "ArtifactChest",
    "XPShrine",
    "ResourceCache",
    "Observatory",
    "StatShrine",
    "BanditCamp",
    "UnitDwelling",
    "QuestGiver",
    "QuestTarget",
    "ForestShrine",
    "HighlandRuin",
    "HolyFountain",
    "Oasis",
    "Campfire",
    "LavaCrystal",
    "SwampAltar",
    "TreasureChest",
    "Crypt",
    "Utopia",
    "Landmark",
    "CursedGround",
    "NeutralOutpost",
    "WitchHut",
    "Stables",
    "TreeOfKnowledge",
    "Barrier",
    "ChokeGuard",
    "Shipyard",
    "FishingHouse",
};
static constexpr int kWorldObjNameCount = 30;

static const char* kFactionNames[] = {
    "HolyOrder","CrimsonWardens","Thornkin","EternalEmpire",
    "Bloodsworn","Voidkin","IronAssembly","Amalgamate","Convergence"
};

static const char* kResourceNames[] = {
    "Gold","Iron","FaithStones","BloodEssence","VerdantSap","Mercury"
};

bool MapEditor::init(int sw, int sh)
{
    m_screenW = sw;
    m_screenH = sh;
    return true;
}

void MapEditor::resize(int sw, int sh)
{
    m_screenW = sw;
    m_screenH = sh;
}

void MapEditor::shutdown() {}

// ── Main render (called each frame in Editor state) ───────────────────────────
void MapEditor::renderImGui(HexMap& map,
                             std::vector<Town>& towns,
                             std::vector<ResourceNode>& resources,
                             std::vector<HexCoord>& heroStarts,
                             std::vector<WorldObject>& worldObjects)
{
    // ── Main menu bar ──────────────────────────────────────────────────────────
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Map", "Ctrl+S"))
                saveMap(m_filePath, map, towns, resources, heroStarts, worldObjects);
            if (ImGui::MenuItem("Load Map", "Ctrl+O"))
                loadMap(m_filePath, map, towns, resources, heroStarts, worldObjects);
            ImGui::Separator();
            if (ImGui::MenuItem("New (clear map")) {
                map.forEach([](HexTile& t){ t.terrain = Terrain::Plains; });
                towns.clear(); resources.clear(); heroStarts.clear();
                worldObjects.clear();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    drawToolbar();
    drawTerrainPalette();
    drawPropertiesPanel(map, m_selectedHex);
    drawTownPanel(towns);
    drawResourcePanel(resources);
    drawTriggerPanel();
    drawMapMetaPanel();
    drawGenPanel(map, towns, resources, heroStarts, worldObjects);
    drawObjectPanel(worldObjects);
}

// ── Hex click handler ─────────────────────────────────────────────────────────
void MapEditor::onHexClicked(HexCoord h,
                              HexMap& map,
                              std::vector<Town>& towns,
                              std::vector<ResourceNode>& resources,
                              std::vector<HexCoord>& heroStarts,
                              std::vector<WorldObject>& worldObjects)
{
    m_selectedHex = h;

    switch (m_tool) {
        case EditorTool::Terrain: {
            // Paint with brush radius
            int brushR = m_brushRadius - 1;
            auto cells = HexGrid::range(h, brushR);
            for (auto& c : cells) {
                HexTile* tile = map.getTile(c);
                if (tile) tile->terrain = m_paintTerrain;
            }
            break;
        }
        case EditorTool::Town:
            placeTown(h, map, towns);
            break;
        case EditorTool::Resource:
            placeResource(h, map, resources);
            break;
        case EditorTool::HeroStart:
            if (std::find(heroStarts.begin(), heroStarts.end(), h) == heroStarts.end())
                heroStarts.push_back(h);
            break;
        case EditorTool::WorldObject:
            placeWorldObject(h, map, worldObjects);
            break;
        case EditorTool::Erase:
            eraseAt(h, map, towns, resources, worldObjects);
            break;
        default: break;
    }
}

// ── Toolbar ───────────────────────────────────────────────────────────────────
void MapEditor::drawToolbar()
{
    ImGui::SetNextWindowPos({(float)m_screenW * 0.5f - 230.f, 24.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({460.f, 44.f}, ImGuiCond_Always);
    ImGui::Begin("##toolbar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

    struct ToolBtn { const char* label; EditorTool tool; };
    static const ToolBtn kButtons[] = {
        {"Terrain [T]",  EditorTool::Terrain},
        {"Town [W]",     EditorTool::Town},
        {"Resource [R]", EditorTool::Resource},
        {"Start [S]",    EditorTool::HeroStart},
        {"Trigger [G]",  EditorTool::Trigger},
        {"Object [O]",   EditorTool::WorldObject},
        {"Erase [E]",    EditorTool::Erase},
    };
    for (auto& b : kButtons) {
        bool active = m_tool == b.tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.5f,0.8f,1.f));
        if (ImGui::Button(b.label)) m_tool = b.tool;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::End();
}

// ── Terrain palette ───────────────────────────────────────────────────────────
void MapEditor::drawTerrainPalette()
{
    if (m_tool != EditorTool::Terrain) return;

    ImGui::SetNextWindowPos({4.f, 30.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({140.f, 370.f}, ImGuiCond_Always);
    ImGui::Begin("Terrain", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    struct TerrainEntry { Terrain t; const char* name; float r,g,b; };
    static const TerrainEntry kTerrains[] = {
        {Terrain::Plains,         "Plains",          0.75f,0.85f,0.4f},
        {Terrain::Forest,         "Forest",          0.15f,0.55f,0.2f},
        {Terrain::Highland,       "Highland",        0.55f,0.45f,0.3f},
        {Terrain::Corrupted,      "Corrupted",       0.5f, 0.2f,0.5f},
        {Terrain::Toxic,          "Toxic",           0.4f, 0.6f,0.2f},
        {Terrain::Sacred,         "Sacred",          0.9f, 0.85f,0.5f},
        {Terrain::Industrial,     "Industrial",      0.5f, 0.5f,0.5f},
        {Terrain::Rocky,          "Rocky",           0.45f,0.4f,0.35f},
        {Terrain::Swamp,          "Swamp",           0.3f, 0.45f,0.25f},
        {Terrain::Water,          "Water",           0.2f, 0.4f,0.75f},
        {Terrain::Volcanic,       "Volcanic",        0.7f, 0.25f,0.1f},
        {Terrain::Barren,         "Barren",          0.65f,0.55f,0.3f},
        {Terrain::Wasteland,      "Wasteland",       0.55f,0.45f,0.3f},
        {Terrain::CorruptedForest,"Corrupt.Forest",  0.3f, 0.2f,0.45f},
        {Terrain::FleshZone,      "FleshZone",       0.75f,0.35f,0.35f},
        {Terrain::Mountain,       "Mountain",        0.55f,0.50f,0.45f},
    };

    for (auto& te : kTerrains) {
        bool sel = m_paintTerrain == te.t;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(te.r,te.g,te.b,1.f));
        else     ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(te.r*0.7f,te.g*0.7f,te.b*0.7f,1.f));
        if (ImGui::Button(te.name, {128.f, 18.f})) m_paintTerrain = te.t;
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::SliderInt("Brush", &m_brushRadius, 1, 3);

    ImGui::End();
}

// ── Properties panel ──────────────────────────────────────────────────────────
void MapEditor::drawPropertiesPanel(HexMap& map, HexCoord hovered)
{
    ImGui::SetNextWindowPos({(float)m_screenW - 200.f, 30.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({196.f, 200.f}, ImGuiCond_Always);
    ImGui::Begin("Properties", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    if (map.inBounds(hovered)) {
        const HexTile* tile = map.getTile(hovered);
        if (tile) {
            ImGui::Text("Hex (%d, %d)", hovered.q, hovered.r);
            ImGui::Text("Terrain: %d", static_cast<int>(tile->terrain));
            ImGui::Text("TownID: %u", tile->townId);
            ImGui::Text("ResID:  %u", tile->resourceId);
            ImGui::Text("Explored: %s", tile->explored ? "yes" : "no");
        }
    } else {
        ImGui::TextDisabled("No tile selected");
    }
    ImGui::End();
}

// ── Town panel ────────────────────────────────────────────────────────────────
void MapEditor::drawTownPanel(std::vector<Town>& towns)
{
    if (m_tool != EditorTool::Town) return;
    ImGui::SetNextWindowPos({4.f, 30.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({180.f, 200.f}, ImGuiCond_Always);
    ImGui::Begin("Towns", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("Placed towns: %d", (int)towns.size());
    for (auto& t : towns)
        ImGui::Text("  [%u] %s (%d,%d)", t.id, t.name.c_str(), t.pos.q, t.pos.r);
    ImGui::Separator();
    ImGui::TextWrapped("Click a land hex to place a town.");
    ImGui::End();
}

// ── Resource panel ────────────────────────────────────────────────────────────
void MapEditor::drawResourcePanel(std::vector<ResourceNode>& resources)
{
    if (m_tool != EditorTool::Resource) return;
    ImGui::SetNextWindowPos({4.f, 30.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({200.f, 280.f}, ImGuiCond_Always);
    ImGui::Begin("Resources", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    // Type selector
    static const char* kResNames[] = {
        "Gold", "Iron", "FaithStones", "BloodEssence",
        "VerdantSap", "Mercury"
    };
    static const int kDefaultAmounts[] = { 250, 3, 2, 2, 2, 2 };
    int typeIdx = static_cast<int>(m_resType);
    if (typeIdx < 0 || typeIdx >= 6) typeIdx = 0;
    if (ImGui::Combo("Type", &typeIdx, kResNames, 6)) {
        m_resType   = static_cast<ResourceType>(typeIdx);
        m_resAmount = kDefaultAmounts[typeIdx];
    }
    ImGui::InputInt("Amount", &m_resAmount);
    if (m_resAmount < 1) m_resAmount = 1;

    ImGui::Separator();
    ImGui::Text("Placed: %d", (int)resources.size());
    for (auto& r : resources) {
        int rt = static_cast<int>(r.type);
        const char* rn = (rt >= 0 && rt < 6) ? kResNames[rt] : "?";
        ImGui::Text("  [%u] %s x%d (%d,%d)", r.id, rn, r.amount, r.pos.q, r.pos.r);
    }
    ImGui::Separator();
    ImGui::TextWrapped("Click a land hex to place a resource node.");
    ImGui::End();
}

// ── Trigger panel ─────────────────────────────────────────────────────────────
void MapEditor::drawTriggerPanel()
{
    if (m_tool != EditorTool::Trigger) return;
    ImGui::SetNextWindowPos({4.f, 30.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({300.f, 380.f}, ImGuiCond_Always);
    ImGui::Begin("Triggers", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("Triggers: %d", (int)m_triggers.size());
    for (int i = 0; i < (int)m_triggers.size(); ++i) {
        auto& t = m_triggers[i];
        bool sel = (m_selectedTrigger == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.8f,0.2f,1));
        if (ImGui::Selectable(t.funcName.c_str(), sel))
            m_selectedTrigger = i;
        if (sel) ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::Text("Add trigger:");
    ImGui::InputText("Function", m_trigFuncBuf, sizeof(m_trigFuncBuf));
    ImGui::InputTextMultiline("Body", m_trigBodyBuf, sizeof(m_trigBodyBuf),
                              {280.f, 80.f});

    static int tileQ = 0, tileR = 0;
    ImGui::InputInt("Q", &tileQ);
    ImGui::InputInt("R", &tileR);

    if (ImGui::Button("Add EnterTile Trigger") && m_trigFuncBuf[0]) {
        MapTriggerEntry te;
        te.type       = "enterTile";
        te.funcName   = m_trigFuncBuf;
        te.scriptBody = m_trigBodyBuf;
        te.q = tileQ; te.r = tileR;
        m_triggers.push_back(te);
        memset(m_trigFuncBuf, 0, sizeof(m_trigFuncBuf));
        memset(m_trigBodyBuf, 0, sizeof(m_trigBodyBuf));
    }

    if (m_selectedTrigger >= 0 && m_selectedTrigger < (int)m_triggers.size()) {
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            m_triggers.erase(m_triggers.begin() + m_selectedTrigger);
            m_selectedTrigger = -1;
        }
    }
    ImGui::End();
}

// ── Map metadata panel ────────────────────────────────────────────────────────
void MapEditor::drawMapMetaPanel()
{
    ImGui::SetNextWindowPos({(float)m_screenW - 200.f, 234.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({196.f, 180.f}, ImGuiCond_Always);
    ImGui::Begin("Map Info", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::InputText("Name",   m_nameBuffer,   sizeof(m_nameBuffer));
    ImGui::InputText("Author", m_authorBuffer, sizeof(m_authorBuffer));
    ImGui::InputTextMultiline("Desc", m_descBuffer, sizeof(m_descBuffer),
                              {180.f, 60.f});
    ImGui::InputText("File", m_filePath, sizeof(m_filePath));
    ImGui::End();
}

// ── World gen panel ───────────────────────────────────────────────────────────
void MapEditor::drawGenPanel(HexMap& map,
                              std::vector<Town>& towns,
                              std::vector<ResourceNode>& resources,
                              std::vector<HexCoord>& heroStarts,
                              std::vector<WorldObject>& worldObjects)
{
    ImGui::SetNextWindowPos({(float)m_screenW - 200.f, 418.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({196.f, 330.f}, ImGuiCond_Always);
    ImGui::Begin("ProGen", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("Procedural Gen");

    // Template picker
    static const char* kTemplateNames[] = {
        "Custom", "Balanced Hexagon", "Jebus Cross 2.0",
        "Jebus Cross 3.0", "Large Jebus", "Ring Island"
    };
    if (ImGui::Combo("Template", &m_genTemplateIdx, kTemplateNames, 6)) {
        if (m_genTemplateIdx > 0) {
            const MapTemplate& tmpl = kMapTemplates[m_genTemplateIdx - 1];
            m_genShapeIdx   = static_cast<int>(tmpl.shape);
            m_genSizeIdx    = static_cast<int>(tmpl.size);
            m_genPlayers    = tmpl.playerCount;
            m_genWaterRatio = tmpl.waterRatio;
        }
    }

    // Shape (disabled when a template is selected)
    static const char* kShapes[] = {"Hexagon", "Jebus Cross", "Jebus Cross 3", "Ring"};
    bool isCustom = (m_genTemplateIdx == 0);
    if (!isCustom) ImGui::BeginDisabled();
    ImGui::Combo("Shape", &m_genShapeIdx, kShapes, 4);
    if (!isCustom) ImGui::EndDisabled();

    static const char* kSizes[] = {"Small","Medium","Large","XLarge"};
    ImGui::Combo("Map Size", &m_genSizeIdx, kSizes, 4);

    ImGui::InputInt("Players", &m_genPlayers);

    int waterPct = static_cast<int>(m_genWaterRatio * 100.f);
    if (ImGui::SliderInt("Water %", &waterPct, 5, 40))
        m_genWaterRatio = waterPct / 100.f;

    ImGui::InputInt("Seed", &m_genSeed);
    ImGui::SliderFloat("Res Density", &m_genResDensity, 0.25f, 3.0f);

    if (ImGui::Button("Generate!", {180.f, 30.f})) {
        WorldGenParams p;
        p.seed            = static_cast<uint32_t>(m_genSeed);
        p.size            = static_cast<MapSize>(m_genSizeIdx);
        p.playerCount     = std::max(1, std::min(m_genPlayers, 8));
        p.resourceDensity = m_genResDensity;
        p.waterRatio      = m_genWaterRatio;
        p.shape           = static_cast<MapShape>(m_genShapeIdx);

        map.create(p.size);
        auto result = WorldGen::generate(map, p);

        towns        = std::move(result.towns);
        resources    = std::move(result.resources);
        heroStarts   = std::move(result.startPositions);
        worldObjects = std::move(result.worldObjects);

        // Pin town IDs onto tiles
        for (auto& t : towns)
            if (HexTile* tile = map.getTile(t.pos)) tile->townId = t.id;
        for (auto& r : resources)
            if (HexTile* tile = map.getTile(r.pos)) tile->resourceId = r.id;
    }
    ImGui::End();
}

// ── World object panel ────────────────────────────────────────────────────────
void MapEditor::drawObjectPanel(std::vector<WorldObject>& worldObjects)
{
    if (m_tool != EditorTool::WorldObject) return;

    ImGui::SetNextWindowPos({4.f, 30.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({196.f, 320.f}, ImGuiCond_Always);
    ImGui::Begin("Objects", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    // Type combo
    int typeIdx = static_cast<int>(m_objType);
    if (ImGui::Combo("Type", &typeIdx, kWorldObjNames, kWorldObjNameCount))
        m_objType = static_cast<WorldObjectType>(typeIdx);

    // Value
    ImGui::InputInt("Value", &m_objValue);

    // Faction combo (visible for types that use faction)
    bool showFaction = (m_objType == WorldObjectType::UnitDwelling ||
                        m_objType == WorldObjectType::Crypt ||
                        m_objType == WorldObjectType::Utopia ||
                        m_objType == WorldObjectType::NeutralOutpost);
    if (showFaction) {
        int fIdx = static_cast<int>(m_objFaction);
        if (ImGui::Combo("Faction", &fIdx, kFactionNames, 9))
            m_objFaction = static_cast<uint8_t>(fIdx);
    }

    // Resource combo (visible for ResourceCache)
    if (m_objType == WorldObjectType::ResourceCache) {
        int rIdx = static_cast<int>(m_objResourceType);
        if (ImGui::Combo("Resource", &rIdx, kResourceNames, 6))
            m_objResourceType = static_cast<ResourceType>(rIdx);
    }

    ImGui::Separator();
    ImGui::Text("Placed: %d", (int)worldObjects.size());
    ImGui::BeginChild("ObjList", {184.f, 120.f}, true);
    int toErase = -1;
    for (int i = 0; i < (int)worldObjects.size(); ++i) {
        auto& wo = worldObjects[i];
        const char* name = (static_cast<int>(wo.type) < kWorldObjNameCount)
                            ? kWorldObjNames[static_cast<int>(wo.type)]
                            : "?";
        ImGui::Text("%s (%d,%d)", name, wo.pos.q, wo.pos.r);
        ImGui::SameLine();
        ImGui::PushID(i);
        if (ImGui::SmallButton("X")) toErase = i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (toErase >= 0)
        worldObjects.erase(worldObjects.begin() + toErase);

    ImGui::Separator();
    ImGui::TextWrapped("Click land hex to place.");
    ImGui::End();
}

// ── Entity placement helpers ──────────────────────────────────────────────────
void MapEditor::placeTown(HexCoord h, HexMap& map, std::vector<Town>& towns)
{
    HexTile* tile = map.getTile(h);
    if (!tile || tile->terrain == Terrain::Water) return;
    if (tile->townId != 0) return;  // already a town here

    Town t;
    t.id      = m_nextTownId++;
    t.name    = "New Town";
    t.faction = FactionId::HolyOrder;
    t.pos     = h;
    t.ownerId = 0;
    towns.push_back(t);
    tile->townId = t.id;
}

void MapEditor::placeResource(HexCoord h, HexMap& map,
                               std::vector<ResourceNode>& resources)
{
    HexTile* tile = map.getTile(h);
    if (!tile || tile->terrain == Terrain::Water) return;
    if (tile->resourceId != 0) return;

    ResourceNode r;
    r.id     = m_nextResourceId++;
    r.pos    = h;
    r.type   = m_resType;
    r.amount = m_resAmount;
    resources.push_back(r);
    tile->resourceId = r.id;
}

void MapEditor::placeWorldObject(HexCoord h, HexMap& map,
                                  std::vector<WorldObject>& worldObjects)
{
    HexTile* tile = map.getTile(h);
    if (!tile || tile->terrain == Terrain::Water) return;

    // Guard: no existing world object at this position
    for (const auto& wo : worldObjects)
        if (wo.pos == h) return;

    WorldObject obj;
    obj.id           = m_nextWorldObjId++;
    obj.type         = m_objType;
    obj.pos          = h;
    obj.value        = m_objValue;
    obj.faction      = m_objFaction;
    obj.resourceType = m_objResourceType;
    obj.questState   = 0;
    worldObjects.push_back(obj);

    if (m_objType == WorldObjectType::Barrier)
        tile->blocked = true;
}

void MapEditor::eraseAt(HexCoord h, HexMap& map,
                         std::vector<Town>& towns,
                         std::vector<ResourceNode>& resources,
                         std::vector<WorldObject>& worldObjects)
{
    HexTile* tile = map.getTile(h);
    if (!tile) return;

    if (tile->townId != 0) {
        uint32_t id = tile->townId;
        towns.erase(std::remove_if(towns.begin(), towns.end(),
                    [id](const Town& t){ return t.id == id; }), towns.end());
        tile->townId = 0;
    }
    if (tile->resourceId != 0) {
        uint32_t id = tile->resourceId;
        resources.erase(std::remove_if(resources.begin(), resources.end(),
                        [id](const ResourceNode& r){ return r.id == id; }),
                        resources.end());
        tile->resourceId = 0;
    }
    // Also erase any world object at this hex
    for (const auto& wo : worldObjects)
        if (wo.pos == h && wo.type == WorldObjectType::Barrier)
            if (tile) tile->blocked = false;
    worldObjects.erase(std::remove_if(worldObjects.begin(), worldObjects.end(),
                       [h](const WorldObject& wo){ return wo.pos == h; }),
                       worldObjects.end());
}

// ── Save / Load ───────────────────────────────────────────────────────────────
bool MapEditor::saveMap(const std::string& path,
                         const HexMap& map,
                         const std::vector<Town>& towns,
                         const std::vector<ResourceNode>& resources,
                         const std::vector<HexCoord>& heroStarts,
                         const std::vector<WorldObject>& worldObjects) const
{
    MapFile mf;
    mf.meta.name        = m_nameBuffer;
    mf.meta.author      = m_authorBuffer;
    mf.meta.description = m_descBuffer;
    mf.meta.playerCount = (int)heroStarts.size();
    mf.meta.size        = static_cast<MapSize>(m_genSizeIdx);
    mf.towns            = towns;
    mf.resources        = resources;
    mf.heroStarts       = heroStarts;
    mf.triggers         = m_triggers;
    mf.worldObjects     = worldObjects;

    for (auto c : map.coords()) {
        const HexTile* tile = map.getTile(c);
        if (!tile) continue;
        MapFile::TileEntry te;
        te.q          = c.q; te.r = c.r;
        te.terrain    = static_cast<int>(tile->terrain);
        te.townId     = tile->townId;
        te.resourceId = tile->resourceId;
        mf.tiles.push_back(te);
    }

    return MapFormat::save(path, mf);
}

bool MapEditor::loadMap(const std::string& path,
                         HexMap& map,
                         std::vector<Town>& towns,
                         std::vector<ResourceNode>& resources,
                         std::vector<HexCoord>& heroStarts,
                         std::vector<WorldObject>& worldObjects)
{
    MapFile mf;
    if (!MapFormat::load(path, mf)) return false;
    MapFormat::applyToMap(map, mf, towns, resources);
    heroStarts   = mf.heroStarts;
    worldObjects = mf.worldObjects;
    m_triggers   = mf.triggers;
    for (const auto& wo : worldObjects)
        if (wo.type == WorldObjectType::Barrier && !wo.collected)
            if (HexTile* t = map.getTile(wo.pos)) t->blocked = true;
    strncpy(m_nameBuffer,   mf.meta.name.c_str(),        sizeof(m_nameBuffer)-1);
    strncpy(m_authorBuffer, mf.meta.author.c_str(),      sizeof(m_authorBuffer)-1);
    strncpy(m_descBuffer,   mf.meta.description.c_str(), sizeof(m_descBuffer)-1);
    return true;
}
