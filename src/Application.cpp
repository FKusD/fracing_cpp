#include "../include/Application.h"
#include "../include/CarGame.h"
#include <iostream>

#include <chrono>

// GLAD для загрузки OpenGL-функций
#include <glad/gl.h>
// Запрещаем GLFW тянуть системный GL/gl.h, чтобы не конфликтовать с GLAD.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h> // Нужно для проверки WantCaptureMouse

#include "imgui_impl_glfw.h"

// Глобальный указатель, чтобы статические функции видели редактор
static TrackEditor* g_editorPtr = nullptr;

static void getCursorPosFramebuffer(GLFWwindow* window, double* outX, double* outY) {
    double x, y;
    glfwGetCursorPos(window, &x, &y);

    int winW, winH;
    int fbW, fbH;
    glfwGetWindowSize(window, &winW, &winH);
    glfwGetFramebufferSize(window, &fbW, &fbH);

    const float sx = (winW > 0) ? (float)fbW / (float)winW : 1.0f;
    const float sy = (winH > 0) ? (float)fbH / (float)winH : 1.0f;

    *outX = x * sx;
    *outY = y * sy;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    // 1. Принудительно отдаем событие в бэкенд ImGui
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    // 2. Если мышь над окном ImGui — выходим, не давая редактору поставить точку
    if (ImGui::GetIO().WantCaptureMouse) return;

    // 3. Если мышь в «мире» — работает редактор
    if (g_editorPtr) {
        double x, y;
        getCursorPosFramebuffer(window, &x, &y);
        g_editorPtr->handleMouseButton(button, action, x, y);
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    // Сообщаем ImGui, куда двинулась мышь
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

    if (g_editorPtr) {
        // Приводим координаты к framebuffer space, чтобы совпадало с glViewport
        int winW, winH;
        int fbW, fbH;
        glfwGetWindowSize(window, &winW, &winH);
        glfwGetFramebufferSize(window, &fbW, &fbH);

        const float sx = (winW > 0) ? (float)fbW / (float)winW : 1.0f;
        const float sy = (winH > 0) ? (float)fbH / (float)winH : 1.0f;

        g_editorPtr->handleMouseMove(xpos * sx, ypos * sy);
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    // СНАЧАЛА ImGui
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

    if (ImGui::GetIO().WantCaptureMouse) return;
    if (g_editorPtr) g_editorPtr->handleScroll(yoffset);
}


void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // СНАЧАЛА всегда отдаем событие ImGui
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

    // Если ImGui сейчас вводит текст — НЕ трогаем редактор
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    if (g_editorPtr) g_editorPtr->handleKeyPress(key, action);
}

void char_callback(GLFWwindow* window, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(window, c);
}


Application::Application() : window(nullptr), game(nullptr) {}

Application::~Application() {
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

bool Application::initialize() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(1280, 720, "2D Car Sim (Box2D)", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetWindowUserPointer(window, this);


#ifndef __APPLE__
    if (gladLoadGL((GLADloadfunc)glfwGetProcAddress) == 0) {
        std::cerr << "Failed to initialize OpenGL loader!" << std::endl;
        return false;
    }
#endif

    glfwSwapInterval(1);

    game = std::make_unique<CarGame>();
    if (!game->initialize()) {
        std::cerr << "Failed to initialize game!" << std::endl;
        return false;
    }

    g_editorPtr = game->getTrackEditor();
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);


    // Инициализируем viewport/editor viewport корректно сразу
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    framebufferSizeCallback(window, fbW, fbH);

    return true;
}

void Application::run() {
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        handleInput();
        game->update(deltaTime);
        game->render();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void Application::handleInput() {
    static bool escPressed = false;
    bool escNow = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;

    if (escNow && !escPressed) {
        if (game->isEditorMode()) {
            game->setEditorMode(false);
        } else {
            glfwSetWindowShouldClose(window, true);
        }
    }
    escPressed = escNow;

    game->handleInput();
}

void Application::shutdown() {
    game->shutdown();
    game.reset();
}

void Application::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);

    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app && app->game) {
        app->game->resize(width, height);
        if (app->game->getTrackEditor()) {
            app->game->getTrackEditor()->setViewportSize(width, height);
        }
    }
}
