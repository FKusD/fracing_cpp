#include "DebugRenderer.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

DebugLineRenderer& debugRenderer() {
    static DebugLineRenderer instance;
    return instance;
}

void DebugLineRenderer::initOnce() {
    if (initialized) return;

    const char* vShaderSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        uniform mat4 uMVP;
        void main() { gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
    )";

    const char* fShaderSrc = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 uColor;
        void main() { FragColor = uColor; }
    )";

    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if(!ok) std::cerr << "Shader Compile Error" << std::endl;
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vShaderSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fShaderSrc);

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    uColor = glGetUniformLocation(program, "uColor");
    uMVP = glGetUniformLocation(program, "uMVP");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    initialized = true;
    std::cout << "[DebugRenderer] Shader Program Created: " << program << std::endl;
}

void DebugLineRenderer::beginFrame() {
    vertices.clear();
}

void DebugLineRenderer::addLine(const b2Vec2& a, const b2Vec2& b) {
    vertices.push_back(a.x); vertices.push_back(a.y);
    vertices.push_back(b.x); vertices.push_back(b.y);
}

// РЕАЛИЗАЦИЯ 4 ШАГА: Рисуем коробку по двум точкам и толщине
void DebugLineRenderer::addRect(const glm::vec2& p1, const glm::vec2& p2, float thickness) {
    glm::vec2 dir = p2 - p1;
    float len = glm::length(dir);
    if (len < 0.0001f) return;

    glm::vec2 unitDir = dir / len;
    // Перпендикуляр к линии
    glm::vec2 normal = glm::vec2(-unitDir.y, unitDir.x) * (thickness * 0.5f);

    glm::vec2 v1 = p1 + normal;
    glm::vec2 v2 = p1 - normal;
    glm::vec2 v3 = p2 + normal;
    glm::vec2 v4 = p2 - normal;

    // Контур коробки
    addLine(b2Vec2(v1.x, v1.y), b2Vec2(v3.x, v3.y));
    addLine(b2Vec2(v3.x, v3.y), b2Vec2(v4.x, v4.y));
    addLine(b2Vec2(v4.x, v4.y), b2Vec2(v2.x, v2.y));
    addLine(b2Vec2(v2.x, v2.y), b2Vec2(v1.x, v1.y));
}

void DebugLineRenderer::flush(const glm::mat4& mvp, float r, float g, float b, float a) {
    if (!initialized || vertices.empty()) return;

    glUseProgram(program);
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform4f(uColor, r, g, b, a);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glDrawArrays(GL_LINES, 0, (GLsizei)(vertices.size() / 2));
    glBindVertexArray(0);
}