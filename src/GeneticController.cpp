#include "../include/GeneticController.h"
#include <algorithm>
#include <cmath>

namespace {
static float relu(float x) { return x > 0.0f ? x : 0.0f; }
static float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
static float clamp11(float x) { return std::clamp(x, -1.0f, 1.0f); }
}

bool GeneticController::Genome::isValid() const {
    return inputSize > 0 && hiddenSize > 0 && outputSize > 0 &&
           (int)w1.size() == hiddenSize * inputSize &&
           (int)b1.size() == hiddenSize &&
           (int)w2.size() == outputSize * hiddenSize &&
           (int)b2.size() == outputSize;
}

void GeneticController::Genome::randomize(std::mt19937& rng, int inSize, int hidden, int outSize, float scale) {
    inputSize = inSize;
    hiddenSize = hidden;
    outputSize = outSize;
    w1.resize(hiddenSize * inputSize);
    b1.resize(hiddenSize);
    w2.resize(outputSize * hiddenSize);
    b2.resize(outputSize);

    std::normal_distribution<float> nd(0.0f, scale);
    for (float& v : w1) v = nd(rng);
    for (float& v : b1) v = nd(rng) * 0.25f;
    for (float& v : w2) v = nd(rng);
    for (float& v : b2) v = nd(rng) * 0.25f;
}

GeneticController::Genome GeneticController::Genome::crossover(const Genome& a, const Genome& b, std::mt19937& rng) {
    Genome out = a;
    if (!a.isValid() || !b.isValid() || a.inputSize != b.inputSize || a.hiddenSize != b.hiddenSize || a.outputSize != b.outputSize)
        return out;

    std::bernoulli_distribution coin(0.5);
    for (size_t i = 0; i < out.w1.size(); ++i) out.w1[i] = coin(rng) ? a.w1[i] : b.w1[i];
    for (size_t i = 0; i < out.b1.size(); ++i) out.b1[i] = coin(rng) ? a.b1[i] : b.b1[i];
    for (size_t i = 0; i < out.w2.size(); ++i) out.w2[i] = coin(rng) ? a.w2[i] : b.w2[i];
    for (size_t i = 0; i < out.b2.size(); ++i) out.b2[i] = coin(rng) ? a.b2[i] : b.b2[i];
    return out;
}

void GeneticController::Genome::mutate(std::mt19937& rng, float sigma, float prob) {
    if (!isValid()) return;
    std::bernoulli_distribution pick(prob);
    std::normal_distribution<float> nd(0.0f, sigma);
    for (float& v : w1) if (pick(rng)) v += nd(rng);
    for (float& v : b1) if (pick(rng)) v += nd(rng);
    for (float& v : w2) if (pick(rng)) v += nd(rng);
    for (float& v : b2) if (pick(rng)) v += nd(rng);
}

GeneticController::GeneticController(const Genome& genome) : genome_(genome) {}

void GeneticController::setGenome(const Genome& genome) {
    genome_ = genome;
}

int GeneticController::inputSizeFromObservation() {
    return 11 + 3 + 3 + Observation::kRayCount;
}

std::vector<float> GeneticController::flattenObservation(const Observation& obs) {
    std::vector<float> x;
    x.reserve(inputSizeFromObservation());

    x.push_back(obs.speed * 0.05f);
    x.push_back(obs.speedForward * 0.05f);
    x.push_back(obs.speedAbs * 0.05f);
    x.push_back(obs.yaw / 3.14159265f);
    x.push_back(obs.steer);
    x.push_back(obs.surfaceMu * 0.5f);
    x.push_back(obs.distToCenterline * 0.1f);
    x.push_back(obs.progress * 2.0f - 1.0f);
    x.push_back(obs.offTrack ? 1.0f : 0.0f);
    x.push_back(obs.pos.x * 0.02f);
    x.push_back(obs.pos.y * 0.02f);

    for (float v : obs.headingError) x.push_back(v / 3.14159265f);
    for (float v : obs.curvature) x.push_back(std::clamp(v * 4.0f, -1.0f, 1.0f));
    for (float v : obs.rayDistance) x.push_back(v * 2.0f - 1.0f);
    return x;
}

VehicleControls GeneticController::update(const Observation& obs, float /*dt*/) {
    VehicleControls out{};
    if (!genome_.isValid()) return out;

    std::vector<float> x = flattenObservation(obs);
    if ((int)x.size() != genome_.inputSize) return out;

    std::vector<float> h(genome_.hiddenSize, 0.0f);
    for (int o = 0; o < genome_.hiddenSize; ++o) {
        float s = genome_.b1[o];
        for (int i = 0; i < genome_.inputSize; ++i) {
            s += genome_.w1[o * genome_.inputSize + i] * x[i];
        }
        h[o] = relu(s);
    }

    float y[3] = {0.0f, 0.0f, 0.0f};
    for (int o = 0; o < 3; ++o) {
        float s = genome_.b2[o];
        for (int i = 0; i < genome_.hiddenSize; ++i) {
            s += genome_.w2[o * genome_.hiddenSize + i] * h[i];
        }
        y[o] = std::tanh(s);
    }

    out.throttle = clamp01(0.5f * (y[0] + 1.0f));
    out.brake    = clamp01(0.5f * (y[1] + 1.0f));
    out.steer    = clamp11(y[2]);

    if (out.throttle > 0.25f) out.brake *= 0.35f;
    out.handbrake = false;
    return out;
}
