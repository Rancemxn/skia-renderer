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
#include "include/core/SkImage.h"

// OpenGL headers
#include <SDL3/SDL_opengl.h>

namespace skia_renderer {

struct GLRenderer::Impl {
    sk_sp<GrDirectContext> grContext;
    sk_sp<SkSurface> surface;  // Offscreen render target managed by Skia
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

    LOG_INFO("  Creating offscreen Skia render target ({}x{})...", m_width, m_height);

    // Create image info for the render target
    // Use BGRA which is often more compatible with OpenGL
    SkImageInfo imageInfo = SkImageInfo::Make(
        m_width, m_height,
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType,
        SkColorSpace::MakeSRGB()
    );

    // Create offscreen render target - this is what Skia's example does
    m_impl->surface = SkSurfaces::RenderTarget(
        m_impl->grContext.get(),
        skgpu::Budgeted::kYes,
        imageInfo,
        0,  // sampleCount = 0 means no MSAA
        kBottomLeft_GrSurfaceOrigin,  // OpenGL convention
        nullptr  // surfaceProps
    );

    if (!m_impl->surface) {
        LOG_ERROR("  Failed to create offscreen render target");
        return false;
    }

    LOG_INFO("  Offscreen render target created successfully ({}x{})", 
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
    
    return true;
}

void GLRenderer::endFrame() {
    if (!m_impl->grContext || !m_impl->surface) {
        return;
    }

    // Flush Skia commands
    m_impl->grContext->flushAndSubmit();

    // Now we need to blit the Skia render target to the default framebuffer
    // Get the backend texture from the Skia surface
    GrBackendTexture backendTex = SkSurfaces::GetBackendTexture(
        m_impl->surface.get(),
        SkSurfaces::BackendHandleAccess::kFlushRead
    );
    
    if (!backendTex.isValid()) {
        LOG_WARN("Failed to get backend texture from surface");
        // Fallback: just swap buffers
        m_glContext->swapBuffers();
        return;
    }

    // Get GL texture info from backend texture
    GrGLTextureInfo texInfo;
    if (!GrBackendTextures::GetGLTextureInfo(backendTex, &texInfo)) {
        LOG_WARN("Failed to get GL texture info");
        m_glContext->swapBuffers();
        return;
    }
    
    LOG_DEBUG("Blitting texture {} to screen", texInfo.fID);

    // Save current GL state
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    
    // Bind default framebuffer (FBO 0)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    
    // Create a temporary FBO for blitting
    GLuint blitFBO = 0;
    glGenFramebuffers(1, &blitFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, blitFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                           GL_TEXTURE_2D, texInfo.fID, 0);
    
    // Check framebuffer status
    GLenum fboStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (fboStatus == GL_FRAMEBUFFER_COMPLETE) {
        // Blit from Skia texture to default framebuffer
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        
        glBlitFramebuffer(
            0, 0, m_width, m_height,  // src
            0, 0, m_width, m_height,  // dst
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );
    } else {
        LOG_WARN("Blit FBO not complete: {}", fboStatus);
    }
    
    // Cleanup
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &blitFBO);
    
    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevProgram) glUseProgram(prevProgram);
    if (prevBlend) glEnable(GL_BLEND);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);

    // Swap buffers
    m_glContext->swapBuffers();
}

void GLRenderer::render() {
    if (!m_impl->grContext || !m_impl->surface) {
        LOG_ERROR("GLRenderer not initialized");
        return;
    }

    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("Failed to get canvas");
        return;
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
