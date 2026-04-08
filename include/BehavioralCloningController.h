#pragma once
#include "Controller.h"
#include <string>
#include <vector>

class BehavioralCloningController : public Controller {
public:
    bool loadModel(const std::string& path, std::string* err = nullptr);
    VehicleControls update(const Observation& obs, float dt) override;

private:
    struct Layer {
        int in = 0;
        int out = 0;
        std::vector<float> W; // row-major [out][in]
        std::vector<float> b; // [out]
        std::string activation; // "relu" / "tanh" / "linear"
    };

    std::vector<Layer> layers_;
    std::vector<float> inputMean_;
    std::vector<float> inputStd_;

    std::vector<float> flattenObservation(const Observation& obs) const;
    std::vector<float> forward(const std::vector<float>& x) const;
};