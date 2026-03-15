#include "Swapchain.h"
#include "core/Logger.h"

#include <algorithm>
#include <limits>

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

    // Only create framebuffers if render pass is valid (not for Skia Graphite)
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

void Swapchain::recreate(int width, int height) {
    if (!m_initialized) {
        return;
    }
    vkDeviceWaitIdle(m_device);
    cleanup();
    createSwapchain(width, height);
    createImageViews();
    if (m_renderPass != VK_NULL_HANDLE) {
        createFramebuffers();
    }
}

void Swapchain::cleanup() {
    // Destroy framebuffers
    for (auto fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, fb, nullptr);
        }
    }
    m_framebuffers.clear();

    // Destroy image views
    for (auto iv : m_imageViews) {
        if (iv != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, iv, nullptr);
        }
    }
    m_imageViews.clear();

    // Images are owned by swapchain
    m_images.clear();

    // Destroy swapchain
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
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

    // Choose surface format - prefer UNORM over SRGB for Skia Graphite compatibility
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM) {
            surfaceFormat = fmt;
            break;
        }
    }
    if (surfaceFormat.format != VK_FORMAT_B8G8R8A8_UNORM) {
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_R8G8B8A8_UNORM) {
                surfaceFormat = fmt;
                break;
            }
        }
    }
    m_format = surfaceFormat.format;

    // Choose present mode - prefer low-latency modes
    // Priority: MAILBOX > IMMEDIATE > FIFO_RELAXED > FIFO
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;  // Fallback
    bool hasMailbox = false, hasImmediate = false, hasFifoRelaxed = false;
    
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) hasMailbox = true;
        else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) hasImmediate = true;
        else if (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) hasFifoRelaxed = true;
    }
    
    // Select based on priority
    if (hasMailbox) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        LOG_INFO("Present mode: MAILBOX (low latency, no tearing)");
    } else if (hasImmediate) {
        presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        LOG_INFO("Present mode: IMMEDIATE (lowest latency, possible tearing)");
    } else if (hasFifoRelaxed) {
        presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        LOG_INFO("Present mode: FIFO_RELAXED (low latency, occasional tearing)");
    } else {
        LOG_INFO("Present mode: FIFO (VSync, stable)");
    }
    
    m_presentMode = presentMode;

    // Choose extent
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        m_extent = capabilities.currentExtent;
    } else {
        m_extent.width = std::clamp(static_cast<uint32_t>(width),
                                    capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
        m_extent.height = std::clamp(static_cast<uint32_t>(height),
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);
    }

    // Image count
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    // Build usage flags for Skia Graphite
    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    // Add optional flags if supported
    if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
        usageFlags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }
    if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_SAMPLED_BIT) {
        usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    
    m_imageUsageFlags = usageFlags;
    LOG_DEBUG("Swapchain usage flags: 0x{:x}", usageFlags);

    // Create swapchain
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = usageFlags;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        LOG_ERROR("Failed to create swapchain");
        return false;
    }

    // Get images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_images.data());

    LOG_INFO("Swapchain created: {}x{}, {} images, format {}", 
              m_extent.width, m_extent.height, imageCount, static_cast<int>(m_format));
    
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

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image view {}", i);
            return false;
        }
    }

    return true;
}

bool Swapchain::createFramebuffers() {
    if (m_renderPass == VK_NULL_HANDLE) {
        return true;
    }

    m_framebuffers.resize(m_imageViews.size());

    for (size_t i = 0; i < m_imageViews.size(); i++) {
        VkImageView attachments[] = {m_imageViews[i]};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_extent.width;
        fbInfo.height = m_extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create framebuffer {}", i);
            return false;
        }
    }

    return true;
}

VkResult Swapchain::acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) {
    return vkAcquireNextImageKHR(
        m_device,
        m_swapchain,
        std::numeric_limits<uint64_t>::max(),
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
