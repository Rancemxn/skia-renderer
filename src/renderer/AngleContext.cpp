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

// Platform-specific EGL extensions and defines (from ANGLE eglext_angle.h)
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
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE 0x3208
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE 0x3207
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE 0x320D
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE 0x320E
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3489
#endif
#ifndef EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE
#define EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE 0x3451
#endif
#endif

// EGL extension for context minor version
#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION_KHR
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#endif
#ifndef EGL_CONTEXT_FLAGS_KHR
#define EGL_CONTEXT_FLAGS_KHR 0x30FC
#endif

AngleContext::AngleContext() = default;

AngleContext::~AngleContext() {
    if (m_initialized) {
        shutdown();
    }
}

bool AngleContext::chooseConfig() {
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
    const char* displayExtensions = eglQueryString(m_display, EGL_EXTENSIONS);
    bool hasCreateContextKHR = displayExtensions && strstr(displayExtensions, "EGL_KHR_create_context");
    
    if (hasCreateContextKHR) {
        LOG_DEBUG("  EGL_KHR_create_context extension available");
    }

    // Try ES 3.x with minor version
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
        (void)error;
    }

    // Try ES 3.0
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
        (void)error;
    }

    // Fallback to ES 2.0
    const EGLint contextAttributesES2[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttributesES2);
    if (m_context == EGL_NO_CONTEXT) {
        LOG_ERROR("  Failed to create EGL context: 0x{:X}", eglGetError());
        return false;
    }
    
    LOG_WARN("  Created ES 2.0 context (fallback) instead of ES {}.{}", majorVersion, minorVersion);
    return true;
}

bool AngleContext::createSurface() {
    if (!m_nativeWindow) {
        LOG_ERROR("  Native window handle is NULL, cannot create surface");
        return false;
    }

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
    
    const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (clientExtensions && strstr(clientExtensions, "EGL_ANGLE_platform_angle")) {
        LOG_INFO("  ANGLE Platform: Available");
    }
}

const char* AngleContext::angleBackendToString(AngleBackendType backend) const {
    switch (backend) {
        case AngleBackendType::Vulkan:   return "Vulkan";
        case AngleBackendType::D3D11:    return "D3D11";
        case AngleBackendType::D3D9:     return "D3D9";
        case AngleBackendType::Metal:    return "Metal";
        case AngleBackendType::OpenGL:   return "OpenGL";
        case AngleBackendType::OpenGLES: return "OpenGL ES";
        default:                         return "Auto";
    }
}

bool AngleContext::initialize(SDL_Window* window, int majorVersion, int minorVersion,
                              AngleBackendType angleBackend) {
    if (m_initialized) {
        LOG_WARN("AngleContext already initialized");
        return true;
    }

    m_requestedBackend = angleBackend;

    LOG_INFO("Initializing ANGLE EGL context...");
    LOG_INFO("  Requested ANGLE backend: {}", angleBackendToString(angleBackend));
    LOG_INFO("  Requested OpenGL ES version: {}.{}", majorVersion, minorVersion);

    m_window = window;

    // Get window size
    SDL_GetWindowSize(window, &m_width, &m_height);
    LOG_INFO("  Window size: {}x{}", m_width, m_height);

    // Get native window handle for surface creation
#if defined(_WIN32)
    m_nativeWindow = reinterpret_cast<EGLNativeWindowType>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), "SDL.window.win32.hwnd", nullptr));
    LOG_INFO("  Native window (HWND): {}", m_nativeWindow ? "valid" : "NULL");
#else
    m_nativeWindow = reinterpret_cast<EGLNativeWindowType>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), "SDL.window.x11.window", nullptr));
#endif

    // Check for ANGLE platform extension
    const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    bool hasAnglePlatform = clientExtensions && strstr(clientExtensions, "EGL_ANGLE_platform_angle");
    
    if (hasAnglePlatform) {
        LOG_INFO("  EGL_ANGLE_platform_angle extension available");
    }

