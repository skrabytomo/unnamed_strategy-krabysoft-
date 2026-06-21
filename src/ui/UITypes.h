#pragma once
#include <functional>
#include <string>

// ── Color ──────────────────────────────────────────────────────────────────────
struct UIColor
{
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    static UIColor rgba(float r, float g, float b, float a = 1.0f) { return {r,g,b,a}; }
    static UIColor hex(unsigned int hex, float a = 1.0f) {
        return {
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >>  8) & 0xFF) / 255.0f,
            ((hex >>  0) & 0xFF) / 255.0f,
            a
        };
    }
};

// ── Game UI palette (dark medieval strategy aesthetic) ─────────────────────────
namespace UITheme {
    // Backgrounds
    constexpr unsigned BG_DARK       = 0x0D0D12;  // near-black with blue tint
    constexpr unsigned BG_PANEL      = 0x1A1A24;  // panel background
    constexpr unsigned BG_PANEL_DARK = 0x12121A;  // deeper panel
    constexpr unsigned BG_HOVER      = 0x252535;  // hover state

    // Borders
    constexpr unsigned BORDER        = 0x3A3A50;  // default border
    constexpr unsigned BORDER_BRIGHT = 0x6060A0;  // active/focused border

    // Text
    constexpr unsigned TEXT_PRIMARY  = 0xE8E4D8;  // warm off-white
    constexpr unsigned TEXT_SECONDARY= 0x9090A8;  // muted grey-blue
    constexpr unsigned TEXT_DISABLED = 0x484858;

    // Accent colors — faction-flavored
    constexpr unsigned GOLD          = 0xC8A84B;  // Holy Order / general positive
    constexpr unsigned BLOOD_RED     = 0x8B1A1A;  // Bloodsworn
    constexpr unsigned VOID_PURPLE   = 0x4B2060;  // Voidkin
    constexpr unsigned IRON_GREY     = 0x607080;  // Iron Assembly
    constexpr unsigned NATURE_GREEN  = 0x2D6040;  // Thornkin
    constexpr unsigned DEATH_TEAL    = 0x205048;  // Eternal Empire

    // Status
    constexpr unsigned HP_GREEN      = 0x2D8B3A;
    constexpr unsigned HP_LOW        = 0xCC4A1A;
    constexpr unsigned MANA_BLUE     = 0x2060C0;
    constexpr unsigned MORALE_GOLD   = 0xC8902A;
    constexpr unsigned DANGER_RED    = 0xCC2222;
}

// ── Rect ──────────────────────────────────────────────────────────────────────
struct Rect
{
    float x = 0, y = 0, w = 0, h = 0;

    bool contains(float px, float py) const {
        return px >= x && px <= x+w && py >= y && py <= y+h;
    }
    float right()  const { return x + w; }
    float bottom() const { return y + h; }
    float centerX() const { return x + w * 0.5f; }
    float centerY() const { return y + h * 0.5f; }
};

// ── Callbacks ─────────────────────────────────────────────────────────────────
using UICallback   = std::function<void()>;
using UIIntCallback = std::function<void(int)>;
