#pragma once
#include <vector>
#include <functional>

// ── Axial hex coordinate ───────────────────────────────────────────────────────
struct HexCoord
{
    int q = 0;  // column
    int r = 0;  // row

    bool operator==(const HexCoord& o) const { return q == o.q && r == o.r; }
    bool operator!=(const HexCoord& o) const { return !(*this == o); }

    // Cube coordinate s (derived)
    int s() const { return -q - r; }
};

// Hash for use in unordered_map
struct HexCoordHash {
    size_t operator()(const HexCoord& h) const {
        return std::hash<int>()(h.q) ^ (std::hash<int>()(h.r) << 16);
    }
};

// ── Flat-top hex layout (HoMM3 style) ─────────────────────────────────────────
// Flat-top: hexes have flat edges on top/bottom, pointy on left/right
// Axial coordinate system — standard for hex grids

class HexGrid
{
public:
    // size   = hex radius (center to corner) in pixels
    // origin = world position of hex (0,0)
    HexGrid(float hexSize, float originX = 0.0f, float originY = 0.0f);

    // ── Coordinate conversion ──────────────────────────────────────────────────
    // Axial → world center of that hex
    void hexToWorld(HexCoord h, float& wx, float& wy) const;

    // World position → which hex contains it
    HexCoord worldToHex(float wx, float wy) const;

    // ── Neighbors ─────────────────────────────────────────────────────────────
    // Returns the 6 neighbors of a hex (may be out of bounds — caller checks)
    static std::vector<HexCoord> neighbors(HexCoord h);

    // ── Distance ──────────────────────────────────────────────────────────────
    static int distance(HexCoord a, HexCoord b);

    // ── Ring / range ──────────────────────────────────────────────────────────
    static std::vector<HexCoord> ring(HexCoord center, int radius);
    static std::vector<HexCoord> range(HexCoord center, int radius);

    // ── Corner positions (for rendering hex outline) ───────────────────────────
    // Returns 6 corner world positions, starting from top-right, clockwise
    void hexCorners(HexCoord h, float out[12]) const; // 6 * (x,y)

    float hexSize()   const { return m_size; }
    float hexWidth()  const { return m_size * 2.0f; }
    float hexHeight() const { return m_size * 1.7320508f; } // size * sqrt(3)

private:
    float m_size;    // circumradius
    float m_ox;      // world origin x
    float m_oy;      // world origin y
};
