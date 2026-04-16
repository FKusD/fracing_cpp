#include "../include/CarGame.h"
#include "../include/Car.h"
#include "../include/Telemetry.h"
#include "../include/Track.h"

// GLAD для OpenGL
#include <glad/gl.h>
// Не даём GLFW подключать свой gl.h
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <cstdio>
#include <cstring>
#define _USE_MATH_DEFINES
#include <math.h>
#include <glm/gtc/constants.hpp>
#include "DebugRenderer.h"
#include "RandomController.h"
#include "ObservationDatasetLogger.h"
#include "BehavioralCloningController.h"
#include "GenomeController.h"
#include <random>

namespace {
GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(len, 1)), '\0');
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::cerr << "Shader compile failed:\n" << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(len, 1)), '\0');
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::cerr << "Program link failed:\n" << log << std::endl;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}



struct SensorDebugState {
    std::vector<Observation> observations;
    bool show = true;
} gSensorDebug;

static float cross2(const glm::vec2& a, const glm::vec2& b) {
    return a.x * b.y - a.y * b.x;
}

static bool segmentsIntersect2D(const glm::vec2& p, const glm::vec2& p2,
                                    const glm::vec2& q, const glm::vec2& q2) {
    glm::vec2 r = p2 - p;
    glm::vec2 s = q2 - q;
    float den = cross2(r, s);

    if (std::abs(den) < 1e-8f) return false;

    glm::vec2 qp = q - p;
    float t = cross2(qp, s) / den;
    float u = cross2(qp, r) / den;

    return (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f);
}

static glm::vec2 rotateLocal(const glm::vec2& forward, const glm::vec2& right, float angleRad) {
    return std::cos(angleRad) * forward + std::sin(angleRad) * right;
}

static float raySegmentHit(const glm::vec2& ro, const glm::vec2& rd,
                           const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 s = b - a;
    const float den = cross2(rd, s);
    if (std::abs(den) < 1e-6f) return -1.0f;
    const glm::vec2 delta = a - ro;
    const float t = cross2(delta, s) / den;
    const float u = cross2(delta, rd) / den;
    if (t >= 0.0f && u >= 0.0f && u <= 1.0f) return t;
    return -1.0f;
}
static int tournamentPick(const std::vector<int>& ranked, std::mt19937& rng, int tournamentSize = 3) {
    if (ranked.empty()) return -1;
    std::uniform_int_distribution<int> uid(0, (int)ranked.size() - 1);
    int best = ranked[uid(rng)];
    for (int i = 1; i < tournamentSize; ++i) {
        int cand = ranked[uid(rng)];
        if (cand < best) best = cand; // ranked already sorted by fitness desc, smaller index = better rank
    }
    return best;
}

    static GenomeSpec crossoverGenome(const GenomeSpec& a, const GenomeSpec& b, std::mt19937& rng) {
    GenomeSpec c = a;
    if (a.genes.size() != b.genes.size()) return c;
    std::bernoulli_distribution coin(0.5);
    for (size_t i = 0; i < c.genes.size(); ++i) {
        c.genes[i] = coin(rng) ? a.genes[i] : b.genes[i];
    }
    return c;
}

    static void mutateGenome(GenomeSpec& g, std::mt19937& rng, float prob, float sigma) {
    std::bernoulli_distribution flip(prob);
    std::normal_distribution<float> nd(0.0f, sigma);
    for (float& w : g.genes) {
        if (flip(rng)) w += nd(rng);
    }
}
struct SimpleRenderer {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint program = 0;
    GLint uColor = -1;
    bool initialized = false;

    void initOnce() {
        if (initialized) return;
        initialized = true;

        // Вершины в NDC: позиция (x,y)
        const float verts[] = {
            -0.6f, -0.4f,
             0.6f, -0.4f,
             0.0f,  0.6f
        };

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        const char* vsSrc = R"GLSL(
            #version 330 core
            layout(loion = 0) in vec2 aPos;
            void main() {
                gl_Position = vec4(aPos, 0.0, 1.0);
            }
        )GLSL";

        const char* fsSrc = R"GLSL(
            #version 330 core
            out vec4 FragColor;
            uniform vec4 uColor;
            void main() {
                FragColor = uColor;
            }
        )GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
        if (!vs || !fs) return;

        program = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!program) return;

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

        uColor = glGetUniformLocation(program, "uColor");

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void draw() {
        if (!initialized) initOnce();
        if (!program || !vao) return;

        glUseProgram(program);
        if (uColor >= 0) {
            glUniform4f(uColor, 0.2f, 0.85f, 0.35f, 1.0f);
        }
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
    }
};

// SimpleRenderer& renderer() {
//     static SimpleRenderer r;
//     return r;
// }

// struct DebugLineRenderer {
//     GLuint vao = 0;
//     GLuint vbo = 0;
//     GLuint program = 0;
//     GLint uColor = -1;
//     GLint uMVP = -1;
//     bool initialized = false;
//
//     // (x,y) pairs
//     std::vector<float> vertices;
//
//     void initOnce() {
//         if (initialized) return;
//         initialized = true;
//
//         glGenVertexArrays(1, &vao);
//         glBindVertexArray(vao);
//
//         glGenBuffers(1, &vbo);
//         glBindBuffer(GL_ARRAY_BUFFER, vbo);
//         glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
//
//         const char* vsSrc = R"GLSL(
//             #version 330 core
//             layout(location = 0) in vec2 aPos;
//             uniform mat4 uMVP;
//             void main() {
//                 gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
//             }
//         )GLSL";
//
//         const char* fsSrc = R"GLSL(
//             #version 330 core
//             out vec4 FragColor;
//             uniform vec4 uColor;
//             void main() {
//                 FragColor = uColor;
//             }
//         )GLSL";
//
//         GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
//         GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
//         if (!vs || !fs) return;
//
//         program = linkProgram(vs, fs);
//         glDeleteShader(vs);
//         glDeleteShader(fs);
//         if (!program) return;
//
//         glEnableVertexAttribArray(0);
//         glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
//
//         uColor = glGetUniformLocation(program, "uColor");
//         uMVP = glGetUniformLocation(program, "uMVP");
//
//         glBindBuffer(GL_ARRAY_BUFFER, 0);
//         glBindVertexArray(0);
//     }
//
//     void beginFrame() {
//         vertices.clear();
//     }
//
//     void addLine(const b2Vec2& a, const b2Vec2& b) {
//         vertices.push_back(a.x);
//         vertices.push_back(a.y);
//         vertices.push_back(b.x);
//         vertices.push_back(b.y);
//     }
//
//     void flush(const glm::mat4& mvp, float r, float g, float b, float a = 1.0f) {
//         if (!initialized) initOnce();
//         if (!program || !vao) return;
//         if (vertices.empty()) return;
//
//         glUseProgram(program);
//         if (uMVP >= 0) glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
//         if (uColor >= 0) glUniform4f(uColor, r, g, b, a);
//
//         glBindVertexArray(vao);
//         glBindBuffer(GL_ARRAY_BUFFER, vbo);
//         glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
//         glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size() / 2));
//         glBindBuffer(GL_ARRAY_BUFFER, 0);
//         glBindVertexArray(0);
//         glUseProgram(0);
//     }
// };
//
// DebugLineRenderer& debugRenderer() {
//     static DebugLineRenderer r;
//     return r;
// }

struct Box2DDebugDraw final : public b2Draw {
    void DrawPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) override {
        for (int32 i = 0; i < vertexCount; ++i) {
            const b2Vec2& a = vertices[i];
            const b2Vec2& b = vertices[(i + 1) % vertexCount];
            debugRenderer().addLine(a, b);
        }
        debugRenderer().flush(currentMVP, color.r, color.g, color.b, 1.0f);
        debugRenderer().beginFrame();
    }

    void DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) override {
        // Для дебага рисуем контур
        DrawPolygon(vertices, vertexCount, color);
    }

    void DrawCircle(const b2Vec2& center, float radius, const b2Color& color) override {
        constexpr int kSeg = 24;
        b2Vec2 prev(center.x + radius, center.y);
        for (int i = 1; i <= kSeg; ++i) {
            float t = (static_cast<float>(i) / static_cast<float>(kSeg)) * 2.0f * static_cast<float>(M_PI);
            b2Vec2 p(center.x + radius * std::cos(t), center.y + radius * std::sin(t));
            debugRenderer().addLine(prev, p);
            prev = p;
        }
        debugRenderer().flush(currentMVP, color.r, color.g, color.b, 1.0f);
        debugRenderer().beginFrame();
    }

    void DrawSolidCircle(const b2Vec2& center, float radius, const b2Vec2& axis, const b2Color& color) override {
        DrawCircle(center, radius, color);
        // Ось
        debugRenderer().addLine(center, b2Vec2(center.x + axis.x * radius, center.y + axis.y * radius));
        debugRenderer().flush(currentMVP, color.r, color.g, color.b, 1.0f);
        debugRenderer().beginFrame();
    }

    void DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color) override {
        debugRenderer().addLine(p1, p2);
        debugRenderer().flush(currentMVP, color.r, color.g, color.b, 1.0f);
        debugRenderer().beginFrame();
    }

    void DrawTransform(const b2Transform& xf) override {
        // Не обязательно, но полезно: оси объекта
        const float kAxisScale = 0.5f;
        b2Vec2 p = xf.p;
        b2Vec2 xAxis = p + kAxisScale * xf.q.GetXAxis();
        b2Vec2 yAxis = p + kAxisScale * xf.q.GetYAxis();
        debugRenderer().addLine(p, xAxis);
        debugRenderer().addLine(p, yAxis);
        debugRenderer().flush(currentMVP, 1.0f, 0.0f, 0.0f, 1.0f);
        debugRenderer().beginFrame();
    }

    void DrawPoint(const b2Vec2& p, float size, const b2Color& color) override {
        // В Box2D point обычно рисуют квадратиком/кружком; для простоты — маленький крестик
        const float s = std::max(size, 1.0f) * 0.02f;
        debugRenderer().addLine(b2Vec2(p.x - s, p.y), b2Vec2(p.x + s, p.y));
        debugRenderer().addLine(b2Vec2(p.x, p.y - s), b2Vec2(p.x, p.y + s));
        debugRenderer().flush(currentMVP, color.r, color.g, color.b, 1.0f);
        debugRenderer().beginFrame();
    }

    glm::mat4 currentMVP{1.0f};
};

Box2DDebugDraw& box2dDebugDraw() {
    static Box2DDebugDraw d;
    return d;
}

// static bool pointInPolygon(const glm::vec2& p, const std::vector<glm::vec2>& poly) {
//     if (poly.size() < 3) return false;
//     bool inside = false;
//     for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
//         const glm::vec2& a = poly[i];
//         const glm::vec2& b = poly[j];
//         const bool intersect = ((a.y > p.y) != (b.y > p.y)) &&
//                                (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + 1e-12f) + a.x);
//         if (intersect) inside = !inside;
//     }
//     return inside;
// }

static glm::vec2 rotateVec(glm::vec2 v, float a) {
    float c = cosf(a), s = sinf(a);
    return { c*v.x - s*v.y, s*v.x + c*v.y };
}

static void gridSlotPose(const StartGrid& g, int slotIndex, glm::vec2& outPos, float& outYaw) {
    int cols = std::max(1, g.cols);
    int rows = std::max(1, g.rows);
    int total = rows * cols;

    int idx = slotIndex % total;
    int r = idx / cols; // 0..rows-1
    int c = idx % cols; // 0..cols-1

    // локальные оси: forward = +X, right = +Y (потом поворачиваем на yaw)
    glm::vec2 forward = rotateVec({1,0}, g.yawRad);
    glm::vec2 right   = rotateVec({0,1}, g.yawRad);

    float colCenter = (cols - 1) * 0.5f;
    float x = -r * g.rowSpacing;                 // назад по рядам
    float y = (c - colCenter) * g.colSpacing;    // влево/вправо

    if (g.serpentine && (r % 2 == 1)) {
        y += g.colSpacing * 0.5f; // “шахматка”
    }

    outPos = g.origin + forward * x + right * y;
    outYaw = g.yawRad;
}


struct ImGuiHud {
    bool initialized = false;

    void init(GLFWwindow* win) {
        if (initialized) return;
        if (!win) return;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        // GLFW + OpenGL3 backend
        ImGui_ImplGlfw_InitForOpenGL(win, true);
        ImGui_ImplOpenGL3_Init("#version 330 core");
        initialized = true;
    }

    void shutdown() {
        if (!initialized) return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        initialized = false;
    }

