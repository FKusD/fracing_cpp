#include "../include/Telemetry.h"
#include <cmath>

Telemetry::Telemetry()
    : currentLapTime(0.0f), lastLapTime(0.0f), currentSector(0), currentSectorTime(0.0f),
      tireTempC(20.0f), lastPosition(0.0f, 0.0f), totalDistance(0.0f), sectorThreshold(100.0f) {
    // Initialize sector times to zero
    for (int i = 0; i < 3; ++i) {
        lastSectorTimes[i] = 0.0f;
    }
}

Telemetry::~Telemetry() {
}

void Telemetry::update(float dt, const glm::vec2& position, float speed, float steer, float throttle, float brake) {
    // Update lap time
    currentLapTime += dt;

    // Update sector time
    currentSectorTime += dt;

    // Calculate distance traveled since last update
    glm::vec2 displacement = position - lastPosition;
    float distance = glm::length(displacement);
    totalDistance += distance;
    lastPosition = position;

    // Simple tire temperature model (increases with speed and lateral forces)
    float lateralForceFactor = std::abs(steer) * std::abs(speed);
    float longitudinalForceFactor = (throttle + brake) * std::abs(speed);
    float tempIncrease = (lateralForceFactor + longitudinalForceFactor) * 0.01f;
    tireTempC += tempIncrease - (tireTempC - 20.0f) * 0.001f; // Natural cooling

    trajectory.push_back(position);
    if (trajectory.size() > maxTrajectoryPoints) {
        trajectory.erase(trajectory.begin());
    }
}

void Telemetry::reset() {
    currentLapTime = 0.0f;
    lastLapTime = 0.0f;
    currentSector = 0;
    currentSectorTime = 0.0f;

    // Reset sector times
    for (int i = 0; i < 3; ++i) {
        lastSectorTimes[i] = 0.0f;
    }

    // Reset other values
    tireTempC = 20.0f;
    totalDistance = 0.0f;

    trajectory.clear();
}