#include "../include/Application.h"
#include "../include/CarGame.h"
#include <iostream>

#include <chrono>

// GLAD для загрузки OpenGL-функций
#include <glad/gl.h>
// Запрещаем GLFW тянуть системный GL/gl.h, чтобы не конфликтовать с GLAD.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // Pass input to game
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