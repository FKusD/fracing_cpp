#pragma once

#include <memory>
#include <vector>
#include "Box2D/Box2D.h"
#include <glm/glm.hpp>
#include <string>
#include "TrackEditor.h"
#include "Controller.h"
#include "ObservationDatasetLogger.h"
#include "BehavioralCloningController.h"
#include "RandomController.h"


class Controller;
// Forward declaration, чтобы не тянуть GLFW и GL в заголовок
struct GLFWwindow;
class Car;
class PhysicsWorld;
class Telemetry;
struct Track;

class CarGame {
public:
    CarGame();
    ~CarGame();

    bool initialize();
    void update(float deltaTime);
    void render();
    void handleInput();
    void resize(int width, int height);
    TrackEditor* getTrackEditor() { return trackEditor.get(); }

    void shutdown();

    bool isEditorMode() const {
        return trackEditor && trackEditor->isEditing();
    }

    void setEditorMode(bool enabled);// {
    //     if (trackEditor) {
    //         trackEditor->setMode(active ? EditorMode::EDIT_WALLS : EditorMode::PLAY);
    //     }
    // }

    VehicleControls computeRacingLineControls(int carIdx);

private:
    // УЛУЧШЕННЫЙ ЗУМ: уменьшаем VIEW_W и VIEW_H для увеличения машины на экране
    // Было: 32x18, стало: 24x13.5 (примерно 33% увеличение)
    static constexpr float VIEW_W = 24.0f;  // было 32.0f
    static constexpr float VIEW_H = 13.5f;  // было 18.0f
    static constexpr float PPM = 32.0f;     // Pixels Per Meter

    std::unique_ptr<TrackEditor> trackEditor;
    b2World* world;
    b2Vec2 gravity;
    std::unique_ptr<Telemetry> telemetry;
    std::unique_ptr<Track> track;
    std::vector<std::unique_ptr<Controller>> controllers; // 1:1 с cars
    std::vector<b2Body*> trackBodies;
    b2Body* arenaBoundsBody = nullptr;

    std::vector<std::unique_ptr<Car>> cars;
    int activeCarIndex = 0;

    Car* activeCar() {
        if (cars.empty()) return nullptr;
        if (activeCarIndex < 0) activeCarIndex = 0;
        if (activeCarIndex >= (int)cars.size()) activeCarIndex = (int)cars.size() - 1;
        return cars[activeCarIndex].get();
    }
    const Car* activeCar() const {
        if (cars.empty()) return nullptr;
        int idx = activeCarIndex;
        if (idx < 0) idx = 0;
        if (idx >= (int)cars.size()) idx = (int)cars.size() - 1;
        return cars[idx].get();
    }

    GLFWwindow* window;

    // Camera
    glm::mat4 projection;
    glm::vec3 cameraPos;

    // НОВОЕ: динамический зум (можно будет изменять в runtime)
    float zoomLevel = 1.0f; // 1.0 = базовый зум, 1.5 = увеличено, 0.5 = уменьшено

    // Timing
    float accumulator;
    static constexpr float STEP = 1.0f / 60.0f;
    static constexpr int V_IT = 6;
    static constexpr int P_IT = 2;

    // Rendering flags
    bool showHud;
    bool showPhysics;
    bool showTrajectory;
    std::string trackPath;

    // ── Пауза и выход ────────────────────────────────────────────────────
    bool isPaused        = false;
    bool showQuitConfirm = false;

    void createArenaBounds();
    void createObstacles();
    void createWallBox(const glm::vec2& center, float hx, float hy, float angleRad);
    bool loadTrack();
    void clearTrackCollision();
    void buildTrackCollision();
    void updateCamera();
    void renderHud();
    void renderTrackBackground();

    // ── Тайминг круга/секторов ──────────────────────────────────────────────
    struct LapTiming {
        bool  hasStartFinish = false;
        int   currentSector  = -1;     // id последнего сектора внутри которого машина
        int   lastPassedSector = -1;   // последний зафиксированный сектор (по порядку)

        float lapTime        = 0.0f;
        float bestLapTime    = -1.0f;
        float lastLapTime    = -1.0f;

        // по индексам сектора (id)
        std::vector<float> currentSplits; // накопленные времена по секторам
        std::vector<float> bestSplits;
        std::vector<float> lastSplits;

        glm::vec2 prevPos{0,0};
        bool hasPrevPos = false;
    } timing;

    void updateLapTiming(float dt);

    std::vector<bool> carIsAI;
    std::vector<std::unique_ptr<Controller>> aiControllers;
    void addAICar();
    void removeLastAICar();
    void resetAllCarsToGrid();

    ObservationDatasetLogger datasetLogger_;
    bool datasetRecording_ = false;
    bool useNeuralController_ = false;

    std::string datasetPath_ = "dataset_bc.csv";
    std::string modelPath_ = "bc_policy.json";

    bool spectatorMode = false;
    bool spectatorFree = false;
    int  spectatorTarget = 0;
    std::vector<int> aiWaypointIndex;   // текущий wp для каждой AI машины

    // Кэш сплайна для AI — пересчитывается при изменении racingLine
    std::vector<glm::vec2> splineCache;   // сэмплированный Catmull-Rom
    int   splineCacheVersion = -1;        // сравниваем с (int)racingLine.size()
    static constexpr int SPLINE_SAMPLES = 16; // сэмплов на сегмент контрольных точек
    void rebuildSplineCache();            // пересчитать splineCache из track->racingLine

    std::string formatTime(float seconds);
    std::string formatLastTime(float seconds);

    // ── Система границ трассы и штрафов ───────────────────────────────────────
    struct CarPenalty {
        float offTrackTime   = 0.0f;   // сколько секунд вне трассы
        float penaltySeconds = 0.0f;   // накопленный штраф
        bool  isOffTrack     = false;
        float slowdownFactor = 1.0f;   // множитель скорости (< 1 = замедление)
    };
    std::vector<CarPenalty> carPenalties;

    // Проверить точку относительно полигона (point-in-polygon)
    static bool pointInPolygon(const glm::vec2& pt, const std::vector<glm::vec2>& poly);
    // Дистанция от точки до ближайшего сегмента полигона
    static float distToPolyline(const glm::vec2& pt, const std::vector<glm::vec2>& poly, bool closed);
    // Обновить статус off-track для всех машин
    void updateOffTrack(float dt);
    // Применить замедление к машинам вне трассы
    void applyOffTrackPenalty(int carIdx, float dt);
};