#pragma once

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontMgr.h"

#include <memory>
#include <string>
#include <chrono>

namespace skia_renderer {

/**
 * @brief Scene rendering component
 * 
 * This class provides the main rendering implementation that can be used
 * by both Vulkan (Graphite) and OpenGL (Ganesh) backends to ensure
 * consistent visual output across different rendering APIs.
 */
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    // Initialize fonts (call once after Skia context is ready)
    bool initializeFonts();

    /**
     * @brief Render a frame to the canvas
     * @param canvas The Skia canvas to render to
     * @param width Canvas width
     * @param height Canvas height
     * @param backendInfo Backend info string (e.g., "Vulkan 1.3" or "OpenGL 3.3")
     * @param rendererName Renderer name (e.g., "Skia Graphite" or "Skia Ganesh")
     */
    void render(SkCanvas* canvas, int width, int height, 
                const std::string& backendInfo, const std::string& rendererName);

    /**
     * @brief Set FPS for display
     * @param fps Current frames per second
     */
    void setFPS(float fps) { m_fps = fps; }

    /**
     * @brief Get current frame count
     */
    uint64_t getFrameCount() const { return m_frameCount; }

    /**
     * @brief Check if fonts are initialized
     */
    bool areFontsInitialized() const { return m_fontsInitialized; }

private:
    // Drawing helpers
    void drawBackground(SkCanvas* canvas, int width, int height, float time);
    void drawAnimatedShapes(SkCanvas* canvas, int width, int height, float time);
    void drawText(SkCanvas* canvas, int width, int height, float time,
                  const std::string& backendInfo, const std::string& rendererName);

    // Font management
    sk_sp<SkFontMgr> m_fontMgr;
    sk_sp<SkTypeface> m_defaultTypeface;
    SkFont m_titleFont;
    SkFont m_defaultFont;
    SkFont m_smallFont;
    bool m_fontsInitialized = false;

    // Timing
    std::chrono::high_resolution_clock::time_point m_startTime;
    float m_fps = 0.0f;
    uint64_t m_frameCount = 0;
};

} // namespace skia_renderer
