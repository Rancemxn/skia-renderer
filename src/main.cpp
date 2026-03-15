#include "core/Application.h"
#include "core/Logger.h"

#include <CLI/CLI.hpp>

#include <iostream>

int main(int argc, char* argv[]) {
    // ========================================
    // CLI11 Setup
    // NOTE: -h is reserved for --help in CLI11
    // Use -W for width, -H for height
    // ========================================
    CLI::App app{"Skia Graphite Renderer - A Vulkan-based rendering engine"};
    
    int width = 1280;
    int height = 720;
    bool verbose = false;
    bool show_version = false;
    
    // -W for width (not -w, to avoid conflicts on Windows)
    app.add_option("-W,--width", width, "Window width")->check(CLI::Range(100, 7680));
    // -H for height (not -h, which is reserved for --help)
    app.add_option("-H,--height", height, "Window height")->check(CLI::Range(100, 4320));
    app.add_flag("-v,--verbose", verbose, "Enable debug logging");
    app.add_flag("--version", show_version, "Print version");
    
    // Parse command line
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    
    // Handle --version early
    if (show_version) {
        std::cout << "Skia Graphite Renderer v1.0.0" << std::endl;
        return 0;
    }
    
    // ========================================
    // Initialize Logger
    // ========================================
    skia_renderer::Logger::init();
    
    if (verbose) {
        skia_renderer::Logger::set_level(spdlog::level::debug);
    }
    
    // ========================================
    // Startup Info
    // ========================================
    LOG_INFO("Skia Graphite Renderer v1.0.0");
    LOG_INFO("========================");
    LOG_INFO("Window size: {}x{}", width, height);
    
    if (verbose) {
        LOG_DEBUG("Verbose mode enabled");
    }

    // ========================================
    // Run Application
    // ========================================
    skia_renderer::Application application("Skia Graphite Renderer", width, height);

    if (!application.initialize()) {
        LOG_ERROR("Failed to initialize application!");
        return 1;
    }

    application.run();
    application.shutdown();

    LOG_INFO("Application terminated successfully");
    return 0;
}