    void newFrame() {
        if (!initialized) return;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void render() {
        if (!initialized) return;
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
};

// ImGuiHud& hud() {
//     static ImGuiHud h;
//     return h;
// }
} // namespace


CarGame::CarGame()
    : world(nullptr), gravity(0.0f, 0.0f), telemetry(nullptr), track(nullptr),
      window(nullptr), accumulator(0.0f), showHud(true), showPhysics(true), showTrajectory(true),
      trackPath("assets/default_track.json") {
}

CarGame::~CarGame() {
    shutdown();
}

bool CarGame::initialize() {
    // Initialize Box2D world
    world = new b2World(gravity);

    // Initialize camera
    projection = glm::ortho(-VIEW_W/2, VIEW_W/2, -VIEW_H/2, VIEW_H/2, -1.0f, 1.0f);
    cameraPos = glm::vec3(0.0f, 0.0f, 0.0f);

    track = std::make_unique<Track>();
    track->name = "Default Track";
    track->spawnPos = glm::vec2(0.0f, 0.0f);
    track->spawnYawRad = 0.0f;

    std::cout << "✓ Track created: " << track.get() << std::endl;
    std::cout << "✓ Track name: " << track->name << std::endl;

    std::cout << "About to create TrackEditor..." << std::endl;
    trackEditor = std::make_unique<TrackEditor>();
    std::cout << "✓ TrackEditor created" << std::endl;
    std::cout << "About to call setTrack..." << std::endl;
    trackEditor->setTrack(track.get());
    std::cout << "✓ setTrack done" << std::endl;
    trackEditor->setViewportSize(1280, 720);

    // Load track (data) and build collision
    const bool trackOk = loadTrack();

    // 2. Потом отдаем редактору (уже финальный указатель)
    if (trackEditor) {
        trackEditor->setTrack(track.get());
    }

    // Границы арены зависят от трека и должны существовать всегда
    createArenaBounds();
    if (trackOk) {
        buildTrackCollision();
    }

    if (autoBuildTrainingGates_) {
        rebuildSplineCache();
        rebuildTrainingGates();
    }

    // Create car at spawn
    const glm::vec2 spawnPos = (trackOk && track) ? track->spawnPos : glm::vec2(0.0f, 0.0f);
    const float spawnYawRad = (trackOk && track) ? track->spawnYawRad : 0.0f;

    cars.clear();
    carIsAI.clear();
    aiControllers.clear();
    aiWaypointIndex.clear();
    activeCarIndex  = 0;
    spectatorMode   = false;
    spectatorFree   = false;
    spectatorTarget = 0;

    const int numCars = 1; // <-- можно потом сделать настройкой / кнопкой в HUD
    for (int i = 0; i < numCars; ++i) {
        glm::vec2 p; float yaw;
        gridSlotPose(track->startGrid, i, p, yaw);

        auto c = std::make_unique<Car>(world, p);
        c->reset(p, yaw);
        cars.push_back(std::move(c));
        carIsAI.push_back(false);
        aiControllers.push_back(nullptr);
        aiWaypointIndex.push_back(0);
        aiConfigs_.push_back(CarAIConfig{});
    }
    trainingFitness_.assign(cars.size(), 0.0f);
    trainingCars_.assign(cars.size(), TrainingCarState{});
    bestGenerationFitness_ = -1e30f;
    bestGenerationCarIdx_ = -1;
    rebuildCarCollisionMasks();

    // Create telemetry system
    telemetry = std::make_unique<Telemetry>();

    // Включаем Box2D debug draw (контуры тел/джойнтов)
    box2dDebugDraw().SetFlags(
        b2Draw::e_shapeBit |
        b2Draw::e_jointBit |
        b2Draw::e_centerOfMassBit
    );
    world->SetDebugDraw(&box2dDebugDraw());

    // ImGui HUD
    window = glfwGetCurrentContext();
    // hud().init(window);
    // Вместо hud().init(window);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Включаем клавиатуру
    ImGui_ImplGlfw_InitForOpenGL(window, true); // true - важно для обработки мыши
    ImGui_ImplOpenGL3_Init("#version 330 core");
    debugRenderer().initOnce();
    return true;
}

void CarGame::update(float deltaTime) {
    static bool wasEditing = false;
    const bool nowEditing = (trackEditor && trackEditor->isEditing());

    // Переход EDIT -> PLAY: пересобираем физику и ресетим машины
    if (wasEditing && !nowEditing) {
        createArenaBounds();
        buildTrackCollision();

        if (autoBuildTrainingGates_) {
            rebuildSplineCache();
            rebuildTrainingGates();
        }

        if (track) {
            for (int i = 0; i < (int)cars.size(); ++i) {
                glm::vec2 p; float yaw;
                gridSlotPose(track->startGrid, i, p, yaw);
                cars[i]->reset(p, yaw);
            }
            if (telemetry) telemetry->reset();
            rebuildCarCollisionMasks();
        }
    }
    wasEditing = nowEditing;

    // Передаём редактору позицию активной машины (для follow camera)
    if (trackEditor) {
        if (Car* c = activeCar()) trackEditor->setCarPosition(c->getPosition());
        trackEditor->update(deltaTime);
    }

    // В редакторе симуляцию не крутим
    if (trackEditor && trackEditor->isEditing()) {
        return;
    }

    // fixed-step симуляция (стоп на паузе)
    if (isPaused) { updateCamera(); return; }
    accumulator += deltaTime * simSpeedMultiplier_;
    int substeps = 0;
    while (accumulator >= STEP && substeps < maxSubstepsPerFrame_) {
        handleInput();

        // 1) обновляем mu под каждой машиной
        if (track) {
            for (auto& c : cars) {
                glm::vec2 p = c->getPosition();
                float mu = c->getDefaultMu();

                for (const auto& s : track->surfaces) {
                    if (pointInPolygon(p, s.polygon)) {
                        mu = std::min(mu, s.mu);
                    }
                }
                c->setSurfaceMu(mu);
            }
        }

        // 2) строим Observation и обновляем каждую машину
        if ((int)gSensorDebug.observations.size() != (int)cars.size())
            gSensorDebug.observations.resize(cars.size());

        /* // auto buildObservation = [&](int ci) -> Observation {
        //     Observation obs;
        //     if (ci < 0 || ci >= (int)cars.size() || !cars[ci]) return obs;
        //
        //     Car* c = cars[ci].get();
        //     obs.pos          = c->getPosition();
        //     obs.speed        = c->getSpeed();
        //     obs.speedForward = c->getSpeed();
        //     obs.speedAbs     = std::abs(c->getSpeed());
        //     obs.yaw          = c->getAngleRad();
        //     obs.steer        = c->getSteer();
        //     obs.surfaceMu    = c->getSurfaceMu();
        //
        //     const glm::vec2 forward(std::cos(obs.yaw), std::sin(obs.yaw));
        //     const glm::vec2 right(-std::sin(obs.yaw), std::cos(obs.yaw));
        //
        //     constexpr float kHalfLen = 0.40f;
        //     constexpr float kHalfWid = 0.20f;
        //     const std::array<glm::vec2, Observation::kCornerCount> corners = {{
        //         obs.pos + forward * kHalfLen - right * kHalfWid,
        //         obs.pos + forward * kHalfLen + right * kHalfWid,
        //         obs.pos - forward * kHalfLen - right * kHalfWid,
        //         obs.pos - forward * kHalfLen + right * kHalfWid
        //     }};
        //     const std::array<float, Observation::kRaysPerCorner> rayAngles = {{
        //         -1.5707963f, -1.0471976f, -0.5235988f, -0.1745329f,
        //          0.1745329f,  0.5235988f,  1.0471976f,  1.5707963f
        //     }};
        //     const float maxRayLen = 18.0f;
        //
        //     auto testSeg = [&](const glm::vec2& ro, const glm::vec2& rd, float curBest,
        //                        const glm::vec2& a, const glm::vec2& b) {
        //         float t = raySegmentHit(ro, rd, a, b);
        //         return (t >= 0.0f && t < curBest) ? t : curBest;
        //     };
        //
        //     int rayIdx = 0;
        //     for (int cornerIdx = 0; cornerIdx < Observation::kCornerCount; ++cornerIdx) {
        //         const glm::vec2 ro = corners[cornerIdx];
        //         for (float ang : rayAngles) {
        //             glm::vec2 rd = rotateLocal(forward, right, ang);
        //             float best = maxRayLen;
        //
        //             if (track) {
        //                 const auto testPolyline = [&](const std::vector<glm::vec2>& poly, bool closed) {
        //                     if (poly.size() < 2) return;
        //                     const int n = (int)poly.size();
        //                     const int segs = closed ? n : (n - 1);
        //                     for (int i = 0; i < segs; ++i) {
        //                         best = testSeg(ro, rd, best, poly[i], poly[(i + 1) % n]);
        //                     }
        //                 };
        //
        //                 testPolyline(track->outerBoundary, true);
        //                 testPolyline(track->innerBoundary, true);
        //
        //                 for (const auto& wall : track->walls) {
        //                     const int wn = (int)wall.vertices.size();
        //                     if (wn < 2) continue;
        //                     for (int i = 0; i < wn - 1; ++i) {
        //                         const glm::vec2 a = wall.vertices[i];
        //                         const glm::vec2 b = wall.vertices[i + 1];
        //                         glm::vec2 seg = b - a;
        //                         float len = glm::length(seg);
        //                         if (len < 1e-4f) continue;
        //                         glm::vec2 tang = seg / len;
        //                         glm::vec2 norm(-tang.y, tang.x);
        //                         glm::vec2 off = norm * (0.5f * wall.thickness);
        //                         const glm::vec2 p0 = a + off;
        //                         const glm::vec2 p1 = b + off;
        //                         const glm::vec2 p2 = b - off;
        //                         const glm::vec2 p3 = a - off;
        //                         best = testSeg(ro, rd, best, p0, p1);
        //                         best = testSeg(ro, rd, best, p1, p2);
        //                         best = testSeg(ro, rd, best, p2, p3);
        //                         best = testSeg(ro, rd, best, p3, p0);
        //                     }
        //                 }
        //             }
        //
        //             obs.rayDistance[rayIdx] = std::clamp(best / maxRayLen, 0.0f, 1.0f);
        //             obs.rayStart[rayIdx] = ro;
        //             obs.rayEnd[rayIdx] = ro + rd * best;
        //             ++rayIdx;
        //         }
        //     }
        //
        //     if (track && track->racingLine.size() >= 2) {
        //         const int ctrlN = (int)track->racingLine.size();
        //         if (splineCacheVersion != ctrlN) rebuildSplineCache();
        //         const std::vector<glm::vec2>& line = splineCache.size() >= 2 ? splineCache : track->racingLine;
        //         const int N = (int)line.size();
        //         if (N >= 2) {
        //             int nearest = (ci < (int)aiWaypointIndex.size()) ? aiWaypointIndex[ci] : 0;
        //             nearest = std::clamp(nearest, 0, N - 1);
        //             float bestD2 = 1e30f;
        //             const int window = std::min(N, 160);
        //             for (int di = 0; di < window; ++di) {
        //                 int idx = (nearest + di) % N;
        //                 glm::vec2 d = line[idx] - obs.pos;
        //                 float d2 = glm::dot(d, d);
        //                 if (d2 < bestD2) {
        //                     bestD2 = d2;
        //                     nearest = idx;
        //                 } else if (di > 8 && d2 > bestD2 * 6.0f) {
        //                     break;
        //                 }
        //             }
        //             obs.progress = (float)nearest / (float)std::max(1, N);
        //             obs.distToCenterline = std::sqrt(std::max(0.0f, bestD2));
        //
        //             const std::array<float, 3> previewDist = {{
        //                 std::clamp(4.0f  + obs.speedAbs * 0.18f, 3.0f, 10.0f),
        //                 std::clamp(8.0f  + obs.speedAbs * 0.25f, 6.0f, 18.0f),
        //                 std::clamp(14.0f + obs.speedAbs * 0.33f, 10.0f, 28.0f)
        //             }};
        //
        //             for (int pi = 0; pi < 3; ++pi) {
        //                 glm::vec2 target = line[(nearest + 1) % N];
        //                 float accum = 0.0f;
        //                 for (int di = 1; di < N; ++di) {
        //                     int a = (nearest + di - 1) % N;
        //                     int b = (nearest + di) % N;
        //                     accum += glm::length(line[b] - line[a]);
        //                     target = line[b];
        //                     if (accum >= previewDist[pi]) break;
        //                 }
        //                 glm::vec2 rel = target - obs.pos;
        //                 float x = glm::dot(rel, forward);
        //                 float y = glm::dot(rel, right);
        //                 obs.headingError[pi] = std::atan2(y, std::max(0.1f, x));
        //                 float denom = x * x + y * y;
        //                 obs.curvature[pi] = (denom > 1e-4f) ? (2.0f * y / denom) : 0.0f;
        //             }
        //         }
        //     }
        //
        //     if (track) {
        //         bool hasOuter = track->outerBoundary.size() >= 3;
        //         bool hasInner = track->innerBoundary.size() >= 3;
        //         obs.offTrack = (hasOuter && !pointInPolygon(obs.pos, track->outerBoundary)) ||
        //                        (hasInner &&  pointInPolygon(obs.pos, track->innerBoundary));
        //     }
        //     return obs;
        // }; */

        for (int ci = 0; ci < (int)cars.size(); ++ci) {
            Observation obs = buildObservation(ci);
            gSensorDebug.observations[ci] = obs;

            if (trainingMode_) {
                if (ci >= (int)trainingFitness_.size()) trainingFitness_.resize(cars.size(), 0.0f);
                if (ci >= (int)trainingCars_.size())    trainingCars_.resize(cars.size());

                auto& st = trainingCars_[ci];

                updateTrainingGateProgress(ci, obs);
                updateTrainingWallState(ci, obs);

                float reward = 0.0f;

                // Единственный позитивный сигнал: правильный следующий gate
                if (st.gateRewardProgress > 0.0f) {
                    // 1.0 = обычный gate, 11.0 = последний gate + lap bonus
                    reward += st.gateRewardProgress * 100.0f;
                    st.gateRewardProgress = 0.0f;
                }

                // Стены
                if (st.justHitWall) {
                    reward -= 20.0f;
                }
                if (st.touchingWall) {
                    reward -= 1.0f * STEP;
                }

                trainingFitness_[ci] += reward;
            }

            if (carIsAI[ci]) {
                VehicleControls ctrl{};

                if (aiControllers[ci]) {
                    ctrl = aiControllers[ci]->update(obs, STEP);
                } else if (track && track->racingLine.size() >= 2) {
                    ctrl = computeRacingLineControls(ci);
                }

                cars[ci]->setControls(ctrl.throttle, ctrl.brake, ctrl.steer, ctrl.handbrake);

                if (datasetRecording_ && datasetLogger_.isOpen() && ci == 0 &&
                    aiConfigs_[ci].type == AIControllerType::NONE) {
                    datasetLogger_.logSample(obs, ctrl, STEP, ci, true, "scripted_ai");
                    }
            } else {
                if (datasetRecording_ && datasetLogger_.isOpen() && ci == activeCarIndex) {
                    VehicleControls playerCtrl{};
                    playerCtrl.throttle  = cars[ci]->getThrottle();
                    playerCtrl.brake     = cars[ci]->getBrake();
                    playerCtrl.steer     = cars[ci]->getSteer();
                    playerCtrl.handbrake = cars[ci]->isHandbrake();
                    datasetLogger_.logSample(obs, playerCtrl, STEP, ci, false, "player");
                }
            }

            cars[ci]->fixedUpdate(STEP);
        }

        if (trainingMode_ && !trainingFitness_.empty()) {
            updateBestTrainingAgent();
        }

        // 3) шаг Box2D
        world->Step(STEP, V_IT, P_IT);

        // 4) телеметрия только активной машины (пока)
        if (telemetry) {
            if (Car* c = activeCar()) {
                telemetry->update(
                    STEP,
                    c->getPosition(),
                    c->getSpeed(),
                    c->getSteer(),
                    c->getThrottle(),
                    c->getBrake(),
                    c->isHandbrake(),
                    c->getSurfaceMu(),
                    c->getLateralSpeed()
                );
            }
        }

        accumulator -= STEP;
        substeps++;
    }

    if (substeps >= maxSubstepsPerFrame_ && accumulator > STEP * 4.0f) {
        accumulator = STEP * 4.0f;
    }

    if (trainingMode_ && trainingRunning_) {
        trainingElapsed_ += deltaTime * simSpeedMultiplier_;
        if (trainingElapsed_ >= trainingEpisodeTime_) {
            bool hasGenomeCars = false;
            for (int i = 0; i < (int)cars.size(); ++i) {
                if (isGenomeCar(i)) { hasGenomeCars = true; break; }
            }

            if (hasGenomeCars) {
                advanceGAGeneration();
            } else if (trainingAutoReset_) {
                resetTrainingEpisode();
            } else {
                trainingRunning_ = false;
            }
        }
    }

    // Вне трассы/штрафы и тайминг считаем по кадру
    updateOffTrack(deltaTime);
    updateLapTiming(deltaTime);

    updateCamera();
}

void CarGame::render() {
    // 1. Получаем размеры окна
    int fbw = 0, fbh = 0;
    GLFWwindow* ctx = glfwGetCurrentContext();
    if (ctx) {
        glfwGetFramebufferSize(ctx, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
    }

    // 2. ИНИЦИАЛИЗАЦИЯ КАДРА IMGUI (Строго один раз!)
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Game Debug",      nullptr, &showHud);
            ImGui::MenuItem("Training",        nullptr, &showTrainingWindow_);
            ImGui::MenuItem("Training Agents", nullptr, &showAgentsWindow_);
            ImGui::MenuItem("Telemetry",       nullptr, &showTelemetryWindow_);
            ImGui::MenuItem("Lap Timing",      nullptr, &showLapWindow_);
            ImGui::MenuItem("Cars",            nullptr, nullptr, false); // Cars пока всегда открыто
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // 3. ОЧИСТКА ЭКРАНА
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // ── Меню паузы ─────────────────────────────────────────────────────
    if (isPaused && !(trackEditor && trackEditor->isEditing())) {
        ImGuiIO& _io = ImGui::GetIO();
        ImVec2 center(_io.DisplaySize.x * 0.5f, _io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_Always);
        ImGuiWindowFlags pf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        ImGui::Begin("##pause", nullptr, pf);
        float tw = ImGui::CalcTextSize("--- PAUSE ---").x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - tw) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f,0.85f,0.2f,1.0f), "--- PAUSE ---");
        ImGui::Separator(); ImGui::Spacing();
        float bw = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("Continue  (P / ESC)", ImVec2(bw,0))) isPaused = false;
        ImGui::Spacing();
        if (ImGui::Button("Reset all cars", ImVec2(bw,0))) {
            resetAllCarsToGrid();
            if (telemetry) telemetry->reset();
            isPaused = false;
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (!showQuitConfirm) {
            if (ImGui::Button("Quit game...", ImVec2(bw,0))) showQuitConfirm = true;
        } else {
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Are you sure? Unsaved data will be lost.");
            float hw = (bw - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Yes, quit", ImVec2(hw,0)))
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(hw,0))) showQuitConfirm = false;
        }
        ImGui::End();
    }

    debugRenderer().beginFrame();

    const bool editing = (trackEditor && trackEditor->isEditing());
    const glm::mat4& mvp = editing ? trackEditor->getProjectionMatrix() : projection;

    // В режиме редактора — НЕ рисуем "игровую физику/траекторию", чтобы не мешала
    if (!editing) {
        renderTrackBackground();

        if (showPhysics) {
            box2dDebugDraw().currentMVP = mvp;
            world->DebugDraw();
        }

        if (showTrajectory && telemetry && telemetry->trajectory.size() >= 2) {
            // Speed heatmap: blue(slow)->green->red(fast) — ONE draw call via colored batch
            const auto& tr   = telemetry->trajectory;
            const auto& spds = telemetry->speeds;
            float maxSpd = 1.0f;
            for (float s : spds) if (s > maxSpd) maxSpd = s;
            for (size_t i = 1; i < tr.size(); ++i) {
                float nt = (i < spds.size()) ? std::clamp(spds[i]/maxSpd, 0.0f, 1.0f) : 0.5f;
                float r  = std::clamp(2.0f*nt - 1.0f, 0.0f, 1.0f);
                float g  = 1.0f - std::abs(2.0f*nt - 1.0f);
                float b  = std::clamp(1.0f - 2.0f*nt, 0.0f, 1.0f);
                debugRenderer().addColoredLine(
                    b2Vec2(tr[i-1].x,tr[i-1].y), b2Vec2(tr[i].x,tr[i].y),
                    r, g, b, 0.85f);
            }
            debugRenderer().flushColored(mvp);  // single GPU draw call
        }

        // Racing line — Bezier (авто-касательные если нет ручек)
        if (track && track->racingLine.size() >= 2) {
            const auto& ctrl = track->racingLine;
            const int RLN = (int)ctrl.size();
            bool hasH = ((int)track->racingLineOutHandle.size() == RLN);
            std::vector<glm::vec2> autoH;
            if (!hasH) {
                autoH.resize(RLN);
                const float tension = 0.33f;
                for (int i = 0; i < RLN; ++i)
                    autoH[i] = (ctrl[(i+1)%RLN] - ctrl[(i-1+RLN)%RLN]) * tension;
            }
            auto bezRL = [](const glm::vec2& p0,const glm::vec2& p1,
                            const glm::vec2& p2,const glm::vec2& p3,float t){
                float u=1-t; return p0*(u*u*u)+p1*(3*u*u*t)+p2*(3*u*t*t)+p3*(t*t*t);
            };
            for (int i = 0; i < RLN; ++i) {
                int j=(i+1)%RLN;
                const glm::vec2& hi=hasH?track->racingLineOutHandle[i]:autoH[i];
                const glm::vec2& hj=hasH?track->racingLineOutHandle[j]:autoH[j];
                glm::vec2 prev=ctrl[i];
                for (int s=1;s<=12;++s){
                    float t=(float)s/12.0f;
                    glm::vec2 cur=bezRL(ctrl[i],ctrl[i]+hi,ctrl[j]-hj,ctrl[j],t);
                    debugRenderer().addLine(b2Vec2(prev.x,prev.y),b2Vec2(cur.x,cur.y));
                    prev=cur;
                }
            }
            debugRenderer().flush(mvp, 1.0f, 0.88f, 0.0f, 0.40f);
        }

        if (showTrainingGates_ && !trainingGates_.empty()) {
            for (int gi = 0; gi < (int)trainingGates_.size(); ++gi) {
                const auto& g = trainingGates_[gi];

                float r = 0.2f, gg = 0.8f, b = 1.0f, a = 0.8f;
                if (!trainingCars_.empty() && bestGenerationCarIdx_ >= 0 &&
                    bestGenerationCarIdx_ < (int)trainingCars_.size()) {
                    int nextGate = trainingCars_[bestGenerationCarIdx_].nextGateIndex;
                    if (gi == nextGate) {
                        r = 1.0f; gg = 0.9f; b = 0.2f; a = 1.0f;
                    }
                    }

                debugRenderer().addLine(b2Vec2(g.a.x, g.a.y), b2Vec2(g.b.x, g.b.y));
                debugRenderer().flush(mvp, r, gg, b, a);
                debugRenderer().beginFrame();
            }
        }

        if (lidarEnabled_ && showLidarRays_ && gSensorDebug.show &&
                activeCarIndex >= 0 && activeCarIndex < (int)gSensorDebug.observations.size()) {
            const Observation& obs = gSensorDebug.observations[activeCarIndex];
            for (int i = 0; i < Observation::kRayCount; ++i) {
                float t = obs.rayDistance[i];
                glm::vec3 color = (t < 0.999f) ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.9f, 0.9f, 0.2f);
                debugRenderer().addLine(
                    b2Vec2(obs.rayStart[i].x, obs.rayStart[i].y),
                    b2Vec2(obs.rayEnd[i].x,   obs.rayEnd[i].y)
                );
                debugRenderer().flush(mvp, color.r, color.g, color.b, 1.0f);
                debugRenderer().beginFrame();
            }
        }

        debugRenderer().flush(mvp, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        // В редакторе очищаем накопленное (на всякий)
        debugRenderer().flush(mvp, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Рендер редактора — ТОЛЬКО если он активен
    if (trackEditor && trackEditor->isEditing()) {
        trackEditor->render();
    }

    // ── GA Monitor — отдельное окно ────────────────────────────────────────────
    if (trainingMode_ && showGAMonitor_) {
        gaMonitor_.renderWindow(&showGAMonitor_);
    }

    Car* car = activeCar();

    // 6. ОТРИСОВКА ИНТЕРФЕЙСА (HUD и Окна редактора)
    if (showHud) {
        ImGui::Begin("Game Debug", &showHud, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show physics (F2)", &showPhysics);
            ImGui::Checkbox("Show trajectory", &showTrajectory);
            if (ImGui::Button("Reset Car (R)")) {
                const glm::vec2 spawnPos = (track ? track->spawnPos : glm::vec2(0.0f, 0.0f));
                car->reset(spawnPos, track ? track->spawnYawRad : 0.0f);
                telemetry->reset();
            }
        }

        ImGui::Separator();
        ImGui::Text("Simulation");
        ImGui::SliderFloat("Sim speed", &simSpeedMultiplier_, 0.1f, 10.0f, "%.1fx");
        // ImGui::SliderInt("Max substeps/frame", &maxSubstepsPerFrame_, 1, 128);
        // ImGui::TextDisabled("F9 start/stop run | F10 reset episode | F11 lidar");
        // ImGui::Text("Training elapsed: %.2f / %.2f", trainingElapsed_, trainingEpisodeTime_);
        // ImGui::Text("Best generation fitness: %.3f", bestGenerationFitness_);
        // ImGui::Text("Best car idx: %d", bestGenerationCarIdx_);
        // if (trainingMode_ && !trainingFitness_.empty()) {
        //     float bestFit = trainingFitness_[0];
        //     for (float f : trainingFitness_) bestFit = std::max(bestFit, f);
        //     ImGui::Text("Best fitness: %.3f", bestFit);
        // }
        // ImGui::Checkbox("Training mode", &trainingMode_);
        // ImGui::Checkbox("Training running", &trainingRunning_);
        // ImGui::Checkbox("Training auto reset", &trainingAutoReset_);
        // ImGui::SliderFloat("Episode time", &trainingEpisodeTime_, 5.0f, 220.0f, "%.1f s");
        //
        // ImGui::Separator();
        // // Компактный GA-блок — детали вынесены в отдельное окно
        // if (ImGui::CollapsingHeader("GA Training")) {
        //     ImGui::Checkbox("Training mode",    &trainingMode_);
        //     ImGui::Checkbox("Show GA monitor",  &showGAMonitor_);
        //     ImGui::Text("Generation: %d   Sigma: %.4f", gaGeneration_, gaMutationSigma_);
        //     if (!trainingFitness_.empty()) {
        //         float best = *std::max_element(trainingFitness_.begin(), trainingFitness_.end());
        //         ImGui::Text("Best fitness this ep: %.3f", best);
        //     }
        //     ImGui::Text("Best ever: %.3f", gaBestFitnessEver_);
        //     if (ImGui::Button("Open GA Monitor")) showGAMonitor_ = true;
        // }
        //
        // if (ImGui::Button("Setup GA population")) {
        //     setupGAPopulation();
        // }
        // ImGui::SameLine();
        // if (ImGui::Button("Save best genome")) {
        //     saveBestGenome();
        // }

        // ImGui::Separator();
        // ImGui::Text("Sensors");
        // ImGui::Checkbox("Lidar enabled", &lidarEnabled_);
        // ImGui::Checkbox("Show lidar rays", &showLidarRays_);
        //
        // ImGui::Separator();
        // ImGui::Text("Cars interaction");
        // int modeIdx = (interactionMode_ == CarInteractionMode::GHOSTS) ? 0 : 1;
        // const char* modeNames[] = { "Ghosts", "Collisions" };
        // if (ImGui::Combo("Mode", &modeIdx, modeNames, IM_ARRAYSIZE(modeNames))) {
        //     interactionMode_ = (modeIdx == 0) ? CarInteractionMode::GHOSTS : CarInteractionMode::COLLISIONS;
        //     rebuildCarCollisionMasks();
        // }

        if (car && telemetry && ImGui::CollapsingHeader("Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& s = telemetry->getLatest();
            ImGui::Text("Pos: (%.2f, %.2f)", s.pos.x, s.pos.y);
            ImGui::Text("Speed: %.2f m/s (%.1f km/h)", s.speed, s.speed * 3.6f);
            ImGui::Text("Steer: %.2f  Throttle: %.2f  Brake: %.2f", s.steer, s.throttle, s.brake);
            ImGui::Text("Handbrake: %s", s.handbrake ? "ON" : "OFF");
            ImGui::Text("Surface mu: %.3f", s.mu);
            ImGui::Text("Lateral speed (drift): %.2f m/s", s.vLat);
            ImGui::Text("BC: F6 start rec | F7 stop rec | F8 load model");
            ImGui::Text("REC: %s", datasetRecording_ ? "ON" : "OFF");
            ImGui::Text("CTRL: %s", useNeuralController_ ? "BC" : "SCRIPTED");
            ImGui::Text("Dataset: %s", datasetPath_.c_str());
            ImGui::Text("Model: %s", modelPath_.c_str());
            if (activeCarIndex >= 0 && activeCarIndex < (int)gSensorDebug.observations.size()) {
                const Observation& obs = gSensorDebug.observations[activeCarIndex];
                ImGui::Separator();
                ImGui::Text("Sensors: rays=%d  offTrack=%s  centerDist=%.2f",
                            Observation::kRayCount, obs.offTrack ? "YES" : "no", obs.distToCenterline);
                ImGui::Text("Heading err: %.2f  %.2f  %.2f",
                            obs.headingError[0], obs.headingError[1], obs.headingError[2]);
                ImGui::Text("Curvature:   %.3f  %.3f  %.3f",
                            obs.curvature[0], obs.curvature[1], obs.curvature[2]);
            }
            if (trainingMode_ && activeCarIndex >= 0 && activeCarIndex < (int)trainingCars_.size()) {
                const auto& st = trainingCars_[activeCarIndex];
                ImGui::Separator();
                ImGui::Text("Training state");
                ImGui::Text("Completed laps: %d", st.completedLaps);
                ImGui::Text("Next gate: %d", st.nextGateIndex);
                ImGui::Text("Passed this lap: %d", st.gatesPassedThisLap);
                ImGui::Text("Passed total: %d", st.gatesPassedTotal);
                ImGui::Text("Touching wall: %s", st.touchingWall ? "YES" : "NO");
                ImGui::Text("Just hit wall: %s", st.justHitWall ? "YES" : "NO");
                ImGui::Text("Wall touch time: %.2f", st.wallTouchTime);
            }
        }

        if (ImGui::CollapsingHeader("Lap Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!track || track->startFinish.start == track->startFinish.end) {
                ImGui::TextColored(ImVec4(1.0f,0.6f,0.2f,1.0f), "Start/Finish not set.");
                ImGui::Text("Editor -> Start/Finish -> Draw -> 2 clicks");
            } else {
                // ── Текущий круг (крупно) ──────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,1.0f,1.0f,1.0f));
                ImGui::Text("Lap:   %s", formatTime(timing.lapTime).c_str());
                ImGui::PopStyleColor();
                ImGui::TextDisabled("Last:%-12s  Best:%s",
                    formatLastTime(timing.lastLapTime).c_str(),
                    formatLastTime(timing.bestLapTime).c_str());

                // ── Сектора с дельтой ──────────────────────────────────
                if (!timing.currentSplits.empty()) {
                    ImGui::Separator();
                    int nSec = (int)timing.currentSplits.size();
                    for (int i = 0; i < nSec; ++i) {
                        float cur = timing.currentSplits[i];
                        float lst = timing.lastSplits[i];
                        float bst = timing.bestSplits[i];
                        bool active = (timing.lastPassedSector == i);

                        // Заголовок S1 / S2 / ...
                        ImVec4 hcol = active ? ImVec4(1,1,0.3f,1) : ImVec4(0.6f,0.6f,0.6f,1);
                        ImGui::TextColored(hcol, "S%d", i+1);
                        ImGui::SameLine(36);

                        // Текущее
                        if (cur > 0.0f)
                            ImGui::Text("%s", formatTime(cur).c_str());
                        else
                            ImGui::TextDisabled("--:--.---");
                        ImGui::SameLine(130);

                        // Дельта последнего к лучшему
                        if (lst > 0.0f && bst > 0.0f) {
                            float d = lst - bst;
                            ImVec4 dc = (d <= 0.001f) ? ImVec4(0.3f,1,0.3f,1)
                                                      : ImVec4(1,0.4f,0.4f,1);
                            ImGui::TextColored(dc, "%+.3fs", d);
                        } else if (lst > 0.0f) {
                            ImGui::TextDisabled("%s", formatTime(lst).c_str());
                        } else {
                            ImGui::TextDisabled("  ---");
                        }
                        ImGui::SameLine(200);
                        ImGui::TextDisabled("%s", bst>0 ? formatTime(bst).c_str() : "best:---");
                    }
                } else {
                    ImGui::TextDisabled("No sectors - add sector polygons in editor");
                }

                // Off-track статус
                if (!carPenalties.empty() && carPenalties[0].isOffTrack) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1,0.25f,0.25f,1), "! OFF TRACK");
                    if (carPenalties[0].penaltySeconds > 0.0f)
                        ImGui::TextColored(ImVec4(1,0.5f,0,1), "Penalty: +%.0f sec",
                                           carPenalties[0].penaltySeconds);
                }
            }
        }


        // Вызываем UI редактора ВНУТРИ кадра ImGui
        if (trackEditor && trackEditor->isEditing()) {
            ImGui::Separator();
            trackEditor->renderUI();
        }
        ImGui::End();
    }

    if (showTrainingWindow_) {
        ImGui::Begin("Training", &showTrainingWindow_, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Episode");
        ImGui::Checkbox("Training mode", &trainingMode_);
        ImGui::Checkbox("Training running", &trainingRunning_);
        ImGui::Checkbox("Training auto reset", &trainingAutoReset_);
        ImGui::SliderFloat("Episode time", &trainingEpisodeTime_, 5.0f, 120.0f, "%.1f s");
        ImGui::Text("Elapsed: %.2f / %.2f", trainingElapsed_, trainingEpisodeTime_);

        ImGui::Separator();
        ImGui::Text("Simulation");
        ImGui::SliderFloat("Sim speed", &simSpeedMultiplier_, 0.1f, 10.0f, "%.1fx");
        ImGui::SliderInt("Max substeps/frame", &maxSubstepsPerFrame_, 1, 128);

        ImGui::Separator();
        ImGui::Text("Sensors");
        ImGui::Checkbox("Lidar enabled", &lidarEnabled_);
        ImGui::Checkbox("Show lidar rays", &showLidarRays_);

        ImGui::Separator();
        ImGui::Text("Cars interaction");
        int modeIdx = (interactionMode_ == CarInteractionMode::GHOSTS) ? 0 : 1;
        const char* modeNames[] = { "Ghosts", "Collisions" };
        if (ImGui::Combo("Mode", &modeIdx, modeNames, IM_ARRAYSIZE(modeNames))) {
            interactionMode_ = (modeIdx == 0) ? CarInteractionMode::GHOSTS : CarInteractionMode::COLLISIONS;
            rebuildCarCollisionMasks();
        }

        ImGui::Separator();
        ImGui::Text("Spawn");
        ImGui::Checkbox("Single-point training spawn", &trainingSpawnSinglePoint_);
        ImGui::InputFloat2("Spawn pos", &trainingSpawnPos_.x);
        ImGui::SliderFloat("Spawn yaw", &trainingSpawnYaw_, -3.14159f, 3.14159f, "%.2f");
        ImGui::SliderFloat("Spawn jitter pos", &trainingSpawnJitterPos_, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Spawn jitter yaw", &trainingSpawnJitterYaw_, 0.0f, 0.5f, "%.3f");

        ImGui::Separator();
        ImGui::Text("Training gates");
        ImGui::Checkbox("Auto build gates", &autoBuildTrainingGates_);
        ImGui::Checkbox("Show training gates", &showTrainingGates_);
        ImGui::SliderInt("Gate count", &trainingGateCount_, 4, 64);
        ImGui::SliderFloat("Gate half width", &trainingGateHalfWidth_, 1.0f, 12.0f, "%.1f");
        if (ImGui::Button("Rebuild gates")) {
            rebuildSplineCache();
            rebuildTrainingGates();
        }
        ImGui::Text("Built gates: %d", (int)trainingGates_.size());

        ImGui::Separator();
        ImGui::Text("Fitness rules:");
        ImGui::BulletText("+100 next correct gate");
        ImGui::BulletText("+1000 lap");
        ImGui::BulletText("-20 wall hit");
        ImGui::BulletText("-1 * dt while touching wall");

        ImGui::Separator();
        ImGui::Text("Genetic");
        ImGui::SliderInt("GA population", &gaPopulationSize_, 2, 64);
        ImGui::SliderInt("GA elite", &gaEliteCount_, 1, 16);
        ImGui::SliderInt("GA hidden", &gaHiddenSize_, 8, 64);
        ImGui::SliderFloat("GA mutation sigma", &gaMutationSigma_, 0.01f, 0.5f, "%.3f");
        ImGui::SliderFloat("GA mutation prob", &gaMutationProbability_, 0.01f, 0.5f, "%.3f");
        ImGui::Text("Generation: %d", gaGeneration_);
        ImGui::Text("Best gen fitness: %.3f", bestGenerationFitness_);
        ImGui::Text("Best car: %d", bestGenerationCarIdx_);

        if (ImGui::Button("Setup GA population")) setupGAPopulation();
        ImGui::SameLine();
        if (ImGui::Button("Save best genome")) saveBestGenome();

        ImGui::Separator();
        ImGui::TextDisabled("F9 start/stop | F10 reset episode | F11 lidar | F12 setup GA");

        ImGui::End();
    }

    if (showAgentsWindow_ && trainingMode_ && !trainingCars_.empty()) {
    ImGui::Begin("Training Agents", &showAgentsWindow_, ImGuiWindowFlags_None);

    if (ImGui::BeginTable("training_agents_table", 8,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {

        ImGui::TableSetupColumn("Car");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Fitness");
        ImGui::TableSetupColumn("Lap");
        ImGui::TableSetupColumn("NextGate");
        ImGui::TableSetupColumn("PassedLap");
        ImGui::TableSetupColumn("PassedTotal");
        ImGui::TableSetupColumn("Wall");
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)cars.size(); ++i) {
            if (i >= (int)trainingCars_.size()) continue;

            const auto& st = trainingCars_[i];
            float fit = (i < (int)trainingFitness_.size()) ? trainingFitness_[i] : 0.0f;
            // float spd = cars[i] ? std::abs(cars[i]->getSpeed()) * 3.6f : 0.0f;
            const char* typeName = (i < (int)aiConfigs_.size())
                ? controllerTypeName(aiConfigs_[i].type)
                : "Unknown";

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (i == bestGenerationCarIdx_)
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "#%d", i);
            else
                ImGui::Text("#%d", i);

            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", typeName);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", fit);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", st.completedLaps);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", st.nextGateIndex);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%d", st.gatesPassedThisLap);
            ImGui::TableSetColumnIndex(6); ImGui::Text("%d", st.gatesPassedTotal);

            ImGui::TableSetColumnIndex(7);
            if (st.touchingWall)
                ImGui::TextColored(ImVec4(1.0f,0.25f,0.25f,1.0f), "YES");
            else
                ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1.0f), "NO");
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

    /* if (showHud) {
        ImGui::Begin("Training", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Episode");
        ImGui::Checkbox("Training mode", &trainingMode_);
        ImGui::Checkbox("Training running", &trainingRunning_);
        ImGui::Checkbox("Training auto reset", &trainingAutoReset_);
        ImGui::SliderFloat("Episode time", &trainingEpisodeTime_, 5.0f, 120.0f, "%.1f s");
        ImGui::Text("Elapsed: %.2f / %.2f", trainingElapsed_, trainingEpisodeTime_);

        ImGui::Separator();
        ImGui::Text("Simulation");
        ImGui::SliderFloat("Sim speed", &simSpeedMultiplier_, 0.1f, 10.0f, "%.1fx");
        ImGui::SliderInt("Max substeps/frame", &maxSubstepsPerFrame_, 1, 128);

        ImGui::Separator();
        ImGui::Text("Sensors");
        ImGui::Checkbox("Lidar enabled", &lidarEnabled_);
        ImGui::Checkbox("Show lidar rays", &showLidarRays_);

        ImGui::Separator();
        ImGui::Text("Spawn");
        ImGui::Checkbox("Single-point training spawn", &trainingSpawnSinglePoint_);
        ImGui::InputFloat2("Spawn pos", &trainingSpawnPos_.x);
        ImGui::SliderFloat("Spawn yaw", &trainingSpawnYaw_, -3.14159f, 3.14159f, "%.2f");
        ImGui::SliderFloat("Spawn jitter pos", &trainingSpawnJitterPos_, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Spawn jitter yaw", &trainingSpawnJitterYaw_, 0.0f, 0.5f, "%.3f");

        ImGui::Separator();
        ImGui::Text("Genetic");
        ImGui::SliderInt("GA population", &gaPopulationSize_, 2, 64);
        ImGui::SliderInt("GA elite", &gaEliteCount_, 1, 16);
        ImGui::SliderInt("GA hidden", &gaHiddenSize_, 8, 64);
        ImGui::SliderFloat("GA mutation sigma", &gaMutationSigma_, 0.01f, 0.5f, "%.3f");
        ImGui::SliderFloat("GA mutation prob", &gaMutationProbability_, 0.01f, 0.5f, "%.3f");
        ImGui::Text("Generation: %d", gaGeneration_);
        ImGui::Text("Best gen fitness: %.3f", bestGenerationFitness_);
        ImGui::Text("Best car: %d", bestGenerationCarIdx_);

        if (ImGui::Button("Setup GA population")) setupGAPopulation();
        ImGui::SameLine();
        if (ImGui::Button("Save best genome")) saveBestGenome();

        ImGui::Separator();
        ImGui::TextDisabled("F9 start/stop | F10 reset episode | F11 lidar | F12 setup GA");

        ImGui::End();
    } */


    // ─── Окно "Cars" ──────────────────────────────────────────────────────────
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(310, 60), ImVec2(430, 480));
        ImGui::Begin("Cars", nullptr, 0);

        // ── Строка статуса ──
        if (spectatorMode) {
            if (spectatorFree)
                ImGui::TextColored({0.4f,0.8f,1.0f,1.0f}, "Spectator  [Free cam  WASD]");
            else
                ImGui::TextColored({0.4f,0.8f,1.0f,1.0f}, "Spectator  -> #%d", spectatorTarget);
        } else {
            bool ai = activeCarIndex < (int)carIsAI.size() && carIsAI[activeCarIndex];
            ImGui::Text("Driving: #%d  [%s]", activeCarIndex, ai ? "AI" : "Player");
        }

        float btnX = ImGui::GetContentRegionAvail().x - 88;
        ImGui::SameLine(btnX < 10 ? 10 : btnX);
        if (ImGui::SmallButton(spectatorMode ? "Exit Spec" : "Spectator")) {
            spectatorMode   = !spectatorMode;
            spectatorFree   = false;
            spectatorTarget = activeCarIndex;
        }
        ImGui::Separator();

        // ── Кнопки парка ──
        if (ImGui::Button("+AI"))      addAICar();
        ImGui::SameLine();
        if (ImGui::Button("-AI"))      removeLastAICar();
        ImGui::SameLine();
        if (ImGui::Button("Reset All")) resetAllCarsToGrid();
        if (spectatorMode) {
            ImGui::SameLine();
            if (ImGui::SmallButton(spectatorFree ? "Lock" : "Free cam"))
                spectatorFree = !spectatorFree;
        }
        ImGui::Separator();

        // ── Список со скроллом ──
        float listH = std::max(30.0f, ImGui::GetContentRegionAvail().y - 4);
        ImGui::BeginChild("##carlist", ImVec2(0, listH), true);

        for (int i = 0; i < (int)cars.size(); ++i) {
            ImGui::PushID(i);

            bool ai    = i < (int)carIsAI.size() && carIsAI[i];
            bool hilit = spectatorMode ? (!spectatorFree && i == spectatorTarget)
                                       : (i == activeCarIndex);

            float spd_kmh = std::abs(cars[i]->getSpeed()) * 3.6f;
            glm::vec2 pos = cars[i]->getPosition();

            // Цвет: игрок-зелёный, AI-белый
            ImGui::PushStyleColor(ImGuiCol_Text,
                ai ? ImVec4(1,1,1,1) : ImVec4(0.3f,1.0f,0.5f,1.0f));

            char label[128];
            const char* ctrlName = "Player";
            if (ai && i < (int)aiConfigs_.size()) {
                ctrlName = controllerTypeName(aiConfigs_[i].type);
            }
            snprintf(label, sizeof(label), "%s#%-2d [%s]  %3.0f km/h  (%4.0f,%4.0f)",
                     ai ? "[AI] " : "[P]  ", i, ctrlName, spd_kmh, pos.x, pos.y);

            if (ImGui::Selectable(label, hilit, ImGuiSelectableFlags_AllowDoubleClick)) {
                // Одиночный — следить камерой
                spectatorTarget = i;
                spectatorFree   = false;
                if (!spectatorMode) activeCarIndex = i;
                // Двойной — взять управление
                if (ImGui::IsMouseDoubleClicked(0)) {
                    carIsAI[i]     = false;
                    spectatorMode  = false;
                    activeCarIndex = i;
                    if (i < (int)aiWaypointIndex.size()) aiWaypointIndex[i] = 0;
                    if (telemetry) telemetry->reset();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Dbl-click: take control\nRMB: menu");

            // ── Контекстное меню ──
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (ImGui::MenuItem("Take control (Player)")) {
                    carIsAI[i]     = false;
                    spectatorMode  = false;
                    activeCarIndex = i;
                    if (i < (int)aiWaypointIndex.size()) aiWaypointIndex[i] = 0;
                    if (telemetry) telemetry->reset();
                }
                if (!ai && ImGui::MenuItem("Hand to AI")) {
                    carIsAI[i] = true;
                    if (i >= (int)aiWaypointIndex.size())
                        aiWaypointIndex.resize(i+1, 0);
                    aiWaypointIndex[i] = 0;
                    if (activeCarIndex == i) spectatorMode = true;
                }
                if (ai) {
                    ImGui::Separator();
                    ImGui::TextDisabled("AI Policy");

                    int typeIdx = 0;
                    if (i < (int)aiConfigs_.size()) {
                        switch (aiConfigs_[i].type) {
                            case AIControllerType::NONE:                typeIdx = 0; break;
                            case AIControllerType::RANDOM:              typeIdx = 1; break;
                            case AIControllerType::BEHAVIORAL_CLONING:  typeIdx = 2; break;
                            case AIControllerType::GENOME:              typeIdx = 3; break;
                        }
                    }

                    const char* aiTypes[] = { "Scripted", "Random", "BC", "Genome" };
                    if (ImGui::Combo("Type", &typeIdx, aiTypes, IM_ARRAYSIZE(aiTypes))) {
                        if (i < (int)aiConfigs_.size()) {
                            aiConfigs_[i].type =
                                (typeIdx == 0) ? AIControllerType::NONE :
                                (typeIdx == 1) ? AIControllerType::RANDOM :
                                (typeIdx == 2) ? AIControllerType::BEHAVIORAL_CLONING :
                                                 AIControllerType::GENOME;
                            assignControllerForCar(i);
                        }
                    }
                    if (i < (int)aiConfigs_.size()) {
                        char pathBuf[256];
                        std::memset(pathBuf, 0, sizeof(pathBuf));
                        const std::string& src = aiConfigs_[i].modelPath.empty() ? modelPath_ : aiConfigs_[i].modelPath;
                        std::strncpy(pathBuf, src.c_str(), sizeof(pathBuf) - 1);

                        if (ImGui::InputText("Model path", pathBuf, sizeof(pathBuf))) {
                            aiConfigs_[i].modelPath = pathBuf;
                        }
                    }

                    if (i < (int)aiConfigs_.size()) {
                        bool coll = aiConfigs_[i].collidesWithCars;
                        if (ImGui::Checkbox("Collides with cars", &coll)) {
                            aiConfigs_[i].collidesWithCars = coll;
                            rebuildCarCollisionMasks();
                        }
                    }

                    if (ImGui::Button("Reload controller")) {
                        assignControllerForCar(i);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Follow cam")) {
                    spectatorTarget = i;
                    spectatorFree   = false;
                    spectatorMode   = true;
                }
                if (ImGui::MenuItem("Reset car")) {
                    glm::vec2 p; float yaw;
                    if (track) gridSlotPose(track->startGrid, i, p, yaw);
                    else { p = {(float)i*2.5f, 0}; yaw = 0; }
                    cars[i]->reset(p, yaw);
                    if (i < (int)aiWaypointIndex.size()) aiWaypointIndex[i] = 0;
                    rebuildCarCollisionMasks();
                }
                if (ai && ImGui::MenuItem("Remove")) {
                    cars.erase(cars.begin() + i);
                    carIsAI.erase(carIsAI.begin() + i);
                    aiControllers.erase(aiControllers.begin() + i);
                    if (i < (int)aiWaypointIndex.size())
                        aiWaypointIndex.erase(aiWaypointIndex.begin() + i);
                    activeCarIndex  = std::min(activeCarIndex,  (int)cars.size()-1);
                    spectatorTarget = std::min(spectatorTarget, (int)cars.size()-1);
                    ImGui::EndPopup(); ImGui::PopStyleColor(); ImGui::PopID();
                    break;
                }
                ImGui::EndPopup();
            }

            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::End();
    }
    // ─────────────────────────────────────────────────────────────────────────

    // 7. ЗАВЕРШЕНИЕ КАДРА IMGUI
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void CarGame::handleInput() {
    // Get current window from global context (this is a simplified approach)
    // In a real implementation, we'd pass the window pointer properly
    GLFWwindow* currentWindow = glfwGetCurrentContext();
    if (!currentWindow) {
        // Try to get the main window through some global reference
        // For now, we'll skip input handling if we can't find the window
        return;
    }

    // ESC / P — пауза (только вне редактора)
    {
        static bool escWas = false, pWas = false;
        bool escNow = glfwGetKey(currentWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        bool pNow   = glfwGetKey(currentWindow, GLFW_KEY_P)      == GLFW_PRESS;
        if (!(trackEditor && trackEditor->isEditing())) {
            if ((escNow && !escWas) || (pNow && !pWas)) {
                if (showQuitConfirm) showQuitConfirm = false;
                else isPaused = !isPaused;
            }
        }
        escWas = escNow; pWas = pNow;
    }

    // F3 - переключение режима редактора
    static bool f3Pressed = false;
    bool f3CurrentlyPressed = glfwGetKey(currentWindow, GLFW_KEY_F3) == GLFW_PRESS;
    // Найди этот блок в handleInput():
    if (f3CurrentlyPressed && !f3Pressed && trackEditor) {
        if (trackEditor->getMode() == EditorMode::PLAY) {
            trackEditor->setMode(EditorMode::EDIT_WALLS);
        } else {
            trackEditor->setMode(EditorMode::PLAY);

            createArenaBounds();
            buildTrackCollision();
            // if (car && track) {
            //     car->reset(track->spawnPos, track->spawnYawRad);
            // }

        }
    }
    f3Pressed = f3CurrentlyPressed;

    // Если редактор активен, не обрабатываем игровые контролы
    if (trackEditor && trackEditor->isEditing()) {
        return;
    }

    // Check for toggle keys
    static bool f1Pressed = false;
    static bool f2Pressed = false;

    bool f1CurrentlyPressed = glfwGetKey(currentWindow, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1CurrentlyPressed && !f1Pressed) {
        showHud = !showHud;
    }
    f1Pressed = f1CurrentlyPressed;

    bool f2CurrentlyPressed = glfwGetKey(currentWindow, GLFW_KEY_F2) == GLFW_PRESS;
    if (f2CurrentlyPressed && !f2Pressed) {
        showPhysics = !showPhysics;
    }
    f2Pressed = f2CurrentlyPressed;

    // F6/F7/F8 — behavioral cloning pipeline
    static bool f6Pressed = false;
    static bool f7Pressed = false;
    static bool f8Pressed = false;

    bool f6Now = glfwGetKey(currentWindow, GLFW_KEY_F6) == GLFW_PRESS;
    bool f7Now = glfwGetKey(currentWindow, GLFW_KEY_F7) == GLFW_PRESS;
    bool f8Now = glfwGetKey(currentWindow, GLFW_KEY_F8) == GLFW_PRESS;

    if (f6Now && !f6Pressed) {
        std::string err;
        if (!datasetLogger_.open(datasetPath_, &err)) {
            std::cerr << "Dataset open failed: " << err << std::endl;
        } else {
            datasetRecording_ = true;
            useNeuralController_ = false;
            std::cout << "Dataset recording ON -> " << datasetPath_ << std::endl;
        }
    }
    f6Pressed = f6Now;

    if (f7Now && !f7Pressed) {
        datasetRecording_ = false;
        datasetLogger_.close();
        std::cout << "Dataset recording OFF" << std::endl;
    }
    f7Pressed = f7Now;

    if (f8Now && !f8Pressed) {
        if (!aiControllers.empty()) {
            auto bc = std::make_unique<BehavioralCloningController>();
            std::string err;
            if (!bc->loadModel(modelPath_, &err)) {
                std::cerr << "BC model load failed: " << err << std::endl;
            } else {
                aiControllers[0] = std::move(bc);
                carIsAI[0] = true;
                useNeuralController_ = true;
                datasetRecording_ = false;
                datasetLogger_.close();
                std::cout << "BC model loaded: " << modelPath_ << std::endl;
            }
        }
    }
    f8Pressed = f8Now;

    static bool f9Pressed  = false;
    static bool f10Pressed = false;
    static bool f11Pressed = false;

    bool f9Now  = glfwGetKey(currentWindow, GLFW_KEY_F9)  == GLFW_PRESS;
    bool f10Now = glfwGetKey(currentWindow, GLFW_KEY_F10) == GLFW_PRESS;
    bool f11Now = glfwGetKey(currentWindow, GLFW_KEY_F11) == GLFW_PRESS;

    if (f9Now && !f9Pressed) {
        trainingRunning_ = !trainingRunning_;
        if (trainingRunning_) {
            trainingElapsed_ = 0.0f;
            std::cout << "Training run: ON" << std::endl;
        } else {
            std::cout << "Training run: OFF" << std::endl;
        }
    }
    f9Pressed = f9Now;

    if (f10Now && !f10Pressed) {
        resetTrainingEpisode();
        std::cout << "Training episode reset" << std::endl;
    }
    f10Pressed = f10Now;

    if (f11Now && !f11Pressed) {
        lidarEnabled_ = !lidarEnabled_;
        std::cout << "Lidar: " << (lidarEnabled_ ? "ON" : "OFF") << std::endl;
    }
    f11Pressed = f11Now;

    static bool f12Pressed = false;
    bool f12Now = glfwGetKey(currentWindow, GLFW_KEY_F12) == GLFW_PRESS;

    if (f12Now && !f12Pressed) {
        setupGAPopulation();
        std::cout << "GA population setup" << std::endl;
    }
    f12Pressed = f12Now;

    // Get control inputs
    if (spectatorMode) {
        // Spectator free cam — WASD двигают камеру (обрабатывается в updateCamera)
        // Tab: переключить за кем следим
        static bool tabSpSpec = false;
        bool tabSpNow = glfwGetKey(currentWindow, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tabSpNow && !tabSpSpec && !cars.empty() && !spectatorFree) {
            spectatorTarget = (spectatorTarget + 1) % (int)cars.size();
        }
        tabSpSpec = tabSpNow;
    } else {
        // Обычный Tab: переключить активную машину
        static bool tabPressed = false;
        bool tabNow = glfwGetKey(currentWindow, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tabNow && !tabPressed && !cars.empty()) {
            activeCarIndex = (activeCarIndex + 1) % (int)cars.size();
            spectatorTarget = activeCarIndex;
            if (telemetry) telemetry->reset();
        }
        tabPressed = tabNow;

        float throttle = 0.0f, brake = 0.0f, steer = 0.0f;
        bool handbrake = false;

        if (glfwGetKey(currentWindow, GLFW_KEY_W) == GLFW_PRESS ||
            glfwGetKey(currentWindow, GLFW_KEY_UP) == GLFW_PRESS)    throttle = 1.0f;
        if (glfwGetKey(currentWindow, GLFW_KEY_S) == GLFW_PRESS ||
            glfwGetKey(currentWindow, GLFW_KEY_DOWN) == GLFW_PRESS)  brake = 1.0f;
        if (glfwGetKey(currentWindow, GLFW_KEY_A) == GLFW_PRESS ||
            glfwGetKey(currentWindow, GLFW_KEY_LEFT) == GLFW_PRESS)  steer += 1.0f;
        if (glfwGetKey(currentWindow, GLFW_KEY_D) == GLFW_PRESS ||
            glfwGetKey(currentWindow, GLFW_KEY_RIGHT) == GLFW_PRESS) steer -= 1.0f;
        handbrake = glfwGetKey(currentWindow, GLFW_KEY_SPACE) == GLFW_PRESS;

        if (Car* car = activeCar()) car->setControls(throttle, brake, steer, handbrake);
    }

    // Reset car on 'R' press
    static bool rPressed = false;
    bool rCurrentlyPressed = glfwGetKey(currentWindow, GLFW_KEY_R) == GLFW_PRESS;
    if (rCurrentlyPressed && !rPressed) {
        const glm::vec2 spawnPos = (track ? track->spawnPos : glm::vec2(0.0f, 0.0f));
        const float spawnYawRad = (track ? track->spawnYawRad : 0.0f);
        if (Car* car = activeCar()) {
            car->reset(spawnPos, spawnYawRad);
        }
        if (telemetry) telemetry->reset();
        rebuildCarCollisionMasks();
    }
    rPressed = rCurrentlyPressed;
}

void CarGame::resize(int width, int height) {
    glViewport(0, 0, width, height);
    if (trackEditor) {
        trackEditor->setViewportSize(width, height);
    }
}

void CarGame::rebuildCarCollisionMasks() {
    for (int i = 0; i < (int)cars.size(); ++i) {
        if (!cars[i]) continue;

        bool enabled = (interactionMode_ == CarInteractionMode::COLLISIONS);

        if (i < (int)aiConfigs_.size()) {
            enabled = enabled && aiConfigs_[i].collidesWithCars;
        }

        cars[i]->setCarCollisionEnabled(enabled);
    }
}

void CarGame::shutdown() {
    // Вместо hud().shutdown() используем прямые вызовы очистки ImGui
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    if (world) {
        clearTrackCollision();
        delete world;
        world = nullptr;
    }

    cars.clear();
    telemetry.reset();
    track.reset();
}

static float frandRange(uint32_t& seed, float a, float b) {
    seed = 1664525u * seed + 1013904223u;
    float t = (float)(seed & 0x00FFFFFF) / (float)0x01000000;
    return a + (b - a) * t;
}

void CarGame::getSpawnPoseForCar(int idx, glm::vec2& outPos, float& outYaw) {
    if (trainingMode_ && trainingSpawnSinglePoint_) {
        uint32_t s = gaSeed_ + 7919u * (uint32_t)idx;
        outPos = trainingSpawnPos_;
        outPos.x += frandRange(s, -trainingSpawnJitterPos_, trainingSpawnJitterPos_);
        outPos.y += frandRange(s, -trainingSpawnJitterPos_, trainingSpawnJitterPos_);
        outYaw = trainingSpawnYaw_ + frandRange(s, -trainingSpawnJitterYaw_, trainingSpawnJitterYaw_);
        return;
    }

    if (track) gridSlotPose(track->startGrid, idx, outPos, outYaw);
    else { outPos = {(float)idx * 2.5f, 0.0f}; outYaw = 0.0f; }
}

bool CarGame::isGenomeCar(int carIdx) const {
    return carIdx >= 0 &&
           carIdx < (int)cars.size() &&
           carIdx < (int)carIsAI.size() &&
           carIdx < (int)aiConfigs_.size() &&
           carIsAI[carIdx] &&
           aiConfigs_[carIdx].type == AIControllerType::GENOME;
}

void CarGame::resetGAFitness() {
    trainingFitness_.assign(cars.size(), 0.0f);
    gaLastProgress_.assign(cars.size(), 0.0f);
}

void CarGame::saveBestGenome() {
    int bestIdx = -1;
    float bestFit = -1e30f;
    for (int i = 0; i < (int)cars.size(); ++i) {
        if (!isGenomeCar(i)) continue;
        if (i < (int)trainingFitness_.size() && trainingFitness_[i] > bestFit) {
            bestFit = trainingFitness_[i];
            bestIdx = i;
        }
    }
    if (bestIdx >= 0 && bestIdx < (int)gaGenomes_.size()) {
        GenomeController gc(gaGenomes_[bestIdx]);
        std::string err;
        if (!gc.saveGenome(gaBestGenomePath_, &err)) {
            std::cerr << "Save best genome failed: " << err << std::endl;
        } else {
            std::cout << "Saved best genome -> " << gaBestGenomePath_
                      << "  fitness=" << bestFit << std::endl;
        }
    }
}

void CarGame::setupGAPopulation() {
    int currentAI = 0;
    for (bool ai : carIsAI) if (ai) ++currentAI;

    while (currentAI < gaPopulationSize_) {
        addAICar();
        ++currentAI;
    }
    while (currentAI > gaPopulationSize_) {
        removeLastAICar();
        --currentAI;
    }

    gaGenomes_.resize(cars.size());
    trainingFitness_.resize(cars.size(), 0.0f);
    gaLastProgress_.resize(cars.size(), 0.0f);

    for (int i = 0; i < (int)cars.size(); ++i) {
        if (!carIsAI[i]) continue;

        aiConfigs_[i].type = AIControllerType::GENOME;
        aiConfigs_[i].modelPath.clear();

        gaGenomes_[i] = GenomeController::makeRandom(gaSeed_++, gaHiddenSize_, 0.35f);
        aiControllers[i] = std::make_unique<GenomeController>(gaGenomes_[i]);
        aiWaypointIndex[i] = 0;
    }

    gaGeneration_ = 0;
    trainingMode_ = true;
    trainingRunning_ = true;
    resetTrainingEpisode();
    resetGAFitness();
    gaBestFitnessEver_    = -1e30f;
    gaBestFitnessLastGen_ = -1e30f;
    gaStagnationCounter_  = 0;
    // gaMonitor_.clear();   // раскомментировать если хотите сбрасывать историю графиков
    rebuildCarCollisionMasks();
}

GenerationStats CarGame::collectGenerationStats() const {
    GenerationStats s;
    s.generation    = gaGeneration_;
    s.populationSize= gaPopulationSize_;
    s.eliteCount    = gaEliteCount_;
    s.mutationSigma = gaMutationSigma_;
    s.episodeTime   = trainingEpisodeTime_;

    // Собираем только геном-машины
    std::vector<int> ids;
    for (int i = 0; i < (int)cars.size(); ++i)
        if (isGenomeCar(i)) ids.push_back(i);

    if (ids.empty()) return s;

    // Cортируем по убыванию фитнеса (для таблицы популяции)
    std::vector<int> sorted = ids;
    std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        float fa = (a < (int)trainingFitness_.size()) ? trainingFitness_[a] : -1e30f;
        float fb = (b < (int)trainingFitness_.size()) ? trainingFitness_[b] : -1e30f;
        return fa > fb;
    });

    float sumFit = 0.f, sumProg = 0.f, sumSpd = 0.f;
    float wFit = -1e30f, wFit2 = 1e30f;

    s.individuals.reserve(sorted.size());
    for (int rank = 0; rank < (int)sorted.size(); ++rank) {
        int ci = sorted[rank];
        float fit  = (ci < (int)trainingFitness_.size()) ? trainingFitness_[ci] : 0.f;
        float prog = (ci < (int)gaLastProgress_.size())  ? gaLastProgress_[ci]  : 0.f;
        float spd  = cars[ci]->getSpeed();

        GenerationStats::Individual ind;
        ind.fitness  = fit;
        ind.progress = prog;
        ind.speed    = std::abs(spd);
        ind.isElite  = (rank < gaEliteCount_);
        s.individuals.push_back(ind);

        sumFit += fit;
        sumProg+= prog;
        sumSpd += ind.speed;
        wFit    = std::max(wFit,  fit);
        wFit2   = std::min(wFit2, fit);
    }

    const float N = (float)sorted.size();
    s.bestFitness  = wFit;
    s.meanFitness  = sumFit / N;
    s.worstFitness = wFit2;
    s.bestProgress = 0.f;
    s.meanProgress = sumProg / N;
    s.meanSpeed    = sumSpd  / N;
    for (const auto& ind : s.individuals)
        s.bestProgress = std::max(s.bestProgress, ind.progress);

    // Diversity: std отклонение L2-норм геномов
    if (!gaGenomes_.empty()) {
        std::vector<float> norms;
        for (int ci : sorted) {
            if (ci >= (int)gaGenomes_.size()) continue;
            const auto& g = gaGenomes_[ci].genes;
            float norm2 = 0.f;
            for (float w : g) norm2 += w * w;
            norms.push_back(std::sqrt(norm2));
        }
        if (!norms.empty()) {
            float mean = 0.f;
            for (float v : norms) mean += v;
            mean /= (float)norms.size();
            float var = 0.f;
            for (float v : norms) var += (v - mean) * (v - mean);
            s.diversity = std::sqrt(var / (float)norms.size());
        }
    }

    return s;
}

void CarGame::adaptMutationSigma(float bestFitnessThisGen) {
    constexpr int kStagnationLimit   = 4;     // поколений без улучшения → повышаем sigma
    constexpr float kSigmaDecay      = 0.92f; // при прогрессе уменьшаем
    constexpr float kSigmaBoost      = 1.12f; // при стагнации повышаем
    constexpr float kSigmaMin        = 0.008f;
    constexpr float kSigmaMax        = 0.55f;

    if (bestFitnessThisGen > gaBestFitnessLastGen_ + 1e-4f) {
        // Есть прогресс — сжимаем sigma (эксплуатация)
        gaMutationSigma_  *= kSigmaDecay;
        gaStagnationCounter_ = 0;
    } else {
        // Стагнация
        ++gaStagnationCounter_;
        if (gaStagnationCounter_ >= kStagnationLimit) {
            // Расширяем sigma (исследование)
            gaMutationSigma_  *= kSigmaBoost;
            gaStagnationCounter_ = 0;
        }
    }

    gaMutationSigma_ = std::clamp(gaMutationSigma_, kSigmaMin, kSigmaMax);
    gaBestFitnessLastGen_ = std::max(gaBestFitnessLastGen_, bestFitnessThisGen);
    gaBestFitnessEver_    = std::max(gaBestFitnessEver_,    bestFitnessThisGen);
}

void CarGame::advanceGAGeneration() {
    std::vector<int> ids;
    for (int i = 0; i < (int)cars.size(); ++i)
        if (isGenomeCar(i)) ids.push_back(i);
    if (ids.empty()) return;

    // ── A. Собираем статистику ДО смены геномов ───────────────────────────────
    GenerationStats stats = collectGenerationStats();

    // ── B. Адаптируем sigma ───────────────────────────────────────────────────
    adaptMutationSigma(stats.bestFitness);

    // После adaptMutationSigma sigma уже обновлена — пишем актуальное значение
    stats.mutationSigma = gaMutationSigma_;
    gaMonitor_.addGeneration(stats);

    // ── C. Сортируем по фитнесу ───────────────────────────────────────────────
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
        float fa = (a < (int)trainingFitness_.size()) ? trainingFitness_[a] : -1e30f;
        float fb = (b < (int)trainingFitness_.size()) ? trainingFitness_[b] : -1e30f;
        return fa > fb;
    });

    std::vector<int> parentPool;
    for (int idx : ids) {
        if (idx < (int)trainingCars_.size()) {
            if (trainingCars_[idx].completedLaps > 0 || trainingCars_[idx].gatesPassedTotal >= 2) {
                parentPool.push_back(idx);
            }
        }
    }
    if (parentPool.empty()) {
        parentPool = ids;
    }

    saveBestGenome();

    // ── D. Эволюция: элита + кроссовер + мутация ─────────────────────────────
    std::mt19937 rng(gaSeed_++);
    std::vector<GenomeSpec> next = gaGenomes_;
    int eliteCount = std::clamp(gaEliteCount_, 1, (int)ids.size());

    // Строим список рангов (индексов в ids) для турнира
    std::vector<int> ranks((int)ids.size());
    std::iota(ranks.begin(), ranks.end(), 0);  // 0,1,2,...

    for (int rank = eliteCount; rank < (int)ids.size(); ++rank) {
        int dstCar = ids[rank];

        // Турнирная выборка двух родителей
        int paRank = tournamentPick(ranks, rng, 3);
        int pbRank = tournamentPick(ranks, rng, 3);
        if (paRank < 0) paRank = 0;
        if (pbRank < 0) pbRank = 0;

        int paIdx = ids[paRank];
        int pbIdx = ids[pbRank];

        const GenomeSpec& A = gaGenomes_[paIdx];
        const GenomeSpec& B = gaGenomes_[pbIdx];
        GenomeSpec child = crossoverGenome(A, B, rng);
        mutateGenome(child, rng, gaMutationProbability_, gaMutationSigma_);
        next[dstCar] = std::move(child);
    }

    gaGenomes_ = std::move(next);

    for (int idx : ids) {
        aiControllers[idx] = std::make_unique<GenomeController>(gaGenomes_[idx]);
        aiWaypointIndex[idx] = 0;
    }

    ++gaGeneration_;
    resetTrainingEpisode();
    resetGAFitness();
    trainingRunning_ = true;

    std::cout << "[GA] Gen " << gaGeneration_
              << "  best=" << stats.bestFitness
              << "  mean=" << stats.meanFitness
              << "  prog=" << stats.bestProgress * 100.f << "%"
              << "  σ=" << gaMutationSigma_
              << std::endl;
}

