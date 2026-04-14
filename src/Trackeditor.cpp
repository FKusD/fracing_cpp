#include "TrackEditor.h"
#include "DebugRenderer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

namespace {
static std::string baseNameNoExt(const std::string& path) {
    std::filesystem::path p(path);
    return p.stem().string();
}

static bool endsWith(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    return std::equal(suf.rbegin(), suf.rend(), s.rbegin());
}

static std::string ensureJsonExt(std::string name) {
    if (!endsWith(name, ".json")) name += ".json";
    return name;
}

static glm::vec2 polygonCentroid(const std::vector<glm::vec2>& poly) {
    if (poly.empty()) return {0,0};
    glm::vec2 c(0,0);
    for (auto& p : poly) c += p;
    return c * (1.0f / (float)poly.size());
}

} // namespace

// ── Кубические Безье сплайны ─────────────────────────────────────────────────
// G1-непрерывные кубические Безье с автоматическими касательными.
// tension 0.33 ≈ Catmull-Rom, можно регулировать в UI.
namespace {

static glm::vec2 bezierCubicPt(const glm::vec2& p1, const glm::vec2& c1,
                                 const glm::vec2& c2, const glm::vec2& p2, float t) {
    float u=1-t, u2=u*u, u3=u2*u, t2=t*t, t3=t2*t;
    return u3*p1 + 3.0f*u2*t*c1 + 3.0f*u*t2*c2 + t3*p2;
}

// Вычисляет управляющие точки по tension (возвращает cp1[i]=правая, cp2[i]=левая ручка)
static void buildBezierHandles(const std::vector<glm::vec2>& pts, float tension,
                                 std::vector<glm::vec2>& cp1,
                                 std::vector<glm::vec2>& cp2) {
    int N = (int)pts.size();
    cp1.resize(N); cp2.resize(N);
    for (int i = 0; i < N; ++i) {
        glm::vec2 tang = (pts[(i+1)%N] - pts[(i-1+N)%N]) * tension;
        cp1[i] = pts[i] + tang;
        cp2[i] = pts[i] - tang;
    }
}

static std::vector<glm::vec2> sampleBezierSpline(const std::vector<glm::vec2>& pts,
                                                   int samplesPerSeg,
                                                   float tension) {
    std::vector<glm::vec2> out;
    int N = (int)pts.size();
    if (N < 2) return out;
    std::vector<glm::vec2> cp1, cp2;
    buildBezierHandles(pts, tension, cp1, cp2);
    out.reserve(N * samplesPerSeg);
    for (int i = 0; i < N; ++i) {
        int j = (i+1)%N;
        for (int s = 0; s < samplesPerSeg; ++s) {
            float t = (float)s / samplesPerSeg;
            out.push_back(bezierCubicPt(pts[i], cp1[i], cp2[j], pts[j], t));
        }
    }
    return out;
}

// Ближайшая точка в списке к worldPos в радиусе radius (-1 если нет)
static int findNearest(const std::vector<glm::vec2>& pts,
                        const glm::vec2& worldPos, float radius) {
    int   best  = -1;
    float best2 = radius * radius;
    for (int i = 0; i < (int)pts.size(); ++i) {
        glm::vec2 d = pts[i] - worldPos;
        float d2 = d.x*d.x + d.y*d.y;
        if (d2 < best2) { best2 = d2; best = i; }
    }
    return best;
}

} // namespace (bezier)

static float gridStep(float base, int pow2) {
    pow2 = std::clamp(pow2, 0, 12);
    return base / float(1 << pow2);
}


TrackEditor::TrackEditor()
    : mode(EditorMode::PLAY)
    , currentTrack(nullptr)
    , isDirty(false)
    , selectedObjectIndex(-1)
    , currentWallThickness(0.5f)
    , currentWallFriction(0.5f)
    , currentWallRestitution(0.15f)
    , currentSurfaceMu(1.15f)
    , showGrid(true)
    , gridPow2(1)  // 0.5 по умолчанию
    , gridBase(1.0f)
    , snapToGrid(false)
    , projectionMatrix(1.0f)
    , viewportWidth(1280)
    , viewportHeight(720)
    , mouseWorldPos(0.0f)
    , lastMouseWorldPos(0.0f)
    , isDragging(false)
    , historyIndex(-1)
    , selObjType(SelType::NONE)
    , selObjIdx(-1)
    , selPtIdx(-1)
    , isDraggingPoint(false)
    , useSplines(true)
    , splineSamples(12)
{
    projectionMatrix = glm::mat4(1.0f);

    std::cout << "[TrackEditor] Constructor START" << std::endl;
    selectedObjectIndex = -1;
    std::cout << "[TrackEditor] Constructor END" << std::endl;

    std::memset(saveAsBuf.data(), 0, saveAsBuf.size());
}

void TrackEditor::updateEditorProjection() {
    if (viewportWidth <= 0 || viewportHeight <= 0) return;
    float aspect = (float)viewportWidth / (float)std::max(1, viewportHeight);

    const float baseViewHeight = 30.0f;
    float viewH = baseViewHeight / std::max(0.05f, cameraZoom);
    float viewW = viewH * aspect;

    float l = cameraPos.x - viewW * 0.5f;
    float r = cameraPos.x + viewW * 0.5f;
    float b = cameraPos.y - viewH * 0.5f;
    float t = cameraPos.y + viewH * 0.5f;

    projectionMatrix = glm::ortho(l, r, b, t, -1.0f, 1.0f);
}

void TrackEditor::refreshTrackList() {
    trackFiles.clear();
    std::filesystem::create_directories("tracks");

    for (auto& e : std::filesystem::directory_iterator("tracks")) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() == ".json") {
            trackFiles.push_back(e.path().string());
        }
    }
    std::sort(trackFiles.begin(), trackFiles.end());

    selectedTrackFile = -1;
    if (!currentTrackPath.empty()) {
        for (int i = 0; i < (int)trackFiles.size(); ++i) {
            if (trackFiles[i] == currentTrackPath) {
                selectedTrackFile = i;
                break;
            }
        }
    }
}

void TrackEditor::syncArenaBufFromTrack() {
    if (!currentTrack) return;
    arenaSizeBuf[0] = currentTrack->arenaHalfExtents.x * 2.0f;
    arenaSizeBuf[1] = currentTrack->arenaHalfExtents.y * 2.0f;
    arenaBufInit = true;
}

void TrackEditor::setMode(EditorMode newMode) {
    if (mode == newMode) return;

    if (mode == EditorMode::EDIT_WALLS && !currentWallPoints.empty()) {
        finishCurrentWall();
    }
    if (mode == EditorMode::EDIT_SURFACES && !currentSurfacePoints.empty()) {
        finishCurrentSurface();
    }
    if (mode == EditorMode::EDIT_RACING_LINE && !currentRacingLinePoints.empty()) {
        finishCurrentRacingLine();
    }
    if ((mode == EditorMode::EDIT_OUTER_BOUNDARY || mode == EditorMode::EDIT_INNER_BOUNDARY) &&
        !currentBoundaryPoints.empty()) {
        // не автосохраняем — просто отменяем незавершённое
        currentBoundaryPoints.clear();
    }
    if (mode == EditorMode::EDIT_START_FINISH) {
        hasStartFinishTemp = false;
    }

    mode = newMode;
    selectedObjectIndex = -1;
    currentWallPoints.clear();
    currentSurfacePoints.clear();
    currentRacingLinePoints.clear();
    currentBoundaryPoints.clear();
    hasStartFinishTemp = false;

    if (mode != EditorMode::PLAY) {
        cameraPos = carWorldPos;
        updateEditorProjection();
    }
}

void TrackEditor::setTrack(Track* track) {
    std::cout << "[setTrack] START" << std::endl;
    currentTrack = track;
    isDirty = false;
    history.clear();
    historyIndex = -1;

    if (track) {
        addHistoryState("Initial state");
        if (!arenaBufInit) syncArenaBufFromTrack();
    }
}

void TrackEditor::newTrack() {
    if (!currentTrack) return;

    currentTrack->name = "New Track";
    currentTrack->walls.clear();
    currentTrack->checkpoints.clear();
    currentTrack->surfaces.clear();
    currentTrack->sectors.clear();
    currentTrack->racingLine.clear();
    currentTrack->racingLineOutHandle.clear();
    currentTrack->outerBoundary.clear();
    currentTrack->innerBoundary.clear();
    currentTrack->startFinish = Checkpoint{};
    currentTrack->spawnPos = glm::vec2(0.0f, 0.0f);
    currentTrack->spawnYawRad = 0.0f;
    currentTrack->arenaHalfExtents = glm::vec2(60.0f, 60.0f);

    isDirty = false;
    history.clear();
    historyIndex = -1;
    addHistoryState("New track");

    currentTrackPath.clear();
    arenaBufInit = false;
    syncArenaBufFromTrack();

    // очистить незавершённые операции редактора
    currentWallPoints.clear();
    currentSurfacePoints.clear();
    currentRacingLinePoints.clear();
    currentBoundaryPoints.clear();
    hasStartFinishTemp = false;
}

bool TrackEditor::saveTrack(const std::string& filename) {
    if (!currentTrack) return false;

    std::string error;
    if (saveTrackToFile(filename, *currentTrack, &error)) {
        isDirty = false;
        currentTrackPath = filename;
        refreshTrackList();
        std::cout << "Track saved: " << filename << std::endl;
        return true;
    } else {
        std::cerr << "Failed to save track: " << error << std::endl;
        return false;
    }
}

bool TrackEditor::loadTrack(const std::string& filename) {
    if (!currentTrack) return false;

    std::string error;
    if (loadTrackFromFile(filename, *currentTrack, &error)) {
        isDirty = false;
        history.clear();
        historyIndex = -1;
        addHistoryState("Loaded track");
        currentTrackPath = filename;
        arenaBufInit = false;
        syncArenaBufFromTrack();
        refreshTrackList();

        // сбросить незавершённые операции
        currentWallPoints.clear();
        currentSurfacePoints.clear();
        currentRacingLinePoints.clear();
        currentBoundaryPoints.clear();
        hasStartFinishTemp = false;
        std::cout << "Track loaded: " << filename << std::endl;
        return true;
    } else {
        std::cerr << "Failed to load track: " << error << std::endl;
        return false;
    }
}

