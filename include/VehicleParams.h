#pragma once

struct VehicleParams {
    // Geometry
    float wheelbase = 2.8f; // meters

    // Mass and dynamics
    float mass = 1200.0f;       // kg
    float engineForce = 9000.0f; // N (max traction)
    float brakeForce = 14000.0f; // N (max braking)

    // Resistances (simplified)
    float rolling = 30.0f; // N per m/s (linear)
    float drag = 45.0f;    // N per (m/s)^2

    // Steering
    float steerLockLowSpeedDeg = 32.0f; // max steering angle at low speed
    float steerLockHighSpeedDeg = 8.0f; // max steering angle at high speed
    float steerHighSpeedAt = 35.0f;     // m/s, where high-speed settings apply

    // Grip limits
    float g = 9.81f;         // m/s^2
    float muAsphalt = 1.15f; // base grip coefficient

    // Limits
    float maxForwardSpeed = 60.0f; // m/s (safety limit)
    float maxReverseSpeed = 12.0f; // m/s
};