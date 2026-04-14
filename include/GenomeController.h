#pragma once
#include "Controller.h"
#include <string>
#include <vector>

struct GenomeSpec {
    int inputSize = 0;
    int hiddenSize = 32;
    int outputSize = 3;
    std::vector<float> genes;
};

class GenomeController : public Controller {
public:
    GenomeController();
    explicit GenomeController(const GenomeSpec& spec);

    void setGenome(const GenomeSpec& spec);
    const GenomeSpec& genome() const { return genome_; }

    bool saveGenome(const std::string& path, std::string* err = nullptr) const;
    bool loadGenome(const std::string& path, std::string* err = nullptr);

    static int expectedInputSize();
    static GenomeSpec makeRandom(uint32_t seed, int hiddenSize = 32, float initScale = 0.35f);

    VehicleControls update(const Observation& obs, float dt) override;

private:
    GenomeSpec genome_;

    std::vector<float> flattenObservation(const Observation& obs) const;
    std::vector<float> forward(const std::vector<float>& x) const;
};