void TrackEditor::update(float dt) {
    if (!currentTrack) return;
    if (!isEditing()) return;
    (void)dt;

    if (followCar && !isPanning) {
        cameraPos = carWorldPos;
    }
    updateEditorProjection();
}

void TrackEditor::handleMouseButton(int button, int action, double mouseX, double mouseY) {
    if (!isEditing() || !currentTrack) return;

    // ── ПКМ / MMB: pan ───────────────────────────────────────────────────────
    if (button == 2) {
        if (action == 1) {
            isPanning = true;
            lastMouseScreenX = mouseX;
            lastMouseScreenY = mouseY;
            followCar = false;
        } else {
            isPanning = false;
        }
        return;
    }

    mouseWorldPos = screenToWorld(mouseX, mouseY);
    glm::vec2 snappedPos = snapToGridIfNeeded(mouseWorldPos);

    // ── Pick radius: ~12px в мировых единицах ────────────────────────────────
    float aspect = (float)viewportWidth / std::max(1, viewportHeight);
    float viewH  = 30.0f / std::max(0.05f, cameraZoom);
    float pickR  = viewH / (float)viewportHeight * 28.0f;

    // ── ЛКМ НАЖАТА ───────────────────────────────────────────────────────────
    if (button == 0 && action == 1) {

        // 1) SELECT/MOVE: всегда пробуем выделить/начать drag
        if (tool == EditorTool::SELECT_MOVE) {
            trySelectPoint(mouseWorldPos, pickR);
            if (selObjType != SelType::NONE) {
                isDraggingPoint = true;
                dragStartWorld  = mouseWorldPos;
            }
            return; // select никогда не добавляет геометрию
        }

        // 2) DRAW: добавляем геометрию (в draw также работает drag по существующим точкам)
        if (trySelectPoint(mouseWorldPos, pickR) && selObjType != SelType::NONE) {
            isDraggingPoint = true;
            dragStartWorld  = mouseWorldPos;
            return;
        }

        if (mode == EditorMode::EDIT_WALLS) {
            currentWallPoints.push_back(snappedPos);
        }
        else if (mode == EditorMode::EDIT_SURFACES) {
            currentSurfacePoints.push_back(snappedPos);
        }
        else if (mode == EditorMode::EDIT_SPAWN) {
            currentTrack->startGrid.origin = snappedPos;
            currentTrack->spawnPos = snappedPos;
            addHistoryState("Move spawn point");
            isDirty = true;
        }
        else if (mode == EditorMode::EDIT_RACING_LINE) {
            currentRacingLinePoints.push_back(snappedPos);
        }
        else if (mode == EditorMode::EDIT_OUTER_BOUNDARY || mode == EditorMode::EDIT_INNER_BOUNDARY) {
            currentBoundaryPoints.push_back(snappedPos);
        }
        else if (mode == EditorMode::EDIT_START_FINISH) {
            if (!hasStartFinishTemp) {
                startFinishTempA = snappedPos;
                hasStartFinishTemp = true;
            } else {
                currentTrack->startFinish.start = startFinishTempA;
                currentTrack->startFinish.end   = snappedPos;
                hasStartFinishTemp = false;
                addHistoryState("Set start/finish");
                isDirty = true;
            }
        }
        else if (mode == EditorMode::EDIT_CHECKPOINTS) {
            static glm::vec2 checkpointStart;
            static bool hasStart = false;
            if (!hasStart) {
                checkpointStart = snappedPos;
                hasStart = true;
            } else {
                Checkpoint cp;
                cp.start = checkpointStart;
                cp.end   = snappedPos;
                currentTrack->checkpoints.push_back(cp);
                addHistoryState("Add checkpoint");
                isDirty  = true;
                hasStart = false;
            }
        }
    }

    // ── ЛКМ ОТПУЩЕНА ─────────────────────────────────────────────────────────
    if (button == 0 && action == 0 && isDraggingPoint) {
        isDraggingPoint = false;
        // selObjType      = SelType::NONE;
        addHistoryState("Move point");
        isDirty = true;
    }

    // ── ПКМ: завершить рисование ──────────────────────────────────────────────
    if (button == 1 && action == 1) {
        if (mode == EditorMode::EDIT_WALLS && currentWallPoints.size() >= 2)
            finishCurrentWall();
        else if (mode == EditorMode::EDIT_SURFACES && currentSurfacePoints.size() >= 3)
            finishCurrentSurface();
        else if (mode == EditorMode::EDIT_RACING_LINE && currentRacingLinePoints.size() >= 2)
            finishCurrentRacingLine();
        else if (mode == EditorMode::EDIT_OUTER_BOUNDARY && currentBoundaryPoints.size() >= 3)
            finishCurrentBoundary();
        else if (mode == EditorMode::EDIT_INNER_BOUNDARY && currentBoundaryPoints.size() >= 3)
            finishCurrentBoundary();
        else if (mode == EditorMode::EDIT_START_FINISH) {
            // ПКМ отменяет незавершённую постановку
            hasStartFinishTemp = false;
        }
    }
}

void TrackEditor::handleMouseMove(double mouseX, double mouseY) {
    lastMouseWorldPos = mouseWorldPos;
    mouseWorldPos = screenToWorld(mouseX, mouseY);

    // Перетаскивание выделенной точки
    if (isDraggingPoint && selObjType != SelType::NONE) {
        applyDragPoint(snapToGridIfNeeded(mouseWorldPos));
        return;
    }

    if (isPanning) {
        glm::vec2 w0 = screenToWorld(lastMouseScreenX, lastMouseScreenY);
        glm::vec2 w1 = screenToWorld(mouseX, mouseY);
        cameraPos += (w0 - w1);
        lastMouseScreenX = mouseX;
        lastMouseScreenY = mouseY;
        updateEditorProjection();
    }
}

void TrackEditor::handleKeyPress(int key, int action) {
    if (!isEditing()) return;
    if (action != 1) return;

    if (key == 259) { // Backspace
        deleteLastPoint();
    }

    if (key == 256) { // Escape
        currentWallPoints.clear();
        currentSurfacePoints.clear();
        currentRacingLinePoints.clear();
        currentBoundaryPoints.clear();
        hasStartFinishTemp = false;
    }

    if (key == 71) { // G
        showGrid = !showGrid;
    }

    if (key == 72) { // H
        snapToGrid = !snapToGrid;
    }

    if (key == 70) { // F
        followCar = !followCar;
    }
}

void TrackEditor::handleScroll(double yoffset) {
    if (!isEditing()) return;

    ImGuiIO& io = ImGui::GetIO();

    // Shift+wheel -> толщина стены (только в режиме стен)
    if (io.KeyShift && mode == EditorMode::EDIT_WALLS) {
        currentWallThickness += static_cast<float>(yoffset) * 0.05f;
        currentWallThickness = std::clamp(currentWallThickness, 0.1f, 3.0f);
        return;
    }

    // Обычное колесо -> зум камеры
    float factor = std::pow(1.12f, (float)yoffset);
    cameraZoom = std::clamp(cameraZoom * factor, 0.15f, 12.0f);
    followCar = false;
    updateEditorProjection();
}


void TrackEditor::finishCurrentWall() {
    if (currentWallPoints.size() < 2 || !currentTrack) return;

    WallSegment wall;
    wall.vertices = currentWallPoints;
    wall.thickness = currentWallThickness;
    wall.friction = currentWallFriction;
    wall.restitution = currentWallRestitution;

    currentTrack->walls.push_back(wall);
    currentWallPoints.clear();

    addHistoryState("Add wall");
    isDirty = true;
}

void TrackEditor::finishCurrentSurface() {
    if (currentSurfacePoints.size() < 3 || !currentTrack) return;

    SurfaceArea surface;
    surface.polygon = currentSurfacePoints;
    surface.mu = currentSurfaceMu;

    currentTrack->surfaces.push_back(surface);
    currentSurfacePoints.clear();

    addHistoryState("Add surface");
    isDirty = true;
}

void TrackEditor::finishCurrentRacingLine() {
    if (currentRacingLinePoints.size() < 2 || !currentTrack) return;

    for (const auto& p : currentRacingLinePoints)
        currentTrack->racingLine.push_back(p);
    currentRacingLinePoints.clear();

    // Rebuild ALL handles with Catmull-Rom tangents (smooth from the start)
    auto& rl = currentTrack->racingLine;
    int N = (int)rl.size();
    currentTrack->racingLineOutHandle.resize(N);
    for (int i = 0; i < N; ++i)
        currentTrack->racingLineOutHandle[i] = (rl[(i+1)%N] - rl[(i-1+N)%N]) * splineTension;

    addHistoryState("Add racing line");
    isDirty = true;
}

