#include "VulkanContext.h"
#include "Swapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <VkBootstrap.h>

#include <iostream>
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

    if (!createCommandPool()) {
        return false;
    }

    if (!createRenderPass()) {
        return false;
    }

    // Create swapchain
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    
    m_swapchain = std::make_unique<Swapchain>();
    if (!m_swapchain->initialize(
        m_deviceInfo.physicalDevice,
        m_deviceInfo.device,
        m_surface,
        m_renderPass,
        width,
        height)) {
        std::cerr << "Failed to create swapchain" << std::endl;
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

    // Cleanup command buffers and pool
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_deviceInfo.device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    m_commandBuffers.clear();

    // Cleanup synchronization objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(m_deviceInfo.device, m_inFlightFences[i], nullptr);
    }
    
    // Cleanup per-image render finished semaphores
    for (size_t i = 0; i < m_renderFinishedSemaphores.size(); i++) {
        vkDestroySemaphore(m_deviceInfo.device, m_renderFinishedSemaphores[i], nullptr);
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
    std::cout << "Vulkan context destroyed." << std::endl;
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
    // Add debug utils extension for validation layers
    instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    std::cout << "Instance extensions:" << std::endl;
    for (const auto& ext : instanceExtensions) {
        std::cout << "  " << ext << std::endl;
    }

    vkb::InstanceBuilder builder;
    
#ifdef ENABLE_VULKAN_VALIDATION
    // Enable validation layers in debug builds
    // Set Vulkan 1.3 as minimum required version for Skia Graphite
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
        .require_api_version(1, 3)
        .request_validation_layers()
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
        .build();
#else
    // Release build - no validation layers
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
        .require_api_version(1, 3)
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .build();
#endif

    if (!instanceResult) {
        std::cerr << "Failed to create Vulkan instance: " 
                  << instanceResult.error().message() << std::endl;
        return false;
    }

    vkb::Instance vkbInstance = instanceResult.value();
    m_instance = vkbInstance.instance;
    m_vkbInstance = std::move(vkbInstance);  // Save for later use

    // Create surface
    if (!SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface)) {
        std::cerr << "Failed to create Vulkan surface: " << SDL_GetError() << std::endl;
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanContext::createDevice() {
    if (m_vkbInstance.instance == VK_NULL_HANDLE) {
        std::cerr << "Vulkan instance not created via vk-bootstrap" << std::endl;
        return false;
    }
    
    vkb::PhysicalDeviceSelector selector(m_vkbInstance);
    
    // Vulkan 1.3 is required for Skia Graphite
    auto physicalDeviceResult = selector
        .set_surface(m_surface)
        .set_minimum_version(1, 3)
        .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .select();

    if (!physicalDeviceResult) {
        std::cerr << "Failed to select physical device: " 
                  << physicalDeviceResult.error().message() << std::endl;
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
    std::cout << "Physical Device: " << props.deviceName << std::endl;
    std::cout << "  API Version: " 
              << VK_API_VERSION_MAJOR(apiVersion) << "."
              << VK_API_VERSION_MINOR(apiVersion) << "."
              << VK_API_VERSION_PATCH(apiVersion) << std::endl;
    std::cout << "  Driver Version: " << props.driverVersion << std::endl;

    // Enable Vulkan 1.3 features required for Graphite
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.synchronization2 = VK_TRUE;
    vulkan13Features.dynamicRendering = VK_TRUE;

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    deviceBuilder.add_pNext(&vulkan13Features);

    auto deviceResult = deviceBuilder.build();
    if (!deviceResult) {
        std::cerr << "Failed to create device: " 
                  << deviceResult.error().message() << std::endl;
        return false;
    }

    vkb::Device vkbDevice = deviceResult.value();
    m_deviceInfo.device = vkbDevice.device;

    // Get queues
    auto graphicsQueueResult = vkbDevice.get_queue(vkb::QueueType::graphics);
    auto presentQueueResult = vkbDevice.get_queue(vkb::QueueType::present);
    
    if (!graphicsQueueResult || !presentQueueResult) {
        std::cerr << "Failed to get device queues" << std::endl;
        return false;
    }

    m_deviceInfo.graphicsQueue = graphicsQueueResult.value();
    m_deviceInfo.presentQueue = presentQueueResult.value();
    m_deviceInfo.graphicsFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    m_deviceInfo.presentFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::present).value();

    std::cout << "  Graphics Queue Family: " << m_deviceInfo.graphicsFamilyIndex << std::endl;
    std::cout << "  Present Queue Family: " << m_deviceInfo.presentFamilyIndex << std::endl;

    return true;
}

bool VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_deviceInfo.graphicsFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_deviceInfo.device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool" << std::endl;
        return false;
    }

    // Allocate command buffers (one per frame in flight)
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_deviceInfo.device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffers" << std::endl;
        return false;
    }

    return true;
}

