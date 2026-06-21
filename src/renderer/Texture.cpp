#include "../core/DevLog.h"
#include "Texture.h"
#include "gl_includes.h"
#include <stdio.h>

// stb_image — single header, define implementation once here
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Texture::~Texture()
{
    if (m_id) glDeleteTextures(1, &m_id);
}

bool Texture::load(const std::string& path, bool pixelArt, bool flipV, bool repeat)
{
    stbi_set_flip_vertically_on_load(flipV ? 1 : 0);

    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &m_width, &m_height, &channels, 4);
    if (!data) {
        fprintf(stderr, "Texture: failed to load %s — %s\n", path.c_str(), stbi_failure_reason());
        return false;
    }

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);

    // Filtering — nearest for pixel art, linear for smooth
    GLint filter = pixelArt ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    gLog("Texture loaded: %s (%dx%d)\n", path.c_str(), m_width, m_height);
    return true;
}

void Texture::bind(int slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

void Texture::unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}
