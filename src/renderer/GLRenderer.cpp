#include "GLRenderer.h"
#include "GLContext.h"
#include "core/Logger.h"

// Skia core headers
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include "include/core/SkRect.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"

// Skia platform-specific font manager headers
#if defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#elif defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#endif

// Skia Ganesh (OpenGL) headers
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/GrContextOptions.h"

// OpenGL headers
#include <SDL3/SDL_opengl.h>

#include <chrono>
#include <cmath>

namespace skia_renderer {

struct GLRenderer::Impl {
    sk_sp<GrDirectContext> grContext;
    sk_sp<SkSurface> surface;
    
    // Timing for animation
    std::chrono::high_resolution_clock::time_point startTime;
    
    // Font management
    sk_sp<SkFontMgr> fontMgr;
    sk_sp<SkTypeface> defaultTypeface;
    SkFont defaultFont;
    SkFont smallFont;
    bool fontsInitialized = false;
    
    // Debug
    uint64_t frameCount = 0;
};

GLRenderer::GLRenderer() : m_impl(std::make_unique<Impl>()) {}

GLRenderer::~GLRenderer() {
    if (m_initialized) {
        shutdown();
    }
}

bool GLRenderer::initialize(SDL_Window* window, int width, int height, const BackendConfig& config) {
    m_window = window;
    m_width = width;
    m_height = height;
    m_impl->startTime = std::chrono::high_resolution_clock::now();

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

    m_initialized = true;
    LOG_INFO("Skia Ganesh OpenGL renderer initialized ({}x{})", width, height);
    return true;
}

bool GLRenderer::createSkiaContext() {
    LOG_INFO("  Creating Skia Ganesh context...");

    // Initialize font manager (platform-specific)
    LOG_INFO("  Initializing font manager...");
#if defined(_WIN32)
    m_impl->fontMgr = SkFontMgr_New_DirectWrite();
#elif defined(__linux__)
    m_impl->fontMgr = SkFontMgr_New_FontConfig(nullptr, nullptr);
#elif defined(__APPLE__)
    m_impl->fontMgr = SkFontMgr_New_CoreText(nullptr);
#else
    m_impl->fontMgr = SkFontMgr::RefEmpty();
#endif
    if (!m_impl->fontMgr) {
        LOG_WARN("  Failed to create platform font manager, using empty font manager");
        m_impl->fontMgr = SkFontMgr::RefEmpty();
    }

    // Get a default typeface
    m_impl->defaultTypeface = m_impl->fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
    if (!m_impl->defaultTypeface) {
        LOG_WARN("  Failed to match default typeface, trying legacy method");
        int familyCount = m_impl->fontMgr->countFamilies();
        if (familyCount > 0) {
            m_impl->defaultTypeface = m_impl->fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
        }
    }

    if (m_impl->defaultTypeface) {
        LOG_INFO("  Font typeface loaded successfully");
        m_impl->defaultFont = SkFont(m_impl->defaultTypeface, 20.0f);
        m_impl->defaultFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        m_impl->defaultFont.setSubpixel(true);
        m_impl->defaultFont.setHinting(SkFontHinting::kSlight);

        m_impl->smallFont = SkFont(m_impl->defaultTypeface, 14.0f);
        m_impl->smallFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        m_impl->smallFont.setSubpixel(true);
        m_impl->smallFont.setHinting(SkFontHinting::kSlight);

        m_impl->fontsInitialized = true;
    } else {
        LOG_WARN("  No font typeface available, text rendering may fail");
    }

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

    // Get the default framebuffer (0 = window framebuffer)
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

    LOG_INFO("  Creating Skia surface (FBO: {}, {}x{})...", framebuffer, m_width, m_height);

    // Create backend render target wrapping the default framebuffer
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = static_cast<GrGLuint>(framebuffer);
    fbInfo.fFormat = GL_RGBA8;

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
    // Nothing special needed for OpenGL
    return m_initialized;
}

void GLRenderer::endFrame() {
    if (!m_impl->grContext) {
        return;
    }

    // Flush and submit Skia rendering
    m_impl->grContext->flushAndSubmit();

    // Swap buffers
    m_glContext->swapBuffers();
}

void GLRenderer::render() {
    if (!m_impl->grContext || !m_impl->surface) {
        LOG_ERROR("GLRenderer not initialized");
        return;
    }

    m_impl->frameCount++;

    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("Failed to get canvas");
        return;
    }

