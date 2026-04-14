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
    float getLongitudinalSpeed() const { return vLong; }
    float getLateralSpeed() const { return vLat; }

    float getThrottle() const { return controls.throttle; }
    float getBrake() const { return controls.brake; }
    float getSteer() const { return controls.steer; }
    bool isHandbrake() const { return controls.handbrake; }

    void setSurfaceMu(float mu) { muSurface = mu; }
    float getSurfaceMu() const { return muSurface; }
    float getDefaultMu() const { return params.muAsphalt; }

    const VehicleParams& getParams() const { return params; }

    b2Body* getBody() { return body; }
    const b2Body* getBody() const { return body; }
    void setCarCollisionEnabled(bool enabled);

private:
    b2World* world;
    b2Body* body;

    VehicleParams params;
    VehicleState state;
    VehicleControls controls;
    VehicleModel model;

    float muSurface;

    float vLong = 0.0f;
    float vLat  = 0.0f;

    void createBody(const glm::vec2& startPos);
};