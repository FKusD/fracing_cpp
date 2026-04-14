#include "GenomeController.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

using json = nlohmann::json;

namespace {
static float relu(float x) { return x > 0.0f ? x : 0.0f; }
static float fast_tanh(float x) { return std::tanh(x); }
}

GenomeController::GenomeController() {
    genome_ = makeRandom(0xC0FFEEu);
}

GenomeController::GenomeController(const GenomeSpec& spec) {
    setGenome(spec);
}

int GenomeController::expectedInputSize() {
    // speedForward, speedAbs, yaw, steer, surfaceMu, distToCenterline, progress, offTrack,
    // heading[3], curvature[3], rays[kRayCount]
    return 8 + 3 + 3 + Observation::kRayCount;
}

GenomeSpec GenomeController::makeRandom(uint32_t seed, int hiddenSize, float initScale) {
    GenomeSpec g;
    g.inputSize = expectedInputSize();
    g.hiddenSize = hiddenSize;
    g.outputSize = 3;
    const int total = g.inputSize * g.hiddenSize + g.hiddenSize + g.hiddenSize * g.outputSize + g.outputSize;
    g.genes.resize(total);

    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0f, initScale);
    for (float& w : g.genes) w = nd(rng);
    return g;
}

void GenomeController::setGenome(const GenomeSpec& spec) {
    genome_ = spec;
    if (genome_.inputSize <= 0) genome_.inputSize = expectedInputSize();
    if (genome_.outputSize != 3) genome_.outputSize = 3;
}

bool GenomeController::saveGenome(const std::string& path, std::string* err) const {
    std::ofstream out(path);
    if (!out.is_open()) {
        if (err) *err = "cannot open genome file for write: " + path;
        return false;
    }
    json j;
    j["input_size"] = genome_.inputSize;
    j["hidden_size"] = genome_.hiddenSize;
    j["output_size"] = genome_.outputSize;
    j["genes"] = genome_.genes;
    out << j.dump(2);
    return true;
}

bool GenomeController::loadGenome(const std::string& path, std::string* err) {
    std::ifstream in(path);
    if (!in.is_open()) {
        if (err) *err = "cannot open genome file: " + path;
        return false;
    }
    json j;
    in >> j;
    GenomeSpec g;
    g.inputSize = j.value("input_size", expectedInputSize());
    g.hiddenSize = j.value("hidden_size", 32);
    g.outputSize = j.value("output_size", 3);
    g.genes = j.at("genes").get<std::vector<float>>();
    setGenome(g);
    return true;
}

std::vector<float> GenomeController::flattenObservation(const Observation& obs) const {
    std::vector<float> x;
    x.reserve(expectedInputSize());

    x.push_back(std::clamp(obs.speedForward / 25.0f, -1.0f, 1.0f));
    x.push_back(std::clamp(obs.speedAbs / 25.0f, 0.0f, 1.0f));
    x.push_back(std::clamp(obs.yaw / 3.14159265f, -1.0f, 1.0f));
    x.push_back(std::clamp(obs.steer, -1.0f, 1.0f));
    x.push_back(std::clamp(obs.surfaceMu / 2.0f, 0.0f, 1.0f));
    x.push_back(std::clamp(obs.distToCenterline / 10.0f, 0.0f, 1.0f));
    x.push_back(std::clamp(obs.progress, 0.0f, 1.0f));
    x.push_back(obs.offTrack ? 1.0f : 0.0f);

    for (float v : obs.headingError) x.push_back(std::clamp(v / 1.57f, -1.0f, 1.0f));
    for (float v : obs.curvature)    x.push_back(std::clamp(v * 4.0f, -1.0f, 1.0f));
    for (float v : obs.rayDistance)  x.push_back(std::clamp(v, 0.0f, 1.0f));

    return x;
}

std::vector<float> GenomeController::forward(const std::vector<float>& x) const {
    const int I = genome_.inputSize;
    const int H = genome_.hiddenSize;
    const int O = genome_.outputSize;
    const int expected = I * H + H + H * O + O;
    if ((int)genome_.genes.size() != expected || (int)x.size() != I) {
        return std::vector<float>(3, 0.0f);
    }

    const float* p = genome_.genes.data();
    const float* w1 = p;             p += I * H;
    const float* b1 = p;             p += H;
    const float* w2 = p;             p += H * O;
    const float* b2 = p;

    std::vector<float> h(H, 0.0f);
    for (int j = 0; j < H; ++j) {
        float s = b1[j];
        for (int i = 0; i < I; ++i) s += w1[j * I + i] * x[i];
        h[j] = relu(s);
    }

    std::vector<float> y(O, 0.0f);
    for (int o = 0; o < O; ++o) {
        float s = b2[o];
        for (int j = 0; j < H; ++j) s += w2[o * H + j] * h[j];
        y[o] = s;
    }
    return y;
}

VehicleControls GenomeController::update(const Observation& obs, float /*dt*/) {
    VehicleControls ctrl{};
    std::vector<float> x = flattenObservation(obs);
    std::vector<float> y = forward(x);

    // output: steer, throttle, brake
    ctrl.steer = std::clamp(fast_tanh(y[0]), -1.0f, 1.0f);
    ctrl.throttle = std::clamp(0.5f * (fast_tanh(y[1]) + 1.0f), 0.0f, 1.0f);
    ctrl.brake = std::clamp(0.5f * (fast_tanh(y[2]) + 1.0f), 0.0f, 1.0f);
    ctrl.handbrake = false;

    // simple arbitration to reduce simultaneous full throttle/brake
    if (ctrl.throttle > ctrl.brake) ctrl.brake *= 0.35f;
    else ctrl.throttle *= 0.35f;

    return ctrl;
}
