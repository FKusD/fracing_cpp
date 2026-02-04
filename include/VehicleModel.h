#pragma once

#include "VehicleState.h"
#include "VehicleControls.h"
#include "VehicleParams.h"

class VehicleModel {
public:
    /**
     * Updates VehicleState by dt.
     * muSurface: surface grip coefficient (e.g. 1.15 asphalt, 0.7 wet, 0.5 gravel).
     */
    void update(VehicleState& s, const VehicleControls& u, const VehicleParams& p, float muSurface, float dt);
};