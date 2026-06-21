#pragma once
#include <string>
#include <vector>
#include "../world/HexMap.h"
#include "../world/HexGrid.h"
#include "../world/WorldObject.h"
#include "../world/WorldGen.h"
#include "../town/Town.h"
#include "../data/ResourceNode.h"
#include "../data/MapFormat.h"

// ── Editor tools ──────────────────────────────────────────────────────────────
enum class EditorTool
{
    Terrain,
    Town,
    Resource,
    HeroStart,
    Trigger,
    Erase,
    WorldObject,
};

// ── MapEditor ─────────────────────────────────────────────────────────────────
// ImGui-based editor overlay. Rendered on top of the world map when active.
// The game's render path calls renderImGui() after ImGui::NewFrame().
class MapEditor
{
public:
    // Must be called after ImGui context is created
    bool init(int screenW, int screenH);
    void resize(int screenW, int screenH);
    void shutdown();

    // Called each frame when in Editor state — draws all ImGui windows
    void renderImGui(HexMap& map,
                     std::vector<Town>& towns,
                     std::vector<ResourceNode>& resources,
                     std::vector<HexCoord>& heroStarts,
                     std::vector<WorldObject>& worldObjects);

    // Called when user left-clicks on the hex grid in editor mode
    void onHexClicked(HexCoord h,
                      HexMap& map,
                      std::vector<Town>& towns,
                      std::vector<ResourceNode>& resources,
                      std::vector<HexCoord>& heroStarts,
                      std::vector<WorldObject>& worldObjects);

    // Save/load the current map in editor to/from file
    bool saveMap(const std::string& path,
                 const HexMap& map,
                 const std::vector<Town>& towns,
                 const std::vector<ResourceNode>& resources,
                 const std::vector<HexCoord>& heroStarts,
                 const std::vector<WorldObject>& worldObjects) const;

    bool loadMap(const std::string& path,
                 HexMap& map,
                 std::vector<Town>& towns,
                 std::vector<ResourceNode>& resources,
                 std::vector<HexCoord>& heroStarts,
                 std::vector<WorldObject>& worldObjects);

    EditorTool   activeTool()         const { return m_tool; }
    Terrain      selectedTerrain()    const { return m_paintTerrain; }
    bool         isHoverHighlight()   const { return true; }

    // Trigger entries edited in the editor (exported to MapFile on save)
    const std::vector<MapTriggerEntry>& triggers() const { return m_triggers; }

private:
    void drawToolbar();
    void drawTerrainPalette();
    void drawPropertiesPanel(HexMap& map, HexCoord hovered);
    void drawTownPanel(std::vector<Town>& towns);
    void drawResourcePanel(std::vector<ResourceNode>& resources);
    void drawTriggerPanel();
    void drawMapMetaPanel();
    void drawGenPanel(HexMap& map,
                      std::vector<Town>& towns,
                      std::vector<ResourceNode>& resources,
                      std::vector<HexCoord>& heroStarts,
                      std::vector<WorldObject>& worldObjects);
    void drawObjectPanel(std::vector<WorldObject>& worldObjects);

    void placeTown(HexCoord h, HexMap& map, std::vector<Town>& towns);
    void placeResource(HexCoord h, HexMap& map,
                       std::vector<ResourceNode>& resources);
    void placeWorldObject(HexCoord h, HexMap& map,
                          std::vector<WorldObject>& worldObjects);
    void eraseAt(HexCoord h, HexMap& map,
                 std::vector<Town>& towns,
                 std::vector<ResourceNode>& resources,
                 std::vector<WorldObject>& worldObjects);

    EditorTool m_tool          = EditorTool::Terrain;
    Terrain    m_paintTerrain  = Terrain::Plains;
    HexCoord   m_selectedHex   = {-999, -999};

    // Map metadata being edited
    MapMetadata m_meta;
    char m_nameBuffer[128]   = "Unnamed Map";
    char m_authorBuffer[64]  = "";
    char m_descBuffer[256]   = "";

    // Trigger editing
    std::vector<MapTriggerEntry> m_triggers;
    int m_selectedTrigger = -1;
    char m_trigFuncBuf[64]  = "";
    char m_trigBodyBuf[1024]= "";

    // World gen panel state
    int      m_genSeed        = 42;
    int      m_genPlayers     = 2;
    float    m_genResDensity  = 1.0f;
    int      m_genSizeIdx     = 1;   // index into Small/Medium/Large/XLarge
    int      m_genShapeIdx    = 0;   // 0=Hexagon, 1=JebusCross, 2=Ring
    int      m_genTemplateIdx = 0;   // 0=Custom, 1-4=named templates
    float    m_genWaterRatio  = 0.15f;

    // File dialog state
    char m_filePath[256] = "maps/untitled.map";

    // ID counter for entities placed in editor
    uint32_t m_nextTownId       = 100;
    uint32_t m_nextResourceId   = 1000;
    uint32_t m_nextWorldObjId   = 5000;

    // Terrain brush radius (1=single, 2=radius-1 7 tiles, 3=radius-2 19 tiles)
    int m_brushRadius = 1;

    // WorldObject tool state
    WorldObjectType m_objType         = WorldObjectType::XPShrine;
    int             m_objValue        = 100;
    uint8_t         m_objFaction      = 0;
    ResourceType    m_objResourceType = ResourceType::Gold;

    int m_screenW = 1280, m_screenH = 720;
};
