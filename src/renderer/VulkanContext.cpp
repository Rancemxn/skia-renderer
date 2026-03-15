#include "VulkanContext.h"
#include "Swapchain.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <VkBootstrap.h>

#include <cstring>
#include <array>

namespace skia_renderer {

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    if (m_initialized) {
        shutdown();
    }
}

bool VulkanContext::initialize(SDL_Window* window) {
    if (!createInstance(window)) {
        return false;
    }

    if (!createDevice()) {
        return false;
    }

    // Create swapchain (no render pass needed for Skia Graphite)
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    
    m_swapchain = std::make_unique<Swapchain>();
    if (!m_swapchain->initialize(
        m_deviceInfo.physicalDevice,
        m_deviceInfo.device,
        m_surface,
        VK_NULL_HANDLE,  // No render pass - Skia manages rendering
        width,
        height)) {
        LOG_ERROR("Failed to create swapchain");
        return false;
    }

    if (!createSyncObjects()) {
        return false;
    }

    m_initialized = true;
    return true;
}

void VulkanContext::shutdown() {
    if (!m_initialized) {
        return;
    }

    waitIdle();

    // Cleanup synchronization objects
    for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++) {
        if (m_imageAvailableSemaphores[i]) {
            vkDestroySemaphore(m_deviceInfo.device, m_imageAvailableSemaphores[i], nullptr);
        }
    }
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_inFlightFences[i]) {
            vkDestroyFence(m_deviceInfo.device, m_inFlightFences[i], nullptr);
        }
    }

    // Cleanup swapchain
    m_swapchain.reset();

    // Cleanup device
    if (m_deviceInfo.device) {
        vkDestroyDevice(m_deviceInfo.device, nullptr);
        m_deviceInfo.device = VK_NULL_HANDLE;
    }

    // Cleanup surface
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    // Cleanup instance
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_initialized = false;
    LOG_INFO("Vulkan context destroyed.");
}

bool VulkanContext::createInstance(SDL_Window* window) {
    // Get required extensions from SDL
    Uint32 extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    
    std::vector<const char*> instanceExtensions;
    for (Uint32 i = 0; i < extensionCount; i++) {
        instanceExtensions.push_back(extensions[i]);
    }

    // Add required extensions for Skia Graphite
    instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#ifdef ENABLE_VULKAN_VALIDATION
    instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    LOG_DEBUG("Instance extensions:");
    for (const auto& ext : instanceExtensions) {
        LOG_DEBUG("  {}", ext);
    }

    vkb::InstanceBuilder builder;
    
#ifdef ENABLE_VULKAN_VALIDATION
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
        .require_api_version(1, 3)
        .request_validation_layers()
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
        .build();
#else
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
        .require_api_version(1, 3)
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .build();
#endif

    if (!instanceResult) {
        LOG_ERROR("Failed to create Vulkan instance: {}", instanceResult.error().message());
        return false;
    }

    vkb::Instance vkbInstance = instanceResult.value();
    m_instance = vkbInstance.instance;
    m_vkbInstance = std::move(vkbInstance);

    // Create surface
    if (!SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface)) {
        LOG_ERROR("Failed to create Vulkan surface: {}", SDL_GetError());
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanContext::createDevice() {
    if (m_vkbInstance.instance == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan instance not created via vk-bootstrap");
        return false;
    }
    
    vkb::PhysicalDeviceSelector selector(m_vkbInstance);
    
    auto physicalDeviceResult = selector
        .set_surface(m_surface)
        .set_minimum_version(1, 3)
        .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .select();

    if (!physicalDeviceResult) {
        LOG_ERROR("Failed to select physical device: {}", physicalDeviceResult.error().message());
        return false;
    }

    vkb::PhysicalDevice vkbPhysicalDevice = physicalDeviceResult.value();
    m_deviceInfo.physicalDevice = vkbPhysicalDevice.physical_device;

    // Store device properties
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_deviceInfo.physicalDevice, &props);
    m_deviceName = props.deviceName;
    
    // Print device info
    uint32_t apiVersion = props.apiVersion;
    LOG_INFO("Physical Device: {}", props.deviceName);
    LOG_INFO("  API Version: {}.{}.{}",
              VK_API_VERSION_MAJOR(apiVersion),
              VK_API_VERSION_MINOR(apiVersion),
              VK_API_VERSION_PATCH(apiVersion));
    LOG_INFO("  Driver Version: {}", props.driverVersion);

    // Enable Vulkan 1.3 features required for Graphite
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.synchronization2 = VK_TRUE;
    vulkan13Features.dynamicRendering = VK_TRUE;

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    deviceBuilder.add_pNext(&vulkan13Features);

    auto deviceResult = deviceBuilder.build();
    if (!deviceResult) {
        LOG_ERROR("Failed to create device: {}", deviceResult.error().message());
        return false;
    }

    vkb::Device vkbDevice = deviceResult.value();
    m_deviceInfo.device = vkbDevice.device;

    // Get queues
    auto graphicsQueueResult = vkbDevice.get_queue(vkb::QueueType::graphics);
    auto presentQueueResult = vkbDevice.get_queue(vkb::QueueType::present);
    
    if (!graphicsQueueResult || !presentQueueResult) {
        LOG_ERROR("Failed to get device queues");
        return false;
    }

    m_deviceInfo.graphicsQueue = graphicsQueueResult.value();
    m_deviceInfo.presentQueue = presentQueueResult.value();
    m_deviceInfo.graphicsFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    m_deviceInfo.presentFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::present).value();

    LOG_INFO("  Graphics Queue Family: {}", m_deviceInfo.graphicsFamilyIndex);
    LOG_INFO("  Present Queue Family: {}", m_deviceInfo.presentFamilyIndex);

    return true;
}

