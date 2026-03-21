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
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"

// OpenGL ES headers (via ANGLE)
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

namespace skia_renderer {

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

    // Create EGL interface for ANGLE
    // Note: For ANGLE, we use GrGLMakeEGLInterface instead of GrGLMakeNativeInterface
    sk_sp<const GrGLInterface> glInterface = GrGLMakeEGLInterface();

    if (!glInterface) {
        LOG_ERROR("  Failed to create EGL GL interface");
        return false;
    }

    // Log GL interface info
    LOG_INFO("  GL interface created: {}", 
              glInterface->fStandard == kGL_GrGLStandard ? "Desktop GL" : 
              glInterface->fStandard == kGLES_GrGLStandard ? "OpenGL ES" : "Unknown");

    LOG_INFO("  ANGLE EGL interface successfully created");

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

    // Create backend render target for the default framebuffer
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = framebuffer;
    fbInfo.fFormat = GL_RGBA8;

    // Create backend surface
    GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeGL(
        m_width, m_height, fbInfo, kTopLeft_GrSurfaceOrigin);

    // Create Skia surface
    SkSurfaceProps surfaceProps;
    surfaceProps.fColorType = kRGBA_8888_SkColorType;
    surfaceProps.fSurfaceOrigin = kTopLeft_GrSurfaceOrigin;

    m_impl->surface = SkSurfaces::WrapRenderTargets(
        m_impl->grContext.get(), backendRT, &surfaceProps);
    
    if (!m_impl->surface) {
        LOG_ERROR("  Failed to create Skia surface");
        return false;
    }

    LOG_INFO("  Skia surface created ({}x{}, FBO {})", m_width, m_height, framebuffer);
    return true;
}

void AngleRenderer::destroySurface() {
    m_impl->surface.reset();
}

void AngleRenderer::shutdown() {
    if (!m_initialized) return;

    LOG_INFO("Shutting down ANGLE renderer...");

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
    destroySurface();
    createSurface();
    
    LOG_DEBUG("ANGLE renderer resized to {}x{}", width, height);
}

bool AngleRenderer::beginFrame() {
    if (!m_initialized || !m_impl->grContext || !m_impl->surface) {
        return false;
    }

    // Make ANGLE context current
    m_angleContext->makeCurrent();
    
    return true;
}

void AngleRenderer::endFrame() {
    if (!m_initialized || !m_impl->grContext) {
        return;
    }

    // Flush Skia context
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
    
    // Render the scene
    m_sceneRenderer->render(canvas, m_width, m_height, backendInfo);
}

void AngleRenderer::setFPS(float fps) {
    m_fps = fps;
}

} // namespace skia_renderer

#endif // USE_ANGLE