void TrackEditor::deleteLastPoint() {
    // Если рисуем — удаляем последнюю точку рисования
    if (!currentWallPoints.empty() && mode == EditorMode::EDIT_WALLS) {
        currentWallPoints.pop_back(); return;
    }
    if (!currentBoundaryPoints.empty() &&
        (mode == EditorMode::EDIT_OUTER_BOUNDARY || mode == EditorMode::EDIT_INNER_BOUNDARY)) {
        currentBoundaryPoints.pop_back(); return;
    }
    if (!currentSurfacePoints.empty() && mode == EditorMode::EDIT_SURFACES) {
        currentSurfacePoints.pop_back(); return;
    }
    if (!currentRacingLinePoints.empty() && mode == EditorMode::EDIT_RACING_LINE) {
        currentRacingLinePoints.pop_back(); return;
    }

    // Иначе — удаляем выделенную точку объекта
    if (selObjType == SelType::NONE || selPtIdx < 0 || !currentTrack) return;

    if (selObjType == SelType::RACING_PT) {
        auto& rl = currentTrack->racingLine;
        if (selPtIdx < (int)rl.size()) {
            rl.erase(rl.begin() + selPtIdx);
            if (currentTrack->racingLineOutHandle.size() == rl.size() + 1) {
                currentTrack->racingLineOutHandle.erase(currentTrack->racingLineOutHandle.begin() + selPtIdx);
            }
            selPtIdx = std::min(selPtIdx, (int)rl.size()-1);
            if (rl.empty()) selObjType = SelType::NONE;
            addHistoryState("Delete racing line point");
            isDirty = true;
        }
    } else if (selObjType == SelType::RACING_HANDLE) {
        // удаление ручки -> сбросить на дефолт
        if (selPtIdx >= 0 && selPtIdx < (int)currentTrack->racingLineOutHandle.size()) {
            currentTrack->racingLineOutHandle[selPtIdx] = glm::vec2(2.0f, 0.0f);
            addHistoryState("Reset racing handle");
            isDirty = true;
        }
    } else if (selObjType == SelType::WALL_PT && selObjIdx < (int)currentTrack->walls.size()) {
        auto& verts = currentTrack->walls[selObjIdx].vertices;
        if (selPtIdx < (int)verts.size()) {
            verts.erase(verts.begin() + selPtIdx);
            if (verts.size() < 2) {
                currentTrack->walls.erase(currentTrack->walls.begin() + selObjIdx);
                selObjType = SelType::NONE;
            } else {
                selPtIdx = std::min(selPtIdx, (int)verts.size()-1);
            }
            addHistoryState("Delete wall point");
            isDirty = true;
        }
    } else if (selObjType == SelType::SURFACE_PT && selObjIdx < (int)currentTrack->surfaces.size()) {
        auto& poly = currentTrack->surfaces[selObjIdx].polygon;
        if (selPtIdx < (int)poly.size()) {
            poly.erase(poly.begin() + selPtIdx);
            if (poly.size() < 3) {
                currentTrack->surfaces.erase(currentTrack->surfaces.begin() + selObjIdx);
                selObjType = SelType::NONE;
            } else {
                selPtIdx = std::min(selPtIdx, (int)poly.size()-1);
            }
            addHistoryState("Delete surface point");
            isDirty = true;
        }
    } else if (selObjType == SelType::CHECKPOINT_PT && selObjIdx < (int)currentTrack->checkpoints.size()) {
        currentTrack->checkpoints.erase(currentTrack->checkpoints.begin() + selObjIdx);
        selObjType = SelType::NONE;
        addHistoryState("Delete checkpoint");
        isDirty = true;
    } else if (selObjType == SelType::OUTER_BOUNDARY_PT) {
        if (selPtIdx >= 0 && selPtIdx < (int)currentTrack->outerBoundary.size()) {
            currentTrack->outerBoundary.erase(currentTrack->outerBoundary.begin() + selPtIdx);
            addHistoryState("Delete outer boundary point");
            isDirty = true;
        }
        selObjType = SelType::NONE;
    } else if (selObjType == SelType::INNER_BOUNDARY_PT) {
        if (selPtIdx >= 0 && selPtIdx < (int)currentTrack->innerBoundary.size()) {
            currentTrack->innerBoundary.erase(currentTrack->innerBoundary.begin() + selPtIdx);
            addHistoryState("Delete inner boundary point");
            isDirty = true;
        }
        selObjType = SelType::NONE;
    } else if (selObjType == SelType::START_FINISH_A || selObjType == SelType::START_FINISH_B) {
        currentTrack->startFinish = Checkpoint{};
        selObjType = SelType::NONE;
        addHistoryState("Clear start/finish");
        isDirty = true;
    }
}

// ── Select & Drag ─────────────────────────────────────────────────────────────

bool TrackEditor::trySelectPoint(const glm::vec2& worldPos, float pickR) {
    if (!currentTrack) return false;

    // 0) Racing line handles (тянуть "ползунки") — самый высокий приоритет
    if (!currentTrack->racingLine.empty() && currentTrack->racingLineOutHandle.size() == currentTrack->racingLine.size()) {
        // ищем ближайший out-handle
        int best = -1;
        float bestD2 = pickR * pickR;
        for (int i = 0; i < (int)currentTrack->racingLine.size(); ++i) {
            glm::vec2 hp = currentTrack->racingLine[i] + currentTrack->racingLineOutHandle[i];
            glm::vec2 d = hp - worldPos;
            float d2 = d.x*d.x + d.y*d.y;
            if (d2 < bestD2) { bestD2 = d2; best = i; }
        }
        if (best >= 0) {
            selObjType = SelType::RACING_HANDLE;
            selObjIdx  = 0;
            selPtIdx   = best;
            return true;
        }
    }

    // 1) Racing line — приоритет, т.к. рисуем поверх всего
    {
        int idx = findNearest(currentTrack->racingLine, worldPos, pickR);
        if (idx >= 0) { selObjType=SelType::RACING_PT; selObjIdx=0; selPtIdx=idx; return true; }
    }

    // 1.5) Границы
    {
        int idx = findNearest(currentTrack->outerBoundary, worldPos, pickR);
        if (idx >= 0) { selObjType = SelType::OUTER_BOUNDARY_PT; selObjIdx = 0; selPtIdx = idx; return true; }
    }
    {
        int idx = findNearest(currentTrack->innerBoundary, worldPos, pickR);
        if (idx >= 0) { selObjType = SelType::INNER_BOUNDARY_PT; selObjIdx = 0; selPtIdx = idx; return true; }
    }

    // 1.6) Старт/финиш
    {
        const auto& sf = currentTrack->startFinish;
        glm::vec2 ds = sf.start - worldPos;
        glm::vec2 de = sf.end   - worldPos;
        float r2 = pickR * pickR;
        if ((ds.x*ds.x + ds.y*ds.y) < r2) { selObjType = SelType::START_FINISH_A; selObjIdx = 0; selPtIdx = 0; return true; }
        if ((de.x*de.x + de.y*de.y) < r2) { selObjType = SelType::START_FINISH_B; selObjIdx = 0; selPtIdx = 0; return true; }
    }
    // 2) Стены
    for (int wi=0; wi<(int)currentTrack->walls.size(); ++wi) {
        int idx = findNearest(currentTrack->walls[wi].vertices, worldPos, pickR);
        if (idx>=0) { selObjType=SelType::WALL_PT; selObjIdx=wi; selPtIdx=idx; return true; }
    }
    // 3) Поверхности
    for (int si=0; si<(int)currentTrack->surfaces.size(); ++si) {
        int idx = findNearest(currentTrack->surfaces[si].polygon, worldPos, pickR);
        if (idx>=0) { selObjType=SelType::SURFACE_PT; selObjIdx=si; selPtIdx=idx; return true; }
    }
    // 4) Чекпоинты (две точки каждый)
    for (int ci=0; ci<(int)currentTrack->checkpoints.size(); ++ci) {
        auto& cp = currentTrack->checkpoints[ci];
        glm::vec2 ds=cp.start-worldPos, de=cp.end-worldPos;
        float r2=pickR*pickR;
        if (ds.x*ds.x+ds.y*ds.y<r2) { selObjType=SelType::CHECKPOINT_PT; selObjIdx=ci; selPtIdx=0; return true; }
        if (de.x*de.x+de.y*de.y<r2) { selObjType=SelType::CHECKPOINT_PT; selObjIdx=ci; selPtIdx=1; return true; }
    }

    selObjType = SelType::NONE;
    return false;
}

void TrackEditor::applyDragPoint(const glm::vec2& newPos) {
    if (!currentTrack) return;
    switch (selObjType) {
    case SelType::RACING_PT:
        if (selPtIdx < (int)currentTrack->racingLine.size())
            currentTrack->racingLine[selPtIdx] = newPos;
        break;
    case SelType::RACING_HANDLE:
        if (selPtIdx < (int)currentTrack->racingLine.size() &&
            currentTrack->racingLineOutHandle.size() == currentTrack->racingLine.size()) {
            glm::vec2 p = currentTrack->racingLine[selPtIdx];
            glm::vec2 h = newPos - p;
            // ограничим длину ручки, чтобы не улетало в космос
            float len = std::sqrt(h.x*h.x + h.y*h.y);
            if (len > 20.0f) h *= (20.0f / len);
            currentTrack->racingLineOutHandle[selPtIdx] = h;
        }
        break;
    case SelType::WALL_PT:
        if (selObjIdx < (int)currentTrack->walls.size()) {
            auto& v = currentTrack->walls[selObjIdx].vertices;
            if (selPtIdx < (int)v.size()) v[selPtIdx] = newPos;
        }
        break;
    case SelType::SURFACE_PT:
        if (selObjIdx < (int)currentTrack->surfaces.size()) {
            auto& p = currentTrack->surfaces[selObjIdx].polygon;
            if (selPtIdx < (int)p.size()) p[selPtIdx] = newPos;
        }
        break;
    case SelType::CHECKPOINT_PT:
        if (selObjIdx < (int)currentTrack->checkpoints.size()) {
            auto& cp = currentTrack->checkpoints[selObjIdx];
            if (selPtIdx==0) cp.start=newPos; else cp.end=newPos;
        }
        break;
    case SelType::OUTER_BOUNDARY_PT:
        if (selPtIdx < (int)currentTrack->outerBoundary.size())
            currentTrack->outerBoundary[selPtIdx] = newPos;
        break;
    case SelType::INNER_BOUNDARY_PT:
        if (selPtIdx < (int)currentTrack->innerBoundary.size())
            currentTrack->innerBoundary[selPtIdx] = newPos;
        break;
    case SelType::START_FINISH_A:
        currentTrack->startFinish.start = newPos;
        break;
    case SelType::START_FINISH_B:
        currentTrack->startFinish.end = newPos;
        break;
    default: break;
    }
    isDirty = true;
}

