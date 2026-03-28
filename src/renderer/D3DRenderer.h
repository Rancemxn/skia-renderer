#pragma once

#ifdef _WIN32

#include "IRenderer.h"
#include <memory>

namespace skia_renderer {

class D3DContext;
class SceneRenderer;

/**
 * @brief Ganesh D3D12 renderer implementation
 *
 * Implements the IRenderer interface using Skia's Ganesh backend with Direct3D 12.
 * This provides native Windows GPU acceleration without Vulkan or OpenGL.
 */
class D3DRenderer : public IRenderer {
public:
    D3DRenderer();
    ~D3DRenderer() override;

    // IRenderer interface implementation
    bool initialize(SDL_Window* window, int width, int height, const BackendConfig& config) override;
    void shutdown() override;
    void resize(int width, int height) override;
    void render() override;
    void setFPS(float fps) override;
    BackendType getBackendType() const override { return BackendType::D3D12; }
    std::string getBackendName() const override { return "Ganesh D3D12"; }
    bool isInitialized() const override { return m_initialized; }
    bool beginFrame() override;
    void endFrame() override;

private:
    bool createSkiaContext();
    bool createSurface();
    void destroySurface();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::unique_ptr<D3DContext> m_d3dContext;
    std::unique_ptr<SceneRenderer> m_sceneRenderer;
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    float m_fps = 0.0f;
};

#else

// Stub implementation for non-Windows platforms
#include "IRenderer.h"

namespace skia_renderer {

class D3DRenderer : public IRenderer {
public:
    D3DRenderer() = default;
    ~D3DRenderer() override = default;

    bool initialize(SDL_Window*, int, int, const BackendConfig&) override { return false; }
    void shutdown() override {}
    void resize(int, int) override {}
    void render() override {}
    void setFPS(float) override {}
    BackendType getBackendType() const override { return BackendType::D3D12; }
    std::string getBackendName() const override { return "Ganesh D3D12 (Unavailable)"; }
    bool isInitialized() const override { return false; }
    bool beginFrame() override { return false; }
    void endFrame() override {}
};

#endif // _WIN32

} // namespace skia_renderer