bool CarGame::loadTrack() {
    track = std::make_unique<Track>();
    std::string err;
    if (!loadTrackFromFile(trackPath, *track, &err)) {
        std::cerr << "Failed to load track from '" << trackPath << "': " << err << std::endl;
        track.reset();
        return false;
    }
    return true;
}

void CarGame::clearTrackCollision() {
    if (!world) return;
    for (b2Body* b : trackBodies) {
        if (b) world->DestroyBody(b);
    }
    trackBodies.clear();
}

void CarGame::buildTrackCollision() {
    if (!world || !track) return;

    // 1. Сначала полностью удаляем старые тела из списка trackBodies
    // (Эти тела мы храним в векторе trackBodies, чтобы знать, что удалять)
    for (b2Body* b : trackBodies) {
        if (b) world->DestroyBody(b);
    }
    trackBodies.clear();

    // 2. Создаем физику заново из актуальных данных track->walls
    //    Важно: делаем 1 body на стену и добавляем к нему:
    //      - прямоугольники сегментов
    //      - "caps" (круги) в вершинах, чтобы углы у толстых стен были аккуратными.
    for (const auto& wall : track->walls) {
        if (wall.vertices.size() < 2) continue;

        b2BodyDef bd;
        bd.type = b2_staticBody;
        bd.position.Set(0.0f, 0.0f);
        bd.angle = 0.0f;
        b2Body* body = world->CreateBody(&bd);
        trackBodies.push_back(body);

        const float r = std::max(0.02f, wall.thickness * 0.5f);

        // Сегменты как боксы (локально в body)
        for (size_t i = 0; i + 1 < wall.vertices.size(); ++i) {
            const glm::vec2 a = wall.vertices[i];
            const glm::vec2 b = wall.vertices[i + 1];

            glm::vec2 diff = b - a;
            float len = glm::length(diff);
            if (len < 0.001f) continue;

            glm::vec2 mid = (a + b) * 0.5f;
            float ang = std::atan2(diff.y, diff.x);

            b2PolygonShape seg;
            seg.SetAsBox(len * 0.5f, r, b2Vec2(mid.x, mid.y), ang);

            b2FixtureDef fd;
            fd.shape = &seg;
            fd.friction = wall.friction;
            fd.restitution = wall.restitution;
            body->CreateFixture(&fd);
        }

        // Капы (круги) в каждой вершине
        for (const auto& v : wall.vertices) {
            b2CircleShape cap;
            cap.m_p.Set(v.x, v.y);
            cap.m_radius = r;

            b2FixtureDef fd;
            fd.shape = &cap;
            fd.friction = wall.friction;
            fd.restitution = wall.restitution;
            body->CreateFixture(&fd);
        }
    }
    std::cout << "[Physics] Rebuilt! Wall bodies: " << trackBodies.size() << std::endl;
}

