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
// These are the official ANGLE platform type values
#if defined(_WIN32)
#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE 0x3450
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE 0x3451
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE 0x3452
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE 0x3453
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE 0x3454
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3455
#endif
#ifndef EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE
#define EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE 0x3456
#endif
#endif

// EGL extension for context minor version (may not be defined in all headers)
#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION_KHR
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#endif
// EGL create context flags
#ifndef EGL_CONTEXT_FLAGS_KHR
#define EGL_CONTEXT_FLAGS_KHR 0x30FC
#endif
#ifndef EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR
#define EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR 0x00000001
#endif

AngleContext::AngleContext() = default;

AngleContext::~AngleContext() {
    if (m_initialized) {
        shutdown();
    }
}

bool AngleContext::chooseConfig() {
    // EGL config attributes for OpenGL ES 3.x
    // We request RGBA8888, 24-bit depth, 8-bit stencil
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
        EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(m_display, configAttributes, &m_config, 1, &numConfigs)) {
        LOG_ERROR("  Failed to choose EGL config: 0x{:X}", eglGetError());
        return false;
    }

    if (numConfigs == 0) {
        // Try with more permissive attributes (ES2 compatible)
        LOG_WARN("  No ES3 config found, trying ES2 compatible config...");
        const EGLint configAttributesES2[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_STENCIL_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        
        if (!eglChooseConfig(m_display, configAttributesES2, &m_config, 1, &numConfigs)) {
            LOG_ERROR("  Failed to choose ES2 EGL config: 0x{:X}", eglGetError());
            return false;
        }
        
        if (numConfigs == 0) {
            LOG_ERROR("  No matching EGL configs found");
            return false;
        }
    }

    // Log the chosen config details
    EGLint red, green, blue, alpha, depth, stencil;
    eglGetConfigAttrib(m_display, m_config, EGL_RED_SIZE, &red);
    eglGetConfigAttrib(m_display, m_config, EGL_GREEN_SIZE, &green);
    eglGetConfigAttrib(m_display, m_config, EGL_BLUE_SIZE, &blue);
    eglGetConfigAttrib(m_display, m_config, EGL_ALPHA_SIZE, &alpha);
    eglGetConfigAttrib(m_display, m_config, EGL_DEPTH_SIZE, &depth);
    eglGetConfigAttrib(m_display, m_config, EGL_STENCIL_SIZE, &stencil);
    
    LOG_INFO("  EGL config chosen: R{}G{}B{}A{} D{} S{}", red, green, blue, alpha, depth, stencil);

    return true;
}

bool AngleContext::createContext(int majorVersion, int minorVersion) {
    // Check for EGL_KHR_create_context extension
    const char* displayExtensions = eglQueryString(m_display, EGL_EXTENSIONS);
    bool hasCreateContextKHR = displayExtensions && strstr(displayExtensions, "EGL_KHR_create_context");
    
    if (hasCreateContextKHR) {
        LOG_DEBUG("  EGL_KHR_create_context extension available");
    }

    // Try creating context with requested version (ES 3.x)
    // The EGL_CONTEXT_MINOR_VERSION_KHR requires EGL_KHR_create_context extension
    if (hasCreateContextKHR && majorVersion >= 3) {
        const EGLint contextAttributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, majorVersion,
            EGL_CONTEXT_MINOR_VERSION_KHR, minorVersion,
            EGL_CONTEXT_FLAGS_KHR, 0,
            EGL_NONE
        };

        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttributes);
        if (m_context != EGL_NO_CONTEXT) {
            LOG_INFO("  Created OpenGL ES {}.{} context", majorVersion, minorVersion);
            return true;
        }
        
        EGLint error = eglGetError();
        LOG_DEBUG("  ES {}.{} context creation failed (error 0x{:X})", majorVersion, minorVersion, error);
        (void)error;  // suppress unused variable warning in non-debug builds
    }

    // Try with just major version (ES 3.0)
    if (majorVersion >= 3) {
        const EGLint contextAttributesES3[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        
        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttributesES3);
        if (m_context != EGL_NO_CONTEXT) {
            LOG_INFO("  Created OpenGL ES 3.0 context");
            return true;
        }
        
        EGLint error = eglGetError();
        LOG_DEBUG("  ES 3.0 context creation failed (error 0x{:X})", error);
        (void)error;  // suppress unused variable warning in non-debug builds
    }

    // Fallback to ES 2.0
    const EGLint contextAttributesES2[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttributesES2);
    if (m_context == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        LOG_ERROR("  Failed to create EGL context: 0x{:X}", error);
        LOG_ERROR("  All context creation attempts failed");
        return false;
    }
    
    LOG_WARN("  Created ES 2.0 context (fallback) instead of ES {}.{}", majorVersion, minorVersion);
    return true;
}

