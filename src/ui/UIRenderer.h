#pragma once
#include "../renderer/gl_includes.h"
#include <string>
#include <vector>
#include "UITypes.h"
#include "../renderer/Shader.h"

struct ImDrawList;

// UIRenderer — immediate-mode style, called each frame
// Draws filled rects, borders, simple text (text = placeholder until font system added)
class UIRenderer
{
public:
    bool init(int screenW, int screenH);
    void resize(int screenW, int screenH);

    // Call before any UI draw calls
    void beginFrame();
    void endFrame();

    // ── Primitives ─────────────────────────────────────────────────────────────
    void drawRect(const Rect& r, UIColor fill);
    void drawRect(const Rect& r, UIColor fill, UIColor border, float borderW = 1.0f);
    void drawRectRounded(const Rect& r, UIColor fill, float radius = 4.0f);

    // Bar (HP, mana, morale etc.)
    void drawBar(const Rect& r, float fraction,
                 UIColor fill, UIColor bg, UIColor border);

    // Queue a text draw; call flushText() during an active ImGui frame to render.
    void drawText(const std::string& text, float x, float y,
                  UIColor color, float size = 14.0f);

    // Flush queued text draws using ImGui's font — must be called between
    // ImGui::NewFrame() and ImGui::EndFrame() (i.e. inside beginImGuiFrame/end).
    void flushText(ImDrawList* dl);

    // Tooltip background
    void drawTooltip(const Rect& r);

    int screenW() const { return m_screenW; }
    int screenH() const { return m_screenH; }

    struct QuadVert { float x, y, r, g, b, a; };

private:
    void flushQuads();

    Shader  m_shader;
    GLuint  m_vao = 0, m_vbo = 0, m_ibo = 0;

    static constexpr int MAX_QUADS = 2048;
    QuadVert m_verts[MAX_QUADS * 4];
    int      m_quadCount = 0;

    struct TextCmd { float x, y, size; UIColor color; std::string text; };
    std::vector<TextCmd> m_textQueue;

    float m_proj[16] = {};
    int   m_screenW  = 1280;
    int   m_screenH  = 720;
};
