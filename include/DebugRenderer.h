#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/gl.h>
#include "Box2D/Box2D.h"

class DebugLineRenderer {
public:
    void initOnce();
    void beginFrame();

    // Standard uniform-color lines
    void addLine(const b2Vec2& a, const b2Vec2& b);
    void addRect(const glm::vec2& p1, const glm::vec2& p2, float thickness);
    void flush(const glm::mat4& mvp, float r, float g, float b, float a = 1.0f);

    // Per-vertex color batch (speed heatmap, gradients) — one GPU draw call
    void addColoredLine(const b2Vec2& a, const b2Vec2& b,
                        float r, float g, float bl, float alpha);
    void flushColored(const glm::mat4& mvp);

private:
    // Uniform-color pipeline
    GLuint program = 0, vao = 0, vbo = 0;
    GLint  uColor = -1, uMVP = -1;
    bool   initialized = false;
    std::vector<float> vertices;

    // Per-vertex-color pipeline
    GLuint programColored = 0, vaoColored = 0, vboColored = 0;
    GLint  uMVPColored = -1;
    bool   initializedColored = false;
    // layout per vertex: x y r g b a
    std::vector<float> coloredVertices;

    void initColored();
};

DebugLineRenderer& debugRenderer();