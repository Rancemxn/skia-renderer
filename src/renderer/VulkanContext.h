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

    // Swapchain management for Skia Graphite integration
    bool beginFrame();  // Acquires swapchain image
    void endFrame();    // Presents swapchain image
    
    void waitIdle();
    void resize(int width, int height);

    // Getters for Skia Graphite
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_deviceInfo.physicalDevice; }
    VkDevice getDevice() const { return m_deviceInfo.device; }
    VkQueue getGraphicsQueue() const { return m_deviceInfo.graphicsQueue; }
    VkQueue getPresentQueue() const { return m_deviceInfo.presentQueue; }
    uint32_t getGraphicsFamilyIndex() const { return m_deviceInfo.graphicsFamilyIndex; }
    
    VkExtent2D getSwapchainExtent() const;
    VkFormat getSwapchainFormat() const;
    std::string getDeviceName() const { return m_deviceName; }
    uint32_t getCurrentImageIndex() const { return m_currentImageIndex; }
    Swapchain* getSwapchain() const { return m_swapchain.get(); }
    
    // Synchronization objects for Skia Graphite
    VkSemaphore getImageAvailableSemaphore() const;
    VkSemaphore getRenderFinishedSemaphore() const;
    VkFence getInFlightFence() const;

private:
    bool createInstance(SDL_Window* window);
    bool createDevice();
    bool createSyncObjects();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    
    // vk-bootstrap Instance - must be kept alive for PhysicalDeviceSelector
    vkb::Instance m_vkbInstance;
    VulkanDeviceInfo m_deviceInfo;
    
    std::unique_ptr<Swapchain> m_swapchain;
    
    // Synchronization
    std::vector<VkSemaphore> m_imageAvailableSemaphores;   // MAX_FRAMES_IN_FLIGHT
    std::vector<VkSemaphore> m_renderFinishedSemaphores;   // Per image
    std::vector<VkFence> m_inFlightFences;                  // MAX_FRAMES_IN_FLIGHT
    std::vector<uint32_t> m_imageToFrame;
    
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    
    std::string m_deviceName;
    bool m_initialized = false;
    bool m_frameStarted = false;
};

} // namespace skia_renderer
