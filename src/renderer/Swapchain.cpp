#include "Swapchain.h"

#include <iostream>
#include <algorithm>
#include <array>

namespace skia_renderer {

Swapchain::Swapchain() = default;

Swapchain::~Swapchain() {
    if (m_initialized) {
        shutdown();
    }
}

bool Swapchain::initialize(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkSurfaceKHR surface,
    VkRenderPass renderPass,
    int width,
    int height) {
    
    m_physicalDevice = physicalDevice;
    m_device = device;
    m_surface = surface;
    m_renderPass = renderPass;

    if (!createSwapchain(width, height)) {
        return false;
    }

    if (!createImageViews()) {
        return false;
    }

    // Only create framebuffers if render pass is provided (not for Skia Graphite)
    if (m_renderPass != VK_NULL_HANDLE) {
        if (!createFramebuffers()) {
            return false;
        }
    }

    m_initialized = true;
    return true;
}

void Swapchain::shutdown() {
    if (!m_initialized) {
        return;
    }

    cleanup();
    m_initialized = false;
}

void Swapchain::cleanup() {
    // Cleanup framebuffers
    for (auto framebuffer : m_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
    }
    m_framebuffers.clear();

    // Cleanup image views
    for (auto imageView : m_imageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
    }
    m_imageViews.clear();

    // Images are owned by swapchain, no need to destroy them
    m_images.clear();

    // Cleanup swapchain
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::recreate(int width, int height) {
    vkDeviceWaitIdle(m_device);
    
    cleanup();
    
    createSwapchain(width, height);
    createImageViews();
    
    if (m_renderPass != VK_NULL_HANDLE) {
        createFramebuffers();
    }
}

bool Swapchain::createSwapchain(int width, int height) {
    // Get surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);

    // Get surface formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    // Get present modes
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    // Choose surface format (prefer SRGB for correct color rendering)
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && 
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }
    m_format = surfaceFormat.format;

    // Choose present mode (prefer mailbox for low latency)
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = mode;
            break;
        }
    }

    // Choose extent
    if (capabilities.currentExtent.width != UINT32_MAX) {
        m_extent = capabilities.currentExtent;
    } else {
        m_extent.width = std::clamp(static_cast<uint32_t>(width), 
                                    capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
        m_extent.height = std::clamp(static_cast<uint32_t>(height),
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);
    }

    // Get image count (try for triple buffering)
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    // Create swapchain with usage flags suitable for Skia Graphite
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    // Add TRANSFER_DST for potential blit operations and COLOR_ATTACHMENT for rendering
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | 
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create swapchain: " << result << std::endl;
        return false;
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_images.data());

    std::cout << "Swapchain created: " << m_extent.width << "x" << m_extent.height 
              << ", " << imageCount << " images, format " << m_format << std::endl;
    
    return true;
}

bool Swapchain::createImageViews() {
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(m_device, &createInfo, nullptr, &m_imageViews[i]);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create image view " << i << ": " << result << std::endl;
            return false;
        }
    }

    return true;
}

bool Swapchain::createFramebuffers() {
    if (m_renderPass == VK_NULL_HANDLE) {
        return true;  // No render pass, no framebuffers needed
    }

    m_framebuffers.resize(m_imageViews.size());

    for (size_t i = 0; i < m_imageViews.size(); i++) {
        VkImageView attachments[] = {m_imageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_extent.width;
        framebufferInfo.height = m_extent.height;
        framebufferInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffers[i]);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create framebuffer " << i << ": " << result << std::endl;
            return false;
        }
    }

    return true;
}

VkResult Swapchain::acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) {
    return vkAcquireNextImageKHR(
        m_device,
        m_swapchain,
        UINT64_MAX,
        semaphore,
        fence,
        imageIndex);
}

VkFramebuffer Swapchain::getFramebuffer(size_t index) const {
    if (index < m_framebuffers.size()) {
        return m_framebuffers[index];
    }
    return VK_NULL_HANDLE;
}

VkImage Swapchain::getImage(size_t index) const {
    if (index < m_images.size()) {
        return m_images[index];
    }
    return VK_NULL_HANDLE;
}

VkImageView Swapchain::getImageView(size_t index) const {
    if (index < m_imageViews.size()) {
        return m_imageViews[index];
    }
    return VK_NULL_HANDLE;
}

} // namespace skia_renderer
