#pragma once
#include "Controller.h"
#include <random>

// struct Observation {
//     float speed = 0.0f;
//     glm::vec2 pos = {0,0};
//     // ... добавь что нужно
// };

class RandomController : public Controller {
public:
    RandomController(uint32_t seed = 0xC0FFEE);

    VehicleControls update(const Observation& obs, float dt) override;

private:
    std::mt19937 rng;
    float timer = 0.0f;
    float nextChange = 0.4f;

    VehicleControls current{};
};