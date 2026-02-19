#include "../include/VehicleModel.h"
#include <algorithm>
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>

void VehicleModel::update(VehicleState& s, const VehicleControls& u, const VehicleParams& p, float muSurface, float dt) {
    muSurface = std::max(0.05f, muSurface);

    // 1) Steering: angle limit depending on speed (higher speed -> less steering lock)
    float t = std::clamp(std::abs(s.v) / p.steerHighSpeedAt, 0.0f, 1.0f);
    float steerLockDeg = p.steerLockLowSpeedDeg + t * (p.steerLockHighSpeedDeg - p.steerLockLowSpeedDeg);
    float steerLockRad = steerLockDeg * M_PI / 180.0f;

    float steerAngle = std::clamp(u.steer, -1.0f, 1.0f) * steerLockRad;

    // 2) Longitudinal forces
    float traction = u.throttle * p.engineForce; // N
    float braking = u.brake * p.brakeForce;      // N

    // ИСПРАВЛЕНИЕ РУЧНИКА: он должен останавливать машину, а не толкать назад
    if (u.handbrake) {
        // Ручник блокирует задние колеса - сильное торможение
        braking = std::max(braking, 0.85f * p.brakeForce);

        // Сильно снижаем сцепление (имитация блокировки колес)
        muSurface *= 0.4f;

        // ВАЖНО: при низкой скорости ручник должен полностью останавливать машину
        if (std::abs(s.v) < 0.5f) {
            s.v = 0.0f;
            // Блокируем любое движение при стоящей машине с ручником
            return;
        }
    }

    // Resistances
    float resist = p.rolling * s.v + p.drag * s.v * std::abs(s.v); // N (rolling + quadratic drag)

    // Resultant force along direction of travel
    float F = traction - braking - resist;

    // Grip limitation: |F_long| <= mu*m*g
    float Fmax = muSurface * p.mass * p.g;
    F = std::clamp(F, -Fmax, Fmax);

    float aLong = F / p.mass; // m/s^2

    // 3) Integrate velocity
    s.v += aLong * dt;
    s.v = std::clamp(s.v, -p.maxReverseSpeed, p.maxForwardSpeed);

    // При очень малой скорости и без газа - останавливаем машину
    if (std::abs(s.v) < 0.1f && u.throttle < 0.01f) {
        s.v = 0.0f;
    }

    // 4) Turning kinematics (bicycle model): yawRate = v/L * tan(delta)
    // For small angles tan(delta) ~ delta, but we'll keep tan.
    float yawRate = (std::abs(p.wheelbase) > 1e-3f) ? (s.v / p.wheelbase) * std::tan(steerAngle) : 0.0f;

    // 5) Lateral acceleration limit: a_lat = |v * yawRate| <= mu*g
    float aLat = std::abs(s.v * yawRate);
    float aLatMax = muSurface * p.g;

    if (aLat > aLatMax && aLat > 1e-4f) {
        float scale = aLatMax / aLat;
        yawRate *= scale;
    }

    // 6) Integrate yaw
    s.yawRad += yawRate * dt;

    // 7) Integrate position: pos += forward * v * dt
    float cos_yaw = std::cos(s.yawRad);
    float sin_yaw = std::sin(s.yawRad);

    s.pos.x += cos_yaw * s.v * dt;
    s.pos.y += sin_yaw * s.v * dt;
}