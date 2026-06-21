#pragma once
#include "../renderer/gl_includes.h"
#include "../renderer/Texture.h"
#include <string>
#include "HexMap.h"
#include "HexGrid.h"
#include "../renderer/Camera2D.h"
#include "../renderer/Shader.h"

class HexMapRenderer
{
public:
    HexMapRenderer() = default;
    ~HexMapRenderer();

    bool init(float hexSize, const std::string& basePath = "");
    void update(float dt) { m_time += dt; }
    void render(const HexMap& map, const Camera2D& camera,
                HexCoord hovered, HexCoord selected,
                bool fogDisabled = false);

    const HexGrid& grid() const { return m_grid; }

private:
    void buildHexMesh();
    void drawHex(float cx, float cy, float r, float g, float b, float a, float scale = 1.0f);

    HexGrid  m_grid{ 32.0f };
    Shader   m_shader;
    float    m_time  = 0.0f;

    GLuint   m_vao  = 0;
    GLuint   m_vbo  = 0;

    static constexpr int NUM_TERRAIN    = 16;
    static constexpr int MAX_VARIANTS   = 24;
    static constexpr int HEX_VERTS      =  8; // center + 6 corners + close

    Texture  m_terrainTex[NUM_TERRAIN][MAX_VARIANTS];
    int      m_variantCount[NUM_TERRAIN] = {};
};
