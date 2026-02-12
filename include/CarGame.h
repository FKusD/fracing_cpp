#pragma once

#include <memory>
#include <vector>
#include "Box2D/Box2D.h"
#include <glm/glm.hpp>
#include <string>

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

    void shutdown();

private:
    static constexpr float VIEW_W = 32.0f;
    static constexpr float VIEW_H = 18.0f;
    static constexpr float PPM = 32.0f; // Pixels Per Meter

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