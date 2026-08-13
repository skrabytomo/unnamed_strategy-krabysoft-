#include "Camera2D.h"
#include <string.h>
#include <algorithm>
#include <cmath>

void Camera2D::setViewport(int w, int h)
{
    m_w = w;
    m_h = h;
}

void Camera2D::setPosition(float x, float y)
{
    m_x = x;
    m_y = y;
}

void Camera2D::setZoom(float zoom)
{
    m_zoom = std::max(0.1f, zoom);
}

void Camera2D::setContentScale(float scale)
{
    m_contentScale = std::max(0.1f, scale);
}

void Camera2D::pan(float dx, float dy)
{
    m_x += dx / m_zoom;
    m_y += dy / m_zoom;
}

void Camera2D::zoomBy(float factor)
{
    setZoom(m_zoom * factor);
}

// Builds column-major orthographic matrix
// World origin at top-left, Y down (matches screen convention)
// Applies zoom and camera translation
void Camera2D::getMatrix(float out[16]) const
{
    memset(out, 0, sizeof(float) * 16);

    // Pixel-perfect ortho: round the half-extents so the projection
    // never maps a world unit to a fractional pixel coordinate.
    float invScale = 1.0f / m_contentScale;
    float hw = std::round((m_w * 0.5f * invScale) / m_zoom);
    float hh = std::round((m_h * 0.5f * invScale) / m_zoom);

    float left   =  m_x - hw;
    float right  =  m_x + hw;
    float bottom =  m_y + hh;  // Y-down: bottom > top
    float top    =  m_y - hh;
    float near_  = -1.0f;
    float far_   =  1.0f;

    // Standard ortho formula (column-major)
    out[0]  =  2.0f / (right - left);
    out[5]  =  2.0f / (top - bottom);
    out[10] = -2.0f / (far_ - near_);
    out[12] = -(right + left)   / (right - left);
    out[13] = -(top   + bottom) / (top   - bottom);
    out[14] = -(far_  + near_)  / (far_  - near_);
    out[15] =  1.0f;
}

void Camera2D::screenToWorld(float sx, float sy, float& wx, float& wy) const
{
    float invScale = 1.0f / m_contentScale;
    float hw = (m_w * 0.5f * invScale) / m_zoom;
    float hh = (m_h * 0.5f * invScale) / m_zoom;

    wx = m_x - hw + (sx * invScale / m_w) * (hw * 2.0f);
    wy = m_y - hh + (sy * invScale / m_h) * (hh * 2.0f);
}

void Camera2D::worldToScreen(float wx, float wy, float& sx, float& sy) const
{
    float invScale = 1.0f / m_contentScale;
    float hw = (m_w * 0.5f * invScale) / m_zoom;
    float hh = (m_h * 0.5f * invScale) / m_zoom;

    sx = (wx - m_x) * m_zoom * m_contentScale + m_w * 0.5f;
    sy = (wy - m_y) * m_zoom * m_contentScale + m_h * 0.5f;
}

void Camera2D::snapWorldToPixel(float& wx, float& wy) const
{
    float sx, sy;
    worldToScreen(wx, wy, sx, sy);
    sx = std::round(sx);
    sy = std::round(sy);
    screenToWorld(sx, sy, wx, wy);
}
