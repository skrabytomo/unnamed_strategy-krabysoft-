#pragma once
#include "gl_includes.h"
#include <string>

class Shader
{
public:
    Shader() = default;
    ~Shader();

    // Load from file paths
    bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);
    // Load from source strings
    bool loadFromSource(const char* vertSrc, const char* fragSrc);

    void bind()   const;
    void unbind() const;

    // Uniform setters
    void setInt  (const char* name, int value)          const;
    void setFloat(const char* name, float value)        const;
    void setVec2 (const char* name, float x, float y)  const;
    void setVec4 (const char* name, float x, float y,
                  float z, float w)                     const;
    void setMat4 (const char* name, const float* mat)   const;

    GLuint id() const { return m_program; }
    bool   ok() const { return m_program != 0; }

private:
    static GLuint compileStage(GLenum type, const char* src);
    static bool   linkProgram(GLuint prog);

    GLuint m_program = 0;
};