void TrackEditor::renderSelectionHandles() {
    if (!currentTrack) return;

    // Вспомогательная лямбда — рисует ручки для набора точек
    auto drawHandles = [&](const std::vector<glm::vec2>& pts,
                           bool isActiveObj) {
        float s = 0.22f;
        for (int i = 0; i < (int)pts.size(); ++i) {
            bool isSel = isActiveObj && (i == selPtIdx);
            float hs = isSel ? s * 2.0f : s;
            const glm::vec2& p = pts[i];
            debugRenderer().addLine(b2Vec2(p.x-hs,p.y), b2Vec2(p.x+hs,p.y));
            debugRenderer().addLine(b2Vec2(p.x,p.y-hs), b2Vec2(p.x,p.y+hs));
            // Диагонали для выделенной точки
            if (isSel) {
                debugRenderer().addLine(b2Vec2(p.x-hs*.6f,p.y-hs*.6f), b2Vec2(p.x+hs*.6f,p.y+hs*.6f));
                debugRenderer().addLine(b2Vec2(p.x+hs*.6f,p.y-hs*.6f), b2Vec2(p.x-hs*.6f,p.y+hs*.6f));
            }
        }
        if (isActiveObj) debugRenderer().flush(projectionMatrix, 1.0f, 0.45f, 0.0f, 1.0f); // оранжевый
        else             debugRenderer().flush(projectionMatrix, 0.8f, 0.8f,  0.4f, 0.5f); // тускло-жёлтый
    };

    // Показываем ручки только для объекта, на котором есть выделение
    if (selObjType == SelType::RACING_PT || selObjType == SelType::RACING_HANDLE) {
        drawHandles(currentTrack->racingLine, true);

        // ручки (out-handle) + линия до них
        if (currentTrack->racingLineOutHandle.size() == currentTrack->racingLine.size()) {
            float s = 0.30f;
            for (int i = 0; i < (int)currentTrack->racingLine.size(); ++i) {
                const glm::vec2 p  = currentTrack->racingLine[i];
                const glm::vec2 hp = p + currentTrack->racingLineOutHandle[i];
                debugRenderer().addLine(b2Vec2(p.x,p.y), b2Vec2(hp.x,hp.y));
                bool isSel = (selObjType == SelType::RACING_HANDLE) && (i == selPtIdx);
                float hs = isSel ? s*2.0f : s;
                debugRenderer().addLine(b2Vec2(hp.x-hs,hp.y), b2Vec2(hp.x+hs,hp.y));
                debugRenderer().addLine(b2Vec2(hp.x,hp.y-hs), b2Vec2(hp.x,hp.y+hs));
            }
            debugRenderer().flush(projectionMatrix, 0.2f, 1.0f, 0.3f, 0.9f);
        }
    } else if (selObjType == SelType::WALL_PT && selObjIdx < (int)currentTrack->walls.size()) {
        drawHandles(currentTrack->walls[selObjIdx].vertices, true);
    } else if (selObjType == SelType::SURFACE_PT && selObjIdx < (int)currentTrack->surfaces.size()) {
        drawHandles(currentTrack->surfaces[selObjIdx].polygon, true);
    } else if (selObjType == SelType::CHECKPOINT_PT && selObjIdx < (int)currentTrack->checkpoints.size()) {
        auto& cp = currentTrack->checkpoints[selObjIdx];
        std::vector<glm::vec2> tmp = {cp.start, cp.end};
        drawHandles(tmp, true);
    } else if (selObjType == SelType::OUTER_BOUNDARY_PT) {
        drawHandles(currentTrack->outerBoundary, true);
    } else if (selObjType == SelType::INNER_BOUNDARY_PT) {
        drawHandles(currentTrack->innerBoundary, true);
    } else if (selObjType == SelType::START_FINISH_A || selObjType == SelType::START_FINISH_B) {
        std::vector<glm::vec2> tmp = { currentTrack->startFinish.start, currentTrack->startFinish.end };
        drawHandles(tmp, true);
    }

    // Если ничего не выделено — показываем все точки всех объектов (тусклые)
    if (selObjType == SelType::NONE) {
        for (auto& w : currentTrack->walls)    drawHandles(w.vertices, false);
        for (auto& s : currentTrack->surfaces) drawHandles(s.polygon,  false);
        if (!currentTrack->racingLine.empty()) drawHandles(currentTrack->racingLine, false);
        if (!currentTrack->outerBoundary.empty()) drawHandles(currentTrack->outerBoundary, false);
        if (!currentTrack->innerBoundary.empty()) drawHandles(currentTrack->innerBoundary, false);
    }
}

glm::vec2 TrackEditor::snapToGridIfNeeded(const glm::vec2& pos) const {
    if (!snapToGrid) return pos;

    float step = gridStep(gridBase, gridPow2);
    return glm::vec2(
        std::round(pos.x / step) * step,
        std::round(pos.y / step) * step
    );
}


glm::vec2 TrackEditor::screenToWorld(double screenX, double screenY) const {
    if (viewportWidth <= 0 || viewportHeight <= 0) return glm::vec2(0, 0);

    float ndcX = (2.0f * static_cast<float>(screenX) / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(screenY) / viewportHeight);

    glm::mat4 invProj = glm::inverse(projectionMatrix);
    glm::vec4 worldPos = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    return glm::vec2(worldPos.x, worldPos.y);
}

void TrackEditor::addHistoryState(const std::string& description) {
    if (!currentTrack) return;

    if (historyIndex < static_cast<int>(history.size()) - 1) {
        history.erase(history.begin() + historyIndex + 1, history.end());
    }

    HistoryState state;
    state.trackState = *currentTrack;
    state.description = description;
    history.push_back(state);

    if (history.size() > MAX_HISTORY) {
        history.erase(history.begin());
    } else {
        historyIndex++;
    }
}

void TrackEditor::undo() {
    if (historyIndex > 0 && !history.empty() && currentTrack) {
        historyIndex--;
        *currentTrack = history[historyIndex].trackState;
        isDirty = true;
        arenaBufInit = false;
    }
}

void TrackEditor::redo() {
    if (historyIndex < static_cast<int>(history.size()) - 1 && currentTrack) {
        historyIndex++;
        *currentTrack = history[historyIndex].trackState;
        isDirty = true;
        arenaBufInit = false;
        std::cout << "Redo: " << history[historyIndex].description << std::endl;
    }
}

void TrackEditor::clearAll() {
    if (currentTrack) {
        currentTrack->walls.clear();
        currentTrack->checkpoints.clear();
        currentTrack->surfaces.clear();
        addHistoryState("Clear all");
        isDirty = true;
    }
}

void TrackEditor::render() {
    if (!currentTrack) return;
    if (!isEditing()) return;

    if (showGrid) {
        renderGrid();
    }

    renderWalls();
    renderCheckpoints();
    // Start/finish line
    if (currentTrack && (currentTrack->startFinish.start != currentTrack->startFinish.end)) {
        auto a = currentTrack->startFinish.start;
        auto b = currentTrack->startFinish.end;
        debugRenderer().addLine(b2Vec2(a.x,a.y), b2Vec2(b.x,b.y));
        debugRenderer().flush(projectionMatrix, 1.0f, 0.2f, 0.2f, 0.95f);
    }
    renderSpawnPoint();
    renderSurfaces();
    renderBoundaries();
    renderRacingLine();
    renderSelectionHandles();

    if (mode == EditorMode::EDIT_WALLS) {
        renderCurrentWall();
    }
    else if (mode == EditorMode::EDIT_SURFACES) {
        renderCurrentSurface();
    }
    else if (mode == EditorMode::EDIT_RACING_LINE) {
        // Рисуем строящуюся линию
        for (size_t i = 1; i < currentRacingLinePoints.size(); ++i) {
            const auto& a = currentRacingLinePoints[i-1];
            const auto& b = currentRacingLinePoints[i];
            debugRenderer().addLine(b2Vec2(a.x,a.y), b2Vec2(b.x,b.y));
        }
        if (!currentRacingLinePoints.empty()) {
            const auto& last = currentRacingLinePoints.back();
            debugRenderer().addLine(b2Vec2(last.x, last.y), b2Vec2(mouseWorldPos.x, mouseWorldPos.y));
        }
        debugRenderer().flush(projectionMatrix, 1.0f, 0.9f, 0.0f, 1.0f);
    }

    if (currentTrack) {
        glm::vec2 h = currentTrack->arenaHalfExtents;
        glm::vec2 a(-h.x, -h.y), b(-h.x, h.y), c(h.x, h.y), d(h.x, -h.y);

        debugRenderer().addLine(b2Vec2(a.x,a.y), b2Vec2(b.x,b.y));
        debugRenderer().addLine(b2Vec2(b.x,b.y), b2Vec2(c.x,c.y));
        debugRenderer().addLine(b2Vec2(c.x,c.y), b2Vec2(d.x,d.y));
        debugRenderer().addLine(b2Vec2(d.x,d.y), b2Vec2(a.x,a.y));
        debugRenderer().flush(projectionMatrix, 0.7f, 0.3f, 1.0f, 1.0f);
    }


    renderMouseCursor();
}

