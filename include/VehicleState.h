#pragma once

#include <glm/glm.hpp>

struct VehicleState {
    glm::vec2 pos = glm::vec2(0.0f, 0.0f); // position (meters)
    float yawRad = 0.0f;                   // heading (radians)
    float v = 0.0f;                       // velocity (m/s)
};