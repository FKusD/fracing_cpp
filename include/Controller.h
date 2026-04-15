#pragma once
#include "VehicleControls.h"
#include <glm/glm.hpp>
#include <array>
#include <string>

enum class AIControllerType {
    NONE = 0,        // scripted fallback из CarGame
    RANDOM,
    BEHAVIORAL_CLONING,
    GENOME
};

struct Observation {
    static constexpr int kRayCount = 24; // 360 / 15 deg

    float speed = 0.0f;
    float speedForward = 0.0f;
    float speedAbs = 0.0f;
    float yaw = 0.0f;
    float steer = 0.0f;
    float surfaceMu = 1.0f;
    float distToCenterline = 0.0f;
    float progress = 0.0f;
    bool offTrack = false;

    glm::vec2 pos = {0,0};
    glm::vec2 trackTangent = {1.0f, 0.0f};
    float trackForwardDot = 0.0f;

    std::array<float, 3> headingError{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> curvature{{0.0f, 0.0f, 0.0f}};

    std::array<float, kRayCount> rayDistance{};
    std::array<glm::vec2, kRayCount> rayStart{};
    std::array<glm::vec2, kRayCount> rayEnd{};
};

class Controller {
public:
    virtual ~Controller() = default;
    virtual VehicleControls update(const Observation& obs, float dt) = 0;
};