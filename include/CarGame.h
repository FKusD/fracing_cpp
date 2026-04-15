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
#include "GenomeController.h"
#include <cstdint>
#include "GATrainingMonitor.h"


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
    void resetTrainingEpisode();

    //     if (trackEditor) {
    //         trackEditor->setMode(active ? EditorMode::EDIT_WALLS : EditorMode::PLAY);
    //     }
    // }

    VehicleControls computeRacingLineControls(int carIdx);

private:
    enum class CarInteractionMode {
        GHOSTS = 0,
        COLLISIONS
    };

    struct CarAIConfig {
        AIControllerType type = AIControllerType::NONE; // NONE = scripted fallback
        std::string modelPath;
        bool collidesWithCars = true;
    };
    std::vector<CarAIConfig> aiConfigs_;
    std::vector<float> trainingFitness_;

    struct TrainingCarState {
        float trackPos01 = 0.0f;
        float prevTrackPos01 = 0.0f;
        float forwardAlignment = 0.0f;   // cos between car forward and track tangent
        float validProgressDelta = 0.0f;
        float bestTrackPos01 = 0.0f;

        int lapCount = 0;
        int invalidFinishAttempts = 0;

        bool wrongWay = false;
        float wrongWayTime = 0.0f;
        float reverseProgressTime = 0.0f;
        float trackForwardDot = 0.0f;
        bool crossedFinishForward = false;
    };

    std::vector<TrainingCarState> trainingCars_;
    float bestGenerationFitness_ = -1e30f;
    int bestGenerationCarIdx_ = -1;

    void updateTrainingCarState(int carIdx, const Observation& obs);

    CarInteractionMode interactionMode_ = CarInteractionMode::COLLISIONS;

    float simSpeedMultiplier_ = 1.0f;
    int maxSubstepsPerFrame_ = 8;

    bool trainingMode_ = false;
    bool trainingUseAllCores_ = false; // пока задел под будущее

    bool lidarEnabled_ = true;
    bool showLidarRays_ = true;

    float trainingEpisodeTime_ = 20.0f;
    float trainingElapsed_ = 0.0f;
    bool trainingAutoReset_ = false;
    bool trainingRunning_ = false;

    std::vector<GenomeSpec> gaGenomes_;
    std::vector<float> gaLastProgress_;

    int gaGeneration_ = 0;
    int gaPopulationSize_ = 16;
    int gaEliteCount_ = 4;
    int gaHiddenSize_ = 24;

    float gaMutationSigma_ = 0.18f;
    float gaMutationProbability_ = 0.12f;

    // ── GA Monitor ──────────────────────────────────────────────────────────────
    GATrainingMonitor gaMonitor_;
    bool showGAMonitor_ = true;         // показывать ли окно

    // ── Adaptive sigma ──────────────────────────────────────────────────────────
    float gaBestFitnessEver_     = -1e30f;  // лучший фитнес за всё время
    float gaBestFitnessLastGen_  = -1e30f;  // лучший в предыдущем поколении
    int   gaStagnationCounter_   = 0;       // счётчик поколений без улучшения

    // ── Fitness weights (вынесено для удобства тюнинга из UI) ──────────────────
    float gaWProgress_     = 300.f;   // вес за продвижение вперёд
    float gaWSpeed_        = 0.01f;   // бонус за скорость
    float gaWOffTrack_     = 0.08f;   // штраф за вылет
    float gaWStall_        = 0.02f;   // штраф за стояние
    float gaWHeading_      = 0.005f;  // штраф за плохую ориентацию

    uint32_t gaSeed_ = 1337u;
    std::string gaBestGenomePath_ = "ga_best_genome.json";

    void setupGAPopulation();
    void advanceGAGeneration();
    void resetGAFitness();
    void saveBestGenome();
    bool isGenomeCar(int carIdx) const;

    bool trainingSpawnSinglePoint_ = true;
    glm::vec2 trainingSpawnPos_ = glm::vec2(0.0f, 0.0f);
    float trainingSpawnYaw_ = 0.0f;
    float trainingSpawnJitterPos_ = 0.35f;
    float trainingSpawnJitterYaw_ = 0.10f;

    void getSpawnPoseForCar(int idx, glm::vec2& outPos, float& outYaw);

    Observation buildObservation(int carIdx) const;
    void rebuildCarCollisionMasks();
    void assignControllerForCar(int carIdx);
    const char* controllerTypeName(AIControllerType t) const;

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

    GenerationStats collectGenerationStats() const;
    void            renderGAMonitorWindow();
    void            adaptMutationSigma(float bestFitnessThisGen);

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