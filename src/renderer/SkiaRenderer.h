#pragma once

#include "IRenderer.h"
#include <vulkan/vulkan.h>

#include <memory>

namespace skia_renderer {

class VulkanContext;
class DemoRenderer;

class SkiaRenderer : public IRenderer {
public:
    SkiaRenderer();
    ~SkiaRenderer() override;

    // Delete copy and move
    SkiaRenderer(const SkiaRenderer&) = delete;
    SkiaRenderer& operator=(const SkiaRenderer&) = delete;
    SkiaRenderer(SkiaRenderer&&) = delete;
    SkiaRenderer& operator=(SkiaRenderer&&) = delete;

    // IRenderer interface implementation
    bool initialize(SDL_Window* window, int width, int height, const BackendConfig& config) override;
    void shutdown() override;
    void resize(int width, int height) override;
    void render() override;
    void setFPS(float fps) override { m_fps = fps; }
    BackendType getBackendType() const override { return BackendType::Vulkan; }
    std::string getBackendName() const override { return "Graphite Vulkan"; }
    bool isInitialized() const override { return m_initialized; }
    bool beginFrame() override;
    void endFrame() override;

    // Vulkan-specific initialization (called by Application)
    bool initialize(VulkanContext* context, int width, int height);

    // Get the render finished semaphore for the current image (for presentation)
    VkSemaphore getRenderFinishedSemaphore() const;

private:
    bool createSkiaContext();
    bool createSwapchainSurfaces();        // Direct-to-swapchain mode
    bool createOffscreenRenderTarget();    // Fallback: offscreen + blit mode
    void destroyOffscreenRenderTarget();
    
    // Blit methods - uses synchronization2 API (Vulkan 1.3 or VK_KHR_synchronization2)
    void blitToSwapchain(VkImage srcImage, VkSemaphore waitSemaphore);
    
    // Vulkan 1.3 path - uses synchronization2 API
    void blitToSwapchainVulkan13(VkImage srcImage, VkSemaphore waitSemaphore);
    
    // Vulkan 1.1 fallback - uses legacy synchronization API
    void blitToSwapchainVulkan11(VkImage srcImage, VkSemaphore waitSemaphore);
    
    bool createBlitResources();            // Create command pool/buffer for blit
    bool checkDirectRenderingSupported() const;  // Check if GPU supports required flags

    VulkanContext* m_context = nullptr;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::unique_ptr<DemoRenderer> m_demoRenderer;

    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    bool m_useOffscreenRendering = false;  // Fallback mode when direct rendering not supported
    float m_fps = 0.0f;  // Current FPS for display
};

} // namespace skia_renderer
