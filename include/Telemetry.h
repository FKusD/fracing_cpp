#pragma once

#include <glm/glm.hpp>
#include <vector>

class Telemetry {
public:

    std::vector<glm::vec2> trajectory;  // Last positions
    int maxTrajectoryPoints = 1000;

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

    struct Sample {
        float t = 0.0f;
        glm::vec2 pos{0.0f, 0.0f};
        float speed = 0.0f;      // m/s
        float steer = 0.0f;      // -1..1
        float throttle = 0.0f;   // 0..1
        float brake = 0.0f;      // 0..1
        bool handbrake = false;
        float mu = 1.0f;
        float vLat = 0.0f;       // m/s
    };

    void update(float dt, const glm::vec2& position, float speed, float steer, float throttle, float brake,
            bool handbrake, float mu, float vLat);

    const Sample& getLatest() const { return latest; }



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
    Sample latest;

};