void CarGame::createArenaBounds() {
    if (!world) return;

    // удалить старые bounds, если уже были
    if (arenaBoundsBody) {
        world->DestroyBody(arenaBoundsBody);
        arenaBoundsBody = nullptr;
    }

    float hx = 60.0f;
    float hy = 60.0f;

    if (track) {
        hx = std::max(1.0f, track->arenaHalfExtents.x);
        hy = std::max(1.0f, track->arenaHalfExtents.y);
    }

    b2BodyDef bd;
    bd.type = b2_staticBody;
    arenaBoundsBody = world->CreateBody(&bd);

    b2ChainShape loop;
    b2Vec2 vertices[] = {
        b2Vec2(-hx, -hy),
        b2Vec2(-hx,  hy),
        b2Vec2( hx,  hy),
        b2Vec2( hx, -hy)
    };
    loop.CreateLoop(vertices, 4);

    b2FixtureDef fd;
    fd.shape = &loop;
    fd.friction = 0.35f;
    fd.restitution = 0.25f;
    arenaBoundsBody->CreateFixture(&fd);
}

void CarGame::createObstacles() {
    createWallBox(glm::vec2(10.0f, 10.0f), 6.0f, 1.0f, 25.0f * M_PI / 180.0f);
    createWallBox(glm::vec2(-15.0f, 5.0f), 1.0f, 8.0f, 0.0f);
    createWallBox(glm::vec2(0.0f, -12.0f), 10.0f, 1.0f, 0.0f);
}

