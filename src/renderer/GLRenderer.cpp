#include "GLRenderer.h"
#include "GLContext.h"
#include "DemoRenderer.h"
#include "core/Logger.h"

// Skia Ganesh (OpenGL) headers
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"

// OpenGL headers
#include <SDL3/SDL_opengl.h>

namespace skia_renderer {

struct GLRenderer::Impl {
    sk_sp<GrDirectContext> grContext;
    sk_sp<SkSurface> surface;
};

GLRenderer::GLRenderer() 
    : m_impl(std::make_unique<Impl>())
    , m_demoRenderer(std::make_unique<DemoRenderer>()) {
}

GLRenderer::~GLRenderer() {
    if (m_initialized) {
        shutdown();
    }
}

bool GLRenderer::initialize(SDL_Window* window, int width, int height, const BackendConfig& config) {
    m_window = window;
    m_width = width;
    m_height = height;

    LOG_INFO("Initializing Skia Ganesh OpenGL renderer...");

    // Create OpenGL context
    m_glContext = std::make_unique<GLContext>();
    if (!m_glContext->initialize(window, config.glMajor, config.glMinor)) {
        LOG_ERROR("Failed to create OpenGL context");
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

    // Initialize demo renderer fonts
    if (!m_demoRenderer->initializeFonts()) {
        LOG_WARN("Failed to initialize fonts for demo renderer");
    }

    m_initialized = true;
    LOG_INFO("Skia Ganesh OpenGL renderer initialized ({}x{})", width, height);
    return true;
}

bool GLRenderer::createSkiaContext() {
    LOG_INFO("  Creating Skia Ganesh context...");

    // Create GL interface using SDL's GL loader
    auto getProc = [](void* ctx, const char name[]) -> GrGLFuncPtr {
        (void)ctx;
        return reinterpret_cast<GrGLFuncPtr>(SDL_GL_GetProcAddress(name));
    };

    sk_sp<const GrGLInterface> glInterface = GrGLMakeAssembledInterface(nullptr, getProc);
    if (!glInterface) {
        LOG_ERROR("  Failed to create GL interface");
        return false;
    }

    // Log GL interface info
    LOG_INFO("  GL interface created: {}", glInterface->fStandard == kGL_GrGLStandard ? "Desktop GL" : 
                                                  glInterface->fStandard == kGLES_GrGLStandard ? "OpenGL ES" : "Unknown");

    // Create context options
    GrContextOptions options;
    options.fPreferExternalImagesOverES3 = true;

    // Create Ganesh context
    m_impl->grContext = GrDirectContexts::MakeGL(glInterface, options);
    if (!m_impl->grContext) {
        LOG_ERROR("  Failed to create Skia Ganesh context");
        return false;
    }

    LOG_INFO("  Skia Ganesh context created successfully");
    return true;
}

bool GLRenderer::createSurface() {
    if (!m_impl->grContext) {
        LOG_ERROR("  No Skia context available");
        return false;
    }

    // Make sure GL context is current
    m_glContext->makeCurrent();

    // Get the default framebuffer (0 = window framebuffer)
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

    LOG_INFO("  Creating Skia surface (FBO: {}, {}x{})...", framebuffer, m_width, m_height);
    
    // Verify framebuffer is valid
    if (framebuffer < 0) {
        LOG_ERROR("  Invalid framebuffer ID: {}", framebuffer);
        return false;
    }

    // Create backend render target wrapping the default framebuffer
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = static_cast<GrGLuint>(framebuffer);
    fbInfo.fFormat = GL_RGBA8;

    // Log actual framebuffer info
    LOG_INFO("  FBO info: ID={}, Format=GL_RGBA8", fbInfo.fFBOID);

    GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeGL(
        m_width, m_height,
        0,      // sampleCnt
        8,      // stencilBits
        fbInfo
    );

    if (!backendRT.isValid()) {
        LOG_ERROR("  Failed to create backend render target");
        return false;
    }
    
    LOG_INFO("  Backend render target: {}x{}, samples={}, stencil={}",
             backendRT.width(), backendRT.height(), 
             backendRT.sampleCount(), backendRT.stencilBits());

    // Create Skia surface
    // OpenGL uses bottom-left origin
    SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    m_impl->surface = SkSurfaces::WrapBackendRenderTarget(
        m_impl->grContext.get(),
        backendRT,
        kBottomLeft_GrSurfaceOrigin,  // OpenGL convention
        kRGBA_8888_SkColorType,
        SkColorSpace::MakeSRGB(),
        &props
    );

    if (!m_impl->surface) {
        LOG_ERROR("  Failed to wrap framebuffer as Skia surface");
        return false;
    }

    LOG_INFO("  Skia surface created successfully");
    
    // Verify we can get a canvas
    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("  Failed to get canvas from surface");
        return false;
    }
    
    LOG_INFO("  Canvas obtained successfully, imageInfo: {}x{}",
             m_impl->surface->width(), m_impl->surface->height());
    
    return true;
}

void GLRenderer::destroySurface() {
    m_impl->surface.reset();
}

void GLRenderer::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down Skia Ganesh OpenGL renderer...");

