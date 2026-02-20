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

    // Больше поворачиваемость (настройка)
    steerLockDeg *= 1.35f;
    steerLockDeg = std::min(steerLockDeg, 60.0f);
    float steerLockRad = steerLockDeg * M_PI / 180.0f;

    float steerAngle = std::clamp(u.steer, -1.0f, 1.0f) * steerLockRad;

    // 2) Longitudinal forces
    float traction = u.throttle * p.engineForce * 0.70f;
    float braking = u.brake * p.brakeForce;      // N

    // РУЧНИК: должен всегда замедлять, независимо от направления движения
    if (u.handbrake) {
        // При ручнике отключаем тягу
        traction = 0.0f;

        // Сцепление падает (скольжение)
        muSurface *= 0.4f;

        // При малой скорости — стоп
        if (std::abs(s.v) < 0.7f) {
            s.v = 0.0f;
            return;
        }

        // Ручник создаёт силу, направленную ПРОТИВ текущей скорости
        float signV = (s.v >= 0.0f) ? 1.0f : -1.0f;
        // braking тут храним как модуль силы (положительный)
        braking = std::max(braking, 0.90f * p.brakeForce);

        // Важно: позже мы учтём направление через signV
        // Поэтому сохраним signV в локальную переменную ниже (см. дальше)
    }


    // Resistances should oppose motion
    float resist = p.rolling * ((s.v >= 0.0f) ? 1.0f : -1.0f) + p.drag * s.v * std::abs(s.v);

    // Base force (keep your scheme: brake acts like reverse throttle)
    float F = traction - braking - resist;

    // If handbrake: override braking to oppose velocity
    if (u.handbrake) {
        float signV = (s.v >= 0.0f) ? 1.0f : -1.0f;
        // Add extra braking opposite motion (i.e. subtract signV * |brake|)
        // Since F is "forward-direction force", opposing motion means:
        // when v>0 -> subtract positive; when v<0 -> subtract negative (=> adds positive)
        F -= signV * (0.90f * p.brakeForce);
    }


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