    // Calculate time for animation
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - m_impl->startTime).count();

    // Clear background with gradient
    SkPaint bgPaint;
    bgPaint.setAntiAlias(true);
    
    // Animated gradient background
    float hue1 = fmodf(time * 30.0f, 360.0f);
    float hue2 = fmodf(hue1 + 60.0f, 360.0f);
    
    // HSV to RGB conversion
    auto hsvToRgb = [](float h, float s, float v) -> SkColor {
        float c = v * s;
        float x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1));
        float m = v - c;
        float r, g, b;
        
        if (h < 60) { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }
        
        return SkColorSetARGB(255, 
            static_cast<uint8_t>((r + m) * 255),
            static_cast<uint8_t>((g + m) * 255),
            static_cast<uint8_t>((b + m) * 255));
    };
    
    SkColor color1 = hsvToRgb(hue1, 0.3f, 0.15f);
    (void)hue2; // Reserved for future gradient use
    
    canvas->clear(color1);

    // Draw animated rectangles
    int rectCount = 8;
    for (int i = 0; i < rectCount; i++) {
        float phase = time * 1.5f + i * 0.5f;
        float x = m_width * 0.1f + (m_width * 0.8f * (i / (float)rectCount));
        float y = m_height * 0.3f + sinf(phase) * (m_height * 0.15f);
        float rectWidth = 40 + cosf(phase * 0.7f) * 20;
        float rectHeight = 40 + cosf(phase * 0.5f) * 20;
        
        SkPaint rectPaint;
        rectPaint.setAntiAlias(true);
        rectPaint.setColor(hsvToRgb(fmodf(hue1 + i * 40, 360), 0.7f, 0.9f));
        rectPaint.setStyle(SkPaint::kFill_Style);
        
        SkRect rect = SkRect::MakeXYWH(x - rectWidth/2, y - rectHeight/2, rectWidth, rectHeight);
        
        // Rounded rectangle
        SkScalar radius = 8.0f + sinf(phase) * 4.0f;
        canvas->drawRoundRect(rect, radius, radius, rectPaint);
    }

    // Draw animated circles
    int circleCount = 6;
    for (int i = 0; i < circleCount; i++) {
        float angle = time + i * (3.14159f * 2 / circleCount);
        float centerX = m_width / 2.0f;
        float centerY = m_height / 2.0f;
        float radius = 150.0f + i * 30.0f;
        
        float x = centerX + cosf(angle) * radius;
        float y = centerY + sinf(angle) * radius;
        float circleSize = 15 + i * 5;
        
        SkPaint circlePaint;
        circlePaint.setAntiAlias(true);
        circlePaint.setColor(hsvToRgb(fmodf(hue2 + i * 30, 360), 0.6f, 0.85f));
        circlePaint.setStyle(SkPaint::kFill_Style);
        
        canvas->drawCircle(x, y, circleSize, circlePaint);
    }

    // Draw center text
    if (m_impl->fontsInitialized) {
        SkPaint textPaint;
        textPaint.setAntiAlias(true);
        textPaint.setColor(SK_ColorWHITE);
        
        // Draw title
        const char* title = "Skia Ganesh OpenGL";
        SkRect bounds;
        m_impl->defaultFont.measureText(title, strlen(title), SkTextEncoding::kUTF8, &bounds);
        float textX = (m_width - bounds.width()) / 2.0f;
        float textY = m_height / 2.0f - 60;
        
        // Shadow
        SkPaint shadowPaint;
        shadowPaint.setAntiAlias(true);
        shadowPaint.setColor(SkColorSetARGB(128, 0, 0, 0));
        canvas->drawSimpleText(title, strlen(title), SkTextEncoding::kUTF8,
                               textX + 2, textY + 2, m_impl->defaultFont, shadowPaint);
        
        // Main text
        canvas->drawSimpleText(title, strlen(title), SkTextEncoding::kUTF8,
                               textX, textY, m_impl->defaultFont, textPaint);
        
        // Draw FPS and frame info
        char infoText[256];
        snprintf(infoText, sizeof(infoText), "FPS: %.1f | Frame: %lu | %dx%d",
                 m_fps, static_cast<unsigned long>(m_impl->frameCount), m_width, m_height);
        
        bounds = SkRect();
        m_impl->smallFont.measureText(infoText, strlen(infoText), SkTextEncoding::kUTF8, &bounds);
        float infoX = (m_width - bounds.width()) / 2.0f;
        float infoY = textY + 35;
        
        canvas->drawSimpleText(infoText, strlen(infoText), SkTextEncoding::kUTF8,
                               infoX, infoY, m_impl->smallFont, textPaint);
        
        // Draw OpenGL info
        char glInfo[256];
        snprintf(glInfo, sizeof(glInfo), "Backend: %s | %s",
                 getBackendName().c_str(),
                 m_glContext->getGLRendererString().c_str());
        
        bounds = SkRect();
        m_impl->smallFont.measureText(glInfo, strlen(glInfo), SkTextEncoding::kUTF8, &bounds);
        float glInfoX = (m_width - bounds.width()) / 2.0f;
        float glInfoY = infoY + 25;
        
        canvas->drawSimpleText(glInfo, strlen(glInfo), SkTextEncoding::kUTF8,
                               glInfoX, glInfoY, m_impl->smallFont, textPaint);
    }

    // Flush context
    m_impl->grContext->flushAndSubmit();
}

void GLRenderer::setFPS(float fps) {
    m_fps = fps;
}

} // namespace skia_renderer
