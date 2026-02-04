#pragma once

struct VehicleControls {
    float throttle = 0.0f;  // 0.0 to 1.0
    float brake = 0.0f;     // 0.0 to 1.0
    float steer = 0.0f;     // -1.0 to 1.0
    bool handbrake = false; // true/false
};