void CarGame::createWallBox(const glm::vec2& center, float hx, float hy, float angleRad) {
    b2BodyDef bd;
    bd.type = b2_staticBody;
    bd.position.Set(center.x, center.y);
    bd.angle = angleRad;
    b2Body* b = world->CreateBody(&bd);

    b2PolygonShape shape;
    shape.SetAsBox(hx, hy);

    b2FixtureDef fd;
    fd.shape = &shape;
    fd.friction = 0.35f;
    fd.restitution = 0.25f;
    b->CreateFixture(&fd);
}

void CarGame::updateCamera() {
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    if (h == 0) h = 1;
    float aspect = (float)w / (float)h;

    float viewH = spectatorMode ? 30.0f : 20.0f;
    float viewW = viewH * aspect;

    if (spectatorMode) {
        if (spectatorFree) {
            // Свободная камера WASD
            GLFWwindow* win = glfwGetCurrentContext();
            if (win && !ImGui::GetIO().WantCaptureKeyboard) {
                float spd = viewH * 0.022f;
                if (glfwGetKey(win,GLFW_KEY_W)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_UP)==GLFW_PRESS)    cameraPos.y += spd;
                if (glfwGetKey(win,GLFW_KEY_S)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_DOWN)==GLFW_PRESS)  cameraPos.y -= spd;
                if (glfwGetKey(win,GLFW_KEY_A)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_LEFT)==GLFW_PRESS)  cameraPos.x -= spd;
                if (glfwGetKey(win,GLFW_KEY_D)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_RIGHT)==GLFW_PRESS) cameraPos.x += spd;
            }
        } else {
            // Плавно следим за spectatorTarget
            int tgt = std::clamp(spectatorTarget, 0, (int)cars.size()-1);
            if (!cars.empty()) {
                glm::vec2 p = cars[tgt]->getPosition();
                cameraPos.x += (p.x - cameraPos.x) * 0.07f;
                cameraPos.y += (p.y - cameraPos.y) * 0.07f;
            }
        }
    } else {
        // Обычный — следим за активной машиной
        if (!cars.empty() && activeCarIndex < (int)cars.size()) {
            glm::vec2 p = cars[activeCarIndex]->getPosition();
            cameraPos.x += (p.x - cameraPos.x) * 0.10f;
            cameraPos.y += (p.y - cameraPos.y) * 0.10f;
        }
    }

    const glm::mat4 proj = glm::ortho(-viewW/2, viewW/2, -viewH/2, viewH/2, -1.0f, 1.0f);
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-cameraPos.x, -cameraPos.y, 0.0f));
    projection = proj * view;
}

