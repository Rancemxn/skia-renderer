#pragma once

#include <vulkan/vulkan.h>

#include <vector>
#include <string>
#include <memory>

// Forward declarations
struct SDL_Window;

// vk-bootstrap - include full header for Instance type
#include <VkBootstrap.h>

namespace skia_renderer {

class Swapchain;

struct VulkanDeviceInfo {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamilyIndex = 0;
    uint32_t presentFamilyIndex = 0;
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // Delete copy and move
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    bool initialize(SDL_Window* window);
    void shutdown();

    bool beginFrame();
    void endFrame();
    void waitIdle();
    void resize(int width, int height);

    // Getters
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_deviceInfo.physicalDevice; }
    VkDevice getDevice() const { return m_deviceInfo.device; }
    VkQueue getGraphicsQueue() const { return m_deviceInfo.graphicsQueue; }
    VkQueue getPresentQueue() const { return m_deviceInfo.presentQueue; }
    uint32_t getGraphicsFamilyIndex() const { return m_deviceInfo.graphicsFamilyIndex; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    VkFramebuffer getCurrentFramebuffer() const;
    VkExtent2D getSwapchainExtent() const;
    VkFormat getSwapchainFormat() const;
    std::string getDeviceName() const { return m_deviceName; }
    uint32_t getCurrentImageIndex() const { return m_currentImageIndex; }
    Swapchain* getSwapchain() const { return m_swapchain.get(); }
    
    // Get current command buffer for external rendering
    VkCommandBuffer getCurrentCommandBuffer() const;

private:
    bool createInstance(SDL_Window* window);
    bool createDevice();
    bool createRenderPass();
    bool createSyncObjects();
    bool createCommandPool();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    
    // vk-bootstrap Instance - must be kept alive for PhysicalDeviceSelector
    vkb::Instance m_vkbInstance;
    VulkanDeviceInfo m_deviceInfo;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    
    std::unique_ptr<Swapchain> m_swapchain;
    
    // Command buffers
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    
    // Synchronization
    // Per-frame semaphores for acquire (safe because we wait on fence before reusing)
    std::vector<VkSemaphore> m_imageAvailableSemaphores;   // MAX_FRAMES_IN_FLIGHT
    std::vector<VkSemaphore> m_renderFinishedSemaphores;   // Per image (present waits on these)
    std::vector<VkFence> m_inFlightFences;                  // Per frame
    std::vector<uint32_t> m_imageToFrame;                   // Which frame is using each image
    
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    
    std::string m_deviceName;
    bool m_initialized = false;
    bool m_frameStarted = false;
};

} // namespace skia_renderer
