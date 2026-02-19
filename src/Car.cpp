#include "../include/Car.h"
#include <cmath>

Car::Car(b2World* world, const glm::vec2& startPos)
    : world(world), body(nullptr), muSurface(params.muAsphalt) {
    state.pos = startPos;
    state.yawRad = 0.0f;
    state.v = 0.0f;
    createBody(startPos);
}

Car::~Car() {
    if (body && world) {
        world->DestroyBody(body);
        body = nullptr;
    }
}

void Car::setControls(float throttle, float brake, float steer, bool handbrake) {
    controls.throttle = throttle;
    controls.brake = brake;
    controls.steer = steer;
    controls.handbrake = handbrake;
}

void Car::fixedUpdate(float dt) {
    // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: читаем реальную скорость от Box2D ПЕРЕД обновлением физики
    // Это позволяет учитывать столкновения и правильно отображать скорость
    b2Vec2 currentVel = body->GetLinearVelocity();
    float actualSpeed = currentVel.Length();

    // Определяем направление движения относительно ориентации машины
    float cos_yaw = cosf(state.yawRad);
    float sin_yaw = sinf(state.yawRad);

    // Проекция скорости на направление машины
    float forwardVel = currentVel.x * cos_yaw + currentVel.y * sin_yaw;

    // Если машина столкнулась и её реальная скорость сильно отличается от расчетной,
    // синхронизируем состояние с физикой Box2D
    if (std::abs(actualSpeed - std::abs(state.v)) > 1.0f) {
        // Машина столкнулась или была остановлена физикой
        state.v = forwardVel;
    }

    // Обновляем физическую модель
    model.update(state, controls, params, muSurface, dt);

    // Синхронизируем позицию с Box2D (но не телепортируем, чтобы коллизии работали)
    const b2Vec2 currentPos = body->GetPosition();

    // ИСПРАВЛЕНИЕ: Обновляем только ориентацию, позицию оставляем Box2D
    body->SetTransform(currentPos, state.yawRad);

    // Применяем расчетную скорость к Box2D
    cos_yaw = cosf(state.yawRad);
    sin_yaw = sinf(state.yawRad);

    b2Vec2 vel(cos_yaw * state.v, sin_yaw * state.v);
    body->SetLinearVelocity(vel);

    // Не даем Box2D "докручивать" машину контактами (мы задаем yaw сами выше)
    body->SetAngularVelocity(0.0f);

    // ВАЖНО: После столкновения со стеной синхронизируем состояние позиции
    state.pos.x = currentPos.x;
    state.pos.y = currentPos.y;
}

void Car::reset(const glm::vec2& pos) {
    reset(pos, 0.0f);
}

void Car::reset(const glm::vec2& pos, float yawRad) {
    if (body) {
        world->DestroyBody(body);
    }

    state.pos = pos;
    state.yawRad = yawRad;
    state.v = 0.0f;

    createBody(pos);
}

glm::vec2 Car::getPosition() const {
    b2Vec2 pos = body->GetPosition();
    return glm::vec2(pos.x, pos.y);
}

float Car::getAngleRad() const {
    return body->GetAngle();
}

float Car::getSpeed() const {
    return state.v;
}

void Car::createBody(const glm::vec2& startPos) {
    b2BodyDef bd;
    bd.type = b2_dynamicBody;
    bd.position.Set(startPos.x, startPos.y);
    bd.angle = state.yawRad;
    // CCD против "пролёта" сквозь тонкие стенки на большой скорости
    bd.bullet = true;
    // Мы управляем ориентацией сами (yaw), поэтому фиксируем вращение от физических моментов
    bd.fixedRotation = true;

    // ИСПРАВЛЕНИЕ: Добавляем линейное затухание для более реалистичного поведения при столкновениях
    bd.linearDamping = 0.1f;

    body = world->CreateBody(&bd);

    b2PolygonShape shape;
    // Slightly smaller (roughly 1.5-2x smaller)
    shape.SetAsBox(0.4f, 0.2f);

    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = 1.0f;
    fd.friction = 0.5f;
    // ИСПРАВЛЕНИЕ: Уменьшаем отскок для более реалистичных столкновений
    fd.restitution = 0.05f; // было 0.15f - теперь почти нет отскока

    body->CreateFixture(&fd);
}