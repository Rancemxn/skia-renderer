#include "DemoRenderer.h"
#include "core/Logger.h"

#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include "include/core/SkRect.h"
#include "include/core/SkColorSpace.h"

// Skia platform-specific font manager headers
#if defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#elif defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#endif

#include <cmath>

namespace skia_renderer {

DemoRenderer::DemoRenderer()
    : m_startTime(std::chrono::high_resolution_clock::now()) {
}

DemoRenderer::~DemoRenderer() = default;

bool DemoRenderer::initializeFonts() {
    LOG_INFO("  Initializing font manager...");
    
#if defined(_WIN32)
    m_fontMgr = SkFontMgr_New_DirectWrite();
#elif defined(__linux__)
    m_fontMgr = SkFontMgr_New_FontConfig(nullptr, nullptr);
#elif defined(__APPLE__)
    m_fontMgr = SkFontMgr_New_CoreText(nullptr);
#else
    m_fontMgr = SkFontMgr::RefEmpty();
#endif

    if (!m_fontMgr) {
        LOG_WARN("  Failed to create platform font manager, using empty font manager");
        m_fontMgr = SkFontMgr::RefEmpty();
    }

    // Get a default typeface
    m_defaultTypeface = m_fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
    if (!m_defaultTypeface) {
        LOG_WARN("  Failed to match default typeface, trying legacy method");
        int familyCount = m_fontMgr->countFamilies();
        if (familyCount > 0) {
            m_defaultTypeface = m_fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
        }
    }

    if (m_defaultTypeface) {
        LOG_INFO("  Font typeface loaded successfully");
        m_defaultFont = SkFont(m_defaultTypeface, 20.0f);
        m_defaultFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        m_defaultFont.setSubpixel(true);
        m_defaultFont.setHinting(SkFontHinting::kSlight);

        m_smallFont = SkFont(m_defaultTypeface, 14.0f);
        m_smallFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        m_smallFont.setSubpixel(true);
        m_smallFont.setHinting(SkFontHinting::kSlight);

        m_fontsInitialized = true;
    } else {
        LOG_WARN("  No font typeface available, text rendering may fail");
    }

    return m_fontsInitialized;
}

void DemoRenderer::render(SkCanvas* canvas, int width, int height,
                          const std::string& backendInfo, const std::string& rendererName) {
    if (!canvas) {
        return;
    }

    m_frameCount++;

    // Calculate time for animation
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - m_startTime).count();

    // Draw all components
    drawBackground(canvas, width, height, time);
    drawAnimatedShapes(canvas, width, height, time);
    drawText(canvas, width, height, time, backendInfo, rendererName);
}

void DemoRenderer::drawBackground(SkCanvas* canvas, int width, int height, float time) {
    // Animated gradient background
    float hue1 = fmodf(time * 30.0f, 360.0f);
    
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
    
    // Dark gradient background with subtle animation
    SkColor bgColor = hsvToRgb(hue1, 0.3f, 0.12f);
    canvas->clear(bgColor);

    // Draw subtle grid pattern
    SkPaint gridPaint;
    gridPaint.setAntiAlias(true);
    gridPaint.setColor(SkColorSetARGB(30, 255, 255, 255));
    gridPaint.setStrokeWidth(1.0f);
    gridPaint.setStyle(SkPaint::kStroke_Style);

    int gridSize = 50;
    for (int x = 0; x < width; x += gridSize) {
        canvas->drawLine(x, 0, x, height, gridPaint);
    }
    for (int y = 0; y < height; y += gridSize) {
        canvas->drawLine(0, y, width, y, gridPaint);
    }
}

void DemoRenderer::drawAnimatedShapes(SkCanvas* canvas, int width, int height, float time) {
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

    float hue1 = fmodf(time * 30.0f, 360.0f);
    float centerX = width / 2.0f;
    float centerY = height / 2.0f;

    // Draw rotating rectangle
    SkPaint paint;
    paint.setColor(SkColorSetARGB(255, 100, 180, 255));
    paint.setAntiAlias(true);

    float size = std::min(static_cast<float>(width), static_cast<float>(height)) * 0.15f;

    canvas->save();
    canvas->translate(centerX, centerY);
    canvas->rotate(time * 45.0f);

    SkRect rect = SkRect::MakeXYWH(-size/2, -size/2, size, size);
    canvas->drawRect(rect, paint);
    canvas->restore();

    // Draw pulsing circle
    float pulseScale = 1.0f + 0.3f * std::sin(time * 3.0f);
    paint.setColor(SkColorSetARGB(200, 255, 120, 100));
    canvas->drawCircle(
        centerX + std::sin(time * 1.5f) * 80,
        centerY + std::cos(time * 1.5f) * 80,
        25 * pulseScale,
        paint
    );

    // Draw animated rectangles
    int rectCount = 8;
    for (int i = 0; i < rectCount; i++) {
        float phase = time * 1.5f + i * 0.5f;
        float x = width * 0.1f + (width * 0.8f * (i / (float)rectCount));
        float y = height * 0.3f + sinf(phase) * (height * 0.15f);
        float rectWidth = 40 + cosf(phase * 0.7f) * 20;
        float rectHeight = 40 + cosf(phase * 0.5f) * 20;
        
        SkPaint rectPaint;
        rectPaint.setAntiAlias(true);
        rectPaint.setColor(hsvToRgb(fmodf(hue1 + i * 40, 360), 0.7f, 0.9f));
        rectPaint.setStyle(SkPaint::kFill_Style);
        
        SkRect shapeRect = SkRect::MakeXYWH(x - rectWidth/2, y - rectHeight/2, rectWidth, rectHeight);
        
        // Rounded rectangle
        SkScalar radius = 8.0f + sinf(phase) * 4.0f;
        canvas->drawRoundRect(shapeRect, radius, radius, rectPaint);
    }

    // Draw animated circles
    int circleCount = 6;
    for (int i = 0; i < circleCount; i++) {
        float angle = time + i * (3.14159f * 2 / circleCount);
        float radius = 150.0f + i * 30.0f;
        
        float x = centerX + cosf(angle) * radius;
        float y = centerY + sinf(angle) * radius;
        float circleSize = 15 + i * 5;
        
        SkPaint circlePaint;
        circlePaint.setAntiAlias(true);
        circlePaint.setColor(hsvToRgb(fmodf(hue1 + 60 + i * 30, 360), 0.6f, 0.85f));
        circlePaint.setStyle(SkPaint::kFill_Style);
        
        canvas->drawCircle(x, y, circleSize, circlePaint);
    }
}

