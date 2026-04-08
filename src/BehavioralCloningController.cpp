#include "BehavioralCloningController.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

static float act_relu(float x) { return x > 0.0f ? x : 0.0f; }
static float act_tanh(float x) { return std::tanh(x); }

bool BehavioralCloningController::loadModel(const std::string& path, std::string* err) {
    layers_.clear();
    inputMean_.clear();
    inputStd_.clear();

    std::ifstream in(path);
    if (!in.is_open()) {
        if (err) *err = "cannot open model file: " + path;
        return false;
    }

    json j;
    in >> j;

    if (j.contains("input_mean")) inputMean_ = j["input_mean"].get<std::vector<float>>();
    if (j.contains("input_std"))  inputStd_  = j["input_std"].get<std::vector<float>>();

    if (!j.contains("layers")) {
        if (err) *err = "model json has no 'layers'";
        return false;
    }

    for (auto& jl : j["layers"]) {
        Layer L;
        L.in = jl["in"].get<int>();
        L.out = jl["out"].get<int>();
        L.activation = jl["activation"].get<std::string>();
        L.W = jl["weights"].get<std::vector<float>>();
        L.b = jl["bias"].get<std::vector<float>>();
        layers_.push_back(std::move(L));
    }

    return !layers_.empty();
}

std::vector<float> BehavioralCloningController::flattenObservation(const Observation& obs) const {
    std::vector<float> x;
    x.reserve(10 + 3 + 3 + Observation::kRayCount);

    x.push_back(obs.speed);
    x.push_back(obs.speedForward);
    x.push_back(obs.speedAbs);
    x.push_back(obs.yaw);
    x.push_back(obs.steer);
    x.push_back(obs.surfaceMu);
    x.push_back(obs.distToCenterline);
    x.push_back(obs.progress);
    x.push_back(obs.offTrack ? 1.0f : 0.0f);
    x.push_back(obs.pos.x);
    x.push_back(obs.pos.y);

    for (float v : obs.headingError) x.push_back(v);
    for (float v : obs.curvature)    x.push_back(v);
    for (float v : obs.rayDistance)  x.push_back(v);

    if (inputMean_.size() == x.size() && inputStd_.size() == x.size()) {
        for (size_t i = 0; i < x.size(); ++i) {
            float s = std::max(1e-6f, inputStd_[i]);
            x[i] = (x[i] - inputMean_[i]) / s;
        }
    }

    return x;
}

std::vector<float> BehavioralCloningController::forward(const std::vector<float>& x0) const {
    std::vector<float> x = x0;

    for (const auto& L : layers_) {
        std::vector<float> y(L.out, 0.0f);
        for (int o = 0; o < L.out; ++o) {
            float s = L.b[o];
            for (int i = 0; i < L.in; ++i) {
                s += L.W[o * L.in + i] * x[i];
            }

            if (L.activation == "relu") s = act_relu(s);
            else if (L.activation == "tanh") s = act_tanh(s);

            y[o] = s;
        }
        x = std::move(y);
    }

    return x;
}

VehicleControls BehavioralCloningController::update(const Observation& obs, float /*dt*/) {
    VehicleControls ctrl{};
    if (layers_.empty()) return ctrl;

    std::vector<float> x = flattenObservation(obs);
    std::vector<float> y = forward(x);
    if (y.size() < 3) return ctrl;

    ctrl.throttle = std::clamp(y[0], 0.0f, 1.0f);
    ctrl.brake    = std::clamp(y[1], 0.0f, 1.0f);
    ctrl.steer    = std::clamp(y[2], -1.0f, 1.0f);
    ctrl.handbrake = false;
    return ctrl;
}