void CarGame::renderHud() {
    // For now, just print basic info to console since we don't have text rendering implemented yet
    // In a real implementation, this would use a text rendering library like FreeType with OpenGL
    Car* car = activeCar();
    if (!car) return;

    static float lastPrintTime = 0.0f;
    lastPrintTime += 1.0f/60.0f; // Assuming 60 FPS

    if (lastPrintTime > 1.0f) { // Print every second
        glm::vec2 p = car->getPosition();
        float angleDeg = car->getAngleRad() * 180.0f / M_PI;

        std::cout << "=== HUD INFO ===" << std::endl;
        std::cout << "Pos: (" << p.x << ", " << p.y << ")" << std::endl;
        std::cout << "Yaw: " << angleDeg << " deg" << std::endl;
        std::cout << "Speed: " << car->getSpeed() << " m/s" << std::endl;
        std::cout << "Input: T=" << car->getThrottle() << " B=" << car->getBrake()
                  << " S=" << car->getSteer() << " HB=" << (car->isHandbrake() ? "ON" : "OFF") << std::endl;
        std::cout << "Mu: " << car->getSurfaceMu() << " | Wheelbase: " << car->getParams().wheelbase << std::endl;
        std::cout << "EngineF: " << car->getParams().engineForce << "N BrakeF: " << car->getParams().brakeForce << "N" << std::endl;
        std::cout << "Controls: W throttle | S brake | A/D steer | SPACE handbrake | R reset" << std::endl;
        std::cout << "================" << std::endl;

        lastPrintTime = 0.0f;
    }
}

void CarGame::setEditorMode(bool enabled) {
    if (!trackEditor) return;

    if (enabled) {
        trackEditor->setMode(EditorMode::EDIT_WALLS);
        clearTrackCollision();
    } else {
        trackEditor->setMode(EditorMode::PLAY);
        createArenaBounds();
        buildTrackCollision();
        if (track) {
            if (Car* car = activeCar()) {
                car->reset(track->spawnPos, track->spawnYawRad);
            }
        }
    }
}

void CarGame::resetTrainingEpisode() {
    resetAllCarsToGrid();
    trainingElapsed_ = 0.0f;

    for (int& wp : aiWaypointIndex) wp = 0;

    trainingFitness_.assign(cars.size(), 0.0f);
    trainingCars_.assign(cars.size(), TrainingCarState{});

    bestGenerationFitness_ = -1e30f;
    bestGenerationCarIdx_ = -1;

    if (telemetry) telemetry->reset();
}

void CarGame::updateBestTrainingAgent() {
    bestGenerationCarIdx_ = -1;
    bestGenerationFitness_ = -1e30f;

    auto better = [&](int a, int b) {
        const auto& A = trainingCars_[a];
        const auto& B = trainingCars_[b];

        if (A.completedLaps != B.completedLaps) return A.completedLaps > B.completedLaps;
        if (A.gatesPassedTotal != B.gatesPassedTotal) return A.gatesPassedTotal > B.gatesPassedTotal;
        return trainingFitness_[a] > trainingFitness_[b];
    };

    for (int i = 0; i < (int)cars.size(); ++i) {
        if (i >= (int)trainingFitness_.size()) continue;
        if (i >= (int)trainingCars_.size()) continue;

        // в элиту пускаем только тех, кто хотя бы немного реально ехал
        bool validForElite =
            (trainingCars_[i].completedLaps > 0) ||
            (trainingCars_[i].gatesPassedTotal >= 2);

        if (!validForElite) continue;

        if (bestGenerationCarIdx_ < 0 || better(i, bestGenerationCarIdx_)) {
            bestGenerationCarIdx_ = i;
            bestGenerationFitness_ = trainingFitness_[i];
        }
    }
}

void CarGame::updateTrainingWallState(int ci, const Observation& obs) {
    if (ci < 0 || ci >= (int)trainingCars_.size()) return;

    auto& st = trainingCars_[ci];
    st.justHitWall = false;

    float minRayAny = 1.0f;
    for (int ri = 0; ri < Observation::kRayCount; ++ri) {
        minRayAny = std::min(minRayAny, obs.rayDistance[ri]);
    }

    bool nowTouchingWall = (minRayAny < 0.025f);

    if (nowTouchingWall && !st.touchingWall) {
        st.justHitWall = true;
    }

    st.touchingWall = nowTouchingWall;

    if (st.touchingWall) st.wallTouchTime += STEP;
    else st.wallTouchTime = 0.0f;
}

void CarGame::rebuildTrainingGates() {
    trainingGates_.clear();
    if (!track) return;

    const std::vector<glm::vec2>& line =
        (splineCache.size() >= 2) ? splineCache : track->racingLine;

    const int N = (int)line.size();
    if (N < 4) return;

    int gateCount = std::clamp(trainingGateCount_, 4, std::max(4, N / 2));
    trainingGates_.reserve(gateCount);

    for (int gi = 0; gi < gateCount; ++gi) {
        int idx = (gi * N) / gateCount;
        int prev = (idx - 1 + N) % N;
        int next = (idx + 1) % N;

        glm::vec2 c = line[idx];
        glm::vec2 tan = line[next] - line[prev];
        float len = glm::length(tan);
        if (len < 1e-5f) continue;
        tan /= len;

        glm::vec2 normal(-tan.y, tan.x);

        TrainingGate g;
        g.center = c;
        g.a = c - normal * trainingGateHalfWidth_;
        g.b = c + normal * trainingGateHalfWidth_;
        trainingGates_.push_back(g);
    }
}