void TrackEditor::renderGrid() {
    float step = gridStep(gridBase, gridPow2);
    if (step <= 0.0f) return;

    const float majorStep = step * 5.0f;

    // Получаем границы видимой области из inverse(projection)
    glm::mat4 inv = glm::inverse(projectionMatrix);
    auto toWorld = [&](float ndcX, float ndcY) {
        glm::vec4 w = inv * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        return glm::vec2(w.x, w.y);
    };

    glm::vec2 bl = toWorld(-1.f, -1.f);
    glm::vec2 tr = toWorld( 1.f,  1.f);

    float minX = std::min(bl.x, tr.x);
    float maxX = std::max(bl.x, tr.x);
    float minY = std::min(bl.y, tr.y);
    float maxY = std::max(bl.y, tr.y);

    // запас
    float pad = step * 2.0f;
    minX -= pad; maxX += pad;
    minY -= pad; maxY += pad;

    // стартуем по ближайшей линии сетки
    float startX = std::floor(minX / step) * step;
    float endX   = std::ceil (maxX / step) * step;
    float startY = std::floor(minY / step) * step;
    float endY   = std::ceil (maxY / step) * step;

    if (gridStyle == GridStyle::LINES) {
        // minor
        for (float x = startX; x <= endX; x += step) {
            bool major = (std::fmod(std::abs(x), majorStep) < (step * 0.5f));
            if (!major) debugRenderer().addLine(b2Vec2(x, startY), b2Vec2(x, endY));
        }
        for (float y = startY; y <= endY; y += step) {
            bool major = (std::fmod(std::abs(y), majorStep) < (step * 0.5f));
            if (!major) debugRenderer().addLine(b2Vec2(startX, y), b2Vec2(endX, y));
        }
        debugRenderer().flush(projectionMatrix, 0.15f, 0.15f, 0.15f, 0.22f);

        // major
        for (float x = startX; x <= endX; x += step) {
            bool major = (std::fmod(std::abs(x), majorStep) < (step * 0.5f));
            if (major) debugRenderer().addLine(b2Vec2(x, startY), b2Vec2(x, endY));
        }
        for (float y = startY; y <= endY; y += step) {
            bool major = (std::fmod(std::abs(y), majorStep) < (step * 0.5f));
            if (major) debugRenderer().addLine(b2Vec2(startX, y), b2Vec2(endX, y));
        }
        debugRenderer().flush(projectionMatrix, 0.22f, 0.22f, 0.22f, 0.35f);
    } else {
        // dots (маленькие крестики на пересечениях)
        float s = step * 0.10f;
        for (float x = startX; x <= endX; x += step) {
            for (float y = startY; y <= endY; y += step) {
                bool major = (std::fmod(std::abs(x), majorStep) < (step * 0.5f)) &&
                             (std::fmod(std::abs(y), majorStep) < (step * 0.5f));
                float hs = major ? (s * 1.8f) : s;
                debugRenderer().addLine(b2Vec2(x - hs, y), b2Vec2(x + hs, y));
                debugRenderer().addLine(b2Vec2(x, y - hs), b2Vec2(x, y + hs));
            }
        }
        debugRenderer().flush(projectionMatrix, 0.2f, 0.2f, 0.2f, 0.30f);
    }
}

void TrackEditor::renderCurrentWall() {
    if (currentWallPoints.empty()) return;

    for (size_t i = 0; i + 1 < currentWallPoints.size(); i++) {
        debugRenderer().addLine(
            b2Vec2(currentWallPoints[i].x, currentWallPoints[i].y),
            b2Vec2(currentWallPoints[i + 1].x, currentWallPoints[i + 1].y)
        );
    }

    debugRenderer().addLine(
        b2Vec2(currentWallPoints.back().x, currentWallPoints.back().y),
        b2Vec2(mouseWorldPos.x, mouseWorldPos.y)
    );

    debugRenderer().flush(projectionMatrix, 0.0f, 1.0f, 0.0f, 1.0f);
}

void TrackEditor::renderCurrentSurface() {
    if (currentSurfacePoints.empty()) return;

    // контур того, что сейчас рисуешь
    for (size_t i = 0; i + 1 < currentSurfacePoints.size(); ++i) {
        debugRenderer().addLine(
            b2Vec2(currentSurfacePoints[i].x, currentSurfacePoints[i].y),
            b2Vec2(currentSurfacePoints[i+1].x, currentSurfacePoints[i+1].y)
        );
    }

    // линия до курсора (чтобы понимать куда добавится следующая точка)
    debugRenderer().addLine(
        b2Vec2(currentSurfacePoints.back().x, currentSurfacePoints.back().y),
        b2Vec2(mouseWorldPos.x, mouseWorldPos.y)
    );

    // лучи от центра
    if (currentSurfacePoints.size() >= 3) {
        glm::vec2 cen = polygonCentroid(currentSurfacePoints);
        for (auto& v : currentSurfacePoints) {
            debugRenderer().addLine(b2Vec2(cen.x, cen.y), b2Vec2(v.x, v.y));
        }
    }

    debugRenderer().flush(projectionMatrix, 0.8f, 0.4f, 1.0f, 0.9f);
}


void TrackEditor::renderWalls() {
    if (!currentTrack) return;

    debugRenderer().beginFrame();

    for (const auto& wall : currentTrack->walls) {
        for (size_t i = 0; i + 1 < wall.vertices.size(); i++) {
            if (currentRenderMode == RenderMode::SIMPLE_BOXES) {
                debugRenderer().addRect(wall.vertices[i], wall.vertices[i + 1], wall.thickness);
            } else {
                debugRenderer().addLine(
                    b2Vec2(wall.vertices[i].x, wall.vertices[i].y),
                    b2Vec2(wall.vertices[i + 1].x, wall.vertices[i + 1].y)
                );
            }
        }
    }

    debugRenderer().flush(projectionMatrix, 1.0f, 1.0f, 1.0f, 1.0f);
}

void TrackEditor::renderCheckpoints() {
    if (!currentTrack) return;

    for (const auto& cp : currentTrack->checkpoints) {
        debugRenderer().addLine(b2Vec2(cp.start.x, cp.start.y), b2Vec2(cp.end.x, cp.end.y));
        debugRenderer().flush(projectionMatrix, 1.0f, 1.0f, 0.0f, 1.0f);
    }
}

void TrackEditor::renderSpawnPoint() {
    if (!currentTrack) return;

    glm::vec2 pos = currentTrack->spawnPos;
    float size = 1.0f;

    debugRenderer().addLine(b2Vec2(pos.x - size, pos.y), b2Vec2(pos.x + size, pos.y));
    debugRenderer().addLine(b2Vec2(pos.x, pos.y - size), b2Vec2(pos.x, pos.y + size));

    float angle = currentTrack->spawnYawRad;
    glm::vec2 dir(cos(angle), sin(angle));
    debugRenderer().addLine(b2Vec2(pos.x, pos.y), b2Vec2(pos.x + dir.x * 2.0f, pos.y + dir.y * 2.0f));

    debugRenderer().flush(projectionMatrix, 0.0f, 1.0f, 1.0f, 1.0f);
}

void TrackEditor::renderSurfaces() {
    if (!currentTrack) return;

    for (const auto& s : currentTrack->surfaces) {
        if (s.polygon.size() < 3) continue;

        // цвет по mu: <0.8 красноватый, >1.2 зеленоватый
        float t = std::clamp((s.mu - 0.6f) / 1.0f, 0.0f, 1.0f);
        float r = (1.0f - t) * 0.9f + t * 0.2f;
        float g = (1.0f - t) * 0.2f + t * 0.9f;
        float b = 0.8f;

        // 1) контур
        for (size_t i = 0; i < s.polygon.size(); ++i) {
            const auto& a = s.polygon[i];
            const auto& c = s.polygon[(i + 1) % s.polygon.size()];
            debugRenderer().addLine(b2Vec2(a.x, a.y), b2Vec2(c.x, c.y));
        }
        debugRenderer().flush(projectionMatrix, r, g, b, 1.0f);

        // 2) псевдо-заливка лучами от центра
        glm::vec2 cen = polygonCentroid(s.polygon);
        for (size_t i = 0; i < s.polygon.size(); ++i) {
            const auto& v = s.polygon[i];
            debugRenderer().addLine(b2Vec2(cen.x, cen.y), b2Vec2(v.x, v.y));
        }
        debugRenderer().flush(projectionMatrix, r, g, b, 0.20f);
    }
}



void TrackEditor::finishCurrentBoundary() {
    if (currentBoundaryPoints.size() < 3 || !currentTrack) return;
    if (mode == EditorMode::EDIT_OUTER_BOUNDARY) {
        currentTrack->outerBoundary = currentBoundaryPoints;
        addHistoryState("Set outer boundary");
    } else {
        currentTrack->innerBoundary = currentBoundaryPoints;
        addHistoryState("Set inner boundary");
    }
    currentBoundaryPoints.clear();
    isDirty = true;
}

void TrackEditor::renderBoundaries() {
    if (!currentTrack) return;

    // Внешняя граница — зелёная
    if (currentTrack->outerBoundary.size() >= 2) {
        const auto& ob = currentTrack->outerBoundary;
        for (size_t i = 1; i < ob.size(); ++i)
            debugRenderer().addLine(b2Vec2(ob[i-1].x,ob[i-1].y), b2Vec2(ob[i].x,ob[i].y));
        debugRenderer().addLine(b2Vec2(ob.back().x,ob.back().y), b2Vec2(ob[0].x,ob[0].y));
        debugRenderer().flush(projectionMatrix, 0.2f, 1.0f, 0.3f, 0.80f);
    }
    // Внутренняя граница — оранжевая
    if (currentTrack->innerBoundary.size() >= 2) {
        const auto& ib = currentTrack->innerBoundary;
        for (size_t i = 1; i < ib.size(); ++i)
            debugRenderer().addLine(b2Vec2(ib[i-1].x,ib[i-1].y), b2Vec2(ib[i].x,ib[i].y));
        debugRenderer().addLine(b2Vec2(ib.back().x,ib.back().y), b2Vec2(ib[0].x,ib[0].y));
        debugRenderer().flush(projectionMatrix, 1.0f, 0.5f, 0.1f, 0.80f);
    }
    // Строящаяся граница
    if (!currentBoundaryPoints.empty() &&
        (mode == EditorMode::EDIT_OUTER_BOUNDARY || mode == EditorMode::EDIT_INNER_BOUNDARY)) {
        for (size_t i = 1; i < currentBoundaryPoints.size(); ++i) {
            const auto& a = currentBoundaryPoints[i-1];
            const auto& b = currentBoundaryPoints[i];
            debugRenderer().addLine(b2Vec2(a.x,a.y), b2Vec2(b.x,b.y));
        }
        if (!currentBoundaryPoints.empty()) {
            const auto& last = currentBoundaryPoints.back();
            debugRenderer().addLine(b2Vec2(last.x,last.y),
                                    b2Vec2(mouseWorldPos.x, mouseWorldPos.y));
        }
        float r = (mode==EditorMode::EDIT_OUTER_BOUNDARY) ? 0.2f : 1.0f;
        float g = (mode==EditorMode::EDIT_OUTER_BOUNDARY) ? 1.0f : 0.5f;
        debugRenderer().flush(projectionMatrix, r, g, 0.1f, 1.0f);
    }
}

