#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace skia_renderer {

/**
 * @brief OpenGL context manager
 * 
 * Manages SDL OpenGL context and provides OpenGL-related functionality
 * for the Ganesh backend.
 */
class GLContext {
public:
    GLContext();
    ~GLContext();

    // Delete copy and move
    GLContext(const GLContext&) = delete;
    GLContext& operator=(const GLContext&) = delete;
    GLContext(GLContext&&) = delete;
    GLContext& operator=(GLContext&&) = delete;

    /**
     * @brief Initialize OpenGL context
     * @param window SDL window
     * @param majorVersion OpenGL major version (e.g., 3)
     * @param minorVersion OpenGL minor version (e.g., 3)
     * @return true if initialization succeeded
     */
    bool initialize(SDL_Window* window, int majorVersion = 3, int minorVersion = 3);

    /**
     * @brief Shutdown OpenGL context
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
     * @brief Get SDL GL context
     */
    SDL_GLContext getGLContext() const { return m_glContext; }

    /**
     * @brief Check if context is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get OpenGL version string
     */
    std::string getGLVersionString() const;

    /**
     * @brief Get OpenGL renderer string
     */
    std::string getGLRendererString() const;

    /**
     * @brief Get OpenGL vendor string
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

private:
    bool setupGLAttributes(int majorVersion, int minorVersion);
    bool verifyGLVersion(int majorVersion, int minorVersion);

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;
    bool m_initialized = false;
    int m_width = 0;
    int m_height = 0;
    int m_glMajorVersion = 0;
    int m_glMinorVersion = 0;
};

} // namespace skia_renderer
