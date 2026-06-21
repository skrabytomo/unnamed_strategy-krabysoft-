#pragma once
#include "gl_includes.h"
#include <vector>
#include "Shader.h"
#include "Texture.h"

// One sprite to be drawn this frame
struct SpriteCmd
{
    float x, y;          // world position (top-left)
    float w, h;          // size in pixels
    float u0, v0;        // UV top-left   (0..1)
    float u1, v1;        // UV bot-right  (0..1)
    float r, g, b, a;    // tint color
    float z;             // depth layer (lower = drawn first)
};

// SpriteBatch — collect sprites each frame, flush once per texture swap
// Usage:
//   batch.begin(projMatrix);
//   batch.draw(...);
//   batch.end();
class SpriteBatch
{
public:
    static constexpr int MAX_SPRITES = 4096;

    SpriteBatch() = default;
    ~SpriteBatch();

    bool init();
    void begin(const float* projMat4);
    void draw(const Texture& tex, const SpriteCmd& cmd);
    void end();

private:
    void flush();

    // Vertex layout: x y u v r g b a  (8 floats per vertex, 4 verts per quad)
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    Shader  m_shader;
    GLuint  m_vao = 0;
    GLuint  m_vbo = 0;
    GLuint  m_ibo = 0;  // index buffer — two triangles per quad

    std::vector<Vertex>    m_verts;
    const Texture*         m_currentTex = nullptr;
    float                  m_proj[16]   = {};
    bool                   m_active     = false;
};