void DemoRenderer::drawText(SkCanvas* canvas, int width, int height, float time,
                            const std::string& backendInfo, const std::string& rendererName) {
    (void)time; // Reserved for future animations
    
    // Draw text
    SkPaint textPaint;
    textPaint.setColor(SK_ColorWHITE);
    textPaint.setAntiAlias(true);

    if (m_fontsInitialized) {
        // Draw title
        std::string title = rendererName;
        SkRect bounds;
        m_defaultFont.measureText(title.c_str(), title.length(), SkTextEncoding::kUTF8, &bounds);
        float textX = (width - bounds.width()) / 2.0f;
        float textY = height / 2.0f - 60;
        
        // Shadow
        SkPaint shadowPaint;
        shadowPaint.setAntiAlias(true);
        shadowPaint.setColor(SkColorSetARGB(128, 0, 0, 0));
        canvas->drawSimpleText(title.c_str(), title.length(), SkTextEncoding::kUTF8,
                               textX + 2, textY + 2, m_defaultFont, shadowPaint);
        
        // Main text
        canvas->drawSimpleText(title.c_str(), title.length(), SkTextEncoding::kUTF8,
                               textX, textY, m_defaultFont, textPaint);
        
        // Draw FPS and frame info
        char infoText[256];
        snprintf(infoText, sizeof(infoText), "FPS: %.1f | Frame: %lu | %dx%d",
                 m_fps, static_cast<unsigned long>(m_frameCount), width, height);
        
        bounds = SkRect();
        m_smallFont.measureText(infoText, strlen(infoText), SkTextEncoding::kUTF8, &bounds);
        float infoX = (width - bounds.width()) / 2.0f;
        float infoY = textY + 35;
        
        canvas->drawSimpleText(infoText, strlen(infoText), SkTextEncoding::kUTF8,
                               infoX, infoY, m_smallFont, textPaint);
        
        // Draw backend info
        char backendText[256];
        snprintf(backendText, sizeof(backendText), "Backend: %s",
                 backendInfo.c_str());
        
        bounds = SkRect();
        m_smallFont.measureText(backendText, strlen(backendText), SkTextEncoding::kUTF8, &bounds);
        float backendX = (width - bounds.width()) / 2.0f;
        float backendY = infoY + 25;
        
        canvas->drawSimpleText(backendText, strlen(backendText), SkTextEncoding::kUTF8,
                               backendX, backendY, m_smallFont, textPaint);

        // Draw instructions
        textPaint.setColor(SkColorSetARGB(180, 200, 200, 200));
        canvas->drawString("Press ESC to exit", 20, height - 25, m_smallFont, textPaint);
        canvas->drawString("Press V to switch backend", 20, height - 45, m_smallFont, textPaint);
        
    } else {
        // Fallback: try to draw without pre-loaded font
        SkFont fallbackFont;
        fallbackFont.setSize(20);
        
        std::string title = rendererName;
        SkRect bounds;
        fallbackFont.measureText(title.c_str(), title.length(), SkTextEncoding::kUTF8, &bounds);
        float textX = (width - bounds.width()) / 2.0f;
        float textY = height / 2.0f - 60;
        
        canvas->drawSimpleText(title.c_str(), title.length(), SkTextEncoding::kUTF8,
                               textX, textY, fallbackFont, textPaint);
        
        char infoText[256];
        snprintf(infoText, sizeof(infoText), "FPS: %.1f | Frame: %lu | %dx%d",
                 m_fps, static_cast<unsigned long>(m_frameCount), width, height);
        
        fallbackFont.setSize(14);
        bounds = SkRect();
        fallbackFont.measureText(infoText, strlen(infoText), SkTextEncoding::kUTF8, &bounds);
        float infoX = (width - bounds.width()) / 2.0f;
        float infoY = textY + 35;
        
        canvas->drawSimpleText(infoText, strlen(infoText), SkTextEncoding::kUTF8,
                               infoX, infoY, fallbackFont, textPaint);

        char backendText[256];
        snprintf(backendText, sizeof(backendText), "Backend: %s",
                 backendInfo.c_str());
        
        bounds = SkRect();
        fallbackFont.measureText(backendText, strlen(backendText), SkTextEncoding::kUTF8, &bounds);
        float backendX = (width - bounds.width()) / 2.0f;
        float backendY = infoY + 25;
        
        canvas->drawSimpleText(backendText, strlen(backendText), SkTextEncoding::kUTF8,
                               backendX, backendY, fallbackFont, textPaint);
    }
}

} // namespace skia_renderer
