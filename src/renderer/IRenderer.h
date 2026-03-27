#pragma once

#include <memory>
#include <string>

// Forward declarations
struct SDL_Window;

namespace skia_renderer {

// Backend type enumeration
enum class BackendType {
    Vulkan,     // Graphite Vulkan backend (default)
    OpenGL,     // Ganesh OpenGL backend (native)
    ANGLE       // Ganesh OpenGL ES via ANGLE (translates to Vulkan/D3D11/Metal)
};

// ANGLE backend type enumeration
enum class AngleBackendType {
    Auto,       // Auto-detect (try Vulkan first, then D3D11, then OpenGL)
    Vulkan,     // ANGLE Vulkan backend
    D3D11,      // ANGLE Direct3D 11 backend (Windows only)
    D3D9,       // ANGLE Direct3D 9 backend (Windows only)
    Metal,      // ANGLE Metal backend (macOS/iOS only)
    OpenGL,     // ANGLE OpenGL backend
    OpenGLES    // ANGLE OpenGL ES backend
};

// Backend configuration
struct BackendConfig {
    BackendType type = BackendType::Vulkan;
    int vulkanMajor = 1;
    int vulkanMinor = 3;
    int glMajor = 3;      // OpenGL 3.3 Core
    int glMinor = 3;
    int angleMajor = 3;   // OpenGL ES 3.0 (ANGLE)
    int angleMinor = 0;
    AngleBackendType angleBackend = AngleBackendType::Auto;  // ANGLE backend selection
    
    std::string toString() const {
        switch (type) {
            case BackendType::Vulkan:
                return "Vulkan " + std::to_string(vulkanMajor) + "." + std::to_string(vulkanMinor);
            case BackendType::OpenGL:
                return "OpenGL " + std::to_string(glMajor) + "." + std::to_string(glMinor);
            case BackendType::ANGLE:
                return "ANGLE (OpenGL ES " + std::to_string(angleMajor) + "." + std::to_string(angleMinor) + ")";
        }
        return "Unknown";
    }
    
    std::string getAngleBackendString() const {
        switch (angleBackend) {
            case AngleBackendType::Auto:     return "Auto";
            case AngleBackendType::Vulkan:   return "Vulkan";
            case AngleBackendType::D3D11:    return "D3D11";
            case AngleBackendType::D3D9:     return "D3D9";
            case AngleBackendType::Metal:    return "Metal";
            case AngleBackendType::OpenGL:   return "OpenGL";
            case AngleBackendType::OpenGLES: return "OpenGL ES";
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
