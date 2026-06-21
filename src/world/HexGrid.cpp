#include "HexGrid.h"
#include <cmath>
#include <algorithm>

static constexpr float SQRT3 = 1.7320508f;

HexGrid::HexGrid(float hexSize, float originX, float originY)
    : m_size(hexSize), m_ox(originX), m_oy(originY)
{}

// ── Flat-top axial → world ─────────────────────────────────────────────────────
// Flat-top layout matrix:
//   x = size * (3/2 * q)
//   y = size * (sqrt(3)/2 * q + sqrt(3) * r)
void HexGrid::hexToWorld(HexCoord h, float& wx, float& wy) const
{
    wx = m_ox + m_size * (1.5f * h.q);
    wy = m_oy + m_size * (SQRT3 * 0.5f * h.q + SQRT3 * h.r);
}

// ── World → flat-top axial ─────────────────────────────────────────────────────
// Inverse of above, then round to nearest hex using cube rounding
HexCoord HexGrid::worldToHex(float wx, float wy) const
{
    float px = wx - m_ox;
    float py = wy - m_oy;

    float fq = (2.0f / 3.0f * px) / m_size;
    float fr = (-1.0f / 3.0f * px + SQRT3 / 3.0f * py) / m_size;
    float fs = -fq - fr;

    // Cube rounding
    int rq = static_cast<int>(std::round(fq));
    int rr = static_cast<int>(std::round(fr));
    int rs = static_cast<int>(std::round(fs));

    float dq = std::abs(rq - fq);
    float dr = std::abs(rr - fr);
    float ds = std::abs(rs - fs);

    if (dq > dr && dq > ds)
        rq = -rr - rs;
    else if (dr > ds)
        rr = -rq - rs;

    return { rq, rr };
}

// ── 6 axial direction vectors (flat-top) ──────────────────────────────────────
static const HexCoord s_dirs[6] = {
    { 1,  0}, { 1, -1}, { 0, -1},
    {-1,  0}, {-1,  1}, { 0,  1}
};

std::vector<HexCoord> HexGrid::neighbors(HexCoord h)
{
    std::vector<HexCoord> result;
    result.reserve(6);
    for (auto& d : s_dirs)
        result.push_back({ h.q + d.q, h.r + d.r });
    return result;
}

// ── Distance (cube distance) ───────────────────────────────────────────────────
int HexGrid::distance(HexCoord a, HexCoord b)
{
    return (std::abs(a.q - b.q) + std::abs(a.r - b.r) +
            std::abs(a.s() - b.s())) / 2;
}

// ── Ring at exact radius ───────────────────────────────────────────────────────
std::vector<HexCoord> HexGrid::ring(HexCoord center, int radius)
{
    std::vector<HexCoord> result;
    if (radius == 0) { result.push_back(center); return result; }

    // Start at direction 4, move radius steps
    HexCoord h = { center.q + s_dirs[4].q * radius,
                   center.r + s_dirs[4].r * radius };
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < radius; ++j) {
            result.push_back(h);
            h = { h.q + s_dirs[i].q, h.r + s_dirs[i].r };
        }
    }
    return result;
}

// ── All hexes within radius ────────────────────────────────────────────────────
std::vector<HexCoord> HexGrid::range(HexCoord center, int radius)
{
    std::vector<HexCoord> result;
    for (int q = -radius; q <= radius; ++q) {
        int r1 = std::max(-radius, -q - radius);
        int r2 = std::min( radius, -q + radius);
        for (int r = r1; r <= r2; ++r)
            result.push_back({ center.q + q, center.r + r });
    }
    return result;
}

// ── 6 corner world positions for flat-top hex ─────────────────────────────────
// Angle for corner i (flat-top): 60 * i degrees
void HexGrid::hexCorners(HexCoord h, float out[12]) const
{
    float cx, cy;
    hexToWorld(h, cx, cy);
    for (int i = 0; i < 6; ++i) {
        float angle = (3.14159265f / 180.0f) * (60.0f * i);
        out[i * 2 + 0] = cx + m_size * std::cos(angle);
        out[i * 2 + 1] = cy + m_size * std::sin(angle);
    }
}
