// Disable CRT secure warnings on MSVC/Clang-cl
#if defined(_MSC_VER) || defined(__MINGW32__)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GLContext.h"
#include "core/Logger.h"

#include <SDL3/SDL_opengl.h>
#include <cstdio>

namespace skia_renderer {

GLContext::GLContext() = default;

GLContext::~GLContext() {
    if (m_initialized) {
        shutdown();
    }
}

bool GLContext::setupGLAttributes(int majorVersion, int minorVersion) {
    // Set OpenGL attributes before creating context
    
    // Request Core Profile (required for OpenGL 3.2+)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    
    // Set OpenGL version
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, majorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minorVersion);
    
    // Enable double buffering
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
    // Set depth buffer size
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    
    // Set stencil buffer size (useful for some Skia operations)
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    // Request RGBA with 8 bits per channel
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    
#if defined(__linux__) && !defined(__ANDROID__)
    // On Linux, prefer EGL over GLX for better compatibility with modern systems
    // SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1);
#endif

    LOG_INFO("  OpenGL attributes set: {}.{} Core Profile", majorVersion, minorVersion);
    return true;
}

bool GLContext::verifyGLVersion(int majorVersion, int minorVersion) {
    // Get actual OpenGL version
    const char* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!versionStr) {
        LOG_ERROR("  Failed to get OpenGL version string");
        return false;
    }

    // Parse version string
    int actualMajor = 0, actualMinor = 0;
    if (sscanf(versionStr, "%d.%d", &actualMajor, &actualMinor) < 2) {
        LOG_WARN("  Could not parse OpenGL version: {}", versionStr);
        return true; // Continue anyway
    }

    m_glMajorVersion = actualMajor;
    m_glMinorVersion = actualMinor;

    LOG_INFO("  OpenGL version: {}.{}", actualMajor, actualMinor);

    // Verify we got at least the requested version
    if (actualMajor < majorVersion || 
        (actualMajor == majorVersion && actualMinor < minorVersion)) {
        LOG_WARN("  Requested OpenGL {}.{} but got {}.{}", 
                 majorVersion, minorVersion, actualMajor, actualMinor);
        // Continue anyway - Skia may still work
    }

    return true;
}

bool GLContext::initialize(SDL_Window* window, int majorVersion, int minorVersion) {
    if (m_initialized) {
        LOG_WARN("GLContext already initialized");
        return true;
    }

    LOG_INFO("Initializing OpenGL context...");

    m_window = window;

    // Get window size
    SDL_GetWindowSize(window, &m_width, &m_height);

    // Setup OpenGL attributes
    if (!setupGLAttributes(majorVersion, minorVersion)) {
        LOG_ERROR("  Failed to setup OpenGL attributes");
        return false;
    }

    // Create OpenGL context
    m_glContext = SDL_GL_CreateContext(window);
    if (!m_glContext) {
        LOG_ERROR("  Failed to create OpenGL context: {}", SDL_GetError());
        return false;
    }

    // Make context current
    if (!SDL_GL_MakeCurrent(window, m_glContext)) {
        LOG_ERROR("  Failed to make OpenGL context current: {}", SDL_GetError());
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
        return false;
    }

    // Enable VSync by default
    setVSync(true);

    // Verify OpenGL version
    if (!verifyGLVersion(majorVersion, minorVersion)) {
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
        return false;
    }

    // Log OpenGL info
    LOG_INFO("  OpenGL Vendor: {}", getGLVendorString());
    LOG_INFO("  OpenGL Renderer: {}", getGLRendererString());

    m_initialized = true;
    LOG_INFO("OpenGL context initialized ({}x{})", m_width, m_height);
    return true;
}

void GLContext::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down OpenGL context...");

    if (m_glContext) {
        SDL_GL_MakeCurrent(nullptr, nullptr);
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
    }

    m_window = nullptr;
    m_initialized = false;
    LOG_INFO("OpenGL context shut down.");
}

void GLContext::swapBuffers() {
    if (m_window && m_glContext) {
        SDL_GL_SwapWindow(m_window);
    }
}

void GLContext::makeCurrent() {
    if (m_window && m_glContext) {
        SDL_GL_MakeCurrent(m_window, m_glContext);
    }
}

void GLContext::setVSync(bool enable) {
    int result = SDL_GL_SetSwapInterval(enable ? 1 : 0);
    if (result != 0) {
        LOG_WARN("  Failed to set VSync: {}", SDL_GetError());
    }
}

std::string GLContext::getGLVersionString() const {
    if (!m_initialized) return "Not initialized";
    
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return version ? version : "Unknown";
}

std::string GLContext::getGLRendererString() const {
    if (!m_initialized) return "Not initialized";
    
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    return renderer ? renderer : "Unknown";
}

std::string GLContext::getGLVendorString() const {
    if (!m_initialized) return "Not initialized";
    
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    return vendor ? vendor : "Unknown";
}

int GLContext::getCurrentFramebuffer() const {
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    return framebuffer;
}

void GLContext::resize(int width, int height) {
    m_width = width;
    m_height = height;
    // OpenGL automatically handles window resize
    LOG_DEBUG("OpenGL context resized to {}x{}", width, height);
}

} // namespace skia_renderer
