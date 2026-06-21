#include "../core/DevLog.h"
#include "SpriteBatch.h"
#include "gl_includes.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

// ── Embedded shaders ───────────────────────────────────────────────────────────
static const char* s_vertSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

out vec2 vUV;
out vec4 vColor;

uniform mat4 uProj;

void main()
{
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUV    = aUV;
    vColor = aColor;
}
)";

static const char* s_fragSrc = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;

out vec4 fragColor;

uniform sampler2D uTex;

void main()
{
    fragColor = texture(uTex, vUV) * vColor;
}
)";

// ── Destructor ─────────────────────────────────────────────────────────────────
SpriteBatch::~SpriteBatch()
{
    if (m_ibo) glDeleteBuffers(1, &m_ibo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

// ── Init ───────────────────────────────────────────────────────────────────────
bool SpriteBatch::init()
{
    if (!m_shader.loadFromSource(s_vertSrc, s_fragSrc)) return false;

    m_verts.reserve(MAX_SPRITES * 4);

    // VAO
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // VBO — dynamic, updated every frame
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_SPRITES * 4 * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

    // Attrib 0 — position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, x)));
    // Attrib 1 — UV
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, u)));
    // Attrib 2 — color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, r)));

    // IBO — static index pattern: 0,1,2, 2,3,0 repeated
    std::vector<unsigned int> indices;
    indices.reserve(MAX_SPRITES * 6);
    for (unsigned int i = 0; i < MAX_SPRITES; ++i) {
        unsigned int base = i * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 0);
    }
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        indices.size() * sizeof(unsigned int),
        indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    // Enable alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gLog("SpriteBatch initialized (max %d sprites)\n", MAX_SPRITES);
    return true;
}

// ── Begin ──────────────────────────────────────────────────────────────────────
void SpriteBatch::begin(const float* projMat4)
{
    m_active = true;
    m_verts.clear();
    m_currentTex = nullptr;
    memcpy(m_proj, projMat4, sizeof(float) * 16);
}

// ── Draw ───────────────────────────────────────────────────────────────────────
void SpriteBatch::draw(const Texture& tex, const SpriteCmd& cmd)
{
    if (!m_active) return;

    // Flush on texture swap
    if (m_currentTex && m_currentTex->id() != tex.id())
        flush();

    m_currentTex = &tex;

    if (static_cast<int>(m_verts.size()) >= MAX_SPRITES * 4)
        flush();

    // Build quad — 4 vertices, CCW
    // top-left, top-right, bot-right, bot-left
    float x0 = cmd.x,        y0 = cmd.y;
    float x1 = cmd.x + cmd.w, y1 = cmd.y + cmd.h;

    Vertex tl{ x0, y1, cmd.u0, cmd.v1, cmd.r, cmd.g, cmd.b, cmd.a };
    Vertex tr{ x1, y1, cmd.u1, cmd.v1, cmd.r, cmd.g, cmd.b, cmd.a };
    Vertex br{ x1, y0, cmd.u1, cmd.v0, cmd.r, cmd.g, cmd.b, cmd.a };
    Vertex bl{ x0, y0, cmd.u0, cmd.v0, cmd.r, cmd.g, cmd.b, cmd.a };

    m_verts.push_back(tl);
    m_verts.push_back(tr);
    m_verts.push_back(br);
    m_verts.push_back(bl);
}

// ── End ────────────────────────────────────────────────────────────────────────
void SpriteBatch::end()
{
    flush();
    m_active = false;
}

// ── Flush ──────────────────────────────────────────────────────────────────────
void SpriteBatch::flush()
{
    if (m_verts.empty() || !m_currentTex) return;

    m_shader.bind();
    m_shader.setMat4("uProj", m_proj);
    m_shader.setInt("uTex", 0);
    m_currentTex->bind(0);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        m_verts.size() * sizeof(Vertex), m_verts.data());

    int quadCount = static_cast<int>(m_verts.size()) / 4;
    glDrawElements(GL_TRIANGLES, quadCount * 6, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    m_shader.unbind();
    m_verts.clear();
    m_currentTex = nullptr;
}
