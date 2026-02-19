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

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    // 1. Принудительно отдаем событие в бэкенд ImGui
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    // 2. Если мышь над окном ImGui — выходим, не давая редактору поставить точку
    if (ImGui::GetIO().WantCaptureMouse) return;

    // 3. Если мышь в «мире» — работает редактор
    if (g_editorPtr) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        g_editorPtr->handleMouseButton(button, action, x, y);
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    // Сообщаем ImGui, куда двинулась мышь
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

    if (g_editorPtr) g_editorPtr->handleMouseMove(xpos, ypos);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window; (void)xoffset;
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (g_editorPtr) g_editorPtr->handleScroll(yoffset);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    // Даем ImGui обработать клавиши (например, ввод текста в поле сохранения)
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (g_editorPtr) g_editorPtr->handleKeyPress(key, action);
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

    // Инициализация загрузчика OpenGL (GLAD)
    #ifndef __APPLE__
        if (gladLoadGL((GLADloadfunc)glfwGetProcAddress) == 0) {
            std::cerr << "Failed to initialize OpenGL loader!" << std::endl;
            return false;
        }
    #endif

    // Enable vsync
    glfwSwapInterval(1);

    // Create the game instance
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

    return true;
}

void Application::run() {
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        // if (ImGui::GetIO().WantCaptureMouse) std::cout << "ImGui catches mouse" << std::endl;
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
    // Используем static bool, чтобы кнопка не "дребезжала" (срабатывала 60 раз в секунду)
    static bool escPressed = false;
    bool escNow = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;

    if (escNow && !escPressed) {
        // Логика переключения
        if (game->isEditorMode()) {
            game->setEditorMode(false); // Выход из редактора в игру
        } else {
            // Если мы в игре — открываем меню или ставим паузу
            // game->togglePauseMenu();
            // А закрываем только если меню уже открыто и жмем еще раз:
            glfwSetWindowShouldClose(window, true); // Временное решение
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
    // Notify game about resize
    // reinterpret_cast<CarGame*>(glfwGetWindowUserPointer(window))->resize(width, height);
}