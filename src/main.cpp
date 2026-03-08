#include "core/Application.h"

#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // Default window size
    int width = 1280;
    int height = 720;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --width <pixels>   Window width (default: 1280)\n"
                      << "  --height <pixels>  Window height (default: 720)\n"
                      << "  --help             Show this help message\n";
            return 0;
        }
    }

    std::cout << "Skia Graphite Renderer\n";
    std::cout << "========================\n";
    std::cout << "Initializing with window size: " << width << "x" << height << std::endl;

    skia_renderer::Application app("Skia Graphite Renderer", width, height);

    if (!app.initialize()) {
        std::cerr << "Failed to initialize application!" << std::endl;
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
