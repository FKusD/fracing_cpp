#pragma once

#include <memory>

// Forward declaration вместо подключения GLFW и игры здесь,
// чтобы избежать лишних зависимостей и конфликтов gl.h.
struct GLFWwindow;
class CarGame;

class Application {
public:
    Application();
    ~Application();

    bool initialize();
    void run();
    void shutdown();

private:
    GLFWwindow* window;
    std::unique_ptr<CarGame> game;

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    void handleInput();
};