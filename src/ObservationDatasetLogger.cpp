#include "ObservationDatasetLogger.h"
#include <iomanip>

bool ObservationDatasetLogger::open(const std::string& path, std::string* err) {
    close();
    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_.is_open()) {
        if (err) *err = "failed to open file: " + path;
        return false;
    }
    headerWritten_ = false;
    writeHeader();
    return true;
}

void ObservationDatasetLogger::close() {
    if (out_.is_open()) out_.close();
    headerWritten_ = false;
}

void ObservationDatasetLogger::writeHeader() {
    if (!out_.is_open() || headerWritten_) return;

    out_ << "speed,speedForward,speedAbs,yaw,steer,surfaceMu,distToCenterline,progress,offTrack,posX,posY,";
    out_ << "heading0,heading1,heading2,";
    out_ << "curv0,curv1,curv2,";
    for (int i = 0; i < Observation::kRayCount; ++i) {
        out_ << "ray" << i;
        out_ << (i + 1 < Observation::kRayCount ? "," : ",");
    }
    out_ << "targetThrottle,targetBrake,targetSteer,targetHandbrake,dt,carIndex,isAI,sourceTag\n";
    headerWritten_ = true;
}

void ObservationDatasetLogger::logSample(const Observation& obs,
                                         const VehicleControls& ctrl,
                                         float dt,
                                         int carIndex,
                                         bool isAI,
                                         const std::string& sourceTag) {
    if (!out_.is_open()) return;

    out_ << std::fixed << std::setprecision(6);
    out_ << obs.speed << ","
         << obs.speedForward << ","
         << obs.speedAbs << ","
         << obs.yaw << ","
         << obs.steer << ","
         << obs.surfaceMu << ","
         << obs.distToCenterline << ","
         << obs.progress << ","
         << (obs.offTrack ? 1 : 0) << ","
         << obs.pos.x << ","
         << obs.pos.y << ",";

    out_ << obs.headingError[0] << "," << obs.headingError[1] << "," << obs.headingError[2] << ",";
    out_ << obs.curvature[0]    << "," << obs.curvature[1]    << "," << obs.curvature[2]    << ",";

    for (int i = 0; i < Observation::kRayCount; ++i) {
        out_ << obs.rayDistance[i] << ",";
    }

    out_ << ctrl.throttle << ","
         << ctrl.brake << ","
         << ctrl.steer << ","
         << (ctrl.handbrake ? 1 : 0) << ","
         << dt << ","
         << carIndex << ","
         << (isAI ? 1 : 0) << ","
         << sourceTag << "\n";
    out_.flush();
}