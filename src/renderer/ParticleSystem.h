#pragma once
#include <cstdint>
#include <vector>
#include <imgui.h>

// Particle presets — visual identity per event type
enum class ParticlePreset : uint8_t
{
    Hit,        // melee/ranged impact: orange sparks
    SpellLight, // light/holy: white-gold rays
    SpellDark,  // death/darkness: purple wisps
    SpellNature,// nature/poison: green motes
    SpellBlood, // blood magic: crimson droplets
    SpellRune,  // runic/mechanical: blue-white arcs
    SpellFlesh, // fleshcraft: sickly green-yellow
    Death,      // unit dies: grey ash
    LevelUp,    // hero levels: gold burst
    Pickup,     // resource / gold pickup: yellow sparkle
    Heal,       // heal: green crosses
    Morale,     // morale bonus: gold stars
};

struct Particle
{
    float  x,  y;      // screen-space position
    float  vx, vy;     // velocity (px/s)
    float  life;       // remaining lifetime [0 .. maxLife]
    float  maxLife;
    float  size;       // radius in pixels
    float  sizeShrink; // size reduction per second
    ImU32  color;      // RGBA8 start color
    ImU32  colorFade;  // RGBA8 end color (interpolated)
    bool   gravity;    // apply downward drift
};

class ParticleSystem
{
public:
    // Emit a burst at screen position (sx, sy)
    void emit(float sx, float sy, ParticlePreset preset, int count = 0);

    // Update all live particles — call once per frame
    void update(float dt);

    // Render into an ImDrawList (background or foreground)
    void render(ImDrawList* dl) const;

    // True if no live particles remain
    bool empty() const { return m_particles.empty(); }

    // Remove all particles (e.g., on state change)
    void clear() { m_particles.clear(); }

private:
    static ImU32 lerpColor(ImU32 a, ImU32 b, float t);

    std::vector<Particle> m_particles;

    // Mersenne-free fast RNG — good enough for visuals
    uint32_t m_rng = 0xDEADBEEFu;
    float    nextF();           // [0, 1)
    float    nextSigned();      // [-1, 1)
};
