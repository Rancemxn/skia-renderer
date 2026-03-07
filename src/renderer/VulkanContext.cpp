#include "VulkanContext.h"
#include "Swapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <VkBootstrap.h>

#include <iostream>
#include <cstring>

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

    // Cleanup synchronization objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_deviceInfo.device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(m_deviceInfo.device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_deviceInfo.device, m_inFlightFences[i], nullptr);
    }

    // Cleanup swapchain
    m_swapchain.reset();

    // Cleanup render pass
    if (m_renderPass) {
        vkDestroyRenderPass(m_deviceInfo.device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

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

    vkb::InstanceBuilder builder;
    
#ifdef ENABLE_VULKAN_VALIDATION
    // Enable validation layers in debug builds
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
        .request_validation_layers()
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
        .build();
#else
    // Release build - no validation layers
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
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

    // Store device name
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_deviceInfo.physicalDevice, &props);
    m_deviceName = props.deviceName;

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

    return true;
}

bool VulkanContext::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchain ? m_swapchain->getFormat() : VK_FORMAT_B8G8R8A8_SRGB;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(
        m_deviceInfo.device,
        &renderPassInfo,
        nullptr,
        &m_renderPass);

    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create render pass" << std::endl;
        return false;
    }

    return true;
}

bool VulkanContext::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_deviceInfo.device, &semaphoreInfo, nullptr, 
                              &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_deviceInfo.device, &semaphoreInfo, nullptr, 
                              &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_deviceInfo.device, &fenceInfo, nullptr, 
                          &m_inFlightFences[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create synchronization objects" << std::endl;
            return false;
        }
    }

    return true;
}

bool VulkanContext::beginFrame() {
    if (m_frameStarted) {
        return false;
    }

    vkWaitForFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame], 
                    VK_TRUE, UINT64_MAX);

    VkResult result = m_swapchain->acquireNextImage(
        m_imageAvailableSemaphores[m_currentFrame],
        &m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain needs recreation
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "Failed to acquire swapchain image" << std::endl;
        return false;
    }

    vkResetFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame]);
    
    m_frameStarted = true;
    return true;
}

void VulkanContext::endFrame() {
    if (!m_frameStarted) {
        return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrame];

    VkResult result = vkQueueSubmit(m_deviceInfo.graphicsQueue, 1, &submitInfo, 
                                     m_inFlightFences[m_currentFrame]);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to submit draw command buffer" << std::endl;
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchain = m_swapchain->getSwapchain();
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    result = vkQueuePresentKHR(m_deviceInfo.presentQueue, &presentInfo);

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

} // namespace skia_renderer
