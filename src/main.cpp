#include "core/Application.h"
#include "core/Logger.h"

// CLI11 for command-line parsing
#include <CLI/CLI.hpp>

// Standard headers
#include <iostream>
#include <cstdlib>

// Entry point
int main(int argc, char* argv[]) {
    // First thing: output to stderr to confirm program started
    // This helps debug if the crash happens before any logging
    std::cerr << "[main] Starting Skia Graphite Renderer..." << std::endl;
    
    // Wrap everything in try-catch to handle any exceptions
    try {
        // ========================================
        // CLI11 Setup
        // ========================================
        std::cerr << "[main] Setting up CLI parser..." << std::endl;
        
        CLI::App app{"Skia Graphite Renderer - A Vulkan-based rendering engine"};
        app.allow_windows_style_options();  // Allow /flag style on Windows
        
        // Window dimensions
        int width = 1280;
        int height = 720;
        app.add_option("-w,--width", width, "Window width")->check(CLI::Range(100, 7680));
        app.add_option("-h,--height", height, "Window height")->check(CLI::Range(100, 4320));
        
        // Verbose mode
        bool verbose = false;
        app.add_flag("-v,--verbose", verbose, "Enable debug logging");
        
        // Version flag
        std::cerr << "[main] Parsing arguments..." << std::endl;
        
        // Parse with explicit error handling
        try {
            app.parse(argc, argv);
        } catch (const CLI::CallForHelp& e) {
            // --help was requested
            std::cout << app.help();
            return 0;
        } catch (const CLI::ParseError& e) {
            // Parse error (invalid arguments, etc.)
            std::cerr << "Command line error: " << e.what() << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }
        
        // ========================================
        // Logger Initialization (after CLI parse)
        // ========================================
        std::cerr << "[main] Initializing logger..." << std::endl;
        
        skia_renderer::Logger::init();
        std::cerr << "[main] Logger initialized." << std::endl;
        
        // Set debug level if verbose
        if (verbose) {
            skia_renderer::Logger::set_level(spdlog::level::debug);
            std::cerr << "[main] Debug logging enabled." << std::endl;
        }
        
        // ========================================
        // Application Startup
        // ========================================
        LOG_INFO("Skia Graphite Renderer v1.0.0");
        LOG_INFO("========================");
        LOG_INFO("Window size: {}x{}", width, height);
        
        if (verbose) {
            LOG_DEBUG("Verbose mode enabled");
        }
        std::cerr << "[main] Creating application..." << std::endl;
        
        skia_renderer::Application application("Skia Graphite Renderer", width, height);
        std::cerr << "[main] Application created, initializing..." << std::endl;

        if (!application.initialize()) {
            LOG_ERROR("Failed to initialize application!");
            return 1;
        }
        std::cerr << "[main] Application initialized, running..." << std::endl;

        application.run();
        std::cerr << "[main] Application loop ended, shutting down..." << std::endl;
        
        application.shutdown();

        LOG_INFO("Application terminated successfully");
        std::cerr << "[main] Clean exit." << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR (std::exception): " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "FATAL ERROR (unknown exception)" << std::endl;
        return 1;
    }
}
