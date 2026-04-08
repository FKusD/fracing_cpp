#pragma once
#include <vector>
#include <glm/glm.hpp>

struct Sample {
    float     t        = 0.0f;
    glm::vec2 pos      = {0,0};
    float     speed    = 0.0f;
    float     steer    = 0.0f;
    float     throttle = 0.0f;
    float     brake    = 0.0f;
    bool      handbrake= false;
    float     mu       = 1.0f;
    float     vLat     = 0.0f;
};

class Telemetry {
public:
    Telemetry();
    ~Telemetry();

    void update(float dt, const glm::vec2& position, float speed,
                float steer, float throttle, float brake);
    void update(float dt, const glm::vec2& position, float speed,
                float steer, float throttle, float brake,
                bool handbrake, float mu, float vLat);

    void reset();

    const Sample& getLatest() const { return latest; }

    // ── Данные для рендера ─────────────────────────────────────────────────
    std::vector<glm::vec2> trajectory;       // позиции (до maxTrajectoryPoints)
    std::vector<float>     speeds;           // скорость в каждой точке траектории

    // ── Устаревшие поля (совместимость) ────────────────────────────────────
    float currentLapTime    = 0.0f;
    float lastLapTime       = 0.0f;
    int   currentSector     = 0;
    float currentSectorTime = 0.0f;
    float lastSectorTimes[3]= {};
    float tireTempC         = 20.0f;
    glm::vec2 lastPosition  = {0,0};
    float totalDistance     = 0.0f;
    float sectorThreshold   = 100.0f;

private:
    Sample latest;
    static constexpr size_t maxTrajectoryPoints = 2000;
};