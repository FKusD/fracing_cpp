#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/gl.h>

enum class RenderMode {
    SIMPLE_BOXES, // Просто контуры или залитые цветом коробки
    TEXTURED      // Красивый вид с текстурами
};

struct WorldRenderer {
    GLuint vao = 0, vbo = 0, program = 0;
    GLuint wallTexture = 0; // Слот для текстуры
    
    void init();
    
    // Отрисовка одной стены как меша (с толщиной)
    void drawWall(const glm::vec2& p1, const glm::vec2& p2, float thickness, RenderMode mode, const glm::mat4& mvp);
    
    // Отрисовка всей трассы сразу
    void renderTrack(const class Track& track, RenderMode mode, const glm::mat4& mvp);
};

// Глобальный доступ
WorldRenderer& getRenderer();