void TrackEditor::renderRacingLine() {
    if (!currentTrack || currentTrack->racingLine.size() < 2) return;

    const auto& ctrl = currentTrack->racingLine;

    if (useSplines) {
        // Bezier: используем ручки если есть, иначе авто-касательные (Catmull-Rom)
        const int N = (int)ctrl.size();
        bool hasH = ((int)currentTrack->racingLineOutHandle.size() == N);

        std::vector<glm::vec2> autoH;
        if (!hasH) {
            autoH.resize(N);
            const float tension = 0.33f;
            for (int i = 0; i < N; ++i)
                autoH[i] = (ctrl[(i+1)%N] - ctrl[(i-1+N)%N]) * tension;
        }

        auto bez = [](const glm::vec2& p0, const glm::vec2& p1,
                      const glm::vec2& p2, const glm::vec2& p3, float t) {
            float u = 1.0f - t;
            return p0*(u*u*u) + p1*(3*u*u*t) + p2*(3*u*t*t) + p3*(t*t*t);
        };

        for (int i = 0; i < N; ++i) {
            int j = (i + 1) % N;
            const glm::vec2& hi = hasH ? currentTrack->racingLineOutHandle[i] : autoH[i];
            const glm::vec2& hj = hasH ? currentTrack->racingLineOutHandle[j] : autoH[j];
            glm::vec2 p0 = ctrl[i], p3 = ctrl[j];
            glm::vec2 p1 = p0 + hi,  p2 = p3 - hj;

            glm::vec2 prev = p0;
            int samples = std::max(3, splineSamples);
            for (int s = 1; s <= samples; ++s) {
                float t = (float)s / (float)samples;
                glm::vec2 cur = bez(p0,p1,p2,p3,t);
                debugRenderer().addLine(b2Vec2(prev.x,prev.y), b2Vec2(cur.x,cur.y));
                prev = cur;
            }
        }
        debugRenderer().flush(projectionMatrix, 1.0f, 0.75f, 0.0f, 0.92f);

        // Якорные крестики
        float hs = 0.14f;
        for (const auto& p : ctrl) {
            debugRenderer().addLine(b2Vec2(p.x-hs,p.y),b2Vec2(p.x+hs,p.y));
            debugRenderer().addLine(b2Vec2(p.x,p.y-hs),b2Vec2(p.x,p.y+hs));
        }
        debugRenderer().flush(projectionMatrix, 1.0f, 0.85f, 0.0f, 0.38f);
    } else {
        for (size_t i = 1; i < ctrl.size(); ++i)
            debugRenderer().addLine(b2Vec2(ctrl[i-1].x, ctrl[i-1].y),
                                    b2Vec2(ctrl[i].x,   ctrl[i].y));
        debugRenderer().addLine(b2Vec2(ctrl.back().x, ctrl.back().y),
                                b2Vec2(ctrl[0].x,     ctrl[0].y));
        debugRenderer().flush(projectionMatrix, 1.0f, 0.85f, 0.0f, 0.85f);
    }
}

void TrackEditor::renderMouseCursor() {
    glm::vec2 pos = snapToGridIfNeeded(mouseWorldPos);
    float size = 0.2f;

    debugRenderer().addLine(b2Vec2(pos.x - size, pos.y), b2Vec2(pos.x + size, pos.y));
    debugRenderer().addLine(b2Vec2(pos.x, pos.y - size), b2Vec2(pos.x, pos.y + size));
    debugRenderer().flush(projectionMatrix, 1.0f, 0.0f, 0.0f, 1.0f);
}

void TrackEditor::renderUI() {
    if (!isEditing()) return;

    ImGui::Separator();
    ImGui::Text("Visual Style:");
    if (ImGui::RadioButton("Boxes (Dev)", currentRenderMode == RenderMode::SIMPLE_BOXES)) {
        currentRenderMode = RenderMode::SIMPLE_BOXES;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Textured", currentRenderMode == RenderMode::TEXTURED)) {
        currentRenderMode = RenderMode::TEXTURED;
    }

    renderMainToolbar();
    renderPropertiesWindow();
    renderObjectsList();
    renderFileMenu();
}

void TrackEditor::renderMainToolbar() {
    ImGui::Begin("Track Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Mode:");

    if (ImGui::RadioButton("Play", mode == EditorMode::PLAY)) setMode(EditorMode::PLAY);
    ImGui::SameLine();
    if (ImGui::RadioButton("Walls", mode == EditorMode::EDIT_WALLS)) setMode(EditorMode::EDIT_WALLS);
    ImGui::SameLine();
    if (ImGui::RadioButton("Checkpoints", mode == EditorMode::EDIT_CHECKPOINTS)) setMode(EditorMode::EDIT_CHECKPOINTS);
    ImGui::SameLine();
    if (ImGui::RadioButton("Spawn", mode == EditorMode::EDIT_SPAWN)) setMode(EditorMode::EDIT_SPAWN);
    ImGui::SameLine();
    if (ImGui::RadioButton("Surfaces", mode == EditorMode::EDIT_SURFACES)) setMode(EditorMode::EDIT_SURFACES);
    ImGui::SameLine();
    if (ImGui::RadioButton("Racing Line", mode == EditorMode::EDIT_RACING_LINE)) setMode(EditorMode::EDIT_RACING_LINE);
    if (ImGui::RadioButton("Outer Boundary", mode == EditorMode::EDIT_OUTER_BOUNDARY)) setMode(EditorMode::EDIT_OUTER_BOUNDARY);
    ImGui::SetItemTooltip("Outer track boundary (penalty on exit)");
    if (ImGui::RadioButton("Inner Boundary", mode == EditorMode::EDIT_INNER_BOUNDARY)) setMode(EditorMode::EDIT_INNER_BOUNDARY);
    ImGui::SetItemTooltip("Inner boundary (grass / island)");
    if (ImGui::RadioButton("Start/Finish", mode == EditorMode::EDIT_START_FINISH)) setMode(EditorMode::EDIT_START_FINISH);
    ImGui::SetItemTooltip("Start/Finish line for lap & sector timing");

    ImGui::Separator();

    ImGui::Text("Tool:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Select/Move", tool == EditorTool::SELECT_MOVE)) tool = EditorTool::SELECT_MOVE;
    ImGui::SameLine();
    if (ImGui::RadioButton("Draw", tool == EditorTool::DRAW)) tool = EditorTool::DRAW;

    ImGui::Separator();

    if (ImGui::Button("New Track")) newTrack();
    ImGui::SameLine();
    if (ImGui::Button("Tracks...")) {
        showTracksWindow = true;
        refreshTrackList();
    }

    ImGui::Separator();

    if (ImGui::Button("Undo (Ctrl+Z)")) undo();
    ImGui::SameLine();
    if (ImGui::Button("Redo (Ctrl+Y)")) redo();
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) clearAll();

    ImGui::Separator();

    ImGui::Checkbox("Show Grid (G)", &showGrid);
    ImGui::Checkbox("Snap to Grid (H)", &snapToGrid);
    ImGui::SameLine();
    ImGui::Text("Grid style:");
    ImGui::SameLine();
    int gs = (gridStyle == GridStyle::LINES) ? 0 : 1;
    if (ImGui::RadioButton("Lines", gs == 0)) gridStyle = GridStyle::LINES;
    ImGui::SameLine();
    if (ImGui::RadioButton("Dots", gs == 1)) gridStyle = GridStyle::DOTS;
    ImGui::Text("Grid step = %.6g", gridStep(gridBase, gridPow2));
    ImGui::SliderInt("Grid / 2^N", &gridPow2, 0, 8);
    ImGui::SliderFloat("Grid base", &gridBase, 0.25f, 10.0f);

    ImGui::Separator();

    if (currentTrack) {
        ImGui::Text("Track: %s", currentTrack->name.c_str());
        ImGui::Text("File: %s", currentTrackPath.empty() ? "(unsaved)" : currentTrackPath.c_str());
        ImGui::Text("Walls: %zu", currentTrack->walls.size());
        ImGui::Text("Checkpoints: %zu", currentTrack->checkpoints.size());
        ImGui::Text("Surfaces: %zu", currentTrack->surfaces.size());

        if (isDirty) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "* Unsaved changes");
        }
    }

    ImGui::Separator();

    ImGui::Text("Controls:");
    ImGui::BulletText("LMB: Select/drag point (Select tool)");
    ImGui::BulletText("LMB: Add point (Draw tool)");
    ImGui::BulletText("RMB: Finish current object");
    ImGui::BulletText("Backspace: Delete last point");
    ImGui::BulletText("Escape: Cancel current operation");
    ImGui::BulletText("Mouse Wheel: Adjust thickness");
    ImGui::BulletText("Ctrl + Wheel: Zoom camera");
    ImGui::BulletText("MMB drag: Pan camera");
    ImGui::BulletText("F: Follow car toggle");

    ImGui::End();
}

