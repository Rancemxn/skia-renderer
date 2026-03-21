#pragma once

#ifdef USE_ANGLE

#include "IRenderer.h"
#include <memory>

namespace skia_renderer {

class AngleContext;
class SceneRenderer;

/**
 * @brief ANGLE (OpenGL ES) renderer implementation
 * 
 * Implements the IRenderer interface using Skia's Ganesh backend with ANGLE.
 * ANGLE translates OpenGL ES to Vulkan/D3D11/Metal for cross-platform compatibility.
 */
class AngleRenderer : public IRenderer {
public:
    AngleRenderer();
    ~AngleRenderer() override;

    // IRenderer interface implementation
    bool initialize(SDL_Window* window, int width, int height, const BackendConfig& config) override;
    void shutdown() override;
    void resize(int width, int height) override;
    void render() override;
    void setFPS(float fps) override;
    BackendType getBackendType() const override { return BackendType::ANGLE; }
    std::string getBackendName() const override;
    bool isInitialized() const override { return m_initialized; }
    bool beginFrame() override;
    void endFrame() override;

private:
    bool createSkiaContext();
    bool createSurface();
    void destroySurface();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::unique_ptr<AngleContext> m_angleContext;
    std::unique_ptr<SceneRenderer> m_sceneRenderer;
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    float m_fps = 0.0f;
};

} // namespace skia_renderer

#endif // USE_ANGLE
