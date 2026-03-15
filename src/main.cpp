#include "core/Application.h"
#include "core/Logger.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <string>
#include <sstream>

// Helper to parse Vulkan version string (e.g., "1.3", "1.1")
skia_renderer::VulkanVersionConfig parseVulkanVersion(const std::string& str) {
    skia_renderer::VulkanVersionConfig config;
    
    // Find the dot separator
    size_t dotPos = str.find('.');
    if (dotPos != std::string::npos) {
        try {
            config.major = std::stoi(str.substr(0, dotPos));
            config.minor = std::stoi(str.substr(dotPos + 1));
        } catch (...) {
            // Invalid format, use defaults
            config.major = 1;
            config.minor = 3;
        }
    } else {
        // No dot, try to parse as single number
        try {
            int val = std::stoi(str);
            config.major = val / 10;
            config.minor = val % 10;
        } catch (...) {
            config.major = 1;
            config.minor = 3;
        }
    }
    
    // Clamp to valid range
    if (config.major < 1) config.major = 1;
    if (config.minor < 1) config.minor = 1;
    if (config.minor > 3) config.minor = 3;
    
    return config;
}

int main(int argc, char* argv[]) {
    // ========================================
    // CLI11 Setup
    // ========================================
    CLI::App app{"Skia Graphite Renderer - A Vulkan-based rendering engine"};
    
    int width = 1280;
    int height = 720;
    bool verbose = false;
    bool show_version = false;
    std::string vulkanVersionStr = "1.3";
    
    app.add_option("-W,--width", width, "Window width")->check(CLI::Range(100, 7680));
    app.add_option("-H,--height", height, "Window height")->check(CLI::Range(100, 4320));
    app.add_flag("-v,--verbose", verbose, "Enable debug logging");
    app.add_flag("--version", show_version, "Print version");
    app.add_option("--vulkan-version", vulkanVersionStr, 
                   "Vulkan API version (e.g., 1.3, 1.2, 1.1). Auto-downgrades if not available.");
    
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
    // Parse Vulkan Version
    // ========================================
    auto vulkanVersion = parseVulkanVersion(vulkanVersionStr);
    
    // ========================================
    // Startup Info
    // ========================================
    LOG_INFO("Skia Graphite Renderer v1.0.0");
    LOG_INFO("========================");
    LOG_INFO("Window size: {}x{}", width, height);
    LOG_INFO("Requested Vulkan version: {}.{}", vulkanVersion.major, vulkanVersion.minor);
    if (verbose) {
        LOG_DEBUG("Verbose mode enabled");
    }

    // ========================================
    // Run Application
    // ========================================
    skia_renderer::Application application(
        "Skia Graphite Renderer", 
        width, 
        height,
        vulkanVersion
    );

    if (!application.initialize()) {
        LOG_ERROR("Failed to initialize application!");
        return 1;
    }

    application.run();
    application.shutdown();

    LOG_INFO("Application terminated successfully");
    return 0;
}
