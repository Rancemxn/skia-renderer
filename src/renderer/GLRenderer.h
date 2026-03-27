#pragma once

#include "IRenderer.h"
#include <memory>

namespace skia_renderer {

class GLContext;
class SceneRenderer;

/**
 * @brief Ganesh OpenGL renderer implementation
 * 
 * Implements the IRenderer interface using Skia's Ganesh backend with OpenGL.
 * This provides broader hardware compatibility compared to the Vulkan backend.
 */
class GLRenderer : public IRenderer {
public:
    GLRenderer();
    ~GLRenderer() override;

    // IRenderer interface implementation
    bool initialize(SDL_Window* window, int width, int height, const BackendConfig& config) override;
    void shutdown() override;
    void resize(int width, int height) override;
    void render() override;
    void setFPS(float fps) override;
    BackendType getBackendType() const override { return BackendType::OpenGL; }
    std::string getBackendName() const override { return "Ganesh OpenGL"; }
    bool isInitialized() const override { return m_initialized; }
    bool beginFrame() override;
    void endFrame() override;

private:
    bool createSkiaContext();
    bool createSurface();
    void destroySurface();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::unique_ptr<GLContext> m_glContext;
    std::unique_ptr<SceneRenderer> m_sceneRenderer;
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    float m_fps = 0.0f;
};

} // namespace skia_renderer
