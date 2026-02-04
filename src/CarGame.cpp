#include "../include/CarGame.h"
#include "../include/Car.h"
#include "../include/Telemetry.h"

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
            layout(location = 0) in vec2 aPos;
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

SimpleRenderer& renderer() {
    static SimpleRenderer r;
    return r;
}

struct DebugLineRenderer {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint program = 0;
    GLint uColor = -1;
    GLint uMVP = -1;
    bool initialized = false;

    // (x,y) pairs
    std::vector<float> vertices;

    void initOnce() {
        if (initialized) return;
        initialized = true;

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

        const char* vsSrc = R"GLSL(
            #version 330 core
            layout(location = 0) in vec2 aPos;
            uniform mat4 uMVP;
            void main() {
                gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
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
        uMVP = glGetUniformLocation(program, "uMVP");

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void beginFrame() {
        vertices.clear();
    }

    void addLine(const b2Vec2& a, const b2Vec2& b) {
        vertices.push_back(a.x);
        vertices.push_back(a.y);
        vertices.push_back(b.x);
        vertices.push_back(b.y);
    }

    void flush(const glm::mat4& mvp, float r, float g, float b, float a = 1.0f) {
        if (!initialized) initOnce();
        if (!program || !vao) return;
        if (vertices.empty()) return;

        glUseProgram(program);
        if (uMVP >= 0) glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        if (uColor >= 0) glUniform4f(uColor, r, g, b, a);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size() / 2));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);
    }
};

DebugLineRenderer& debugRenderer() {
    static DebugLineRenderer r;
    return r;
}

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

ImGuiHud& hud() {
    static ImGuiHud h;
    return h;
}
} // namespace

CarGame::CarGame()
    : world(nullptr), gravity(0.0f, 0.0f), car(nullptr), telemetry(nullptr),
      window(nullptr), accumulator(0.0f), showHud(true), showPhysics(true) {
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

    // Create car at origin
    car = std::make_unique<Car>(world, glm::vec2(0.0f, 0.0f));

    // Create telemetry system
    telemetry = std::make_unique<Telemetry>();

    // Create arena bounds and obstacles
    createArenaBounds();
    createObstacles();

    // Включаем Box2D debug draw (контуры тел/джойнтов)
    box2dDebugDraw().SetFlags(
        b2Draw::e_shapeBit |
        b2Draw::e_jointBit |
        b2Draw::e_centerOfMassBit
    );
    world->SetDebugDraw(&box2dDebugDraw());

    // ImGui HUD
    window = glfwGetCurrentContext();
    hud().init(window);

    return true;
}

void CarGame::update(float deltaTime) {
    handleInput();

    // Fixed timestep update
    accumulator += deltaTime;
    while (accumulator >= STEP) {
        car->fixedUpdate(STEP);
        world->Step(STEP, V_IT, P_IT);
        telemetry->update(
            STEP,
            car->getPosition(),
            car->getSpeed(),
            car->getSteer(),
            car->getThrottle(),
            car->getBrake()
        );
        accumulator -= STEP;
    }

    updateCamera();
}

