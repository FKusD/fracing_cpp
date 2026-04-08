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
    update(dt, position, speed, steer, throttle, brake, false, 1.0f, 0.0f);
}

void Telemetry::update(float dt, const glm::vec2& position, float speed, float steer, float throttle, float brake,
                       bool handbrake, float mu, float vLat) {
    currentLapTime += dt;
    currentSectorTime += dt;

    glm::vec2 displacement = position - lastPosition;
    totalDistance += glm::length(displacement);
    lastPosition = position;

    float lateralForceFactor = std::abs(vLat);
    float longitudinalForceFactor = (throttle + brake) * std::abs(speed);
    float tempIncrease = (lateralForceFactor + longitudinalForceFactor) * 0.01f;
    tireTempC += tempIncrease - (tireTempC - 20.0f) * 0.001f;

    latest.t += dt;
    latest.pos = position;
    latest.speed = speed;
    latest.steer = steer;
    latest.throttle = throttle;
    latest.brake = brake;
    latest.handbrake = handbrake;
    latest.mu = mu;
    latest.vLat = vLat;

    trajectory.push_back(position);
    speeds.push_back(std::abs(speed));
    if (trajectory.size() > maxTrajectoryPoints) trajectory.erase(trajectory.begin());
    if (speeds.size() > maxTrajectoryPoints)    speeds.erase(speeds.begin());
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
    speeds.clear();
    latest = Sample{};
}