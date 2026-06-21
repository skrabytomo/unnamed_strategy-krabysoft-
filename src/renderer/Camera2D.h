#pragma once

// Camera2D — orthographic camera for world map and combat
// Produces a projection matrix for SpriteBatch
class Camera2D
{
public:
    Camera2D() = default;

    void setViewport(int w, int h);
    void setPosition(float x, float y);
    void setZoom(float zoom);

    void pan(float dx, float dy);
    void zoomBy(float factor);   // multiply current zoom

    float x()    const { return m_x; }
    float y()    const { return m_y; }
    float zoom() const { return m_zoom; }

    // Returns column-major 4x4 orthographic projection matrix
    // suitable for glUniformMatrix4fv and SpriteBatch::begin()
    void getMatrix(float out[16]) const;

    // Convert screen pixel → world coordinate
    void screenToWorld(float sx, float sy, float& wx, float& wy) const;

    // Convert world coordinate → screen pixel
    void worldToScreen(float wx, float wy, float& sx, float& sy) const;

private:
    float m_x    = 0.0f;
    float m_y    = 0.0f;
    float m_zoom = 1.0f;
    int   m_w    = 1280;
    int   m_h    = 720;
};
