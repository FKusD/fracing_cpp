#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "Track.h"

// Режим редактирования
enum class EditorMode {
    PLAY,           // Обычная игра - редактор выключен
    EDIT_WALLS,     // Редактирование стен
    EDIT_CHECKPOINTS, // Редактирование чекпоинтов
    EDIT_SPAWN,     // Установка стартовой позиции
    EDIT_SURFACES   // Редактирование зон с разным сцеплением
};

// Состояние редактора
class TrackEditor {
public:
    TrackEditor();
    ~TrackEditor() = default;

    // Основные методы
    void update(float dt);
    void handleMouseButton(int button, int action, double mouseX, double mouseY);
    void handleMouseMove(double mouseX, double mouseY);
    void handleKeyPress(int key, int action);
    void handleScroll(double yoffset);
    
    // Рендеринг
    void render();
    void renderUI(); // ImGui интерфейс
    
    // Управление режимом
    void setMode(EditorMode mode);
    EditorMode getMode() const { return mode; }
    bool isEditing() const { return mode != EditorMode::PLAY; }
    
    // Работа с трассой
    void setTrack(Track* track);
    void newTrack();
    bool saveTrack(const std::string& filename);
    bool loadTrack(const std::string& filename);
    
    // Операции редактирования
    void undo();
    void redo();
    void clearAll();
    
    // Утилиты
    glm::vec2 screenToWorld(double screenX, double screenY) const;
    void setProjectionMatrix(const glm::mat4& proj) { projectionMatrix = proj; }
    void setViewportSize(int width, int height) { viewportWidth = width; viewportHeight = height; }

private:
    // Состояние
    EditorMode mode;
    Track* currentTrack;
    bool isDirty; // Есть несохраненные изменения
    
    // Текущая операция
    std::vector<glm::vec2> currentWallPoints;  // Точки текущей рисуемой стены
    std::vector<glm::vec2> currentSurfacePoints; // Точки текущей зоны
    int selectedObjectIndex; // Индекс выбранного объекта (-1 если ничего не выбрано)
    
    // Параметры текущих объектов
    float currentWallThickness;
    float currentWallFriction;
    float currentWallRestitution;
    float currentSurfaceMu;
    
    // Настройки отображения
    bool showGrid;
    float gridSize;
    bool snapToGrid;
    
    // Для преобразования координат
    glm::mat4 projectionMatrix;
    int viewportWidth;
    int viewportHeight;
    
    // Мышь
    glm::vec2 mouseWorldPos;
    glm::vec2 lastMouseWorldPos;
    bool isDragging;
    
    // История для Undo/Redo
    struct HistoryState {
        Track trackState;
        std::string description;
    };
    std::vector<HistoryState> history;
    int historyIndex;
    static constexpr int MAX_HISTORY = 50;
    
    // Вспомогательные методы
    void addHistoryState(const std::string& description);
    void finishCurrentWall();
    void finishCurrentSurface();
    void deleteLastPoint();
    glm::vec2 snapToGridIfNeeded(const glm::vec2& pos) const;
    
    // Рендеринг элементов
    void renderGrid();
    void renderCurrentWall();
    void renderCurrentSurface();
    void renderWalls();
    void renderCheckpoints();
    void renderSpawnPoint();
    void renderSurfaces();
    void renderMouseCursor();
    
    // UI окна
    void renderMainToolbar();
    void renderPropertiesWindow();
    void renderObjectsList();
    void renderFileMenu();
};