bool VulkanContext::createRenderPass() {
    // We don't use a Vulkan render pass anymore - Skia Graphite handles rendering directly
    // Keep the renderPass member as VK_NULL_HANDLE
    m_renderPass = VK_NULL_HANDLE;
    return true;
}

bool VulkanContext::createSyncObjects() {
    // Create per-frame-in-flight fences
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imagesInFlight.resize(m_swapchain->getImageCount(), UINT32_MAX);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(m_deviceInfo.device, &fenceInfo, nullptr, 
                          &m_inFlightFences[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create fences" << std::endl;
            return false;
        }
    }

    // Create per-swapchain-image semaphores for render completion signaling
    size_t imageCount = m_swapchain->getImageCount();
    m_renderFinishedSemaphores.resize(imageCount);

    for (size_t i = 0; i < imageCount; i++) {
        if (vkCreateSemaphore(m_deviceInfo.device, &semaphoreInfo, nullptr, 
                              &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create render finished semaphores" << std::endl;
            return false;
        }
    }

    return true;
}

bool VulkanContext::beginFrame() {
    if (m_frameStarted) {
        return false;
    }

    // Wait for the frame-in-flight to complete
    vkWaitForFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame], 
                    VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image using the frame's fence for synchronization
    uint32_t imageIndex;
    VkResult result = m_swapchain->acquireNextImage(
        VK_NULL_HANDLE,
        m_inFlightFences[m_currentFrame],
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain needs recreation
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "Failed to acquire swapchain image" << std::endl;
        return false;
    }

    // Store the acquired image index
    m_currentImageIndex = imageIndex;

    // Check if a previous frame is using this image - wait for its fence
    if (m_imagesInFlight[m_currentImageIndex] != UINT32_MAX) {
        vkWaitForFences(m_deviceInfo.device, 1, 
                        &m_inFlightFences[m_imagesInFlight[m_currentImageIndex]], 
                        VK_TRUE, UINT64_MAX);
    }
    m_imagesInFlight[m_currentImageIndex] = m_currentFrame;

    // Wait for the acquire to complete (fence was signaled by acquireNextImage)
    vkWaitForFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame], 
                    VK_TRUE, UINT64_MAX);
    vkResetFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame]);

    m_frameStarted = true;
    return true;
}

void VulkanContext::endFrame() {
    if (!m_frameStarted) {
        return;
    }

    // Wait for Skia's GPU work to complete before presenting
    vkDeviceWaitIdle(m_deviceInfo.device);

    // Present - wait on the per-image render finished semaphore
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 0;  // Skia handles its own synchronization
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchain = m_swapchain->getSwapchain();
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_deviceInfo.presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Need to recreate swapchain
    } else if (result != VK_SUCCESS) {
        std::cerr << "Failed to present swapchain image" << std::endl;
    }

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

VkFramebuffer VulkanContext::getCurrentFramebuffer() const {
    return m_swapchain ? m_swapchain->getFramebuffer(m_currentImageIndex) : VK_NULL_HANDLE;
}

VkExtent2D VulkanContext::getSwapchainExtent() const {
    return m_swapchain ? m_swapchain->getExtent() : VkExtent2D{0, 0};
}

VkFormat VulkanContext::getSwapchainFormat() const {
    return m_swapchain ? m_swapchain->getFormat() : VK_FORMAT_UNDEFINED;
}

VkCommandBuffer VulkanContext::getCurrentCommandBuffer() const {
    return m_commandBuffers[m_currentFrame];
}

} // namespace skia_renderer
