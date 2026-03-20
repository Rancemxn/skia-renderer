#include "core/Application.h"
#include "core/Logger.h"
#include "renderer/IRenderer.h"

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

// Helper to parse backend string
skia_renderer::BackendType parseBackend(const std::string& str) {
    std::string lower = str;
    for (auto& c : lower) c = std::tolower(c);
    
    if (lower == "opengl" || lower == "gl" || lower == "ganesh") {
        return skia_renderer::BackendType::OpenGL;
    }
    // Default to Vulkan
    return skia_renderer::BackendType::Vulkan;
}

int main(int argc, char* argv[]) {
    // ========================================
    // CLI11 Setup
    // ========================================
    CLI::App app{"Skia Renderer - A cross-backend rendering engine (Vulkan/OpenGL)"};
    
    int width = 1280;
    int height = 720;
    bool verbose = false;
    bool show_version = false;
    std::string vulkanVersionStr = "1.3";
    std::string backendStr = "vulkan";
    int glMajor = 3;
    int glMinor = 3;
    
    app.add_option("-W,--width", width, "Window width")->check(CLI::Range(100, 7680));
    app.add_option("-H,--height", height, "Window height")->check(CLI::Range(100, 4320));
    app.add_flag("-v,--verbose", verbose, "Enable debug logging");
    app.add_flag("--version", show_version, "Print version");
    
    // Backend selection
    app.add_option("-b,--backend", backendStr, 
                   "Rendering backend: 'vulkan' (Graphite) or 'opengl' (Ganesh)");
    
    // Vulkan-specific options
    app.add_option("--vulkan-version", vulkanVersionStr, 
                   "Vulkan API version (e.g., 1.3, 1.2, 1.1). Auto-downgrades if not available.");
    
    // OpenGL-specific options
    app.add_option("--gl-major", glMajor, 
                   "OpenGL major version (default: 3)")->check(CLI::Range(2, 4));
    app.add_option("--gl-minor", glMinor, 
                   "OpenGL minor version (default: 3)")->check(CLI::Range(0, 9));
    
    // Parse command line
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    
    // Handle --version early
    if (show_version) {
        std::cout << "Skia Renderer v1.1.0" << std::endl;
        std::cout << "Supported backends: Vulkan (Graphite), OpenGL (Ganesh)" << std::endl;
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
    // Parse Backend Configuration
    // ========================================
    skia_renderer::BackendConfig backendConfig;
    backendConfig.type = parseBackend(backendStr);
    
    if (backendConfig.type == skia_renderer::BackendType::Vulkan) {
        auto vulkanVersion = parseVulkanVersion(vulkanVersionStr);
        backendConfig.vulkanMajor = vulkanVersion.major;
        backendConfig.vulkanMinor = vulkanVersion.minor;
    } else {
        backendConfig.glMajor = glMajor;
        backendConfig.glMinor = glMinor;
    }
    
    // ========================================
    // Startup Info
    // ========================================
    LOG_INFO("Skia Renderer v1.1.0");
    LOG_INFO("========================");
    LOG_INFO("Window size: {}x{}", width, height);
    LOG_INFO("Backend: {}", backendConfig.toString());
    
    if (backendConfig.type == skia_renderer::BackendType::Vulkan) {
        LOG_INFO("Requested Vulkan version: {}.{}", backendConfig.vulkanMajor, backendConfig.vulkanMinor);
    } else {
        LOG_INFO("Requested OpenGL version: {}.{}", backendConfig.glMajor, backendConfig.glMinor);
    }
    
    if (verbose) {
        LOG_DEBUG("Verbose mode enabled");
    }

    // ========================================
    // Run Application
    // ========================================
    std::string windowTitle = std::string("Skia Renderer - ") + backendConfig.toString();
    
    skia_renderer::Application application(
        windowTitle, 
        width, 
        height,
        backendConfig
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