bool CarGame::segmentIntersectsGate(const glm::vec2& p0, const glm::vec2& p1, const TrainingGate& g) const {
    return segmentsIntersect2D(p0, p1, g.a, g.b);
}

void CarGame::updateTrainingGateProgress(int ci, const Observation& obs) {
    if (ci < 0 || ci >= (int)trainingCars_.size()) return;
    if (trainingGates_.empty()) return;

    auto& st = trainingCars_[ci];
    st.gateRewardProgress = 0.0f;

    glm::vec2 prevPos = glm::vec2(0.0f);
    if (cars[ci]) {
        float yaw = obs.yaw;
        glm::vec2 forward(std::cos(yaw), std::sin(yaw));
        prevPos = obs.pos - forward * obs.speedAbs * STEP;
    } else {
        prevPos = obs.pos;
    }

    int nextGate = std::clamp(st.nextGateIndex, 0, (int)trainingGates_.size() - 1);

    // Засчитываем ТОЛЬКО следующий ожидаемый gate
    if (segmentIntersectsGate(prevPos, obs.pos, trainingGates_[nextGate])) {
        st.nextGateIndex++;
        st.gatesPassedThisLap++;
        st.gatesPassedTotal++;
        st.gateRewardProgress = 1.0f;

        if (st.nextGateIndex >= (int)trainingGates_.size()) {
            st.nextGateIndex = 0;
            st.gatesPassedThisLap = 0;
            st.completedLaps++;
            st.gateRewardProgress += 10.0f; // бонус за круг через gateRewardProgress
        }
    }
}

void CarGame::renderTrackBackground() {
    // Placeholder - in a real implementation this would draw a textured quad
    // representing the track
}

std::string CarGame::formatTime(float seconds) {
    if (seconds < 0.0f) {
        seconds = 0.0f;
    }
    int minutes = static_cast<int>(seconds / 60.0f);
    float remaining = seconds - minutes * 60.0f;
    int secs = static_cast<int>(remaining);
    int millis = static_cast<int>((remaining - secs) * 1000.0f);
    char buffer[32];
    sprintf(buffer, "%d:%02d.%03d", minutes, secs, millis);
    return std::string(buffer);
}

std::string CarGame::formatLastTime(float seconds) {
    if (seconds <= 0.0f) {
        return "--:--.---";
    }
    return formatTime(seconds);
}

static bool segSegIntersect(const glm::vec2& a0, const glm::vec2& a1,
                            const glm::vec2& b0, const glm::vec2& b1) {
    auto cross = [](const glm::vec2& u, const glm::vec2& v) {
        return u.x * v.y - u.y * v.x;
    };
    glm::vec2 r = a1 - a0;
    glm::vec2 s = b1 - b0;
    float rxs = cross(r, s);
    float qpxr = cross(b0 - a0, r);

    if (std::abs(rxs) < 1e-7f && std::abs(qpxr) < 1e-7f) {
        // коллинеарны — считаем как "не пересекли" (для тайминга достаточно)
        return false;
    }
    if (std::abs(rxs) < 1e-7f) {
        // параллельны
        return false;
    }
    float t = cross((b0 - a0), s) / rxs;
    float u = cross((b0 - a0), r) / rxs;
    return (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f);
}

void CarGame::updateLapTiming(float dt) {
    if (!track || cars.empty()) return;

    // старт/финиш должен быть задан
    if (track->startFinish.start == track->startFinish.end) {
        timing.hasStartFinish = false;
        timing.hasPrevPos = false;
        timing.lapTime = 0.0f;
        return;
    }
    timing.hasStartFinish = true;

    Car* car = cars[0].get();
    if (!car) return;

    glm::vec2 pos = car->getPosition();
    timing.lapTime += dt;

    // Определяем текущий сектор по полигонам (если есть)
    int maxId = -1;
    for (const auto& s : track->sectors) maxId = std::max(maxId, s.id);
    int sectorCount = (maxId >= 0) ? (maxId + 1) : 0;
    if (sectorCount > 0) {
        if ((int)timing.currentSplits.size() != sectorCount) {
            timing.currentSplits.assign(sectorCount, -1.0f);
            timing.bestSplits.assign(sectorCount, -1.0f);
            timing.lastSplits.assign(sectorCount, -1.0f);
            timing.lastPassedSector = -1;
            timing.currentSector = -1;
        }

        int sec = -1;
        for (const auto& s : track->sectors) {
            if ((int)s.polygon.size() >= 3 && pointInPolygon(pos, s.polygon)) {
                sec = s.id;
                break;
            }
        }
        timing.currentSector = sec;

        // фиксируем сплит при первом попадании в сектор по порядку
        if (sec >= 0 && sec < sectorCount) {
            if (sec == timing.lastPassedSector + 1 && timing.currentSplits[sec] < 0.0f) {
                timing.currentSplits[sec] = timing.lapTime;
                timing.lastPassedSector = sec;
            }
        }
    }

    // Пересечение линии старта/финиша
    if (timing.hasPrevPos) {
        const glm::vec2 a = track->startFinish.start;
        const glm::vec2 b = track->startFinish.end;
        if (segSegIntersect(timing.prevPos, pos, a, b)) {
            // антидребезг: не засчитывать "сразу" после ресета
            if (timing.lapTime > 1.0f) {
                timing.lastLapTime = timing.lapTime;
                if (timing.bestLapTime < 0.0f || timing.lastLapTime < timing.bestLapTime) {
                    timing.bestLapTime = timing.lastLapTime;
                    if (!timing.currentSplits.empty()) timing.bestSplits = timing.currentSplits;
                }
                if (!timing.currentSplits.empty()) timing.lastSplits = timing.currentSplits;

                // старт нового круга
                timing.lapTime = 0.0f;
                if (!timing.currentSplits.empty())
                    std::fill(timing.currentSplits.begin(), timing.currentSplits.end(), -1.0f);
                timing.lastPassedSector = -1;
            }
        }
    }

    timing.prevPos = pos;
    timing.hasPrevPos = true;
}

void CarGame::addAICar() {
    if (!world) return;
    int slotIdx = (int)cars.size();
    glm::vec2 p; float yaw;
    if (track) gridSlotPose(track->startGrid, slotIdx, p, yaw);
    else { p = {slotIdx * 2.5f, 0.0f}; yaw = 0.0f; }

    auto c = std::make_unique<Car>(world, p);
    c->reset(p, yaw);
    cars.push_back(std::move(c));
    carIsAI.push_back(true);

    // static uint32_t aiSeed = 42;
    aiControllers.push_back(nullptr);//std::make_unique<RandomController>(aiSeed++));
    aiWaypointIndex.push_back(0);

    aiConfigs_.push_back(CarAIConfig{});
    aiConfigs_.back().type = AIControllerType::NONE; // scripted by default
    aiConfigs_.back().collidesWithCars = true;
    trainingFitness_.push_back(0.0f);
    trainingCars_.push_back(TrainingCarState{});
    gaGenomes_.resize(cars.size());
    gaLastProgress_.resize(cars.size(), 0.0f);

    assignControllerForCar((int)cars.size() - 1);
    rebuildCarCollisionMasks();
}

Observation CarGame::buildObservation(int ci) const {
    Observation obs;
    if (ci < 0 || ci >= (int)cars.size() || !cars[ci]) return obs;

    const Car* c = cars[ci].get();
    obs.pos          = c->getPosition();
    obs.speed        = c->getSpeed();
    obs.speedForward = c->getSpeed();
    obs.speedAbs     = std::abs(c->getSpeed());
    obs.yaw          = c->getAngleRad();
    obs.steer        = c->getSteer();
    obs.surfaceMu    = c->getSurfaceMu();

    const glm::vec2 forward(std::cos(obs.yaw), std::sin(obs.yaw));
    const glm::vec2 right(-std::sin(obs.yaw), std::cos(obs.yaw));
    const float maxRayLen = 22.0f;

    auto testSeg = [&](const glm::vec2& ro, const glm::vec2& rd, float curBest,
                       const glm::vec2& a, const glm::vec2& b) {
        float t = raySegmentHit(ro, rd, a, b);
        return (t >= 0.0f && t < curBest) ? t : curBest;
    };

    bool needLidar = false;

    if (lidarEnabled_) {
        if (showLidarRays_ && ci == activeCarIndex) {
            needLidar = true;
        }
        if (ci >= 0 && ci < (int)aiConfigs_.size()) {
            AIControllerType t = aiConfigs_[ci].type;
            if (t == AIControllerType::GENOME || t == AIControllerType::BEHAVIORAL_CLONING) {
                needLidar = true;
            }
        }
    }

    if (needLidar) {
        for (int i = 0; i < Observation::kRayCount; ++i) {
            float ang = glm::two_pi<float>() * (float)i / (float)Observation::kRayCount;
            glm::vec2 rd(std::cos(obs.yaw + ang), std::sin(obs.yaw + ang));
            glm::vec2 ro = obs.pos;
            float best = maxRayLen;

            if (track) {
                const auto testPolyline = [&](const std::vector<glm::vec2>& poly, bool closed) {
                    if (poly.size() < 2) return;
                    const int n = (int)poly.size();
                    const int segs = closed ? n : (n - 1);
                    for (int s = 0; s < segs; ++s) {
                        best = testSeg(ro, rd, best, poly[s], poly[(s + 1) % n]);
                    }
                };

                testPolyline(track->outerBoundary, true);
                testPolyline(track->innerBoundary, true);

                for (const auto& wall : track->walls) {
                    const int wn = (int)wall.vertices.size();
                    if (wn < 2) continue;
                    for (int k = 0; k < wn - 1; ++k) {
                        const glm::vec2 a = wall.vertices[k];
                        const glm::vec2 b = wall.vertices[k + 1];
                        glm::vec2 seg = b - a;
                        float len = glm::length(seg);
                        if (len < 1e-4f) continue;
                        glm::vec2 tang = seg / len;
                        glm::vec2 norm(-tang.y, tang.x);
                        glm::vec2 off = norm * (0.5f * wall.thickness);
                        const glm::vec2 p0 = a + off;
                        const glm::vec2 p1 = b + off;
                        const glm::vec2 p2 = b - off;
                        const glm::vec2 p3 = a - off;
                        best = testSeg(ro, rd, best, p0, p1);
                        best = testSeg(ro, rd, best, p1, p2);
                        best = testSeg(ro, rd, best, p2, p3);
                        best = testSeg(ro, rd, best, p3, p0);
                    }
                }
            }

            obs.rayDistance[i] = std::clamp(best / maxRayLen, 0.0f, 1.0f);
            obs.rayStart[i] = ro;
            obs.rayEnd[i] = ro + rd * best;
        }
    } else {
        for (int i = 0; i < Observation::kRayCount; ++i) {
            obs.rayDistance[i] = 1.0f;
            obs.rayStart[i] = obs.pos;
            obs.rayEnd[i] = obs.pos;
        }
    }

    if (track && track->racingLine.size() >= 2) {
        const int ctrlN = (int)track->racingLine.size();
        const std::vector<glm::vec2>& line =
            (splineCacheVersion == ctrlN && splineCache.size() >= 2) ? splineCache : track->racingLine;
        const int N = (int)line.size();
        if (N >= 2) {
            int nearest = (ci < (int)aiWaypointIndex.size()) ? aiWaypointIndex[ci] : 0;
            nearest = std::clamp(nearest, 0, N - 1);

            float bestD2 = 1e30f;
            const int window = std::min(N, 160);
            for (int di = 0; di < window; ++di) {
                int idx = (nearest + di) % N;
                glm::vec2 d = line[idx] - obs.pos;
                float d2 = glm::dot(d, d);
                if (d2 < bestD2) {
                    bestD2 = d2;
                    nearest = idx;
                } else if (di > 8 && d2 > bestD2 * 6.0f) {
                    break;
                }
            }

            int prevIdx = (nearest - 1 + N) % N;
            int nextIdx = (nearest + 1) % N;
            glm::vec2 tan = line[nextIdx] - line[prevIdx];
            float tanLen = glm::length(tan);
            if (tanLen > 1e-5f) tan /= tanLen;
            else tan = forward;

            obs.trackTangent = tan;
            obs.trackForwardDot = glm::dot(forward, tan);

            obs.progress = (float)nearest / (float)std::max(1, N);
            obs.distToCenterline = std::sqrt(std::max(0.0f, bestD2));

            const std::array<float, 3> previewDist = {{
                std::clamp(4.0f  + obs.speedAbs * 0.18f, 3.0f, 10.0f),
                std::clamp(8.0f  + obs.speedAbs * 0.25f, 6.0f, 18.0f),
                std::clamp(14.0f + obs.speedAbs * 0.33f, 10.0f, 28.0f)
            }};

            for (int pi = 0; pi < 3; ++pi) {
                glm::vec2 target = line[(nearest + 1) % N];
                float accum = 0.0f;
                for (int di = 1; di < N; ++di) {
                    int a = (nearest + di - 1) % N;
                    int b = (nearest + di) % N;
                    accum += glm::length(line[b] - line[a]);
                    target = line[b];
                    if (accum >= previewDist[pi]) break;
                }
                glm::vec2 rel = target - obs.pos;
                float x = glm::dot(rel, forward);
                float y = glm::dot(rel, right);
                obs.headingError[pi] = std::atan2(y, std::max(0.1f, x));
                float denom = x*x + y*y;
                obs.curvature[pi] = (denom > 1e-4f) ? (2.0f * y / denom) : 0.0f;
            }
        }
    }

    if (track) {
        bool hasOuter = track->outerBoundary.size() >= 3;
        bool hasInner = track->innerBoundary.size() >= 3;
        obs.offTrack = (hasOuter && !pointInPolygon(obs.pos, track->outerBoundary)) ||
                       (hasInner &&  pointInPolygon(obs.pos, track->innerBoundary));
    }

    return obs;
}

void CarGame::removeLastAICar() {
    for (int i = (int)cars.size()-1; i >= 0; --i) {
        if (carIsAI[i]) {
            cars.erase(cars.begin() + i);
            carIsAI.erase(carIsAI.begin() + i);
            aiControllers.erase(aiControllers.begin() + i);
            if (i < (int)aiWaypointIndex.size())
                aiWaypointIndex.erase(aiWaypointIndex.begin() + i);
            activeCarIndex  = std::min(activeCarIndex,  (int)cars.size()-1);
            spectatorTarget = std::min(spectatorTarget, (int)cars.size()-1);
            if (i < (int)aiConfigs_.size())
                aiConfigs_.erase(aiConfigs_.begin() + i);
            if (i < (int)trainingFitness_.size())
                trainingFitness_.erase(trainingFitness_.begin() + i);
            if (i < (int)trainingCars_.size())
                trainingCars_.erase(trainingCars_.begin() + i);
            if (i < (int)gaGenomes_.size())
                gaGenomes_.erase(gaGenomes_.begin() + i);
            if (i < (int)gaLastProgress_.size())
                gaLastProgress_.erase(gaLastProgress_.begin() + i);
            rebuildCarCollisionMasks();
            return;
        }
    }
}

const char* CarGame::controllerTypeName(AIControllerType t) const {
    switch (t) {
        case AIControllerType::NONE: return "Scripted";
        case AIControllerType::RANDOM: return "Random";
        case AIControllerType::BEHAVIORAL_CLONING: return "BC";
        case AIControllerType::GENOME: return "Genome";
        default: return "Unknown";
    }
}