void TrackEditor::renderPropertiesWindow() {
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (currentTrack) {
        if (!arenaBufInit) syncArenaBufFromTrack();

        ImGui::Separator();
        ImGui::Text("Track Size (Arena Bounds)");

        ImGui::InputFloat2("Size (W,H)", arenaSizeBuf);
        ImGui::SameLine();
        if (ImGui::Button("Apply")) {
            float w = std::max(2.0f, arenaSizeBuf[0]);
            float h = std::max(2.0f, arenaSizeBuf[1]);
            currentTrack->arenaHalfExtents = glm::vec2(w * 0.5f, h * 0.5f);
            addHistoryState("Resize arena");
            isDirty = true;
        }
    }

    if (mode == EditorMode::EDIT_WALLS) {
        ImGui::Text("Wall Properties:");
        ImGui::SliderFloat("Thickness", &currentWallThickness, 0.1f, 3.0f);
        ImGui::SliderFloat("Friction", &currentWallFriction, 0.0f, 1.0f);
        ImGui::SliderFloat("Restitution", &currentWallRestitution, 0.0f, 1.0f);

        if (!currentWallPoints.empty()) {
            ImGui::Separator();
            ImGui::Text("Current wall: %zu points", currentWallPoints.size());
            if (ImGui::Button("Finish Wall (RMB)")) finishCurrentWall();
        }

        // ── Сглаживание стен ────────────────────────────────────────────────
        if (currentTrack && !currentTrack->walls.empty()) {
            ImGui::Separator();
            ImGui::TextColored({0.7f,0.9f,1.0f,1.0f}, "Wall smoothing:");
            auto smoothWall = [](std::vector<glm::vec2>& v, int iters) {
                int N = (int)v.size();
                if (N < 3) return;
                for (int it = 0; it < iters; ++it) {
                    std::vector<glm::vec2> sm(N);
                    sm[0] = v[0]; sm[N-1] = v[N-1]; // концы не трогаем
                    for (int i = 1; i < N-1; ++i)
                        sm[i] = (v[i-1] + v[i]*2.0f + v[i+1]) * 0.25f;
                    v = sm;
                }
            };
            if (ImGui::Button("Smooth selected")) {
                if (selObjType == SelType::WALL_PT && selObjIdx < (int)currentTrack->walls.size()) {
                    smoothWall(currentTrack->walls[selObjIdx].vertices, 1);
                    addHistoryState("Smooth wall"); isDirty = true;
                }
            }
            ImGui::SetItemTooltip("Laplacian smooth (1 iter) on selected wall");
            ImGui::SameLine();
            if (ImGui::Button("Smooth all")) {
                for (auto& w : currentTrack->walls) smoothWall(w.vertices, 1);
                addHistoryState("Smooth all walls"); isDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Smooth all x5")) {
                for (auto& w : currentTrack->walls) smoothWall(w.vertices, 5);
                addHistoryState("Smooth all walls x5"); isDirty = true;
            }
            if (selObjType == SelType::WALL_PT && selObjIdx < (int)currentTrack->walls.size()) {
                if (ImGui::Button("Subdivide wall")) {
                    auto& v = currentTrack->walls[selObjIdx].vertices;
                    std::vector<glm::vec2> sub; sub.reserve(v.size()*2-1);
                    for (size_t i = 0; i+1 < v.size(); ++i) {
                        sub.push_back(v[i]);
                        sub.push_back((v[i]+v[i+1])*0.5f);
                    }
                    sub.push_back(v.back());
                    v = sub;
                    addHistoryState("Subdivide wall"); isDirty = true;
                }
                ImGui::SetItemTooltip("Insert midpoints between vertices");
            }
        }
    }
    else if (mode == EditorMode::EDIT_SURFACES) {
        ImGui::Text("Surface Properties:");
        ImGui::SliderFloat("Grip (mu)", &currentSurfaceMu, 0.1f, 2.0f);

        if (!currentSurfacePoints.empty()) {
            ImGui::Separator();
            ImGui::Text("Current surface: %zu points", currentSurfacePoints.size());
            if (ImGui::Button("Finish Surface (RMB)")) finishCurrentSurface();
        }
    }
    else if (mode == EditorMode::EDIT_SPAWN && currentTrack) {
        ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1.0f), "LMB click = move grid origin");

        ImGui::Separator();
        ImGui::Text("Legacy spawn:");
        ImGui::InputFloat2("Pos##sp", &currentTrack->spawnPos.x);
        ImGui::SliderAngle("Angle##sp", &currentTrack->spawnYawRad);

        ImGui::Separator();
        ImGui::Text("Start Grid:");
        StartGrid& sg = currentTrack->startGrid;

        float originArr[2] = {sg.origin.x, sg.origin.y};
        if (ImGui::InputFloat2("Origin##sg", originArr)) {
            sg.origin = glm::vec2(originArr[0], originArr[1]);
            isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Here##sg")) {
            sg.origin = mouseWorldPos;
            currentTrack->spawnPos = mouseWorldPos;
            isDirty = true;
        }

        if (ImGui::SliderAngle("Yaw##sg", &sg.yawRad, -180.0f, 180.0f)) isDirty = true;

        int rows = sg.rows, cols = sg.cols;
        if (ImGui::InputInt("Rows##sg", &rows)) { sg.rows = std::max(1,rows); isDirty=true; }
        ImGui::SameLine();
        if (ImGui::InputInt("Cols##sg", &cols)) { sg.cols = std::max(1,cols); isDirty=true; }

        float rs = sg.rowSpacing, cs = sg.colSpacing;
        if (ImGui::InputFloat("Row spacing##sg", &rs)) { sg.rowSpacing = std::max(0.5f,rs); isDirty=true; }
        ImGui::SameLine();
        if (ImGui::InputFloat("Col spacing##sg", &cs)) { sg.colSpacing = std::max(0.5f,cs); isDirty=true; }

        if (ImGui::Checkbox("Serpentine##sg", &sg.serpentine)) isDirty = true;

        if (ImGui::Button("Sync spawnPos from grid")) {
            currentTrack->spawnPos    = sg.origin;
            currentTrack->spawnYawRad = sg.yawRad;
            isDirty = true;
        }

        ImGui::Separator();
        ImGui::Text("Pit Grid:");
        StartGrid& pg = currentTrack->pitGrid;

        float pOrigin[2] = {pg.origin.x, pg.origin.y};
        if (ImGui::InputFloat2("Origin##pg", pOrigin)) {
            pg.origin = glm::vec2(pOrigin[0], pOrigin[1]);
            isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Here##pg")) { pg.origin = mouseWorldPos; isDirty=true; }

        if (ImGui::SliderAngle("Yaw##pg", &pg.yawRad, -180.0f, 180.0f)) isDirty=true;
        int pr=pg.rows, pc=pg.cols;
        if (ImGui::InputInt("Rows##pg", &pr)) { pg.rows=std::max(1,pr); isDirty=true; }
        ImGui::SameLine();
        if (ImGui::InputInt("Cols##pg", &pc)) { pg.cols=std::max(1,pc); isDirty=true; }
        float prs=pg.rowSpacing, pcs=pg.colSpacing;
        if (ImGui::InputFloat("Row spacing##pg", &prs)) { pg.rowSpacing=std::max(0.5f,prs); isDirty=true; }
        ImGui::SameLine();
        if (ImGui::InputFloat("Col spacing##pg", &pcs)) { pg.colSpacing=std::max(0.5f,pcs); isDirty=true; }
    }
    else if (mode == EditorMode::EDIT_RACING_LINE && currentTrack) {
        ImGui::TextColored(ImVec4(1.0f,0.9f,0.0f,1.0f), "Racing Line Editor");
        ImGui::Separator();
        ImGui::BulletText("LMB empty space = add point");
        ImGui::BulletText("LMB on point = drag it");
        ImGui::BulletText("Backspace = delete selected point");
        ImGui::BulletText("RMB = finish current segment");

        int pts = (int)currentTrack->racingLine.size();
        ImGui::Text("Control points: %d", pts);
        if (!currentRacingLinePoints.empty())
            ImGui::TextColored({1,1,0,1}, "Drawing: %d pts (RMB to finish)",
                               (int)currentRacingLinePoints.size());

        ImGui::Separator();

        // Сплайн
        ImGui::Checkbox("Bezier (handles)", &useSplines);
        if (useSplines) {
            ImGui::SliderInt("Samples/seg", &splineSamples, 3, 24);
            ImGui::TextDisabled("Drag green handles to shape the curve");
        }

        ImGui::Separator();

        // Инструменты
        if (ImGui::Button("Subdivide")) {
            auto& rl = currentTrack->racingLine;
            if (rl.size() >= 2) {
                std::vector<glm::vec2> sub;
                sub.reserve(rl.size() * 2);
                for (size_t i = 0; i < rl.size(); ++i) {
                    sub.push_back(rl[i]);
                    sub.push_back((rl[i] + rl[(i+1)%rl.size()]) * 0.5f);
                }
                rl = sub;
                int NS = (int)rl.size();
                currentTrack->racingLineOutHandle.resize(NS);
                for (int i = 0; i < NS; ++i)
                    currentTrack->racingLineOutHandle[i] = (rl[(i+1)%NS]-rl[(i-1+NS)%NS])*splineTension;
                addHistoryState("Subdivide racing line");
                isDirty = true;
            }
        }
        ImGui::SetItemTooltip("Insert point between each pair");

        ImGui::SameLine();
        if (ImGui::Button("Insert near cursor")) {
            auto& rl = currentTrack->racingLine;
            if (rl.size() >= 2) {
                int bestSeg = 0;
                float bestD2 = 1e30f;
                glm::vec2 bestMid{0,0};
                for (int i = 0; i < (int)rl.size(); ++i) {
                    int j = (i + 1) % (int)rl.size();
                    glm::vec2 mid = (rl[i] + rl[j]) * 0.5f;
                    glm::vec2 d = mid - mouseWorldPos;
                    float d2 = glm::dot(d, d);
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        bestSeg = i;
                        bestMid = mid;
                    }
                }
                rl.insert(rl.begin() + bestSeg + 1, bestMid);
                currentTrack->racingLineOutHandle.resize(rl.size());
                for (int i = 0; i < (int)rl.size(); ++i)
                    currentTrack->racingLineOutHandle[i] =
                        (rl[(i+1)%rl.size()] - rl[(i-1+(int)rl.size())%(int)rl.size()]) * splineTension;
                addHistoryState("Insert racing point");
                isDirty = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Smooth")) {
            auto& rl = currentTrack->racingLine;
            int N = (int)rl.size();
            if (N >= 3) {
                std::vector<glm::vec2> sm(N);
                for (int i = 0; i < N; ++i)
                    sm[i] = (rl[(i-1+N)%N] + rl[i]*2.0f + rl[(i+1)%N]) * 0.25f;
                rl = sm;
                currentTrack->racingLineOutHandle.resize(N);
                for (int i = 0; i < N; ++i)
                    currentTrack->racingLineOutHandle[i] = (rl[(i+1)%N]-rl[(i-1+N)%N])*splineTension;
                addHistoryState("Smooth racing line");
                isDirty = true;
            }
        }
        ImGui::SetItemTooltip("Laplacian smoothing (1 iteration)");

        ImGui::SameLine();
        if (ImGui::Button("Smooth x5")) {
            auto& rl = currentTrack->racingLine;
            int N = (int)rl.size();
            if (N >= 3) {
                for (int iter = 0; iter < 5; ++iter) {
                    std::vector<glm::vec2> sm(N);
                    for (int i = 0; i < N; ++i)
                        sm[i] = (rl[(i-1+N)%N] + rl[i]*2.0f + rl[(i+1)%N]) * 0.25f;
                    rl = sm;
                }
                currentTrack->racingLineOutHandle.resize(N);
                for (int i = 0; i < N; ++i)
                    currentTrack->racingLineOutHandle[i] = (rl[(i+1)%N]-rl[(i-1+N)%N])*splineTension;
                addHistoryState("Smooth racing line x5");
                isDirty = true;
            }
        }
        ImGui::SetItemTooltip("5 smoothing iterations");

        ImGui::Separator();
        if (ImGui::Button("Finish Segment (RMB)") && currentRacingLinePoints.size() >= 2)
            finishCurrentRacingLine();
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
            currentTrack->racingLine.clear();
            selObjType = SelType::NONE;
            addHistoryState("Clear racing line");
            isDirty = true;
        }

        // Информация о выделенной точке
        if (selObjType == SelType::RACING_PT && selPtIdx >= 0
            && selPtIdx < (int)currentTrack->racingLine.size()) {
            ImGui::Separator();
            auto& p = currentTrack->racingLine[selPtIdx];
            ImGui::Text("Selected pt #%d:", selPtIdx);
            float xy[2] = {p.x, p.y};
            if (ImGui::InputFloat2("XY##selpt", xy)) {
                p = {xy[0], xy[1]};
                isDirty = true;
            }
        }
    }
    else if ((mode == EditorMode::EDIT_OUTER_BOUNDARY || mode == EditorMode::EDIT_INNER_BOUNDARY) && currentTrack) {
        ImGui::TextColored(ImVec4(0.6f,1.0f,0.6f,1.0f), "Track Boundaries");
        ImGui::Separator();
        ImGui::BulletText("Outer = allowed area polygon (car must stay inside)");
        ImGui::BulletText("Inner = forbidden island polygon (car must stay outside)");
        ImGui::BulletText("Penalties: slowdown + time penalty in gameplay");
        ImGui::Separator();
        ImGui::Text("Outer pts: %d", (int)currentTrack->outerBoundary.size());
        ImGui::Text("Inner pts: %d", (int)currentTrack->innerBoundary.size());
        if (!currentBoundaryPoints.empty())
            ImGui::TextColored(ImVec4(1,1,0,1), "Drawing: %d pts (RMB to finish)", (int)currentBoundaryPoints.size());

        if (ImGui::Button("Clear outer")) {
            currentTrack->outerBoundary.clear();
            addHistoryState("Clear outer boundary");
            isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear inner")) {
            currentTrack->innerBoundary.clear();
            addHistoryState("Clear inner boundary");
            isDirty = true;
        }
    }
    else if (mode == EditorMode::EDIT_START_FINISH && currentTrack) {
        ImGui::TextColored(ImVec4(1.0f,0.4f,0.4f,1.0f), "Start / Finish");
        ImGui::Separator();
        ImGui::BulletText("Draw tool: 2 clicks to set the line");
        ImGui::BulletText("Select tool: drag endpoints");
        ImGui::BulletText("Backspace on endpoint: clear line");
        if (hasStartFinishTemp)
            ImGui::TextColored(ImVec4(1,1,0,1), "Waiting for 2nd click...");

        bool hasSF = (currentTrack->startFinish.start != currentTrack->startFinish.end);
        ImGui::Text("Set: %s", hasSF ? "YES" : "NO");
        if (hasSF) {
            ImGui::Text("A: (%.2f, %.2f)", currentTrack->startFinish.start.x, currentTrack->startFinish.start.y);
            ImGui::Text("B: (%.2f, %.2f)", currentTrack->startFinish.end.x, currentTrack->startFinish.end.y);
            if (ImGui::Button("Clear start/finish")) {
                currentTrack->startFinish = Checkpoint{};
                addHistoryState("Clear start/finish");
                isDirty = true;
            }
        }
    }
    else if (mode == EditorMode::EDIT_CHECKPOINTS) {
        ImGui::Text("Checkpoint Mode:");
        ImGui::BulletText("Click start point");
        ImGui::BulletText("Click end point");
    }
    else if ((mode == EditorMode::EDIT_OUTER_BOUNDARY ||
               mode == EditorMode::EDIT_INNER_BOUNDARY) && currentTrack) {
        bool isOuter = (mode == EditorMode::EDIT_OUTER_BOUNDARY);
        ImGui::TextColored(isOuter ? ImVec4(0.2f,1.0f,0.3f,1.0f) : ImVec4(1.0f,0.5f,0.1f,1.0f),
                           isOuter ? "Outer Boundary" : "Inner Boundary");
        ImGui::BulletText("LMB: add point");
        ImGui::BulletText("RMB: finish polygon (min 3 pts)");
        ImGui::BulletText("Backspace: delete last point");

        auto& boundary = isOuter ? currentTrack->outerBoundary : currentTrack->innerBoundary;
        ImGui::Text("Points: %d", (int)boundary.size());
        if (!currentBoundaryPoints.empty())
            ImGui::TextColored({1,1,0,1}, "Drawing: %d pts", (int)currentBoundaryPoints.size());

        ImGui::Separator();
        if (ImGui::Button("Finish (RMB)") && currentBoundaryPoints.size() >= 3)
            finishCurrentBoundary();
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            boundary.clear();
            currentBoundaryPoints.clear();
            addHistoryState(isOuter ? "Clear outer boundary" : "Clear inner boundary");
            isDirty = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Cars outside outer or inside");
        ImGui::TextDisabled("inner boundary will slow down.");
    }

    ImGui::End();
}

