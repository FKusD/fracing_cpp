#pragma once
#include "VehicleControls.h"
#include <glm/glm.hpp>
#include <array>

struct Observation {
    static constexpr int kRaysPerCorner = 8;
    static constexpr int kCornerCount = 4;
    static constexpr int kRayCount = kRaysPerCorner * kCornerCount;

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