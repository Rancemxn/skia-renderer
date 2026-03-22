// Disable CRT secure warnings on MSVC/Clang-cl
#if defined(_MSC_VER) || defined(__MINGW32__)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "AngleContext.h"
#include "core/Logger.h"

#ifdef USE_ANGLE

#include <cstdio>
#include <cstring>

namespace skia_renderer {

// Platform-specific EGL extensions and defines
#if defined(_WIN32)
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE 0x3450
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE 0x3451
#define EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE 0x3452
#define EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE 0x3453
#define EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE 0x3454
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3455
#endif

// EGL extension for context minor version (may not be defined in all headers)
#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif

AngleContext::AngleContext() = default;

AngleContext::~AngleContext() {
    if (m_initialized) {
        shutdown();
    }
}

bool AngleContext::chooseConfig() {
    // EGL config attributes for OpenGL ES 3.x
    const EGLint configAttributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_BUFFER_SIZE, 32,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(m_display, configAttributes, &m_config, 1, &numConfigs)) {
        LOG_ERROR("  Failed to choose EGL config: 0x{:X}", eglGetError());
        return false;
    }

    if (numConfigs == 0) {
        LOG_ERROR("  No matching EGL configs found");
        return false;
    }

    return true;
}

bool AngleContext::createContext(int majorVersion, int minorVersion) {
    // Context attributes for OpenGL ES 3.x
    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, majorVersion,
        EGL_CONTEXT_MINOR_VERSION_KHR, minorVersion,
        EGL_NONE
    };

    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttributes);
    if (m_context == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        LOG_ERROR("  Failed to create EGL context: 0x{:X}", error);
        
        // Try with simpler attributes (ES 2.0 fallback)
        const EGLint contextAttributesES2[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttributesES2);
        if (m_context == EGL_NO_CONTEXT) {
            LOG_ERROR("  Failed to create EGL ES 2.0 context as well");
            return false;
        }
        LOG_WARN("  Created ES 2.0 context instead of ES {}.{}", majorVersion, minorVersion);
    }

    return true;
}

bool AngleContext::createSurface() {
    // Create window surface
    m_surface = eglCreateWindowSurface(m_display, m_config, 
#if defined(_WIN32)
        reinterpret_cast<EGLNativeWindowType>(SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), "SDL.window.win32.hwnd", nullptr)),
#elif defined(__linux__)
        reinterpret_cast<EGLNativeWindowType>(SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), "SDL.window.x11.window", nullptr)),
#elif defined(__APPLE__)
        reinterpret_cast<EGLNativeWindowType>(SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), "SDL.window.cocoa.window", nullptr)),
#else
        (EGLNativeWindowType)nullptr,  // Fallback
#endif
        nullptr);

    if (m_surface == EGL_NO_SURFACE) {
        LOG_ERROR("  Failed to create EGL window surface: 0x{:X}", eglGetError());
        return false;
    }

    return true;
}

void AngleContext::logEGLConfig() {
    const char* displayVendor = eglQueryString(m_display, EGL_VENDOR);
    const char* displayVersion = eglQueryString(m_display, EGL_VERSION);
    
    LOG_INFO("  EGL Vendor: {}", displayVendor ? displayVendor : "Unknown");
    LOG_INFO("  EGL Version: {}", displayVersion ? displayVersion : "Unknown");
    
    // Log ANGLE backend type if available
    const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (clientExtensions && strstr(clientExtensions, "EGL_ANGLE_platform_angle")) {
        LOG_INFO("  ANGLE Platform: Available");
    }
}

bool AngleContext::initialize(SDL_Window* window, int majorVersion, int minorVersion) {
    if (m_initialized) {
        LOG_WARN("AngleContext already initialized");
        return true;
    }

    LOG_INFO("Initializing ANGLE EGL context...");

    m_window = window;

    // Get window size
    SDL_GetWindowSize(window, &m_width, &m_height);

#if defined(_WIN32)
    // On Windows, use ANGLE with platform selection
    // Prefer Vulkan backend, fall back to D3D11
    EGLAttrib displayAttributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
        EGL_NONE
    };
    
    // Try Vulkan backend first
    m_display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, 
        reinterpret_cast<void*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), "SDL.window.win32.hwnd", nullptr)),
        displayAttributes);
    
    if (m_display == EGL_NO_DISPLAY) {
        LOG_WARN("  ANGLE Vulkan backend not available, trying D3D11...");
        displayAttributes[1] = EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
        m_display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE,
            reinterpret_cast<void*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), "SDL.window.win32.hwnd", nullptr)),
            displayAttributes);
    }
