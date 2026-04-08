#include "RandomController.h"
#include <algorithm>

RandomController::RandomController(uint32_t seed) : rng(seed) {}

VehicleControls RandomController::update(const Observation&, float dt) {
    timer -= dt;
    if (timer <= 0.0f) {
        std::uniform_real_distribution<float> t01(0.0f, 1.0f);
        std::uniform_real_distribution<float> steer(-1.0f, 1.0f);

        current.throttle = std::clamp(0.2f + 0.8f * t01(rng), 0.0f, 1.0f);
        current.brake = (t01(rng) < 0.25f) ? (0.1f + 0.2f * t01(rng)) : 0.0f;
        current.steer = steer(rng);
        current.handbrake = (t01(rng) < 0.02f);

        std::uniform_real_distribution<float> dtChange(0.2f, 0.9f);
        timer = dtChange(rng);
    }
    return current;
}