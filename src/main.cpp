#include "core/Application.h"
#include "core/Logger.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Initialize logger
    skia_renderer::Logger::init();
    
    // CLI11 application setup
    CLI::App app{"Skia Graphite Renderer - A Vulkan-based rendering engine using Skia"};
    
    // Window options
    int width = 1280;
    int height = 720;
    app.add_option("-w,--width", width, "Window width in pixels")->check(CLI::Range(100, 7680));
    app.add_option("-h,--height", height, "Window height in pixels")->check(CLI::Range(100, 4320));
    
    // Verbose logging
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Enable verbose (debug) logging");
    
    // Version flag
    std::string version = "1.0.0";
    app.add_flag_function("--version", [&version](size_t) {
        fmt::println("Skia Graphite Renderer v{}", version);
        throw CLI::Success();
    }, "Print version information");
    
    // Parse command line
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    
    // Configure logger level based on verbose flag
    if (verbose) {
        spdlog::set_level(spdlog::level::debug);
    }
    
    // Print startup info
    LOG_INFO("Skia Graphite Renderer v{}", version);
    LOG_INFO("========================");
    LOG_INFO("Initializing with window size: {}x{}", width, height);
    LOG_INFO("Verbose logging: {}", verbose ? "enabled" : "disabled");

    // Create and run application
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
