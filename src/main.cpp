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

// Helper to parse OpenGL version string (e.g., "3.3", "4.6", "4.1")
struct GLVersion { int major; int minor; };
GLVersion parseGLVersion(const std::string& str) {
    GLVersion version{3, 3};  // Default to OpenGL 3.3
    
    // Find the dot separator
    size_t dotPos = str.find('.');
    if (dotPos != std::string::npos) {
        try {
            version.major = std::stoi(str.substr(0, dotPos));
            version.minor = std::stoi(str.substr(dotPos + 1));
        } catch (...) {
            // Invalid format, use defaults
            version.major = 3;
            version.minor = 3;
        }
    } else {
        // No dot, try to parse as single number (e.g., "4" -> 4.0)
        try {
            version.major = std::stoi(str);
            version.minor = 0;
        } catch (...) {
            version.major = 3;
            version.minor = 3;
        }
    }
    
    // Clamp to valid OpenGL versions (2.0 - 4.6)
    if (version.major < 2) version.major = 2;
    if (version.major > 4) version.major = 4;
    if (version.major == 4 && version.minor > 6) version.minor = 6;
    if (version.minor > 9) version.minor = 9;
    
    return version;
}

// Helper to parse backend string
skia_renderer::BackendType parseBackend(const std::string& str) {
    std::string lower = str;
    for (auto& c : lower) c = std::tolower(c);
    
    if (lower == "opengl" || lower == "gl" || lower == "ganesh") {
        return skia_renderer::BackendType::OpenGL;
    }
    if (lower == "angle" || lower == "es" || lower == "gles") {
        return skia_renderer::BackendType::ANGLE;
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
    std::string glVersionStr = "4.6";
    std::string backendStr = "vulkan";
    
    app.add_option("-W,--width", width, "Window width")->check(CLI::Range(100, 7680));
    app.add_option("-H,--height", height, "Window height")->check(CLI::Range(100, 4320));
    app.add_flag("-v,--verbose", verbose, "Enable debug logging");
    app.add_flag("--version", show_version, "Print version");
    
    // Backend selection
    app.add_option("-b,--backend", backendStr, 
                   "Rendering backend: 'vulkan' (Graphite), 'opengl' (Ganesh), or 'angle' (OpenGL ES via ANGLE)");
    
    // Vulkan-specific options
    app.add_option("--vulkan-version", vulkanVersionStr, 
                   "Vulkan API version (e.g., 1.3, 1.2, 1.1). Auto-downgrades if not available.");
    
    // OpenGL-specific options
    app.add_option("--gl-version", glVersionStr, 
                   "OpenGL version (e.g., 3.3, 4.1, 4.6). Default: 3.3");
    
    // Parse command line
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    
    // Handle --version early
    if (show_version) {
        std::cout << "Skia Renderer v1.2.0" << std::endl;
        std::cout << "Supported backends: Vulkan (Graphite), OpenGL (Ganesh), ANGLE (OpenGL ES)" << std::endl;
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
    } else if (backendConfig.type == skia_renderer::BackendType::ANGLE) {
        auto glVersion = parseGLVersion(glVersionStr);
        backendConfig.angleMajor = glVersion.major;
        backendConfig.angleMinor = glVersion.minor;
    } else {
        auto glVersion = parseGLVersion(glVersionStr);
        backendConfig.glMajor = glVersion.major;
        backendConfig.glMinor = glVersion.minor;
    }
    
    // ========================================
    // Startup Info
    // ========================================
    LOG_INFO("Skia Renderer v1.2.0");
    LOG_INFO("========================");
    LOG_INFO("Window size: {}x{}", width, height);
    LOG_INFO("Backend: {}", backendConfig.toString());
    
    if (backendConfig.type == skia_renderer::BackendType::Vulkan) {
        LOG_INFO("Requested Vulkan version: {}.{}", backendConfig.vulkanMajor, backendConfig.vulkanMinor);
    } else if (backendConfig.type == skia_renderer::BackendType::ANGLE) {
        LOG_INFO("Requested ANGLE OpenGL ES version: {}.{}", backendConfig.angleMajor, backendConfig.angleMinor);
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
