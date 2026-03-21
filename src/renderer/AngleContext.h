#pragma once

#ifdef USE_ANGLE

#include <SDL3/SDL.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <memory>
#include <string>

namespace skia_renderer {

/**
 * @brief ANGLE EGL context manager
 * 
 * Manages ANGLE EGL context and provides OpenGL ES functionality
 * for the Ganesh backend via ANGLE translation layer.
 * 
 * ANGLE translates OpenGL ES calls to:
 * - Vulkan (Windows, Linux, macOS)
 * - Direct3D 11 (Windows)
 * - Metal (macOS, iOS)
 */
class AngleContext {
public:
    AngleContext();
    ~AngleContext();

    // Delete copy and move
    AngleContext(const AngleContext&) = delete;
    AngleContext& operator=(const AngleContext&) = delete;
    AngleContext(AngleContext&&) = delete;
    AngleContext& operator=(AngleContext&&) = delete;

    /**
     * @brief Initialize ANGLE EGL context
     * @param window SDL window
     * @param majorVersion OpenGL ES major version (e.g., 3)
     * @param minorVersion OpenGL ES minor version (e.g., 0)
     * @return true if initialization succeeded
     */
    bool initialize(SDL_Window* window, int majorVersion = 3, int minorVersion = 0);

    /**
     * @brief Shutdown ANGLE context
     */
    void shutdown();

    /**
     * @brief Swap buffers (present)
     */
    void swapBuffers();

    /**
     * @brief Make context current
     */
    void makeCurrent();

    /**
     * @brief Set vertical sync
     * @param enable true to enable VSync
     */
    void setVSync(bool enable);

    /**
     * @brief Get SDL window
     */
    SDL_Window* getWindow() const { return m_window; }

    /**
     * @brief Get EGL display
     */
    EGLDisplay getDisplay() const { return m_display; }

    /**
     * @brief Get EGL context
     */
    EGLContext getContext() const { return m_context; }

    /**
     * @brief Get EGL surface
     */
    EGLSurface getSurface() const { return m_surface; }

    /**
     * @brief Check if context is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get OpenGL ES version string
     */
    std::string getGLVersionString() const;

    /**
     * @brief Get OpenGL ES renderer string
     */
    std::string getGLRendererString() const;

    /**
     * @brief Get OpenGL ES vendor string
     */
    std::string getGLVendorString() const;

    /**
     * @brief Get current framebuffer ID (usually 0 for default)
     */
    int getCurrentFramebuffer() const;

    /**
     * @brief Get window width
     */
    int getWidth() const { return m_width; }

    /**
     * @brief Get window height
     */
    int getHeight() const { return m_height; }

    /**
     * @brief Handle resize
     */
    void resize(int width, int height);

    /**
     * @brief Get the underlying ANGLE backend type
     * @return String describing the backend (Vulkan, D3D11, Metal, etc.)
     */
    std::string getAngleBackendString() const;

private:
    bool chooseConfig();
    bool createContext(int majorVersion, int minorVersion);
    bool createSurface();
    bool verifyGLVersion(int majorVersion, int minorVersion);
    void logEGLConfig();

    SDL_Window* m_window = nullptr;
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    EGLConfig m_config = nullptr;
    bool m_initialized = false;
    int m_width = 0;
    int m_height = 0;
    int m_glMajorVersion = 0;
    int m_glMinorVersion = 0;
};

} // namespace skia_renderer

#endif // USE_ANGLE
