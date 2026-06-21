#include "ParticleSystem.h"
#include <cmath>
#include <algorithm>

// ── Fast inline RNG ────────────────────────────────────────────────────────────
float ParticleSystem::nextF()
{
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float ParticleSystem::nextSigned()
{
    return nextF() * 2.0f - 1.0f;
}

// ── Color lerp (component-wise in linear space, no gamma) ─────────────────────
ImU32 ParticleSystem::lerpColor(ImU32 a, ImU32 b, float t)
{
    auto lerp8 = [&](int ca, int cb) -> int {
        return static_cast<int>(ca + (cb - ca) * t);
    };
    int ar = (a >>  0) & 0xFF, br = (b >>  0) & 0xFF;
    int ag = (a >>  8) & 0xFF, bg = (b >>  8) & 0xFF;
    int ab = (a >> 16) & 0xFF, bb = (b >> 16) & 0xFF;
    int aa = (a >> 24) & 0xFF, ba = (b >> 24) & 0xFF;
    return IM_COL32(lerp8(ar,br), lerp8(ag,bg), lerp8(ab,bb), lerp8(aa,ba));
}

// ── Preset table ───────────────────────────────────────────────────────────────
struct PresetDef
{
    int   defaultCount;
    ImU32 colorStart;
    ImU32 colorEnd;
    float speedMin, speedMax;
    float lifeMin,  lifeMax;
    float sizeMin,  sizeMax;
    float sizeShrink;
    bool  gravity;
    float spreadAngle; // radians half-angle; 0 = full 360
};

static const PresetDef kPresets[] = {
    // Hit — orange sparks, fast, short-lived
    { 12, IM_COL32(255,160, 40,240), IM_COL32(200, 50, 0,  0),
      80.f,240.f, 0.25f,0.55f, 2.f,5.f, 6.f, true,  0.f },
    // SpellLight — gold/white rays, medium speed, medium life
    { 18, IM_COL32(255,245,180,220), IM_COL32(255,210, 80,  0),
      60.f,160.f, 0.5f, 1.0f, 2.f,4.f, 3.f, false, 0.f },
    // SpellDark — purple wisps, slow, long-lived
    { 14, IM_COL32(160, 60,220,200), IM_COL32( 80, 20,120,  0),
      30.f,100.f, 0.7f, 1.4f, 2.f,5.f, 2.f, false, 0.f },
    // SpellNature — green motes, slow drift upward
    { 16, IM_COL32( 80,220, 80,200), IM_COL32( 30,100, 30,  0),
      20.f, 80.f, 0.8f, 1.6f, 2.f,4.f, 1.5f,false, 0.f },
    // SpellBlood — crimson droplets, fast downward
    { 14, IM_COL32(220, 20, 40,220), IM_COL32(140, 10, 10,  0),
      60.f,180.f, 0.4f, 0.9f, 2.f,5.f, 4.f, true,  0.f },
    // SpellRune — blue-white arcs, fast burst
    { 16, IM_COL32(120,180,255,220), IM_COL32( 40, 80,200,  0),
      80.f,200.f, 0.3f, 0.8f, 1.5f,4.f, 5.f,false, 0.f },
    // SpellFlesh — sickly green-yellow, slow drift
    { 12, IM_COL32(160,220, 60,200), IM_COL32( 80,140, 20,  0),
      20.f, 70.f, 0.9f, 1.8f, 2.f,5.f, 1.f, true,  0.f },
    // Death — grey ash, slow upward drift
    { 20, IM_COL32(180,180,180,180), IM_COL32( 60, 60, 60,  0),
      10.f, 50.f, 1.0f, 2.0f, 2.f,6.f, 1.f, false, 0.f },
    // LevelUp — gold burst, fast outward, medium life
    { 30, IM_COL32(255,230, 60,255), IM_COL32(255,160, 20,  0),
      80.f,220.f, 0.6f, 1.2f, 3.f,7.f, 4.f, false, 0.f },
    // Pickup — yellow sparkle, fast burst then fade
    { 10, IM_COL32(255,210, 60,240), IM_COL32(200,140, 10,  0),
      60.f,160.f, 0.3f, 0.6f, 2.f,5.f, 6.f, true,  0.f },
    // Heal — green crosses / rings, slow upward
    { 12, IM_COL32( 80,240,120,220), IM_COL32( 30,150, 60,  0),
      20.f, 60.f, 0.7f, 1.3f, 2.f,4.f, 2.f, false, 0.f },
    // Morale — gold stars, burst outward
    { 16, IM_COL32(255,220, 60,255), IM_COL32(220,120, 10,  0),
      50.f,150.f, 0.5f, 1.0f, 2.f,6.f, 3.f, false, 0.f },
};
static_assert(sizeof(kPresets)/sizeof(kPresets[0]) == 12,
              "kPresets must match ParticlePreset enum count");

// ── emit ───────────────────────────────────────────────────────────────────────
void ParticleSystem::emit(float sx, float sy, ParticlePreset preset, int count)
{
    const PresetDef& p = kPresets[static_cast<int>(preset)];
    int n = (count > 0) ? count : p.defaultCount;

    m_particles.reserve(m_particles.size() + static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        float angle = nextF() * 6.2831853f;  // full circle
        float speed = p.speedMin + nextF() * (p.speedMax - p.speedMin);

        Particle pt;
        pt.x  = sx;
        pt.y  = sy;
        pt.vx = std::cos(angle) * speed;
        pt.vy = std::sin(angle) * speed;

        // Death and Heal drift upward on average
        if (preset == ParticlePreset::Death || preset == ParticlePreset::Heal)
            pt.vy -= speed * 0.6f;

        pt.life    = p.lifeMin + nextF() * (p.lifeMax - p.lifeMin);
        pt.maxLife = pt.life;
        pt.size    = p.sizeMin + nextF() * (p.sizeMax - p.sizeMin);
        pt.sizeShrink = p.sizeShrink;
        pt.color   = p.colorStart;
        pt.colorFade = p.colorEnd;
        pt.gravity = p.gravity;

        m_particles.push_back(pt);
    }
}

// ── update ─────────────────────────────────────────────────────────────────────
void ParticleSystem::update(float dt)
{
    static constexpr float kGravity = 120.0f;  // px/s²

    for (auto& p : m_particles) {
        p.life -= dt;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        if (p.gravity) p.vy += kGravity * dt;
        // Air friction
        p.vx *= (1.0f - 2.5f * dt);
        p.vy *= (1.0f - 2.0f * dt);
        p.size -= p.sizeShrink * dt;
        if (p.size < 0.5f) p.size = 0.5f;
    }

    // Remove dead particles
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        m_particles.end());
}

// ── render ─────────────────────────────────────────────────────────────────────
void ParticleSystem::render(ImDrawList* dl) const
{
    if (!dl || m_particles.empty()) return;

    for (const auto& p : m_particles) {
        float alpha = std::max(0.0f, p.life / p.maxLife);
        ImU32 col   = lerpColor(p.colorFade, p.color, alpha);
        // Fade alpha smoothly
        int a = (col >> 24) & 0xFF;
        a = static_cast<int>(a * alpha);
        col = (col & 0x00FFFFFFu) | (static_cast<ImU32>(a) << 24);

        dl->AddCircleFilled({p.x, p.y}, p.size, col);
    }
}