#if defined(_WIN32)
    // Try different backends based on user selection
    if (hasAnglePlatform) {
        // Build list of backends to try
        struct BackendInfo { AngleBackendType type; EGLint eglValue; const char* name; };
        std::vector<BackendInfo> backendOrder;
        
        if (m_requestedBackend == AngleBackendType::Auto) {
            // Auto: D3D11 first (most reliable on Windows), then Vulkan
            backendOrder = {
                {AngleBackendType::D3D11, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, "D3D11"},
                {AngleBackendType::Vulkan, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE, "Vulkan"},
                {AngleBackendType::OpenGL, EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE, "OpenGL"}
            };
        } else if (m_requestedBackend == AngleBackendType::Vulkan) {
            backendOrder = {
                {AngleBackendType::Vulkan, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE, "Vulkan"},
                {AngleBackendType::D3D11, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, "D3D11"}
            };
        } else if (m_requestedBackend == AngleBackendType::D3D11) {
            backendOrder = {
                {AngleBackendType::D3D11, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, "D3D11"},
                {AngleBackendType::Vulkan, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE, "Vulkan"}
            };
        } else if (m_requestedBackend == AngleBackendType::D3D9) {
            backendOrder = {
                {AngleBackendType::D3D9, EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE, "D3D9"},
                {AngleBackendType::D3D11, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, "D3D11"}
            };
        } else if (m_requestedBackend == AngleBackendType::OpenGL) {
            backendOrder = {
                {AngleBackendType::OpenGL, EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE, "OpenGL"},
                {AngleBackendType::D3D11, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, "D3D11"}
            };
        } else {
            backendOrder = {
                {AngleBackendType::D3D11, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, "D3D11"},
                {AngleBackendType::Vulkan, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE, "Vulkan"}
            };
        }
        
        // Try each backend
        for (const auto& info : backendOrder) {
            EGLAttrib displayAttributes[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, info.eglValue,
                EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE, EGL_FALSE,
                EGL_NONE
            };
            
            LOG_INFO("  Trying ANGLE {} backend via eglGetPlatformDisplay...", info.name);
            m_display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, 
                reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), displayAttributes);
            
            if (m_display != EGL_NO_DISPLAY) {
                LOG_INFO("  {} platform display obtained successfully", info.name);
                m_detectedBackend = info.type;
                break;
            } else {
                EGLint error = eglGetError();
                LOG_DEBUG("  {} backend returned no display (error 0x{:X})", info.name, error);
                (void)error;
            }
        }
    }
#endif

    // Fallback to eglGetDisplay if eglGetPlatformDisplay didn't work
    if (m_display == EGL_NO_DISPLAY) {
        LOG_INFO("  Using eglGetDisplay (fallback)...");
        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        m_detectedBackend = AngleBackendType::Auto;
    }

    if (m_display == EGL_NO_DISPLAY) {
        LOG_ERROR("  Failed to get EGL display: 0x{:X}", eglGetError());
        return false;
    }

    // Initialize EGL
    EGLint eglMajor, eglMinor;
    if (!eglInitialize(m_display, &eglMajor, &eglMinor)) {
        LOG_ERROR("  Failed to initialize EGL: 0x{:X}", eglGetError());
        return false;
    }

    LOG_INFO("  EGL initialized: {}.{}", eglMajor, eglMinor);
    logEGLConfig();

    // Bind OpenGL ES API
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOG_WARN("  Failed to bind OpenGL ES API: 0x{:X}", eglGetError());
    }

    // Choose config
    if (!chooseConfig()) {
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    // Determine optimal ES version based on detected backend
    // Vulkan backend: ES 3.1 is widely supported (ES 3.2 may not be fully conformant)
    // D3D11/Metal/OpenGL backends typically support ES 3.0
    int actualMajor = majorVersion;
    int actualMinor = minorVersion;
    
    if (m_detectedBackend == AngleBackendType::Vulkan) {
        LOG_INFO("  Vulkan backend detected - prefer OpenGL ES 3.1 (3.2 may not be fully conformant)");
        // Vulkan backend supports ES 3.1 with compute shaders
        // Note: ES 3.2 is exposed only through workarounds and may not be fully conformant
        if (majorVersion < 3 || (majorVersion == 3 && minorVersion < 1)) {
            LOG_INFO("  Upgrading to OpenGL ES 3.1 for Vulkan backend");
            actualMajor = 3;
            actualMinor = 1;
        }
    } else if (m_detectedBackend == AngleBackendType::D3D11 ||
               m_detectedBackend == AngleBackendType::D3D9) {
        LOG_INFO("  D3D backend detected - using OpenGL ES 3.0");
        // D3D backends work best with ES 3.0
        actualMajor = 3;
        actualMinor = 0;
    }

    // Create context
    if (!createContext(actualMajor, actualMinor)) {
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
    if (!verifyGLVersion(actualMajor, actualMinor)) {
        LOG_WARN("  OpenGL ES version verification issue, continuing...");
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
    
    // Check detected backend first
    switch (m_detectedBackend) {
        case AngleBackendType::Vulkan:   return "ANGLE (Vulkan)";
        case AngleBackendType::D3D11:    return "ANGLE (D3D11)";
        case AngleBackendType::D3D9:     return "ANGLE (D3D9)";
        case AngleBackendType::Metal:    return "ANGLE (Metal)";
        case AngleBackendType::OpenGL:   return "ANGLE (OpenGL)";
        case AngleBackendType::OpenGLES: return "ANGLE (OpenGL ES)";
        default: break;
    }
    
    // Fallback to checking renderer string
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

bool AngleContext::isVulkanBackend() const {
    if (!m_initialized) return false;
    
    // Check detected backend first
    if (m_detectedBackend == AngleBackendType::Vulkan) {
        return true;
    }
    
    // Fallback to checking renderer string
    std::string renderer = getGLRendererString();
    return renderer.find("Vulkan") != std::string::npos;
}

} // namespace skia_renderer

#endif // USE_ANGLE
