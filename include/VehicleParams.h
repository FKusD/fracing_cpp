#pragma once

struct VehicleParams {
    // Geometry
    float wheelbase = 2.5f; // УМЕНЬШЕНО с 2.8м - еще меньший радиус разворота

    // Mass and dynamics
    float mass = 1200.0f;       // kg
    float engineForce = 10000.0f; // УВЕЛИЧЕНО с 9000 - более отзывчивый разгон
    float brakeForce = 16000.0f; // УВЕЛИЧЕНО с 14000 - лучше тормоза

    // Resistances (simplified)
    float rolling = 25.0f; // УМЕНЬШЕНО с 30 - меньше сопротивление качению
    float drag = 40.0f;    // УМЕНЬШЕНО с 45 - меньше аэродинамическое сопротивление

    // МАКСИМАЛЬНО УЛУЧШЕННАЯ РУЛЕЖКА
    float steerLockLowSpeedDeg = 50.0f; // было 45° - теперь 50° (почти как у картинга)
    float steerLockHighSpeedDeg = 15.0f; // было 12° - чуть больше контроля на скорости
    float steerHighSpeedAt = 25.0f;     // было 30 - еще раньше начинаем ограничивать

    // ДОПОЛНИТЕЛЬНЫЙ ПАРАМЕТР: скорость реакции руля (0-1, где 1 = мгновенно)
    float steerResponseSpeed = 0.85f; // быстрая, но не мгновенная реакция

    // Grip limits
    float g = 9.81f;         // m/s^2
    float muAsphalt = 1.20f; // УВЕЛИЧЕНО с 1.15 - немного больше сцепления

    // Limits
    float maxForwardSpeed = 60.0f; // m/s (safety limit)
    float maxReverseSpeed = 12.0f; // m/s
};