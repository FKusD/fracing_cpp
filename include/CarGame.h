#pragma once

#include <memory>
#include <vector>
#include "Box2D/Box2D.h"
#include <glm/glm.hpp>
#include <string>
#include "TrackEditor.h"


// Forward declaration, чтобы не тянуть GLFW и GL в заголовок
struct GLFWwindow;
class Car;
class PhysicsWorld;
class Telemetry;
struct Track;

class CarGame {
public:
    CarGame();
    ~CarGame();

    bool initialize();
    void update(float deltaTime);
    void render();
    void handleInput();
    void resize(int width, int height);
    TrackEditor* getTrackEditor() { return trackEditor.get(); }

    void shutdown();

    bool isEditorMode() const {
        return trackEditor && trackEditor->isEditing();
    }

    void setEditorMode(bool active) {
        if (trackEditor) {
            trackEditor->setMode(active ? EditorMode::EDIT_WALLS : EditorMode::PLAY);
        }
    }

private:
    // УЛУЧШЕННЫЙ ЗУМ: уменьшаем VIEW_W и VIEW_H для увеличения машины на экране
    // Было: 32x18, стало: 24x13.5 (примерно 33% увеличение)
    static constexpr float VIEW_W = 24.0f;  // было 32.0f
    static constexpr float VIEW_H = 13.5f;  // было 18.0f
    static constexpr float PPM = 32.0f;     // Pixels Per Meter

    std::unique_ptr<TrackEditor> trackEditor;
    b2World* world;
    b2Vec2 gravity;
    std::unique_ptr<Car> car;
    std::unique_ptr<Telemetry> telemetry;
    std::unique_ptr<Track> track;
    std::vector<b2Body*> trackBodies;

    GLFWwindow* window;

    // Camera
    glm::mat4 projection;
    glm::vec3 cameraPos;

    // НОВОЕ: динамический зум (можно будет изменять в runtime)
    float zoomLevel = 1.0f; // 1.0 = базовый зум, 1.5 = увеличено, 0.5 = уменьшено

    // Timing
    float accumulator;
    static constexpr float STEP = 1.0f / 60.0f;
    static constexpr int V_IT = 6;
    static constexpr int P_IT = 2;

    // Rendering flags
    bool showHud;
    bool showPhysics;
    bool showTrajectory;
    std::string trackPath;

    void createArenaBounds();
    void createObstacles();
    void createWallBox(const glm::vec2& center, float hx, float hy, float angleRad);
    bool loadTrack();
    void clearTrackCollision();
    void buildTrackCollision();
    void updateCamera();
    void renderHud();
    void renderTrackBackground();

    std::string formatTime(float seconds);
    std::string formatLastTime(float seconds);
};