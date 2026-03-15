#include "core/Application.h"
#include "core/Logger.h"

#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // Initialize logger
    skia_renderer::Logger::init();
    
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
            LOG_INFO("Usage: {} [options]", argv[0]);
            LOG_INFO("Options:");
            LOG_INFO("  --width <pixels>   Window width (default: 1280)");
            LOG_INFO("  --height <pixels>  Window height (default: 720)");
            LOG_INFO("  --help             Show this help message");
            return 0;
        }
    }

    LOG_INFO("Skia Graphite Renderer");
    LOG_INFO("========================");
    LOG_INFO("Initializing with window size: {}x{}", width, height);

    skia_renderer::Application app("Skia Graphite Renderer", width, height);

    if (!app.initialize()) {
        LOG_ERROR("Failed to initialize application!");
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
