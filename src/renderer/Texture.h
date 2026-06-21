#pragma once
#include "gl_includes.h"
#include <string>

class Texture
{
public:
    Texture() = default;
    ~Texture();

    bool load(const std::string& path, bool pixelArt = true, bool flipV = true, bool repeat = false);
    void bind(int slot = 0) const;
    void unbind() const;

    int    width()  const { return m_width; }
    int    height() const { return m_height; }
    GLuint id()     const { return m_id; }
    bool   ok()     const { return m_id != 0; }

private:
    GLuint m_id     = 0;
    int    m_width  = 0;
    int    m_height = 0;
};