void CarGame::render() {
    // На всякий случай обновим viewport под текущий framebuffer
    if (GLFWwindow* ctx = glfwGetCurrentContext()) {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(ctx, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
    }

    // Clear screen
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Render track background
    renderTrackBackground();

    // Render physics debug info if enabled
    if (showPhysics) {
        // Debug draw Box2D в координатах мира с матрицей камеры
        box2dDebugDraw().currentMVP = projection;
        debugRenderer().beginFrame();
        world->DebugDraw();
    } else {
        // Фоллбек: тестовый треугольник
        renderer().draw();
    }

    // HUD поверх мира
    if (showHud) {
        hud().newFrame();

        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("Telemetry / Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        const glm::vec2 pos = car ? car->getPosition() : glm::vec2(0.0f);
        const float speed = car ? car->getSpeed() : 0.0f;
        const float yawDeg = car ? (car->getAngleRad() * 180.0f / static_cast<float>(M_PI)) : 0.0f;

        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::Text("Pos: (%.2f, %.2f)", pos.x, pos.y);
        ImGui::Text("Yaw: %.1f deg", yawDeg);
        ImGui::Text("Speed: %.2f m/s", speed);

        if (car) {
            ImGui::Text("Input: T=%.2f B=%.2f S=%.2f HB=%s",
                        car->getThrottle(), car->getBrake(), car->getSteer(),
                        car->isHandbrake() ? "ON" : "OFF");
            ImGui::Text("Mu: %.2f | Wheelbase: %.2f", car->getMuSurface(), car->getParams().wheelbase);
        }

        if (telemetry) {
            ImGui::Separator();
            ImGui::Text("Lap: %.3fs (last: %.3fs)", telemetry->getCurrentLapTime(), telemetry->getLastLapTime());
            ImGui::Text("Sector: %d | sector time: %.3fs", telemetry->getCurrentSector(), telemetry->getCurrentSectorTime());
            ImGui::Text("Tire temp: %.1f C", telemetry->getTireTempC());
        }

        ImGui::Separator();
        ImGui::Checkbox("Show physics (F2)", &showPhysics);
        ImGui::Checkbox("Show HUD (F1)", &showHud);
        if (ImGui::Button("Reset (R)")) {
            if (car) car->reset(glm::vec2(0.0f, 0.0f));
            if (telemetry) telemetry->reset();
        }
        ImGui::End();

        hud().render();
    }
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

    // Get control inputs
    float throttle = 0.0f;
    float brake = 0.0f;
    float steer = 0.0f;
    bool handbrake = false;

    if (glfwGetKey(currentWindow, GLFW_KEY_W) == GLFW_PRESS ||
        glfwGetKey(currentWindow, GLFW_KEY_UP) == GLFW_PRESS) {
        throttle = 1.0f;
    }
    if (glfwGetKey(currentWindow, GLFW_KEY_S) == GLFW_PRESS ||
        glfwGetKey(currentWindow, GLFW_KEY_DOWN) == GLFW_PRESS) {
        brake = 1.0f;
    }

    if (glfwGetKey(currentWindow, GLFW_KEY_A) == GLFW_PRESS ||
        glfwGetKey(currentWindow, GLFW_KEY_LEFT) == GLFW_PRESS) {
        steer += 1.0f;
    }
    if (glfwGetKey(currentWindow, GLFW_KEY_D) == GLFW_PRESS ||
        glfwGetKey(currentWindow, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        steer -= 1.0f;
    }

    handbrake = glfwGetKey(currentWindow, GLFW_KEY_SPACE) == GLFW_PRESS;

    car->setControls(throttle, brake, steer, handbrake);

    // Reset car on 'R' press
    static bool rPressed = false;
    bool rCurrentlyPressed = glfwGetKey(currentWindow, GLFW_KEY_R) == GLFW_PRESS;
    if (rCurrentlyPressed && !rPressed) {
        car->reset(glm::vec2(0.0f, 0.0f));
        telemetry->reset();
    }
    rPressed = rCurrentlyPressed;
}

void CarGame::resize(int width, int height) {
    // Update aspect ratio if needed
    // For now, we keep the same orthographic projection regardless of window size
}

void CarGame::shutdown() {
    hud().shutdown();

    if (world) {
        delete world;
        world = nullptr;
    }

    car.reset();
    telemetry.reset();
}

void CarGame::createArenaBounds() {
    float half = 60.0f;

    b2BodyDef bd;
    bd.type = b2_staticBody;
    b2Body* bounds = world->CreateBody(&bd);

    b2ChainShape loop;
    b2Vec2 vertices[] = {
        b2Vec2(-half, -half),
        b2Vec2(-half, half),
        b2Vec2(half, half),
        b2Vec2(half, -half)
    };
    loop.CreateLoop(vertices, 4);

    b2FixtureDef fd;
    fd.shape = &loop;
    fd.friction = 0.35f;
    fd.restitution = 0.25f;
    bounds->CreateFixture(&fd);
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
    glm::vec2 p = car->getPosition();
    float lerp = 0.12f;

    cameraPos.x = cameraPos.x * (1.0f - lerp) + p.x * lerp;
    cameraPos.y = cameraPos.y * (1.0f - lerp) + p.y * lerp;

    // MVP для рендера в координатах мира.
    // Важно: порядок умножения матриц в GLM — справа налево (колонно-ориентированные матрицы).
    // Нужно: clip = Ortho * ViewTranslate * worldPos.
    const glm::mat4 proj = glm::ortho(-VIEW_W / 2, VIEW_W / 2, -VIEW_H / 2, VIEW_H / 2, -1.0f, 1.0f);
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-cameraPos.x, -cameraPos.y, 0.0f));
    projection = proj * view;
}

void CarGame::renderHud() {
    // For now, just print basic info to console since we don't have text rendering implemented yet
    // In a real implementation, this would use a text rendering library like FreeType with OpenGL
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
        std::cout << "Mu: " << car->getMuSurface() << " | Wheelbase: " << car->getParams().wheelbase << std::endl;
        std::cout << "EngineF: " << car->getParams().engineForce << "N BrakeF: " << car->getParams().brakeForce << "N" << std::endl;
        std::cout << "Controls: W throttle | S brake | A/D steer | SPACE handbrake | R reset" << std::endl;
        std::cout << "================" << std::endl;

        lastPrintTime = 0.0f;
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