#pragma once
#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <vector>
#include <glad/gl.h>

// Просто объявляем структуру (реализация останется в CarGame.cpp или вынесем позже)
struct DebugLineRenderer {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint program = 0;
    GLint uColor = -1;
    GLint uMVP = -1;
    bool initialized = false;
    std::vector<float> vertices;

    void initOnce();
    void beginFrame();
    void addLine(const b2Vec2& a, const b2Vec2& b);
    void addRect(const glm::vec2& p1, const glm::vec2& p2, float thickness);
    void flush(const glm::mat4& mvp, float r, float g, float b, float a = 1.0f);
};

// Самое важное: даем доступ к функции
DebugLineRenderer& debugRenderer();