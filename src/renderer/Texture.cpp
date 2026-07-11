#include "../core/DevLog.h"
#include "Texture.h"
#include "gl_includes.h"
#include <stdio.h>

// stb_image — single header, define implementation once here
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

Texture::~Texture()
{
    if (m_id) glDeleteTextures(1, &m_id);
}

#ifdef _WIN32
// plain fopen() interprets the byte string using the current ANSI codepage, so
// UTF-8 paths with multi-byte characters (e.g. em-dash, U+2014) fail to open
// even though the file exists — widen to UTF-16 and use _wfopen instead.
static FILE* openUtf8Path(const std::string& path)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return nullptr;
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    return _wfopen(wpath.c_str(), L"rb");
}
#endif

bool Texture::load(const std::string& path, bool pixelArt, bool flipV, bool repeat, bool wrapMirror)
{
    stbi_set_flip_vertically_on_load(flipV ? 1 : 0);

    int channels = 0;
    unsigned char* data = nullptr;
#ifdef _WIN32
    FILE* f = openUtf8Path(path);
    if (!f) {
        fprintf(stderr, "Texture: failed to load %s — can't fopen\n", path.c_str());
        return false;
    }
    data = stbi_load_from_file(f, &m_width, &m_height, &channels, 4);
    fclose(f);
#else
    data = stbi_load(path.c_str(), &m_width, &m_height, &channels, 4);
#endif
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
    GLint wrap = wrapMirror ? GL_MIRRORED_REPEAT : (repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
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
