#pragma once

#include <memory>
#include <string>

// Forward declarations
struct SDL_Window;

namespace skia_renderer {

// Backend type enumeration
enum class BackendType {
    Vulkan,     // Graphite Vulkan backend (default)
    OpenGL      // Ganesh OpenGL backend
};

// Backend configuration
struct BackendConfig {
    BackendType type = BackendType::Vulkan;
    int vulkanMajor = 1;
    int vulkanMinor = 3;
    int glMajor = 3;      // OpenGL 3.3 Core
    int glMinor = 3;
    
    std::string toString() const {
        switch (type) {
            case BackendType::Vulkan:
                return "Vulkan " + std::to_string(vulkanMajor) + "." + std::to_string(vulkanMinor);
            case BackendType::OpenGL:
                return "OpenGL " + std::to_string(glMajor) + "." + std::to_string(glMinor);
        }
        return "Unknown";
    }
};

/**
 * @brief Abstract renderer interface
 * 
 * This interface abstracts the rendering backend, allowing switching between
 * Graphite (Vulkan) and Ganesh (OpenGL) backends at runtime.
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Delete copy and move
    IRenderer(const IRenderer&) = delete;
    IRenderer& operator=(const IRenderer&) = delete;
    IRenderer(IRenderer&&) = delete;
    IRenderer& operator=(IRenderer&&) = delete;

    /**
     * @brief Initialize the renderer
     * @param window SDL window to render to
     * @param width Initial width
     * @param height Initial height
     * @param config Backend configuration
     * @return true if initialization succeeded
     */
    virtual bool initialize(SDL_Window* window, int width, int height, const BackendConfig& config) = 0;

    /**
     * @brief Shutdown the renderer
     */
    virtual void shutdown() = 0;

    /**
     * @brief Handle resize event
     * @param width New width
     * @param height New height
     */
    virtual void resize(int width, int height) = 0;

    /**
     * @brief Render a frame
     */
    virtual void render() = 0;

    /**
     * @brief Set FPS for display
     * @param fps Current frames per second
     */
    virtual void setFPS(float fps) = 0;

    /**
     * @brief Get the backend type
     */
    virtual BackendType getBackendType() const = 0;

    /**
     * @brief Get the backend name
     */
    virtual std::string getBackendName() const = 0;

    /**
     * @brief Check if renderer is initialized
     */
    virtual bool isInitialized() const = 0;

    /**
     * @brief Begin a new frame (called before render)
     * @return true if frame can be rendered
     */
    virtual bool beginFrame() = 0;

    /**
     * @brief End the current frame (called after render)
     */
    virtual void endFrame() = 0;

protected:
    IRenderer() = default;
};

} // namespace skia_renderer