    // Wait for GPU to finish
    if (m_impl->grContext) {
        m_impl->grContext->flushAndSubmit();
    }

    destroySurface();

    m_impl->grContext.reset();

    m_glContext.reset();

    m_initialized = false;
    LOG_INFO("Skia Ganesh OpenGL renderer shut down.");
}

void GLRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;

    // Recreate surface for new size
    if (m_initialized && m_impl->grContext) {
        destroySurface();
        m_glContext->resize(width, height);
        createSurface();
    }

    LOG_INFO("GLRenderer resized to {}x{}", width, height);
}

bool GLRenderer::beginFrame() {
    if (!m_initialized) {
        LOG_ERROR("GLRenderer::beginFrame called but not initialized");
        return false;
    }
    
    // Ensure OpenGL context is current before rendering
    m_glContext->makeCurrent();
    
    // Set viewport to match the current window size
    glViewport(0, 0, m_width, m_height);
    
    // Verify OpenGL state
    GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        LOG_WARN("GLRenderer::beginFrame - GL error before render: {}", glError);
    }
    
    return true;
}

void GLRenderer::endFrame() {
    if (!m_impl->grContext) {
        LOG_ERROR("GLRenderer::endFrame - no GrContext");
        return;
    }

    // Flush and submit Skia rendering
    m_impl->grContext->flushAndSubmit();

    // Check for GL errors after Skia rendering
    GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        LOG_WARN("GLRenderer::endFrame - GL error after Skia render: {}", glError);
    }

    // Swap buffers
    m_glContext->swapBuffers();
}

void GLRenderer::render() {
    if (!m_impl->grContext || !m_impl->surface) {
        LOG_ERROR("GLRenderer not initialized: grContext={}, surface={}", 
                  static_cast<bool>(m_impl->grContext), 
                  static_cast<bool>(m_impl->surface));
        return;
    }

    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("Failed to get canvas from surface");
        return;
    }

    // First, do a simple GL clear test to verify OpenGL is working
    // This helps diagnose if the issue is with OpenGL or Skia
    {
        // Clear to a test color (dark blue) to verify basic GL works
        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        
        GLenum glError = glGetError();
        if (glError != GL_NO_ERROR) {
            LOG_ERROR("GL clear failed: error {}", glError);
        }
    }

    // Update demo renderer state
    m_demoRenderer->setFPS(m_fps);

    // Build backend info string
    std::string backendInfo = m_glContext->getGLRendererString();

    // Use shared demo renderer
    m_demoRenderer->render(canvas, m_width, m_height, backendInfo, "Skia Ganesh OpenGL");
}

void GLRenderer::setFPS(float fps) {
    m_fps = fps;
}

} // namespace skia_renderer
