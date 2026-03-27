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
// Per EGL spec, eglGetProcAddress may not support core GL functions, so we try to get
// them directly from the linked library first. ANGLE links libGLESv2/libEGL which export
// these functions directly.
//
// This approach matches Skia's internal GrGLMakeEGLInterface implementation.
static GrGLFuncPtr egl_get_gl_proc(void* ctx, const char name[]) {
    (void)ctx;  // unused

    // First, try to get the function directly (core functions are often linked)
    // Using a simple approach: check common core function names and return them directly
    // This handles the case where eglGetProcAddress returns NULL for core functions
    #define RETURN_IF_MATCH(func) if (strcmp(name, #func) == 0) return (GrGLFuncPtr)func;

    // Core GL ES 2.0/3.0 functions that should be linked directly
    RETURN_IF_MATCH(glActiveTexture)
    RETURN_IF_MATCH(glAttachShader)
    RETURN_IF_MATCH(glBindAttribLocation)
    RETURN_IF_MATCH(glBindBuffer)
    RETURN_IF_MATCH(glBindFramebuffer)
    RETURN_IF_MATCH(glBindRenderbuffer)
    RETURN_IF_MATCH(glBindTexture)
    RETURN_IF_MATCH(glBlendColor)
    RETURN_IF_MATCH(glBlendEquation)
    RETURN_IF_MATCH(glBlendFunc)
    RETURN_IF_MATCH(glBufferData)
    RETURN_IF_MATCH(glBufferSubData)
    RETURN_IF_MATCH(glCheckFramebufferStatus)
    RETURN_IF_MATCH(glClear)
    RETURN_IF_MATCH(glClearColor)
    RETURN_IF_MATCH(glClearStencil)
    RETURN_IF_MATCH(glColorMask)
    RETURN_IF_MATCH(glCompileShader)
    RETURN_IF_MATCH(glCompressedTexImage2D)
    RETURN_IF_MATCH(glCompressedTexSubImage2D)
    RETURN_IF_MATCH(glCopyTexSubImage2D)
    RETURN_IF_MATCH(glCreateProgram)
    RETURN_IF_MATCH(glCreateShader)
    RETURN_IF_MATCH(glCullFace)
    RETURN_IF_MATCH(glDeleteBuffers)
    RETURN_IF_MATCH(glDeleteFramebuffers)
    RETURN_IF_MATCH(glDeleteProgram)
    RETURN_IF_MATCH(glDeleteRenderbuffers)
    RETURN_IF_MATCH(glDeleteShader)
    RETURN_IF_MATCH(glDeleteTextures)
    RETURN_IF_MATCH(glDepthMask)
    RETURN_IF_MATCH(glDisable)
    RETURN_IF_MATCH(glDisableVertexAttribArray)
    RETURN_IF_MATCH(glDrawArrays)
    RETURN_IF_MATCH(glDrawElements)
    RETURN_IF_MATCH(glEnable)
    RETURN_IF_MATCH(glEnableVertexAttribArray)
    RETURN_IF_MATCH(glFinish)
    RETURN_IF_MATCH(glFlush)
    RETURN_IF_MATCH(glFramebufferRenderbuffer)
    RETURN_IF_MATCH(glFramebufferTexture2D)
    RETURN_IF_MATCH(glFrontFace)
    RETURN_IF_MATCH(glGenBuffers)
    RETURN_IF_MATCH(glGenFramebuffers)
    RETURN_IF_MATCH(glGenRenderbuffers)
    RETURN_IF_MATCH(glGenTextures)
    RETURN_IF_MATCH(glGenerateMipmap)
    RETURN_IF_MATCH(glGetBufferParameteriv)
    RETURN_IF_MATCH(glGetError)
    RETURN_IF_MATCH(glGetFramebufferAttachmentParameteriv)
    RETURN_IF_MATCH(glGetIntegerv)
    RETURN_IF_MATCH(glGetProgramInfoLog)
    RETURN_IF_MATCH(glGetProgramiv)
    RETURN_IF_MATCH(glGetRenderbufferParameteriv)
    RETURN_IF_MATCH(glGetShaderInfoLog)
    RETURN_IF_MATCH(glGetShaderPrecisionFormat)
    RETURN_IF_MATCH(glGetShaderiv)
    RETURN_IF_MATCH(glGetString)
    RETURN_IF_MATCH(glGetUniformLocation)
    RETURN_IF_MATCH(glIsTexture)
    RETURN_IF_MATCH(glLineWidth)
    RETURN_IF_MATCH(glLinkProgram)
    RETURN_IF_MATCH(glPixelStorei)
    RETURN_IF_MATCH(glReadPixels)
    RETURN_IF_MATCH(glRenderbufferStorage)
    RETURN_IF_MATCH(glScissor)
    RETURN_IF_MATCH(glShaderSource)
    RETURN_IF_MATCH(glStencilFunc)
    RETURN_IF_MATCH(glStencilFuncSeparate)
    RETURN_IF_MATCH(glStencilMask)
    RETURN_IF_MATCH(glStencilMaskSeparate)
    RETURN_IF_MATCH(glStencilOp)
    RETURN_IF_MATCH(glStencilOpSeparate)
    RETURN_IF_MATCH(glTexImage2D)
    RETURN_IF_MATCH(glTexParameterf)
    RETURN_IF_MATCH(glTexParameterfv)
    RETURN_IF_MATCH(glTexParameteri)
    RETURN_IF_MATCH(glTexParameteriv)
    RETURN_IF_MATCH(glTexSubImage2D)
    RETURN_IF_MATCH(glUniform1f)
    RETURN_IF_MATCH(glUniform1fv)
    RETURN_IF_MATCH(glUniform1i)
    RETURN_IF_MATCH(glUniform1iv)
    RETURN_IF_MATCH(glUniform2f)
    RETURN_IF_MATCH(glUniform2fv)
    RETURN_IF_MATCH(glUniform2i)
    RETURN_IF_MATCH(glUniform2iv)
    RETURN_IF_MATCH(glUniform3f)
    RETURN_IF_MATCH(glUniform3fv)
    RETURN_IF_MATCH(glUniform3i)
    RETURN_IF_MATCH(glUniform3iv)
    RETURN_IF_MATCH(glUniform4f)
    RETURN_IF_MATCH(glUniform4fv)
    RETURN_IF_MATCH(glUniform4i)
    RETURN_IF_MATCH(glUniform4iv)
    RETURN_IF_MATCH(glUniformMatrix2fv)
    RETURN_IF_MATCH(glUniformMatrix3fv)
    RETURN_IF_MATCH(glUniformMatrix4fv)
    RETURN_IF_MATCH(glUseProgram)
    RETURN_IF_MATCH(glVertexAttrib1f)
    RETURN_IF_MATCH(glVertexAttrib2fv)
    RETURN_IF_MATCH(glVertexAttrib3fv)
    RETURN_IF_MATCH(glVertexAttrib4fv)
    RETURN_IF_MATCH(glVertexAttribPointer)
    RETURN_IF_MATCH(glViewport)

    #undef RETURN_IF_MATCH

    // For extension functions and functions not in the list above, use eglGetProcAddress
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

    // Log GL interface info
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

    LOG_INFO("  Backend render target: {}x{}, stencil={}",
             backendRT.width(), backendRT.height(), backendRT.stencilBits());

    // Create Skia surface wrapping the default framebuffer
    // Use kTopLeft_GrSurfaceOrigin for EGL/ANGLE (top-left origin)
    SkSurfaceProps surfaceProps(0, kUnknown_SkPixelGeometry);
    m_impl->surface = SkSurfaces::WrapBackendRenderTarget(
        m_impl->grContext.get(),
        backendRT,
        kTopLeft_GrSurfaceOrigin,
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