#else
    // On Linux/macOS, use default EGL display
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif

    if (m_display == EGL_NO_DISPLAY) {
        LOG_ERROR("  Failed to get EGL display: 0x{:X}", eglGetError());
        return false;
    }

    // Initialize EGL
    EGLint major, minor;
    if (!eglInitialize(m_display, &major, &minor)) {
        LOG_ERROR("  Failed to initialize EGL: 0x{:X}", eglGetError());
        return false;
    }

    LOG_INFO("  EGL initialized: {}.{}", major, minor);
    logEGLConfig();

    // Choose config
    if (!chooseConfig()) {
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    // Create context
    if (!createContext(majorVersion, minorVersion)) {
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    // Create surface
    if (!createSurface()) {
        eglDestroyContext(m_display, m_context);
        m_context = EGL_NO_CONTEXT;
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    // Make context current
    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        LOG_ERROR("  Failed to make EGL context current: 0x{:X}", eglGetError());
        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
        eglDestroyContext(m_display, m_context);
        m_context = EGL_NO_CONTEXT;
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    // Enable VSync
    eglSwapInterval(m_display, 1);

    // Verify OpenGL ES version
    if (!verifyGLVersion(majorVersion, minorVersion)) {
        // Continue anyway, might still work
        LOG_WARN("  OpenGL ES version verification failed, continuing...");
    }

    m_initialized = true;

    LOG_INFO("  OpenGL ES Vendor: {}", getGLVendorString());
    LOG_INFO("  OpenGL ES Renderer: {}", getGLRendererString());
    LOG_INFO("  OpenGL ES Version: {}", getGLVersionString());
    LOG_INFO("ANGLE EGL context initialized ({}x{})", m_width, m_height);
    return true;
}

bool AngleContext::verifyGLVersion(int majorVersion, int minorVersion) {
    const char* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!versionStr) {
        LOG_ERROR("  Failed to get OpenGL ES version string");
        return false;
    }

    // Parse "OpenGL ES 3.x" format
    int actualMajor = 0, actualMinor = 0;
    if (sscanf(versionStr, "OpenGL ES %d.%d", &actualMajor, &actualMinor) < 2) {
        // Try parsing just the version number
        if (sscanf(versionStr, "%d.%d", &actualMajor, &actualMinor) < 2) {
            LOG_WARN("  Could not parse OpenGL ES version: {}", versionStr);
            return true;
        }
    }

    m_glMajorVersion = actualMajor;
    m_glMinorVersion = actualMinor;

    LOG_INFO("  OpenGL ES version: {}.{}", actualMajor, actualMinor);

    if (actualMajor < majorVersion ||
        (actualMajor == majorVersion && actualMinor < minorVersion)) {
        LOG_WARN("  Requested OpenGL ES {}.{} but got {}.{}",
                 majorVersion, minorVersion, actualMajor, actualMinor);
    }

    return true;
}

void AngleContext::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down ANGLE EGL context...");

    if (m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        
        if (m_surface != EGL_NO_SURFACE) {
            eglDestroySurface(m_display, m_surface);
            m_surface = EGL_NO_SURFACE;
        }
        
        if (m_context != EGL_NO_CONTEXT) {
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
        }
        
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
    }

    m_window = nullptr;
    m_initialized = false;
    LOG_INFO("ANGLE EGL context shut down.");
}

void AngleContext::swapBuffers() {
    if (m_display != EGL_NO_DISPLAY && m_surface != EGL_NO_SURFACE) {
        eglSwapBuffers(m_display, m_surface);
    }
}

void AngleContext::makeCurrent() {
    if (m_display != EGL_NO_DISPLAY && m_surface != EGL_NO_SURFACE && m_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    }
}

void AngleContext::setVSync(bool enable) {
    if (m_display != EGL_NO_DISPLAY) {
        eglSwapInterval(m_display, enable ? 1 : 0);
    }
}

std::string AngleContext::getGLVersionString() const {
    if (!m_initialized) return "Not initialized";
    
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return version ? version : "Unknown";
}

std::string AngleContext::getGLRendererString() const {
    if (!m_initialized) return "Not initialized";
    
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    return renderer ? renderer : "Unknown";
}

std::string AngleContext::getGLVendorString() const {
    if (!m_initialized) return "Not initialized";
    
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    return vendor ? vendor : "Unknown";
}

int AngleContext::getCurrentFramebuffer() const {
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    return framebuffer;
}

void AngleContext::resize(int width, int height) {
    m_width = width;
    m_height = height;
    LOG_DEBUG("ANGLE context resized to {}x{}", width, height);
}

std::string AngleContext::getAngleBackendString() const {
    if (!m_initialized) return "Not initialized";
    
    // The renderer string usually contains the ANGLE backend info
    std::string renderer = getGLRendererString();
    
    if (renderer.find("Vulkan") != std::string::npos) {
        return "ANGLE (Vulkan)";
    } else if (renderer.find("D3D11") != std::string::npos || 
               renderer.find("Direct3D") != std::string::npos) {
        return "ANGLE (D3D11)";
    } else if (renderer.find("Metal") != std::string::npos) {
        return "ANGLE (Metal)";
    } else if (renderer.find("OpenGL") != std::string::npos) {
        return "ANGLE (OpenGL)";
    }
    
    return "ANGLE (Unknown)";
}

} // namespace skia_renderer

#endif // USE_ANGLE
