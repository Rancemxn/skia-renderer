#pragma once

#include "renderer/IRenderer.h"
#include <memory>
#include <string>
#include <cstdint>

namespace skia_renderer {

class IRenderer;
class VulkanContext;

// Vulkan version configuration (only used for Vulkan backend)
struct VulkanVersionConfig {
    int major = 1;
    int minor = 3;  // Default to Vulkan 1.3
    
    uint32_t toVkVersion() const;
    
    static VulkanVersionConfig fromVkVersion(uint32_t version);
    
    std::string toString() const;
};

class Application {
public:
    Application(const std::string& title, int width, int height, 
                const BackendConfig& backendConfig = {});
    ~Application();

    // Delete copy and move
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    bool initialize();
    void run();
    void shutdown();

private:
    void processEvents();
    void update(float deltaTime);
    void render();
    
    bool initializeVulkanBackend();
    bool initializeOpenGLBackend();
    bool initializeANGLEBackend();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace skia_renderer
