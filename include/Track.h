#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>  // For serialization

// JSON serialization for glm::vec2
// Делается через adl_serializer, чтобы nlohmann::json понимал glm-типы.
namespace nlohmann {
template <>
struct adl_serializer<glm::vec2> {
    static void to_json(json& j, const glm::vec2& v) {
        j = json{{"x", v.x}, {"y", v.y}};
    }
    static void from_json(const json& j, glm::vec2& v) {
        j.at("x").get_to(v.x);
        j.at("y").get_to(v.y);
    }
};
} // namespace nlohmann

struct WallSegment {
    std::vector<glm::vec2> vertices;  // Polyline or polygon (closed if first == last)
    float thickness = 0.2f;           // For Box2D fixture half-width
    float friction = 0.5f;
    float restitution = 0.15f;
};

struct Sector {
    std::vector<glm::vec2> polygon;   // Polygon defining sector area
    int id;                           // Sequential ID for progress
};

struct Checkpoint {
    glm::vec2 start;
    glm::vec2 end;                    // Line segment to cross
};

struct SurfaceArea {
    std::vector<glm::vec2> polygon;   // Area with custom mu
    float mu = 1.15f;                 // Grip coefficient
};

struct Track {
    std::string name;
    std::vector<WallSegment> walls;
    std::vector<Sector> sectors;
    std::vector<Checkpoint> checkpoints;
    std::vector<SurfaceArea> surfaces;
    glm::vec2 spawnPos = glm::vec2(0.0f, 0.0f);
    glm::vec2 arenaHalfExtents = glm::vec2(60.0f, 60.0f);

    float spawnYawRad = 0.0f;

    // Serialization
    void toJson(nlohmann::json& j) const;
    void fromJson(const nlohmann::json& j);
};

// JSON for track sub-structs
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WallSegment, vertices, thickness, friction, restitution);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Sector, polygon, id);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Checkpoint, start, end);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SurfaceArea, polygon, mu);

// File IO helpers
bool loadTrackFromFile(const std::string& path, Track& outTrack, std::string* outError = nullptr);
bool saveTrackToFile(const std::string& path, const Track& track, std::string* outError = nullptr);