bool VulkanContext::createSyncObjects() {
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageToFrame.resize(m_swapchain->getImageCount(), UINT32_MAX);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(m_deviceInfo.device, &fenceInfo, nullptr, 
                          &m_inFlightFences[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create fences");
            return false;
        }
        if (vkCreateSemaphore(m_deviceInfo.device, &semaphoreInfo, nullptr, 
                              &m_imageAvailableSemaphores[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image available semaphores");
            return false;
        }
    }

    return true;
}

bool VulkanContext::beginFrame() {
    if (m_frameStarted) {
        return false;
    }

    // Wait for previous frame to complete
    vkWaitForFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame], 
                    VK_TRUE, UINT64_MAX);

    // Reset fence before acquire
    vkResetFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame]);

    // Acquire next swapchain image
    uint32_t imageIndex;
    VkResult result = m_swapchain->acquireNextImage(
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Failed to acquire swapchain image");
        return false;
    }

    m_currentImageIndex = imageIndex;

    // Track which frame is using this image
    m_imageToFrame[m_currentImageIndex] = m_currentFrame;

    m_frameStarted = true;
    return true;
}

void VulkanContext::endFrame(VkSemaphore renderFinishedSemaphore) {
    if (!m_frameStarted) {
        return;
    }

    // Present with synchronization
    // Wait on the render finished semaphore provided by Skia
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = (renderFinishedSemaphore != VK_NULL_HANDLE) ? 1 : 0;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchain = m_swapchain->getSwapchain();
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_deviceInfo.presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swapchain needs recreation
    } else if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to present swapchain image");
    }

    // Signal the in-flight fence to indicate this frame is complete
    // vkQueuePresentKHR doesn't accept a fence, so we use a null submit
    // with vkQueueSubmit2 (Vulkan 1.3 synchronization2) to signal the fence
    vkQueueSubmit2(m_deviceInfo.graphicsQueue, 0, nullptr, m_inFlightFences[m_currentFrame]);

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameStarted = false;
}

void VulkanContext::waitIdle() {
    if (m_deviceInfo.device) {
        vkDeviceWaitIdle(m_deviceInfo.device);
    }
}

void VulkanContext::resize(int width, int height) {
    waitIdle();
    m_swapchain->recreate(width, height);
}

VkExtent2D VulkanContext::getSwapchainExtent() const {
    return m_swapchain ? m_swapchain->getExtent() : VkExtent2D{0, 0};
}

VkFormat VulkanContext::getSwapchainFormat() const {
    return m_swapchain ? m_swapchain->getFormat() : VK_FORMAT_UNDEFINED;
}

VkSemaphore VulkanContext::getImageAvailableSemaphore() const {
    return m_imageAvailableSemaphores[m_currentFrame];
}

VkFence VulkanContext::getInFlightFence() const {
    return m_inFlightFences[m_currentFrame];
}

} // namespace skia_renderer