void TrackEditor::renderObjectsList() {
    if (!currentTrack) return;

    ImGui::Begin("Objects", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (ImGui::CollapsingHeader("Walls", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < currentTrack->walls.size(); i++) {
            const auto& wall = currentTrack->walls[i];
            ImGui::PushID(static_cast<int>(i));

            if (ImGui::Selectable(
                (std::string("Wall ") + std::to_string(i) +
                 " (" + std::to_string(wall.vertices.size()) + " pts)").c_str(),
                selectedObjectIndex == static_cast<int>(i)
            )) {
                selectedObjectIndex = static_cast<int>(i);
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) {
                    currentTrack->walls.erase(currentTrack->walls.begin() + i);
                    addHistoryState("Delete wall");
                    isDirty = true;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Checkpoints")) {
        for (size_t i = 0; i < currentTrack->checkpoints.size(); i++) {
            ImGui::PushID(static_cast<int>(i) + 10000);

            if (ImGui::Selectable((std::string("Checkpoint ") + std::to_string(i)).c_str())) {
                // Select checkpoint
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) {
                    currentTrack->checkpoints.erase(currentTrack->checkpoints.begin() + i);
                    addHistoryState("Delete checkpoint");
                    isDirty = true;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Surfaces")) {
        for (size_t i = 0; i < currentTrack->surfaces.size(); i++) {
            ImGui::PushID(static_cast<int>(i) + 20000);

            if (ImGui::Selectable(
                (std::string("Surface ") + std::to_string(i) +
                 " (mu=" + std::to_string(currentTrack->surfaces[i].mu) + ")").c_str()
            )) {
                // Select surface
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) {
                    currentTrack->surfaces.erase(currentTrack->surfaces.begin() + i);
                    addHistoryState("Delete surface");
                    isDirty = true;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    ImGui::End();
}

void TrackEditor::renderFileMenu() {
    if (!showTracksWindow) return;

    ImGui::Begin("Tracks", &showTracksWindow, ImGuiWindowFlags_AlwaysAutoResize);

    if (ImGui::Button("Refresh")) refreshTrackList();
    ImGui::SameLine();
    if (ImGui::Button("New")) newTrack();

    ImGui::Separator();

    ImGui::Text("Current: %s", currentTrackPath.empty() ? "(unsaved)" : currentTrackPath.c_str());

    if (ImGui::BeginListBox("##track_list", ImVec2(460, 180))) {
        for (int i = 0; i < (int)trackFiles.size(); ++i) {
            const bool isSel = (i == selectedTrackFile);
            std::string label = baseNameNoExt(trackFiles[i]);
            if (ImGui::Selectable(label.c_str(), isSel)) {
                selectedTrackFile = i;
            }
        }
        ImGui::EndListBox();
    }

    bool hasSelection = (selectedTrackFile >= 0 && selectedTrackFile < (int)trackFiles.size());
    std::string selectedPath = hasSelection ? trackFiles[selectedTrackFile] : std::string();

    if (ImGui::Button("Load") && hasSelection) {
        loadTrack(selectedPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (!currentTrackPath.empty()) saveTrack(currentTrackPath);
    }

    ImGui::Separator();
    ImGui::Text("Save As (tracks/)");
    ImGui::InputText("##saveas", saveAsBuf.data(), saveAsBuf.size());
    ImGui::SameLine();
    if (ImGui::Button("Save As")) {
        if (currentTrack) {
            std::string name(saveAsBuf.data());
            if (!name.empty()) {
                name = ensureJsonExt(name);
                std::filesystem::path p = std::filesystem::path("tracks") / name;
                saveTrack(p.string());
            }
        }
    }

    ImGui::Separator();

    if (ImGui::Button("Duplicate") && hasSelection) {
        std::filesystem::path src(selectedPath);
        std::filesystem::path dst = src;
        dst.replace_filename(src.stem().string() + "_copy.json");
        std::error_code ec;
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
        refreshTrackList();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && hasSelection) {
        std::error_code ec;
        std::filesystem::remove(selectedPath, ec);
        if (currentTrackPath == selectedPath) currentTrackPath.clear();
        refreshTrackList();
    }

    ImGui::End();
}