#include "Shader.h"
#include "gl_includes.h"
#include <stdio.h>
#include <fstream>
#include <sstream>

// ── Destructor ─────────────────────────────────────────────────────────────────
Shader::~Shader()
{
    if (m_program) glDeleteProgram(m_program);
}

// ── Load from files ────────────────────────────────────────────────────────────
bool Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath)
{
    auto readFile = [](const std::string& path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) {
            fprintf(stderr, "Shader: cannot open %s\n", path.c_str());
            return {};
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };

    std::string vSrc = readFile(vertPath);
    std::string fSrc = readFile(fragPath);
    if (vSrc.empty() || fSrc.empty()) return false;

    return loadFromSource(vSrc.c_str(), fSrc.c_str());
}

// ── Load from source strings ───────────────────────────────────────────────────
bool Shader::loadFromSource(const char* vertSrc, const char* fragSrc)
{
    GLuint vert = compileStage(GL_VERTEX_SHADER,   vertSrc);
    GLuint frag = compileStage(GL_FRAGMENT_SHADER, fragSrc);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);

    bool ok = linkProgram(prog);

    glDeleteShader(vert);
    glDeleteShader(frag);

    if (!ok) { glDeleteProgram(prog); return false; }

    if (m_program) glDeleteProgram(m_program);
    m_program = prog;
    return true;
}

// ── Bind / unbind ──────────────────────────────────────────────────────────────
void Shader::bind()   const { glUseProgram(m_program); }
void Shader::unbind() const { glUseProgram(0); }

// ── Uniforms ───────────────────────────────────────────────────────────────────
void Shader::setInt(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(m_program, name), v);
}
void Shader::setFloat(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(m_program, name), v);
}
void Shader::setVec2(const char* name, float x, float y) const {
    glUniform2f(glGetUniformLocation(m_program, name), x, y);
}
void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(m_program, name), x, y, z, w);
}
void Shader::setMat4(const char* name, const float* mat) const {
    glUniformMatrix4fv(glGetUniformLocation(m_program, name), 1, GL_FALSE, mat);
}

// ── Private helpers ────────────────────────────────────────────────────────────
GLuint Shader::compileStage(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error (%s):\n%s\n",
            type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::linkProgram(GLuint prog)
{
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader link error:\n%s\n", log);
        return false;
    }
    return true;
}
