#pragma once

#include <vector>
#include <string>
#include <array>
#include <glm/glm.hpp>
#include "Track.h"

// Режим редактирования
enum class EditorMode {
    PLAY,
    EDIT_WALLS,
    EDIT_CHECKPOINTS,
    EDIT_SPAWN,
    EDIT_SURFACES,
    EDIT_RACING_LINE,
    EDIT_OUTER_BOUNDARY,  // внешняя граница трассы
    EDIT_INNER_BOUNDARY,  // внутренняя граница (газон)
    EDIT_START_FINISH     // линия старта/финиша
};

// Инструмент мыши (поведение), поверх EditorMode (что редактируем)
enum class EditorTool {
    SELECT_MOVE,
    DRAW
};

enum class RenderMode {
    SIMPLE_BOXES,
    TEXTURED
};

class TrackEditor {
public:
    TrackEditor();
    ~TrackEditor() = default;

    void update(float dt);
    void handleMouseButton(int button, int action, double mouseX, double mouseY);
    void handleMouseMove(double mouseX, double mouseY);
    void handleKeyPress(int key, int action);
    void handleScroll(double yoffset);

    void render();
    void renderUI();

    void setMode(EditorMode mode);
    EditorMode getMode() const { return mode; }
    bool isEditing() const { return mode != EditorMode::PLAY; }

    void setTrack(Track* track);
    void newTrack();
    bool saveTrack(const std::string& filename);
    bool loadTrack(const std::string& filename);

    void undo();
    void redo();
    void clearAll();

    glm::vec2 screenToWorld(double screenX, double screenY) const;
    void setProjectionMatrix(const glm::mat4& proj) { projectionMatrix = proj; }
    void setViewportSize(int width, int height) { viewportWidth = width; viewportHeight = height; }
    void setCarPosition(const glm::vec2& p) { carWorldPos = p; }
    const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }


private:
    EditorMode mode;
    EditorTool tool = EditorTool::SELECT_MOVE;
    Track* currentTrack;
    bool isDirty;

    std::vector<glm::vec2> currentWallPoints;
    std::vector<glm::vec2> currentSurfacePoints;
    int selectedObjectIndex;

    float currentWallThickness;
    float currentWallFriction;
    float currentWallRestitution;
    float currentSurfaceMu;

    bool showGrid;
    int gridPow2 = 0;            // 0..8 (шаг = base / 2^gridPow2)
    float gridBase = 1.0f;       // базовый шаг (обычно 1.0)
    bool snapToGrid;

    enum class GridStyle { LINES, DOTS };
    GridStyle gridStyle = GridStyle::LINES;

    RenderMode currentRenderMode = RenderMode::SIMPLE_BOXES;

    // Камера редактора
    glm::vec2 cameraPos = glm::vec2(0.0f);
    float cameraZoom = 1.0f;
    bool followCar = true;
    bool isPanning = false;
    glm::vec2 carWorldPos = glm::vec2(0.0f);
    double lastMouseScreenX = 0.0;
    double lastMouseScreenY = 0.0;

    // Браузер треков
    bool showTracksWindow = false;
    std::vector<std::string> trackFiles;
    int selectedTrackFile = -1;
    std::string currentTrackPath;
    std::array<char, 256> saveAsBuf{};

    // Ввод размеров арены (буфер ввода ImGui)
    bool arenaBufInit = false;
    float arenaSizeBuf[2] = {120.0f, 120.0f};

    glm::mat4 projectionMatrix;
    int viewportWidth;
    int viewportHeight;

    glm::vec2 mouseWorldPos;
    glm::vec2 lastMouseWorldPos;
    bool isDragging;

    // AI
    std::vector<glm::vec2> currentRacingLinePoints;
    std::vector<glm::vec2> currentBoundaryPoints;   // строящаяся граница
    // старт/финиш задаётся двумя кликами
    bool hasStartFinishTemp = false;
    glm::vec2 startFinishTempA{0,0};
    void finishCurrentRacingLine();
    void finishCurrentBoundary();
    void renderRacingLine();
    void renderBoundaries();

    // ── Выделение и перетаскивание точек ─────────────────────────────────────
    enum class SelType {
        NONE,
        WALL_PT,
        SURFACE_PT,
        RACING_PT,
        RACING_HANDLE,
        CHECKPOINT_PT,
        OUTER_BOUNDARY_PT,
        INNER_BOUNDARY_PT,
        START_FINISH_A,
        START_FINISH_B
    };
    SelType selObjType  = SelType::NONE;
    int     selObjIdx   = -1;   // индекс объекта (стены/поверхности/…)
    int     selPtIdx    = -1;   // индекс точки внутри объекта
    bool    isDraggingPoint = false;
    glm::vec2 dragStartWorld{0,0}; // мировая позиция при начале драга

    bool    trySelectPoint(const glm::vec2& worldPos, float pickRadius);
    void    applyDragPoint(const glm::vec2& newWorldPos);
    // void    renderSelectionHandles();

    // ── История (должна быть до SelType из-за порядка инициализации) ───────────
    struct HistoryState {
        Track trackState;
        std::string description;
    };
    std::vector<HistoryState> history;
    int historyIndex = -1;
    static constexpr int MAX_HISTORY = 50;

    // ── Сплайны ───────────────────────────────────────────────────────────────
    bool  useSplines    = true;
    int   splineSamples = 12;
    float splineTension = 0.33f;

    void addHistoryState(const std::string& description);
    void finishCurrentWall();
    void finishCurrentSurface();
    void deleteLastPoint();
    glm::vec2 snapToGridIfNeeded(const glm::vec2& pos) const;

    void updateEditorProjection();
    void refreshTrackList();
    void syncArenaBufFromTrack();

    void renderGrid();
    void renderCurrentWall();
    void renderCurrentSurface();
    void renderWalls();
    void renderCheckpoints();
    void renderSpawnPoint();
    void renderSurfaces();
    void renderSelectionHandles();
    void renderMouseCursor();

    void renderMainToolbar();
    void renderPropertiesWindow();
    void renderObjectsList();
    void renderFileMenu();
};