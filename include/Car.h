#pragma once

#include <glm/glm.hpp>
#include "Box2D/Box2D.h"
#include "VehicleParams.h"
#include "VehicleState.h"
#include "VehicleControls.h"
#include "VehicleModel.h"

class Car {
public:
    Car(b2World* world, const glm::vec2& startPos);
    ~Car();

    void setControls(float throttle, float brake, float steer, bool handbrake);
    void fixedUpdate(float dt);

    void reset(const glm::vec2& pos);
    void reset(const glm::vec2& pos, float yawRad);

    glm::vec2 getPosition() const;
    float getAngleRad() const;
    float getSpeed() const;

    float getThrottle() const { return controls.throttle; }
    float getBrake() const { return controls.brake; }
    float getSteer() const { return controls.steer; }
    bool isHandbrake() const { return controls.handbrake; }

    float getMuSurface() const { return muSurface; }
    void setMuSurface(float muSurface) { this->muSurface = muSurface; }

    const VehicleParams& getParams() const { return params; }

private:
    b2World* world;
    b2Body* body;

    VehicleParams params;
    VehicleState state;
    VehicleControls controls;
    VehicleModel model;

    float muSurface;

    void createBody(const glm::vec2& startPos);
};