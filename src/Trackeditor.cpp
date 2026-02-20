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

    mode = newMode;
    selectedObjectIndex = -1;
    currentWallPoints.clear();
    currentSurfacePoints.clear();

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

    // Middle mouse: pan
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

    if (button == 0 && action == 1) {
        if (mode == EditorMode::EDIT_WALLS) {
            currentWallPoints.push_back(snappedPos);
        }
        else if (mode == EditorMode::EDIT_SURFACES) {
            currentSurfacePoints.push_back(snappedPos);
        }
        else if (mode == EditorMode::EDIT_SPAWN) {
            currentTrack->spawnPos = snappedPos;
            addHistoryState("Move spawn point");
            isDirty = true;
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
                cp.end = snappedPos;
                currentTrack->checkpoints.push_back(cp);
                addHistoryState("Add checkpoint");
                isDirty = true;
                hasStart = false;
            }
        }
    }

    if (button == 1 && action == 1) {
        if (mode == EditorMode::EDIT_WALLS && currentWallPoints.size() >= 2) {
            finishCurrentWall();
        }
        else if (mode == EditorMode::EDIT_SURFACES && currentSurfacePoints.size() >= 3) {
            finishCurrentSurface();
        }
    }
}

void TrackEditor::handleMouseMove(double mouseX, double mouseY) {
    lastMouseWorldPos = mouseWorldPos;
    mouseWorldPos = screenToWorld(mouseX, mouseY);

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

void TrackEditor::deleteLastPoint() {
    if (mode == EditorMode::EDIT_WALLS && !currentWallPoints.empty()) {
        currentWallPoints.pop_back();
    }
    else if (mode == EditorMode::EDIT_SURFACES && !currentSurfacePoints.empty()) {
        currentSurfacePoints.pop_back();
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
    renderSpawnPoint();
    renderSurfaces();

    if (mode == EditorMode::EDIT_WALLS) {
        renderCurrentWall();
    }
    else if (mode == EditorMode::EDIT_SURFACES) {
        renderCurrentSurface();
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

    for (float x = startX; x <= endX; x += step) {
        debugRenderer().addLine(b2Vec2(x, startY), b2Vec2(x, endY));
    }
    for (float y = startY; y <= endY; y += step) {
        debugRenderer().addLine(b2Vec2(startX, y), b2Vec2(endX, y));
    }

    debugRenderer().flush(projectionMatrix, 0.2f, 0.2f, 0.2f, 0.35f);
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
    ImGui::BulletText("LMB: Add point");
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
        ImGui::Text("Spawn Point:");
        ImGui::InputFloat2("Position", &currentTrack->spawnPos.x);
        ImGui::SliderAngle("Angle", &currentTrack->spawnYawRad);
    }
    else if (mode == EditorMode::EDIT_CHECKPOINTS) {
        ImGui::Text("Checkpoint Mode:");
        ImGui::BulletText("Click start point");
        ImGui::BulletText("Click end point");
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
