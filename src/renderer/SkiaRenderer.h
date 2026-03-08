#pragma once

#include <vulkan/vulkan.h>

#include <memory>

namespace skia_renderer {

class VulkanContext;

class SkiaRenderer {
public:
    SkiaRenderer();
    ~SkiaRenderer();

    // Delete copy and move
    SkiaRenderer(const SkiaRenderer&) = delete;
    SkiaRenderer& operator=(const SkiaRenderer&) = delete;
    SkiaRenderer(SkiaRenderer&&) = delete;
    SkiaRenderer& operator=(SkiaRenderer&&) = delete;

    bool initialize(VulkanContext* context, int width, int height);
    void shutdown();
    void resize(int width, int height);

    // Skia Graphite handles its own rendering - call this to render and submit
    void render();

private:
    bool createSkiaContext();
    bool createRenderTarget();
    void drawContent();

    VulkanContext* m_context = nullptr;
    
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
};

} // namespace skia_renderer
