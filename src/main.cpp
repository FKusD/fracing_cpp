#include "../include/Application.h"
#include <iostream>

int main() {
    try {
        Application app;

        if (!app.initialize()) {
            std::cerr << "Failed to initialize application!" << std::endl;
            return -1;
        }

        app.run();
        app.shutdown();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}