#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace skia_renderer {

class Swapchain {
public:
    Swapchain();
    ~Swapchain();

    // Delete copy and move
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    bool initialize(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkSurfaceKHR surface,
        VkRenderPass renderPass,  // Can be VK_NULL_HANDLE for Skia Graphite
        int width,
        int height);

    void shutdown();
    void recreate(int width, int height);

    VkResult acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex);
    VkSwapchainKHR getSwapchain() const { return m_swapchain; }
    VkFormat getFormat() const { return m_format; }
    VkExtent2D getExtent() const { return m_extent; }
    VkFramebuffer getFramebuffer(size_t index) const;  // May return VK_NULL_HANDLE if not created
    VkImage getImage(size_t index) const;
    VkImageView getImageView(size_t index) const;
    size_t getImageCount() const { return m_images.size(); }

private:
    bool createSwapchain(int width, int height);
    bool createImageViews();
    bool createFramebuffers();  // Only creates if renderPass is valid
    void cleanup();

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;  // Optional for Skia Graphite
    
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;  // May be empty for Skia Graphite
    
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent = {0, 0};
    
    bool m_initialized = false;
};

} // namespace skia_renderer
