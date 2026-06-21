#include "UIRenderer.h"
#include <imgui.h>
#include <string.h>
#include <stdio.h>
#include <cmath>
#include <algorithm>

static const char* s_uiVert = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uProj;
out vec4 vColor;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* s_uiFrag = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

bool UIRenderer::init(int screenW, int screenH)
{
    m_screenW = screenW;
    m_screenH = screenH;

    if (!m_shader.loadFromSource(s_uiVert, s_uiFrag)) return false;

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(m_verts), nullptr, GL_DYNAMIC_DRAW);

    // pos (2) + color (4) = 6 floats
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVert),
        reinterpret_cast<void*>(offsetof(QuadVert, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVert),
        reinterpret_cast<void*>(offsetof(QuadVert, r)));

    // Static index buffer
    unsigned int indices[MAX_QUADS * 6];
    for (int i = 0; i < MAX_QUADS; ++i) {
        unsigned int b = i * 4;
        indices[i*6+0]=b+0; indices[i*6+1]=b+1; indices[i*6+2]=b+2;
        indices[i*6+3]=b+2; indices[i*6+4]=b+3; indices[i*6+5]=b+0;
    }
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);
    resize(screenW, screenH);
    return true;
}

void UIRenderer::resize(int screenW, int screenH)
{
    m_screenW = screenW;
    m_screenH = screenH;

    // Orthographic: origin top-left, Y down
    float l=0, r=(float)screenW, t=0, b=(float)screenH;
    memset(m_proj, 0, sizeof(m_proj));
    m_proj[0]  =  2.0f/(r-l);
    m_proj[5]  =  2.0f/(t-b);
    m_proj[10] = -1.0f;
    m_proj[12] = -(r+l)/(r-l);
    m_proj[13] = -(t+b)/(t-b);
    m_proj[15] =  1.0f;
}

void UIRenderer::beginFrame()
{
    m_quadCount = 0;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void UIRenderer::endFrame()
{
    flushQuads();
    glEnable(GL_DEPTH_TEST);
}

// ── Push one colored quad ──────────────────────────────────────────────────────
static void pushQuad(UIRenderer::QuadVert* verts, int& count,
                     float x, float y, float w, float h, UIColor c)
{
    if (count >= 2048) return;
    int b = count * 4;
    // TL TR BR BL
    verts[b+0] = {x,   y,   c.r,c.g,c.b,c.a};
    verts[b+1] = {x+w, y,   c.r,c.g,c.b,c.a};
    verts[b+2] = {x+w, y+h, c.r,c.g,c.b,c.a};
    verts[b+3] = {x,   y+h, c.r,c.g,c.b,c.a};
    count++;
}

void UIRenderer::drawRect(const Rect& r, UIColor fill)
{
    pushQuad(m_verts, m_quadCount, r.x, r.y, r.w, r.h, fill);
}

void UIRenderer::drawRect(const Rect& r, UIColor fill, UIColor border, float bw)
{
    pushQuad(m_verts, m_quadCount, r.x, r.y, r.w, r.h, fill);
    // 4 border quads (top, bottom, left, right)
    pushQuad(m_verts, m_quadCount, r.x,       r.y,        r.w, bw,  border);
    pushQuad(m_verts, m_quadCount, r.x,       r.bottom()-bw, r.w, bw, border);
    pushQuad(m_verts, m_quadCount, r.x,       r.y,        bw,  r.h, border);
    pushQuad(m_verts, m_quadCount, r.right()-bw, r.y,     bw,  r.h, border);
}

void UIRenderer::drawRectRounded(const Rect& r, UIColor fill, float /*radius*/)
{
    // For now same as drawRect — proper rounded corners need more geometry
    drawRect(r, fill);
}

void UIRenderer::drawBar(const Rect& r, float fraction,
                          UIColor fill, UIColor bg, UIColor border)
{
    fraction = std::max(0.0f, std::min(1.0f, fraction));
    drawRect(r, bg, border, 1.0f);
    if (fraction > 0.0f) {
        Rect filled = {r.x+1, r.y+1, (r.w-2)*fraction, r.h-2};
        drawRect(filled, fill);
    }
}

void UIRenderer::drawText(const std::string& text, float x, float y,
                           UIColor color, float size)
{
    m_textQueue.push_back({x, y, size, color, text});
}

void UIRenderer::flushText(ImDrawList* dl)
{
    if (!dl || m_textQueue.empty()) return;
    ImFont* font = ImGui::GetFont();
    for (const auto& cmd : m_textQueue) {
        ImU32 col = IM_COL32(
            static_cast<int>(std::min(cmd.color.r, 1.0f) * 255.0f),
            static_cast<int>(std::min(cmd.color.g, 1.0f) * 255.0f),
            static_cast<int>(std::min(cmd.color.b, 1.0f) * 255.0f),
            static_cast<int>(std::min(cmd.color.a, 1.0f) * 255.0f));
        dl->AddText(font, cmd.size, {cmd.x, cmd.y}, col, cmd.text.c_str());
    }
    m_textQueue.clear();
}

void UIRenderer::drawTooltip(const Rect& r)
{
    UIColor bg   = UIColor::hex(UITheme::BG_PANEL, 0.95f);
    UIColor bord = UIColor::hex(UITheme::GOLD, 0.8f);
    drawRect(r, bg, bord, 1.0f);
}

void UIRenderer::flushQuads()
{
    if (m_quadCount == 0) return;
    m_shader.bind();
    m_shader.setMat4("uProj", m_proj);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        m_quadCount * 4 * sizeof(QuadVert), m_verts);
    glDrawElements(GL_TRIANGLES, m_quadCount * 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    m_shader.unbind();
    m_quadCount = 0;
}
