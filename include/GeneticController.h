#pragma once

#include "Controller.h"
#include <random>
#include <string>
#include <vector>

class GeneticController : public Controller {
public:
    struct Genome {
        int inputSize = 0;
        int hiddenSize = 16;
        int outputSize = 3;

        std::vector<float> w1; // [hidden][input]
        std::vector<float> b1; // [hidden]
        std::vector<float> w2; // [output][hidden]
        std::vector<float> b2; // [output]

        bool isValid() const;
        void randomize(std::mt19937& rng, int inSize, int hidden, int outSize = 3, float scale = 0.8f);
        static Genome crossover(const Genome& a, const Genome& b, std::mt19937& rng);
        void mutate(std::mt19937& rng, float sigma = 0.12f, float prob = 0.10f);
    };

    GeneticController() = default;
    explicit GeneticController(const Genome& genome);

    void setGenome(const Genome& genome);
    const Genome& getGenome() const { return genome_; }

    VehicleControls update(const Observation& obs, float dt) override;

    static int inputSizeFromObservation();
    static std::vector<float> flattenObservation(const Observation& obs);

private:
    Genome genome_;
};