bool AngleContext::createSurface() {
    // Create window surface using stored native window handle
    m_surface = eglCreateWindowSurface(m_display, m_config, m_nativeWindow, nullptr);

    if (m_surface == EGL_NO_SURFACE) {
        LOG_ERROR("  Failed to create EGL window surface: 0x{:X}", eglGetError());
        return false;
    }

    LOG_INFO("  EGL surface created successfully");
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

    // Get native window handle for surface creation (not for display)
#if defined(_WIN32)
    m_nativeWindow = reinterpret_cast<EGLNativeWindowType>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), "SDL.window.win32.hwnd", nullptr));
#else
    m_nativeWindow = reinterpret_cast<EGLNativeWindowType>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), "SDL.window.x11.window", nullptr));
#endif

    // Check for ANGLE platform extension
    const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    bool hasAnglePlatform = clientExtensions && strstr(clientExtensions, "EGL_ANGLE_platform_angle");
    bool hasAngleDeviceCreation = clientExtensions && strstr(clientExtensions, "EGL_ANGLE_device_creation");
    
    if (hasAnglePlatform) {
        LOG_INFO("  EGL_ANGLE_platform_angle extension available");
    }
    if (hasAngleDeviceCreation) {
        LOG_INFO("  EGL_ANGLE_device_creation extension available");
    }

    // Log available extensions for debugging
    if (clientExtensions) {
        LOG_DEBUG("  EGL client extensions: {}", clientExtensions);
    }

#if defined(_WIN32)
    // Try different methods to get the EGL display
    // Method 1: Use eglGetPlatformDisplay with ANGLE platform (EGL 1.5+)
    if (hasAnglePlatform) {
        // Try D3D11 backend first (most reliable on Windows)
        EGLAttrib displayAttributesD3D11[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE, EGL_FALSE,
            EGL_NONE
        };
        
        LOG_INFO("  Trying ANGLE D3D11 backend via eglGetPlatformDisplay...");
        m_display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, 
            reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), displayAttributesD3D11);
        
        if (m_display == EGL_NO_DISPLAY) {
            EGLint error = eglGetError();
            LOG_DEBUG("  D3D11 backend returned no display (error 0x{:X})", error);
            (void)error;  // suppress unused variable warning in non-debug builds
        } else {
            LOG_INFO("  D3D11 platform display obtained successfully");
        }
        
        // If D3D11 failed, try Vulkan backend
        if (m_display == EGL_NO_DISPLAY) {
            EGLAttrib displayAttributesVulkan[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
                EGL_NONE
            };
            
            LOG_INFO("  Trying ANGLE Vulkan backend...");
            m_display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE,
                reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), displayAttributesVulkan);
            
            if (m_display != EGL_NO_DISPLAY) {
                LOG_INFO("  Vulkan platform display obtained successfully");
            }
        }
        
        // If both failed, try OpenGL backend
        if (m_display == EGL_NO_DISPLAY) {
            EGLAttrib displayAttributesGL[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE,
                EGL_NONE
            };
            
            LOG_INFO("  Trying ANGLE OpenGL backend...");
            m_display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE,
                reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), displayAttributesGL);
            
            if (m_display != EGL_NO_DISPLAY) {
                LOG_INFO("  OpenGL platform display obtained successfully");
            }
        }
    }
#endif

    // Fallback to eglGetDisplay if eglGetPlatformDisplay didn't work
    if (m_display == EGL_NO_DISPLAY) {
        LOG_INFO("  Using eglGetDisplay (fallback)...");
        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

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

    // Bind OpenGL ES API
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOG_WARN("  Failed to bind OpenGL ES API: 0x{:X}", eglGetError());
        // Continue anyway, might still work
    }

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
