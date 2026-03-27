#include "AngleRenderer.h"
#include "AngleContext.h"
#include "SceneRenderer.h"
#include "core/Logger.h"

#ifdef USE_ANGLE

// Skia Ganesh (OpenGL ES via ANGLE) headers
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"

// OpenGL ES headers (via ANGLE)
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

// EGL header for function loading
#include <EGL/egl.h>
#include <EGL/eglext.h>

// Standard headers
#include <cstring>

namespace skia_renderer {

// Custom GL function loader using EGL
// This is required for ANGLE because GrGLMakeNativeInterface() uses platform-specific
// mechanisms (wglGetProcAddress, glXGetProcAddress) which don't work with EGL contexts.
//
// ANGLE's eglGetProcAddress() returns valid function pointers for ALL GL functions
// (both core and extension), unlike some other EGL implementations.
static GrGLFuncPtr egl_get_gl_proc(void* ctx, const char name[]) {
    (void)ctx;  // unused
    return (GrGLFuncPtr)eglGetProcAddress(name);
}

struct AngleRenderer::Impl {
    sk_sp<GrDirectContext> grContext;
    sk_sp<SkSurface> surface;
};

AngleRenderer::AngleRenderer()
    : m_impl(std::make_unique<Impl>())
    , m_angleContext(std::make_unique<AngleContext>())
    , m_sceneRenderer(std::make_unique<SceneRenderer>()) {
}

AngleRenderer::~AngleRenderer() {
    if (m_initialized) {
        shutdown();
    }
}

bool AngleRenderer::initialize(SDL_Window* window, int width, int height, const BackendConfig& config) {
    m_window = window;
    m_width = width;
    m_height = height;

    LOG_INFO("Initializing Skia Ganesh ANGLE renderer...");

    // Create ANGLE EGL context
    m_angleContext = std::make_unique<AngleContext>();
    if (!m_angleContext->initialize(window, config.angleMajor, config.angleMinor)) {
        LOG_ERROR("Failed to create ANGLE context");
        return false;
    }

    if (!createSkiaContext()) {
        LOG_ERROR("Failed to create Skia context");
        return false;
    }

    if (!createSurface()) {
        LOG_ERROR("Failed to create Skia surface");
        return false;
    }

    // Initialize scene renderer fonts
    if (!m_sceneRenderer->initializeFonts()) {
        LOG_WARN("Failed to initialize fonts for scene renderer");
    }

    m_initialized = true;
    LOG_INFO("Skia Ganesh ANGLE renderer initialized ({}x{})", width, height);
    LOG_INFO("  ANGLE Backend: {}", m_angleContext->getAngleBackendString());
    return true;
}

bool AngleRenderer::createSkiaContext() {
    LOG_INFO("  Creating Skia Ganesh context for ANGLE...");

    // Ensure EGL context is current before creating GL interface
    m_angleContext->makeCurrent();

    // Get GL version info for debugging
    const char* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* vendorStr = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* rendererStr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    LOG_INFO("  GL Version: {}", versionStr ? versionStr : "(null)");
    LOG_INFO("  GL Vendor: {}", vendorStr ? vendorStr : "(null)");
    LOG_INFO("  GL Renderer: {}", rendererStr ? rendererStr : "(null)");

    // Create GL interface for ANGLE/EGL
    // GrGLMakeNativeInterface() uses platform-specific loaders (wglGetProcAddress on Windows)
    // which don't work with EGL contexts. We use GrGLMakeAssembledInterface with our
    // custom EGL-based function loader.
    //
    // GrGLMakeAssembledGLESInterface is preferred because ANGLE provides OpenGL ES,
    // not desktop GL. This ensures the interface is correctly configured for ES.
    sk_sp<const GrGLInterface> glInterface = GrGLMakeAssembledGLESInterface(nullptr, egl_get_gl_proc);

    if (!glInterface) {
        // Fallback: try the generic assembled interface (auto-detects GL vs GLES)
        LOG_DEBUG("  GrGLMakeAssembledGLESInterface failed, trying GrGLMakeAssembledInterface...");
        glInterface = GrGLMakeAssembledInterface(nullptr, egl_get_gl_proc);
    }

    if (!glInterface) {
        LOG_ERROR("  Failed to create GL interface for ANGLE");
        LOG_ERROR("  This usually means the GL context is not current or GL functions cannot be loaded");
        return false;
    }

    LOG_INFO("  GL interface created: {}",
              glInterface->fStandard == kGL_GrGLStandard ? "Desktop GL" :
              glInterface->fStandard == kGLES_GrGLStandard ? "OpenGL ES" : "Unknown");

    // Create context options
    GrContextOptions options;

    // Create Ganesh context
    m_impl->grContext = GrDirectContexts::MakeGL(glInterface, options);
    if (!m_impl->grContext) {
        LOG_ERROR("  Failed to create Skia Ganesh context");
        return false;
    }

    LOG_INFO("  Skia Ganesh context created successfully");
    return true;
}

bool AngleRenderer::createSurface() {
    if (!m_impl->grContext) {
        LOG_ERROR("  No Skia context available");
        return false;
    }

    // Make sure EGL context is current
    m_angleContext->makeCurrent();

    // Get the default framebuffer (0 = window framebuffer)
    GLint framebuffer = m_angleContext->getCurrentFramebuffer();

    LOG_INFO("  Creating Skia surface (FBO: {}, {}x{})...", framebuffer, m_width, m_height);

    // Create backend render target for the default framebuffer
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = framebuffer;
    fbInfo.fFormat = GL_RGBA8;

    // Create backend render target
    // sampleCount=0 means no MSAA, stencilBits=8 for stencil buffer
    int sampleCount = 0;
    int stencilBits = 8;
    GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeGL(
        m_width, m_height, sampleCount, stencilBits, fbInfo);

    if (!backendRT.isValid()) {
        LOG_ERROR("  Failed to create backend render target");
        return false;
    }

    // Create Skia surface wrapping the default framebuffer
    // NOTE: ANGLE translates between the underlying API (Vulkan/D3D11) and OpenGL ES.
    // Even when using Vulkan backend, ANGLE presents an OpenGL ES interface with
    // bottom-left origin convention. Use kBottomLeft_GrSurfaceOrigin for ANGLE.
    SkSurfaceProps surfaceProps(0, kUnknown_SkPixelGeometry);
    m_impl->surface = SkSurfaces::WrapBackendRenderTarget(
        m_impl->grContext.get(),
        backendRT,
        kBottomLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType,
        SkColorSpace::MakeSRGB(),
        &surfaceProps
    );

    if (!m_impl->surface) {
        LOG_ERROR("  Failed to create Skia surface");
        return false;
    }

    LOG_INFO("  Skia surface created successfully");
    return true;
}

void AngleRenderer::destroySurface() {
    m_impl->surface.reset();
}

void AngleRenderer::shutdown() {
    if (!m_initialized) return;

    LOG_INFO("Shutting down ANGLE renderer...");

    // Wait for GPU to finish
    if (m_impl->grContext) {
        m_impl->grContext->flushAndSubmit();
    }

    destroySurface();
    m_impl->grContext.reset();
    m_angleContext.reset();

    m_initialized = false;
    LOG_INFO("ANGLE renderer shut down.");
}

void AngleRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;