void CarGame::assignControllerForCar(int carIdx) {
    if (carIdx < 0 || carIdx >= (int)cars.size()) return;
    if (carIdx >= (int)aiControllers.size()) return;
    if (carIdx >= (int)aiConfigs_.size()) return;

    aiControllers[carIdx].reset();

    const auto& cfg = aiConfigs_[carIdx];
    switch (cfg.type) {
        case AIControllerType::NONE:
            break; // scripted fallback
        case AIControllerType::RANDOM:
            aiControllers[carIdx] = std::make_unique<RandomController>(1234u + (uint32_t)carIdx);
            break;
        case AIControllerType::BEHAVIORAL_CLONING: {
            auto bc = std::make_unique<BehavioralCloningController>();
            std::string err;
            if (!bc->loadModel(cfg.modelPath.empty() ? modelPath_ : cfg.modelPath, &err)) {
                std::cerr << "BC load failed for car " << carIdx << ": " << err << std::endl;
            } else {
                aiControllers[carIdx] = std::move(bc);
            }
            break;
        }
        case AIControllerType::GENOME: {
            auto gc = std::make_unique<GenomeController>();
            std::string err;

            if (!cfg.modelPath.empty()) {
                if (!gc->loadGenome(cfg.modelPath, &err)) {
                    std::cerr << "Genome load failed for car " << carIdx << ": " << err << std::endl;
                    if (carIdx >= (int)gaGenomes_.size()) gaGenomes_.resize(cars.size());
                    gaGenomes_[carIdx] = GenomeController::makeRandom(gaSeed_++, gaHiddenSize_, 0.35f);
                    gc->setGenome(gaGenomes_[carIdx]);
                }
            } else {
                if (carIdx >= (int)gaGenomes_.size()) gaGenomes_.resize(cars.size());
                if (gaGenomes_[carIdx].genes.empty()) {
                    gaGenomes_[carIdx] = GenomeController::makeRandom(gaSeed_++, gaHiddenSize_, 0.35f);
                }
                gc->setGenome(gaGenomes_[carIdx]);
            }

            aiControllers[carIdx] = std::move(gc);
            break;
        }
    }
}

// ─── Pure Pursuit AI с rubber band ──────────────────────────────────────────
//
// Rubber band: сравниваем waypoint-прогресс AI с "лидером" (машина с макс wp).
// Если AI сильно отстаёт → бонус к скорости; сильно впереди → небольшой штраф.
// Диапазон коррекции: ±25% от базовой скорости.
//
// ── Catmull-Rom для AI (тот же алгоритм, что в редакторе) ────────────────────
// ── Кубический Безье (синхронизирован с редактором, tension=0.33) ────────────
static glm::vec2 bezierPtAI(const glm::vec2& p1, const glm::vec2& c1,
                              const glm::vec2& c2, const glm::vec2& p2, float t) {
    float u=1-t, u2=u*u, u3=u2*u, t2=t*t, t3=t2*t;
    return u3*p1 + 3.0f*u2*t*c1 + 3.0f*u*t2*c2 + t3*p2;
}

void CarGame::rebuildSplineCache() {
    const auto& ctrl = track->racingLine;
    const int N = (int)ctrl.size();
    splineCache.clear();
    if (N < 2) { splineCacheVersion = N; return; }

    // Если есть ручки из редактора — используем их.
    // Иначе fallback на автоматические касательные.
    std::vector<glm::vec2> cp1(N), cp2(N);
    if ((int)track->racingLineOutHandle.size() == N) {
        for (int i = 0; i < N; ++i) {
            cp1[i] = ctrl[i] + track->racingLineOutHandle[i];
            cp2[i] = ctrl[i] - track->racingLineOutHandle[i];
        }
    } else {
        const float tension = 0.33f;
        for (int i = 0; i < N; ++i) {
            glm::vec2 tang = (ctrl[(i+1)%N] - ctrl[(i-1+N)%N]) * tension;
            cp1[i] = ctrl[i] + tang;
            cp2[i] = ctrl[i] - tang;
        }
    }
    splineCache.reserve(N * SPLINE_SAMPLES);
    for (int i = 0; i < N; ++i) {
        int j = (i+1)%N;
        for (int s = 0; s < SPLINE_SAMPLES; ++s)
            splineCache.push_back(bezierPtAI(ctrl[i], cp1[i], cp2[j], ctrl[j],
                                              (float)s / SPLINE_SAMPLES));
    }
    splineCacheVersion = N;
    for (int& wp : aiWaypointIndex) wp = 0;
}

VehicleControls CarGame::computeRacingLineControls(int ci) {
    // Перестраиваем кэш если racingLine изменилась (редактор)
    if (!track) return {};
    int ctrlN = (int)track->racingLine.size();
    if (ctrlN < 2) return {};
    if (splineCacheVersion != ctrlN) rebuildSplineCache();

    // Используем сплайн если есть, иначе сырые точки
    const std::vector<glm::vec2>& line = splineCache.size() >= 2 ? splineCache : track->racingLine;
    const int N = (int)line.size();
    if (N < 2) return {};

    glm::vec2 carPos = cars[ci]->getPosition();
    float     carYaw = cars[ci]->getAngleRad();
    float     carSpd = cars[ci]->getSpeed();

    // ── 1. Advance nearest waypoint (окно 80 точек вперёд) ───────────────────
    if (ci >= (int)aiWaypointIndex.size()) aiWaypointIndex.resize(ci+1, 0);
    int& wp = aiWaypointIndex[ci];
    {
        float bestD2 = 1e9f;
        const int window = std::min(N, 80);
        for (int di = 0; di < window; ++di) {
            int idx = (wp + di) % N;
            glm::vec2 d = line[idx] - carPos;
            float d2 = d.x*d.x + d.y*d.y;
            if (d2 < bestD2) { bestD2 = d2; wp = idx; }
            else if (di > 5 && d2 > bestD2 * 4.0f) break;
        }
    }

    // ── 2. Rubber Band ────────────────────────────────────────────────────────
    float rubberFactor = 1.0f;
    {
        int leaderWp = wp;
        for (int k = 0; k < (int)cars.size(); ++k) {
            if (k == ci) continue;
            int kwp = (k < (int)aiWaypointIndex.size()) ? aiWaypointIndex[k] : 0;
            int diff = (kwp - leaderWp + N) % N;
            if (diff < N/2 && kwp != leaderWp) leaderWp = kwp;
        }
        int gap = (leaderWp - wp + N) % N;
        if (gap > N/2) gap = 0;
        rubberFactor = 1.0f + std::clamp((float)gap/(float)N * 2.0f, -0.15f, 0.30f);
    }

    // ── 3. Pure Pursuit lookahead ─────────────────────────────────────────────
    float lookahead = 4.5f + std::abs(carSpd) * 0.28f;
    lookahead = std::clamp(lookahead, 3.5f, 20.0f);

    glm::vec2 target = line[(wp+1)%N];
    {
        float accum = 0.0f;
        for (int di = 1; di < N; ++di) {
            int a=(wp+di-1)%N, b=(wp+di)%N;
            accum += glm::length(line[b]-line[a]);
            target = line[b];
            if (accum >= lookahead) break;
        }
    }

    // ── 4. Steering ───────────────────────────────────────────────────────────
    glm::vec2 toTgt  = target - carPos;
    float desiredYaw = std::atan2(toTgt.y, toTgt.x);
    float yawErr     = desiredYaw - carYaw;
    while (yawErr >  (float)M_PI) yawErr -= 2.0f*(float)M_PI;
    while (yawErr < -(float)M_PI) yawErr += 2.0f*(float)M_PI;

    float steerGain = 1.5f + std::max(0.0f, 8.0f - std::abs(carSpd)) * 0.07f;
    float steer = std::clamp(yawErr * steerGain, -1.0f, 1.0f);

    // ── 5. Curvature scan + тормозная точка (фикс шпилек) ──────────────────────
    // Сканируем SCAN_DIST метров вперёд: находим максимальную кривизну и на каком
    // расстоянии она встречается. Затем по формуле тормозного пути вычисляем
    // максимально допустимую скорость прямо сейчас: v² ≤ v_corner² + 2·a·d
    const float MAX_SPEED  = 22.0f;
    const float MIN_SPEED  =  4.5f;
    const float DECEL_RATE =  9.0f;   // м/с² — эффективное замедление
    const float SCAN_DIST  = 40.0f;   // метров вперёд

    float maxCurv  = 0.0f;
    float curvDist = SCAN_DIST;
    {
        float accum = 0.0f;
        for (int di = 1; di < N; ++di) {
            int a=(wp+di-1)%N, b=(wp+di)%N, cc=(wp+di+1)%N;
            float seg = glm::length(line[b]-line[a]);
            accum += seg;
            if (accum > SCAN_DIST) break;
            glm::vec2 d1=line[b]-line[a], d2=line[cc]-line[b];
            float l1=glm::length(d1), l2=glm::length(d2);
            if (l1<1e-4f||l2<1e-4f) continue;
            float cross = std::abs((d1.x/l1)*(d2.y/l2)-(d1.y/l1)*(d2.x/l2));
            if (cross > maxCurv) { maxCurv = cross; curvDist = accum; }
        }
    }

    // Целевая скорость в самом повороте
    float cornerSpd     = std::clamp(MAX_SPEED - maxCurv * 55.0f, MIN_SPEED, MAX_SPEED);
    // Максимум сейчас чтобы успеть затормозить (v² = vc² + 2ad)
    float brakeLimitSpd = std::sqrt(cornerSpd*cornerSpd + 2.0f*DECEL_RATE*curvDist);
    float baseSpd       = std::clamp(std::min(cornerSpd, brakeLimitSpd) * rubberFactor,
                                     MIN_SPEED, MAX_SPEED * 1.3f);

    // Перед крутым поворотом уменьшаем lookahead для точного попадания в апекс
    if (maxCurv > 0.25f) {
        float tightLA = std::max(3.0f, lookahead * (1.0f - maxCurv * 1.2f));
        glm::vec2 tightTgt = line[(wp+1)%N];
        float acc2 = 0.0f;
        for (int di = 1; di < N; ++di) {
            int a=(wp+di-1)%N, b=(wp+di)%N;
            acc2 += glm::length(line[b]-line[a]);
            tightTgt = line[b];
            if (acc2 >= tightLA) break;
        }
        float blend = std::clamp((maxCurv - 0.25f) * 4.0f, 0.0f, 1.0f);
        target = (1.0f - blend)*target + blend*tightTgt;
        // Пересчитываем steer
        glm::vec2 toT2 = target - carPos;
        float dy2 = std::atan2(toT2.y, toT2.x);
        float ye2 = dy2 - carYaw;
        while (ye2 >  (float)M_PI) ye2 -= 2*(float)M_PI;
        while (ye2 < -(float)M_PI) ye2 += 2*(float)M_PI;
        steer = std::clamp(ye2 * steerGain, -1.0f, 1.0f);
    }

    // ── 6. Продольный PD ──────────────────────────────────────────────────────
    float spdErr = baseSpd - std::abs(carSpd);
    float throttle = 0.0f, brake = 0.0f;

    if (carSpd < -0.5f) {
        throttle = 0.9f;
    } else if (spdErr > 0.3f) {
        throttle = std::clamp(spdErr / 3.5f, 0.15f, 1.0f);
    } else if (spdErr < -1.0f) {
        float bf = std::clamp(-spdErr / 4.5f, 0.15f, 1.0f);
        if (maxCurv > 0.3f) bf = std::min(1.0f, bf * 1.4f); // усиленное торможение в шпилях
        brake = bf;
    } else {
        throttle = 0.2f;
    }

    VehicleControls ctrl;
    ctrl.throttle  = throttle;
    ctrl.brake     = brake;
    ctrl.steer     = steer;
    ctrl.handbrake = false;
    return ctrl;
}

// ─── Система границ трассы ────────────────────────────────────────────────────

// Point-in-polygon (Ray casting)
bool CarGame::pointInPolygon(const glm::vec2& pt, const std::vector<glm::vec2>& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    int n = (int)poly.size();
    for (int i = 0, j = n-1; i < n; j = i++) {
        const glm::vec2& a = poly[i];
        const glm::vec2& b = poly[j];
        if (((a.y > pt.y) != (b.y > pt.y)) &&
            (pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

// Дистанция от точки до ближайшего сегмента
float CarGame::distToPolyline(const glm::vec2& pt, const std::vector<glm::vec2>& poly, bool closed) {
    float minD = 1e9f;
    int n = (int)poly.size();
    int segs = closed ? n : n-1;
    for (int i = 0; i < segs; ++i) {
        const glm::vec2& a = poly[i];
        const glm::vec2& b = poly[(i+1)%n];
        glm::vec2 ab = b-a, ap = pt-a;
        float t = std::clamp(glm::dot(ap,ab)/std::max(1e-6f,glm::dot(ab,ab)), 0.0f, 1.0f);
        float d = glm::length(ap - ab*t);
        minD = std::min(minD, d);
    }
    return minD;
}

void CarGame::updateOffTrack(float dt) {
    if (!track) return;
    bool hasOuter = track->outerBoundary.size() >= 3;
    bool hasInner = track->innerBoundary.size() >= 3;
    if (!hasOuter && !hasInner) return;

    // Убедиться что вектор штрафов нужного размера
    if ((int)carPenalties.size() != (int)cars.size())
        carPenalties.resize(cars.size());

    for (int i = 0; i < (int)cars.size(); ++i) {
        glm::vec2 pos = cars[i]->getPosition();
        bool offTrack = false;

        // Вне внешней границы?
        if (hasOuter && !pointInPolygon(pos, track->outerBoundary))
            offTrack = true;
        // Внутри внутренней границы (газон)?
        if (hasInner && pointInPolygon(pos, track->innerBoundary))
            offTrack = true;

        auto& pen = carPenalties[i];
        pen.isOffTrack = offTrack;

        if (offTrack) {
            pen.offTrackTime += dt;
            // Замедление нарастает до 40% от скорости за 1 секунду вне трассы
            pen.slowdownFactor = std::max(0.35f, 1.0f - pen.offTrackTime * 0.65f);
            // Штраф +1 сек каждые 2 секунды вне трассы
            if (pen.offTrackTime >= 2.0f) {
                pen.penaltySeconds += 1.0f;
                pen.offTrackTime   -= 2.0f;
            }
            applyOffTrackPenalty(i, dt);
        } else {
            pen.offTrackTime   = std::max(0.0f, pen.offTrackTime - dt * 2.0f); // быстро восстанавливаем
            pen.slowdownFactor = std::min(1.0f, pen.slowdownFactor + dt * 2.0f);
        }
    }
}

void CarGame::applyOffTrackPenalty(int ci, float /*dt*/) {
    if (ci >= (int)carPenalties.size()) return;
    float factor = carPenalties[ci].slowdownFactor;
    if (factor >= 1.0f || !cars[ci]) return;

    // Гасим скорость через setControls: добавляем принудительный тормоз
    // (работает для любых машин без доступа к Box2D body напрямую)
    float spd = std::abs(cars[ci]->getSpeed());
    if (spd > 2.0f) {
        float brakeForce = (1.0f - factor) * 0.8f;  // 0..0.8 в зависимости от нарушения
        // Читаем текущие контролы и усиливаем тормоз
        VehicleControls cur;
        cur.throttle  = 0.0f;
        cur.brake     = std::clamp(brakeForce, 0.0f, 0.9f);
        cur.steer     = 0.0f;   // не меняем руль
        cur.handbrake = false;
        // Применяем только если машина едет — не блокируем стоячую
        cars[ci]->setControls(cur.throttle, cur.brake, 0.0f, false);
    }
}


void CarGame::resetAllCarsToGrid() {
    for (int i = 0; i < (int)cars.size(); ++i) {
        glm::vec2 p; float yaw;
        if (track) {
            gridSlotPose(track->startGrid, i, p, yaw);
        } else {
            p = glm::vec2(i * 2.5f, 0.0f);
            yaw = 0.0f;
        }
        cars[i]->reset(p, yaw);
    }
    if (telemetry) telemetry->reset();
    rebuildCarCollisionMasks();
}