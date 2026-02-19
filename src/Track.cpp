//
// Created by fkus on 05.02.2026.
//

#include "../include/Track.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

void Track::toJson(nlohmann::json& j) const {
    j["name"] = name;
    j["walls"] = walls;  // Auto-serializes vectors/structs with NLOHMANN_DEFINE_TYPE
    j["sectors"] = sectors;
    j["checkpoints"] = checkpoints;
    j["surfaces"] = surfaces;
    j["spawnPos"] = spawnPos;
    j["spawnYawRad"] = spawnYawRad;
    j["arenaHalfExtents"] = arenaHalfExtents;
}

void Track::fromJson(const nlohmann::json& j) {
    name = j["name"];
    walls = j["walls"];
    sectors = j["sectors"];
    checkpoints = j["checkpoints"];
    surfaces = j["surfaces"];
    spawnPos = j["spawnPos"];
    spawnYawRad = j["spawnYawRad"];
    arenaHalfExtents = j.value("arenaHalfExtents", glm::vec2(60.0f, 60.0f));
}

static std::string readAllTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool loadTrackFromFile(const std::string& path, Track& outTrack, std::string* outError) {
    try {
        const std::string text = readAllTextFile(path);
        const nlohmann::json j = nlohmann::json::parse(text);
        outTrack.fromJson(j);
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool saveTrackToFile(const std::string& path, const Track& track, std::string* outError) {
    try {
        nlohmann::json j;
        track.toJson(j);

        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Failed to open file for write: " + path);
        }
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}