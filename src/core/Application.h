#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace skia_renderer {

class VulkanContext;
class SkiaRenderer;

// Vulkan version configuration
struct VulkanVersionConfig {
    int major = 1;
    int minor = 3;  // Default to Vulkan 1.3
    
    uint32_t toVkVersion() const {
        return VK_MAKE_API_VERSION(0, major, minor, 0);
    }
    
    static VulkanVersionConfig fromVkVersion(uint32_t version) {
        VulkanVersionConfig cfg;
        cfg.major = VK_API_VERSION_MAJOR(version);
        cfg.minor = VK_API_VERSION_MINOR(version);
        return cfg;
    }
    
    std::string toString() const {
        return std::to_string(major) + "." + std::to_string(minor);
    }
};

class Application {
public:
    Application(const std::string& title, int width, int height, 
                const VulkanVersionConfig& vulkanVersion = {});
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

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace skia_renderer
