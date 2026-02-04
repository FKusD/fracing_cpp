#pragma once

#include <glm/glm.hpp>

class Telemetry {
public:
    Telemetry();
    ~Telemetry();

    void update(float dt, const glm::vec2& position, float speed, float steer, float throttle, float brake);

    void reset();

    float getCurrentLapTime() const { return currentLapTime; }
    float getLastLapTime() const { return lastLapTime; }

    int getCurrentSector() const { return currentSector; }
    float getCurrentSectorTime() const { return currentSectorTime; }
    float getLastSectorTimes() const { return lastSectorTimes[0]; } // Simplified for now

    float getTireTempC() const { return tireTempC; }

private:
    float currentLapTime;
    float lastLapTime;
    int currentSector;
    float currentSectorTime;
    float lastSectorTimes[3];
    float tireTempC;

    glm::vec2 lastPosition;
    float totalDistance;
    float sectorThreshold;
};