#include "TrackEditor.h"
#include "DebugRenderer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>

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
    , gridSize(2.0f)
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
    // ... остальные поля ...

    std::cout << "[TrackEditor] Constructor END" << std::endl;
}

void TrackEditor::setMode(EditorMode newMode) {
    if (mode == newMode) return;
    
    // Завершаем текущую операцию при смене режима
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
}

void TrackEditor::setTrack(Track* track) {
    std::cout << "[setTrack] START" << std::endl;  // ← Добавь
    currentTrack = track;
    isDirty = false;
    history.clear();
    historyIndex = -1;
    
    // Сохраняем начальное состояние в историю
    if (track) {
        addHistoryState("Initial state");
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
    
    isDirty = false;
    history.clear();
    historyIndex = -1;
    addHistoryState("New track");
}

bool TrackEditor::saveTrack(const std::string& filename) {
    if (!currentTrack) return false;
    
    std::string error;
    if (saveTrackToFile(filename, *currentTrack, &error)) {
        isDirty = false;
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
        std::cout << "Track loaded: " << filename << std::endl;
        return true;
    } else {
        std::cerr << "Failed to load track: " << error << std::endl;
        return false;
    }
}

void TrackEditor::update(float dt) {
    if (!currentTrack) return; // ЗАЩИТА
    if (!isEditing()) return;
    // Обновление логики редактора (если нужно)
}

void TrackEditor::handleMouseButton(int button, int action, double mouseX, double mouseY) {
    if (!isEditing() || !currentTrack) return;
    
    mouseWorldPos = screenToWorld(mouseX, mouseY);
    glm::vec2 snappedPos = snapToGridIfNeeded(mouseWorldPos);
    
    // ЛКМ (button 0)
    if (button == 0 && action == 1) { // Press
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
            // Простая реализация: два клика = один чекпоинт
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
    
    // ПКМ (button 1) - завершить текущую операцию
    if (button == 1 && action == 1) { // Press
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
}

void TrackEditor::handleKeyPress(int key, int action) {
    if (!isEditing()) return;
    if (action != 1) return; // Only on press
    
    // Backspace - удалить последнюю точку
    if (key == 259) { // GLFW_KEY_BACKSPACE
        deleteLastPoint();
    }
    
    // Escape - отменить текущую операцию
    if (key == 256) { // GLFW_KEY_ESCAPE
        currentWallPoints.clear();
        currentSurfacePoints.clear();
    }
    
    // Ctrl+Z - undo
    if (key == 90 && (/* check ctrl modifier */true)) {
        undo();
    }
    
    // Ctrl+Y - redo
    if (key == 89 && (/* check ctrl modifier */true)) {
        redo();
    }
    
    // G - toggle grid
    if (key == 71) { // GLFW_KEY_G
        showGrid = !showGrid;
    }
    
    // H - toggle snap to grid
    if (key == 72) { // GLFW_KEY_H
        snapToGrid = !snapToGrid;
    }
}

void TrackEditor::handleScroll(double yoffset) {
    if (!isEditing()) return;
    
    // Изменение толщины стены колесиком мыши
    if (mode == EditorMode::EDIT_WALLS) {
        currentWallThickness += static_cast<float>(yoffset) * 0.05f;
        currentWallThickness = std::max(0.1f, std::min(currentWallThickness, 3.0f));
    }
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
    
    return glm::vec2(
        std::round(pos.x / gridSize) * gridSize,
        std::round(pos.y / gridSize) * gridSize
    );
}

glm::vec2 TrackEditor::screenToWorld(double screenX, double screenY) const {
    // Если матрица пустая, инверсия может вызвать аппаратное исключение
    if (viewportWidth <= 0 || viewportHeight <= 0) return glm::vec2(0,0);

    float ndcX = (2.0f * static_cast<float>(screenX) / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(screenY) / viewportHeight);

    // Попробуй временно закомментировать это и вернуть 0,0
    // glm::mat4 invProj = glm::inverse(projectionMatrix);
    // return glm::vec2(0,0);

    // Если всё же используешь:
    glm::mat4 invProj = glm::inverse(projectionMatrix);
    glm::vec4 worldPos = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    return glm::vec2(worldPos.x, worldPos.y);
}

void TrackEditor::addHistoryState(const std::string& description) {
    if (!currentTrack) return;
    
    // Удаляем все состояния после текущего (для redo)
    if (historyIndex < static_cast<int>(history.size()) - 1) {
        history.erase(history.begin() + historyIndex + 1, history.end());
    }
    
    // Добавляем новое состояние
    HistoryState state;
    state.trackState = *currentTrack;
    state.description = description;
    history.push_back(state);
    
    // Ограничиваем размер истории
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
    }
}

void TrackEditor::redo() {
    if (historyIndex < static_cast<int>(history.size()) - 1 && currentTrack) {
        historyIndex++;
        *currentTrack = history[historyIndex].trackState;
        isDirty = true;
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
// Продолжение TrackEditor.cpp - Рендеринг и UI

// ПРИМЕЧАНИЕ: Этот код нужно добавить к part1

// =============================================================================
// РЕНДЕРИНГ (упрощенная версия - используйте ваш существующий рендерер)
// =============================================================================

void TrackEditor::render() {
    if (!currentTrack) return; // ЗАЩИТА
    if (!isEditing()) return;
    
    if (showGrid) {
        renderGrid();
    }
    
    renderWalls();
    renderCheckpoints();
    renderSpawnPoint();
    renderSurfaces();
    
    // Рендерим текущие незавершенные объекты
    if (mode == EditorMode::EDIT_WALLS) {
        renderCurrentWall();
    }
    else if (mode == EditorMode::EDIT_SURFACES) {
        renderCurrentSurface();
    }
    
    renderMouseCursor();
}

void TrackEditor::renderGrid() {

    float range = 50.0f;
    for (float x = -range; x <= range; x += gridSize) {
        debugRenderer().addLine(
            b2Vec2(x, -range),
            b2Vec2(x, range)
        );
    }
    for (float y = -range; y <= range; y += gridSize) {
        debugRenderer().addLine(
            b2Vec2(-range, y),
            b2Vec2(range, y)
        );
    }
    debugRenderer().flush(projectionMatrix, 0.2f, 0.2f, 0.2f, 0.3f);

}

void TrackEditor::renderCurrentWall() {
    if (currentWallPoints.empty()) return;
    // Цвет: зеленый (незавершенная стена)

    for (size_t i = 0; i + 1 < currentWallPoints.size(); i++) {
        debugRenderer().addLine(
            b2Vec2(currentWallPoints[i].x, currentWallPoints[i].y),
            b2Vec2(currentWallPoints[i+1].x, currentWallPoints[i+1].y)
        );
    }
    // Линия до курсора мыши
    if (!currentWallPoints.empty()) {
        debugRenderer().addLine(
            b2Vec2(currentWallPoints.back().x, currentWallPoints.back().y),
            b2Vec2(mouseWorldPos.x, mouseWorldPos.y)
        );
    }
    debugRenderer().flush(projectionMatrix, 0.0f, 1.0f, 0.0f, 1.0f);

}

void TrackEditor::renderCurrentSurface() {
    // Аналогично renderCurrentWall, но для поверхностей
}

void TrackEditor::renderWalls() {
    if (!currentTrack) return;
    
    debugRenderer().beginFrame(); // Очищаем буфер линий для этого кадра

    for (const auto& wall : currentTrack->walls) {
        for (size_t i = 0; i + 1 < wall.vertices.size(); i++) {
            if (currentRenderMode == RenderMode::SIMPLE_BOXES) {
                // ШАГ 4 В ДЕЙСТВИИ: Рисуем объемные стены
                debugRenderer().addRect(wall.vertices[i], wall.vertices[i+1], wall.thickness);
            } else {
                // Просто тонкие линии для режима текстур (пока нет текстур)
                debugRenderer().addLine(
                    b2Vec2(wall.vertices[i].x, wall.vertices[i].y),
                    b2Vec2(wall.vertices[i+1].x, wall.vertices[i+1].y)
                );
            }
        }
    }
    // Выводим на экран
    debugRenderer().flush(projectionMatrix, 1.0f, 1.0f, 1.0f, 1.0f);
}

void TrackEditor::renderCheckpoints() {
    if (!currentTrack) return;

    for (const auto& cp : currentTrack->checkpoints) {
        debugRenderer().addLine(
            b2Vec2(cp.start.x, cp.start.y),
            b2Vec2(cp.end.x, cp.end.y)
        );
        debugRenderer().flush(projectionMatrix, 1.0f, 1.0f, 0.0f, 1.0f); // Желтый
    }

}

void TrackEditor::renderSpawnPoint() {
    if (!currentTrack) return;

    glm::vec2 pos = currentTrack->spawnPos;
    float size = 1.0f;
    
    // Крестик
    debugRenderer().addLine(
        b2Vec2(pos.x - size, pos.y),
        b2Vec2(pos.x + size, pos.y)
    );
    debugRenderer().addLine(
        b2Vec2(pos.x, pos.y - size),
        b2Vec2(pos.x, pos.y + size)
    );
    
    // Стрелка направления
    float angle = currentTrack->spawnYawRad;
    glm::vec2 dir(cos(angle), sin(angle));
    debugRenderer().addLine(
        b2Vec2(pos.x, pos.y),
        b2Vec2(pos.x + dir.x * 2.0f, pos.y + dir.y * 2.0f)
    );
    
    debugRenderer().flush(projectionMatrix, 0.0f, 1.0f, 1.0f, 1.0f); // Cyan

}

void TrackEditor::renderSurfaces() {
    // TODO: Рисуем зоны с разным сцеплением
}

void TrackEditor::renderMouseCursor() {

    glm::vec2 pos = snapToGridIfNeeded(mouseWorldPos);
    float size = 0.2f;
    
    debugRenderer().addLine(
        b2Vec2(pos.x - size, pos.y),
        b2Vec2(pos.x + size, pos.y)
    );
    debugRenderer().addLine(
        b2Vec2(pos.x, pos.y - size),
        b2Vec2(pos.x, pos.y + size)
    );
    debugRenderer().flush(projectionMatrix, 1.0f, 0.0f, 0.0f, 1.0f); // Красный

}

// =============================================================================
// IMGUI ИНТЕРФЕЙС
// =============================================================================

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
}

void TrackEditor::renderMainToolbar() {
    ImGui::Begin("Track Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("Mode:");
    
    if (ImGui::RadioButton("Play", mode == EditorMode::PLAY)) {
        setMode(EditorMode::PLAY);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Walls", mode == EditorMode::EDIT_WALLS)) {
        setMode(EditorMode::EDIT_WALLS);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Checkpoints", mode == EditorMode::EDIT_CHECKPOINTS)) {
        setMode(EditorMode::EDIT_CHECKPOINTS);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Spawn", mode == EditorMode::EDIT_SPAWN)) {
        setMode(EditorMode::EDIT_SPAWN);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Surfaces", mode == EditorMode::EDIT_SURFACES)) {
        setMode(EditorMode::EDIT_SURFACES);
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("New Track")) {
        newTrack();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Track")) {
        saveTrack("track_autosave.json");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Track")) {
        loadTrack("track_autosave.json");
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("Undo (Ctrl+Z)")) {
        undo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo (Ctrl+Y)")) {
        redo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        clearAll();
    }
    
    ImGui::Separator();
    
    ImGui::Checkbox("Show Grid (G)", &showGrid);
    ImGui::Checkbox("Snap to Grid (H)", &snapToGrid);
    ImGui::SliderFloat("Grid Size", &gridSize, 0.5f, 5.0f);
    
    ImGui::Separator();
    
    if (currentTrack) {
        ImGui::Text("Track: %s", currentTrack->name.c_str());
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
    
    ImGui::End();
}

void TrackEditor::renderPropertiesWindow() {
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (currentTrack) {
        ImGui::Separator();
        ImGui::Text("Track Size (Arena Bounds):");

        glm::vec2 half = currentTrack->arenaHalfExtents;

        // удобнее редактировать как Width/Height
        float sizeWH[2] = { half.x * 2.0f, half.y * 2.0f };

        if (ImGui::InputFloat2("Size (W,H)", sizeWH)) {
            // защита от мусора/нулей
            sizeWH[0] = std::max(2.0f, sizeWH[0]);
            sizeWH[1] = std::max(2.0f, sizeWH[1]);

            currentTrack->arenaHalfExtents = glm::vec2(sizeWH[0] * 0.5f, sizeWH[1] * 0.5f);
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
            if (ImGui::Button("Finish Wall (RMB)")) {
                finishCurrentWall();
            }
        }
    }
    else if (mode == EditorMode::EDIT_SURFACES) {
        ImGui::Text("Surface Properties:");
        ImGui::SliderFloat("Grip (mu)", &currentSurfaceMu, 0.1f, 2.0f);
        
        if (!currentSurfacePoints.empty()) {
            ImGui::Separator();
            ImGui::Text("Current surface: %zu points", currentSurfacePoints.size());
            if (ImGui::Button("Finish Surface (RMB)")) {
                finishCurrentSurface();
            }
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
            
            if (ImGui::Selectable(
                (std::string("Checkpoint ") + std::to_string(i)).c_str()
            )) {
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
    // TODO: Можно добавить отдельное окно для работы с файлами
    // с браузером директорий и т.д.
}