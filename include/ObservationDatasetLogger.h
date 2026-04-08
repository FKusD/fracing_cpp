#pragma once
#include "Controller.h"
#include "VehicleControls.h"
#include <fstream>
#include <string>

class ObservationDatasetLogger {
public:
    bool open(const std::string& path, std::string* err = nullptr);
    void close();
    bool isOpen() const { return out_.is_open(); }

    void logSample(const Observation& obs,
                   const VehicleControls& ctrl,
                   float dt,
                   int carIndex,
                   bool isAI,
                   const std::string& sourceTag);

private:
    std::ofstream out_;
    bool headerWritten_ = false;
    void writeHeader();
};