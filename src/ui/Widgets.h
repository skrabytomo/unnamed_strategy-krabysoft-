#pragma once
#include <string>
#include <vector>
#include <memory>
#include "UITypes.h"
#include "UIRenderer.h"

// ── Base widget ────────────────────────────────────────────────────────────────
class Widget
{
public:
    Widget() = default;
    virtual ~Widget() = default;

    virtual void draw(UIRenderer& r) = 0;
    virtual bool onMouseMove(float x, float y)  { (void)x;(void)y; return false; }
    virtual bool onMouseDown(float x, float y)  { (void)x;(void)y; return false; }
    virtual bool onMouseUp(float x, float y)    { (void)x;(void)y; return false; }

    bool  visible  = true;
    bool  enabled  = true;
    Rect  bounds   = {};
    std::string tooltip;
};

// ── Label ──────────────────────────────────────────────────────────────────────
class Label : public Widget
{
public:
    std::string text;
    UIColor     color    = UIColor::hex(UITheme::TEXT_PRIMARY);
    float       fontSize = 14.0f;
    bool        bold     = false;

    Label() = default;
    Label(const std::string& t, Rect r,
          UIColor c = UIColor::hex(UITheme::TEXT_PRIMARY)) {
        text=t; bounds=r; color=c;
    }

    void draw(UIRenderer& rdr) override {
        if (!visible) return;
        rdr.drawText(text, bounds.x, bounds.y, color, fontSize);
    }
};

// ── Button ─────────────────────────────────────────────────────────────────────
class Button : public Widget
{
public:
    std::string text;
    UICallback  onClick;
    UIColor     colorNormal  = UIColor::hex(UITheme::BG_PANEL);
    UIColor     colorHover   = UIColor::hex(UITheme::BG_HOVER);
    UIColor     colorPressed = UIColor::hex(0x2A2A3A);
    UIColor     colorBorder  = UIColor::hex(UITheme::BORDER);
    UIColor     colorText    = UIColor::hex(UITheme::TEXT_PRIMARY);
    float       fontSize     = 13.0f;

    bool hovered = false;
    bool pressed = false;

    Button() = default;
    Button(const std::string& t, Rect r, UICallback cb = nullptr) {
        text=t; bounds=r; onClick=cb;
    }

    void draw(UIRenderer& rdr) override {
        if (!visible) return;
        UIColor bg = hovered ? (pressed ? colorPressed : colorHover) : colorNormal;
        UIColor brd = hovered ? UIColor::hex(UITheme::BORDER_BRIGHT) : colorBorder;
        if (!enabled) bg = UIColor::hex(UITheme::BG_PANEL, 0.5f);
        rdr.drawRect(bounds, bg, brd, 1.0f);
        // Center text
        float tx = bounds.x + 6.0f;
        float ty = bounds.y + (bounds.h - fontSize * 0.75f) * 0.5f;
        UIColor tc = enabled ? colorText : UIColor::hex(UITheme::TEXT_DISABLED);
        rdr.drawText(text, tx, ty, tc, fontSize);
    }

    bool onMouseMove(float x, float y) override {
        hovered = bounds.contains(x, y);
        return hovered;
    }
    bool onMouseDown(float x, float y) override {
        if (bounds.contains(x, y)) { pressed = true; return true; }
        return false;
    }
    bool onMouseUp(float x, float y) override {
        if (pressed && bounds.contains(x, y) && enabled && onClick)
            onClick();
        pressed = false;
        return bounds.contains(x, y);
    }
};

// ── Panel ──────────────────────────────────────────────────────────────────────
class Panel : public Widget
{
public:
    UIColor bgColor     = UIColor::hex(UITheme::BG_PANEL, 0.92f);
    UIColor borderColor = UIColor::hex(UITheme::BORDER);
    float   borderW     = 1.0f;
    std::string title;

    std::vector<std::shared_ptr<Widget>> children;

    Panel() = default;
    Panel(Rect r) { bounds = r; }

    void addChild(std::shared_ptr<Widget> w) { children.push_back(w); }

    void draw(UIRenderer& rdr) override {
        if (!visible) return;
        rdr.drawRect(bounds, bgColor, borderColor, borderW);
        // Title bar
        if (!title.empty()) {
            Rect titleBar{bounds.x, bounds.y, bounds.w, 24.0f};
            rdr.drawRect(titleBar, UIColor::hex(UITheme::BG_PANEL_DARK),
                         borderColor, 1.0f);
            rdr.drawText(title, bounds.x + 8.0f, bounds.y + 5.0f,
                         UIColor::hex(UITheme::GOLD), 13.0f);
        }
        for (auto& c : children) c->draw(rdr);
    }

    bool onMouseMove(float x, float y) override {
        for (auto& c : children) c->onMouseMove(x, y);
        return bounds.contains(x, y);
    }
    bool onMouseDown(float x, float y) override {
        for (auto& c : children) if (c->onMouseDown(x, y)) return true;
        return bounds.contains(x, y);
    }
    bool onMouseUp(float x, float y) override {
        for (auto& c : children) c->onMouseUp(x, y);
        return bounds.contains(x, y);
    }
};

// ── ProgressBar ────────────────────────────────────────────────────────────────
class ProgressBar : public Widget
{
public:
    float   value    = 1.0f;  // 0..1
    UIColor fillColor  = UIColor::hex(UITheme::HP_GREEN);
    UIColor bgColor    = UIColor::hex(0x1A1A24);
    UIColor borderColor= UIColor::hex(UITheme::BORDER);

    ProgressBar() = default;
    ProgressBar(Rect r, UIColor fill) { bounds=r; fillColor=fill; }

    void draw(UIRenderer& rdr) override {
        if (!visible) return;
        rdr.drawBar(bounds, value, fillColor, bgColor, borderColor);
    }
};

// ── Tooltip ────────────────────────────────────────────────────────────────────
class TooltipWidget : public Widget
{
public:
    std::string text;
    float       fontSize = 12.0f;

    TooltipWidget() = default;

    void show(const std::string& t, float x, float y) {
        text = t;
        float w = std::max(120.0f, t.size() * fontSize * 0.55f);
        bounds = {x, y - 30.0f, w, 26.0f};
        visible = true;
    }
    void hide() { visible = false; }

    void draw(UIRenderer& rdr) override {
        if (!visible || text.empty()) return;
        rdr.drawTooltip(bounds);
        rdr.drawText(text, bounds.x + 6.0f, bounds.y + 7.0f,
                     UIColor::hex(UITheme::TEXT_PRIMARY), fontSize);
    }
};