    // Recreate surface for new size
    if (m_initialized && m_impl->grContext) {
        destroySurface();
        createSurface();
    }

    LOG_DEBUG("ANGLE renderer resized to {}x{}", width, height);
}

bool AngleRenderer::beginFrame() {
    if (!m_initialized || !m_impl->grContext || !m_impl->surface) {
        return false;
    }

    // Make ANGLE context current
    m_angleContext->makeCurrent();

    // Set viewport
    glViewport(0, 0, m_width, m_height);

    return true;
}

void AngleRenderer::endFrame() {
    if (!m_initialized || !m_impl->grContext) {
        return;
    }

    // Flush Skia context - this ensures all rendering commands are submitted
    m_impl->grContext->flushAndSubmit();

    // Swap buffers
    m_angleContext->swapBuffers();
}

void AngleRenderer::render() {
    if (!m_initialized || !m_impl->surface) {
        LOG_ERROR("ANGLE renderer not initialized");
        return;
    }

    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("Failed to get canvas from surface");
        return;
    }

    // Update scene renderer state
    m_sceneRenderer->setFPS(m_fps);

    // Build backend info string
    std::string backendInfo = "ANGLE: " + m_angleContext->getAngleBackendString();
    backendInfo += " | " + m_angleContext->getGLRendererString();

    // Render the scene (rendererName is "Skia Ganesh" for OpenGL ES via ANGLE)
    m_sceneRenderer->render(canvas, m_width, m_height, backendInfo, "Skia Ganesh");

    // Flush after each render call
    m_impl->grContext->flush();
}

void AngleRenderer::setFPS(float fps) {
    m_fps = fps;
}

std::string AngleRenderer::getBackendName() const {
    return "Ganesh ANGLE";
}

} // namespace skia_renderer

#endif // USE_ANGLE
