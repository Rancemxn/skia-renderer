#include "SkiaRenderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"

// Skia core headers
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include "include/core/SkRect.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkImageInfo.h"

// Skia GPU headers
#include "include/gpu/MutableTextureState.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"

// Skia Graphite headers
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/BackendSemaphore.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteTypes.h"
#include "include/gpu/graphite/TextureInfo.h"

// Skia internal headers
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include <iostream>
#include <chrono>
#include <cmath>

namespace skia_renderer {

struct SkiaRenderer::Impl {
    std::unique_ptr<skgpu::graphite::Context> graphiteContext;
    std::unique_ptr<skgpu::graphite::Recorder> recorder;
    skgpu::VulkanExtensions vkExtensions;
    VkPhysicalDeviceFeatures physicalDeviceFeatures{};
    sk_sp<skgpu::VulkanMemoryAllocator> vulkanAllocator;

    // Per-swapchain-image data
    struct SwapchainImageData {
        // For direct rendering mode
        sk_sp<SkSurface> surface;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    };
    std::vector<SwapchainImageData> swapchainImages;

    // For offscreen rendering mode (fallback)
    struct OffscreenRenderTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        sk_sp<SkSurface> surface;
        VkSemaphore skiaFinishedSemaphore = VK_NULL_HANDLE;
        VkFence skiaFinishedFence = VK_NULL_HANDLE;
    } offscreenRT;

    // Blit resources (for fallback mode) - per swapchain image
    VkCommandPool blitCommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> blitCommandBuffers;
    std::vector<VkFence> blitFences;

    // Timing for animation
    std::chrono::high_resolution_clock::time_point startTime;

    // Debug
    uint64_t frameCount = 0;
    bool surfacesCreated = false;
    bool offscreenCreated = false;
};

SkiaRenderer::SkiaRenderer() : m_impl(std::make_unique<Impl>()) {}

SkiaRenderer::~SkiaRenderer() {
    if (m_initialized) {
        shutdown();
    }
}

bool SkiaRenderer::initialize(VulkanContext* context, int width, int height) {
    m_context = context;
    m_width = width;
    m_height = height;
    m_impl->startTime = std::chrono::high_resolution_clock::now();

    std::cout << "Initializing Skia Graphite renderer..." << std::endl;

    // Check if direct rendering is supported
    m_useOffscreenRendering = !checkDirectRenderingSupported();

    if (m_useOffscreenRendering) {
        std::cout << "  Using offscreen rendering + blit (GPU doesn't support required flags)" << std::endl;
    } else {
        std::cout << "  Using direct-to-swapchain rendering" << std::endl;
    }

    if (!createSkiaContext()) {
        std::cerr << "Failed to create Skia context" << std::endl;
        return false;
    }

    if (m_useOffscreenRendering) {
        if (!createOffscreenRenderTarget()) {
            std::cerr << "Failed to create offscreen render target" << std::endl;
            return false;
        }
        if (!createBlitResources()) {
            std::cerr << "Failed to create blit resources" << std::endl;
            return false;
        }
    } else {
        if (!createSwapchainSurfaces()) {
            std::cerr << "Failed to create swapchain surfaces" << std::endl;
            return false;
        }
    }

    m_initialized = true;
    std::cout << "Skia Graphite renderer initialized (" << width << "x" << height << ")" << std::endl;
    return true;
}

bool SkiaRenderer::checkDirectRenderingSupported() const {
    // Check if the swapchain supports the required flags for Skia Graphite
    VkImageUsageFlags swapchainUsage = m_context->getSwapchain()->getImageUsageFlags();

    bool hasSampled = (swapchainUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
    bool hasInputAttachment = (swapchainUsage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) != 0;

    std::cout << "  Swapchain usage flags: 0x" << std::hex << swapchainUsage << std::dec << std::endl;
    std::cout << "    SAMPLED_BIT: " << (hasSampled ? "yes" : "no") << std::endl;
    std::cout << "    INPUT_ATTACHMENT_BIT: " << (hasInputAttachment ? "yes" : "no") << std::endl;

    // Skia Graphite needs BOTH SAMPLED_BIT and INPUT_ATTACHMENT_BIT for direct rendering
    return hasSampled && hasInputAttachment;
}

void SkiaRenderer::shutdown() {
    if (!m_initialized) {
        return;
    }

    std::cout << "Shutting down Skia Graphite renderer..." << std::endl;

    // Wait for GPU to finish
    if (m_impl->graphiteContext) {
        m_impl->graphiteContext->submit();
    }

    VkDevice device = m_context->getDevice();

    // Cleanup offscreen resources
    destroyOffscreenRenderTarget();

    // Cleanup blit fences
    for (auto fence : m_impl->blitFences) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
    }
    m_impl->blitFences.clear();

    // Command buffers are freed with the pool
    m_impl->blitCommandBuffers.clear();

    if (m_impl->blitCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_impl->blitCommandPool, nullptr);
        m_impl->blitCommandPool = VK_NULL_HANDLE;
    }

    // Cleanup per-image resources
    for (auto& imageData : m_impl->swapchainImages) {
        imageData.surface.reset();
        if (imageData.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, imageData.renderFinishedSemaphore, nullptr);
        }
    }
    m_impl->swapchainImages.clear();

    m_impl->recorder.reset();
    m_impl->graphiteContext.reset();
    m_impl->vulkanAllocator.reset();

    m_initialized = false;
    std::cout << "Skia Graphite renderer shut down." << std::endl;
}

void SkiaRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;

    // Mark surfaces for recreation
    for (auto& imageData : m_impl->swapchainImages) {
        imageData.surface.reset();
    }
    m_impl->surfacesCreated = false;

    // Recreate offscreen render target
    if (m_useOffscreenRendering && m_impl->offscreenCreated) {
        destroyOffscreenRenderTarget();
        createOffscreenRenderTarget();
    }

    std::cout << "Skia renderer resized to " << width << "x" << height << std::endl;
}

bool SkiaRenderer::createSkiaContext() {
    auto getProc = [](const char* name, VkInstance instance, VkDevice device) {
        if (device != VK_NULL_HANDLE) {
            return vkGetDeviceProcAddr(device, name);
        }
        return vkGetInstanceProcAddr(instance, name);
    };

    std::cout << "  Creating Skia Graphite context..." << std::endl;

    vkGetPhysicalDeviceFeatures(
        m_context->getPhysicalDevice(),
        &m_impl->physicalDeviceFeatures
    );

    const char* instanceExtensions[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    m_impl->vkExtensions.init(
        getProc,
        m_context->getInstance(),
        m_context->getPhysicalDevice(),
        sizeof(instanceExtensions) / sizeof(instanceExtensions[0]),
        instanceExtensions,
        sizeof(deviceExtensions) / sizeof(deviceExtensions[0]),
        deviceExtensions
    );

    skgpu::VulkanBackendContext backendContext{};
    backendContext.fInstance = m_context->getInstance();
    backendContext.fPhysicalDevice = m_context->getPhysicalDevice();
    backendContext.fDevice = m_context->getDevice();
    backendContext.fQueue = m_context->getGraphicsQueue();
    backendContext.fGraphicsQueueIndex = m_context->getGraphicsFamilyIndex();
    backendContext.fMaxAPIVersion = VK_API_VERSION_1_3;
    backendContext.fVkExtensions = &m_impl->vkExtensions;
    backendContext.fDeviceFeatures = &m_impl->physicalDeviceFeatures;
    backendContext.fGetProc = getProc;

    std::cout << "  Creating VMA memory allocator..." << std::endl;
    m_impl->vulkanAllocator = skgpu::VulkanMemoryAllocators::Make(
        backendContext,
        skgpu::ThreadSafe::kNo
    );

    if (!m_impl->vulkanAllocator) {
        std::cerr << "  Failed to create Vulkan memory allocator" << std::endl;
        return false;
    }
    std::cout << "  VMA memory allocator created" << std::endl;

    backendContext.fMemoryAllocator = m_impl->vulkanAllocator;

    skgpu::graphite::ContextOptions options;
    m_impl->graphiteContext = skgpu::graphite::ContextFactory::MakeVulkan(backendContext, options);

    if (!m_impl->graphiteContext) {
        std::cerr << "  Failed to create Skia Graphite Vulkan context" << std::endl;
        return false;
    }

    m_impl->recorder = m_impl->graphiteContext->makeRecorder();
    if (!m_impl->recorder) {
        std::cerr << "  Failed to create Graphite recorder" << std::endl;
        return false;
    }

    std::cout << "  Skia Graphite context created successfully" << std::endl;
    return true;
}

bool SkiaRenderer::createSwapchainSurfaces() {
    VkDevice device = m_context->getDevice();
    VkFormat swapchainFormat = m_context->getSwapchainFormat();
    VkExtent2D extent = m_context->getSwapchainExtent();
    size_t imageCount = m_context->getSwapchain()->getImageCount();
    uint32_t queueIndex = m_context->getGraphicsFamilyIndex();
    VkImageUsageFlags usageFlags = m_context->getSwapchain()->getImageUsageFlags();

    std::cout << "  Creating surfaces for " << imageCount << " swapchain images..." << std::endl;

    m_impl->swapchainImages.resize(imageCount);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageCount; i++) {
        auto& imageData = m_impl->swapchainImages[i];

        if (imageData.renderFinishedSemaphore == VK_NULL_HANDLE) {
            if (vkCreateSemaphore(device, &semInfo, nullptr, &imageData.renderFinishedSemaphore) != VK_SUCCESS) {
                std::cerr << "  Failed to create semaphore for image " << i << std::endl;
                return false;
            }
        }

        VkImage vkImage = m_context->getSwapchain()->getImage(i);
        if (vkImage == VK_NULL_HANDLE) {
            std::cerr << "  Failed to get Vulkan image " << i << std::endl;
            return false;
        }

        skgpu::graphite::VulkanTextureInfo textureInfo;
        textureInfo.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
        textureInfo.fFormat = swapchainFormat;
        textureInfo.fImageUsageFlags = usageFlags;
        textureInfo.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        textureInfo.fFlags = 0;

        SkISize dimensions = SkISize::Make(extent.width, extent.height);
        skgpu::graphite::BackendTexture backendTexture =
            skgpu::graphite::BackendTextures::MakeVulkan(
                dimensions,
                textureInfo,
                VK_IMAGE_LAYOUT_UNDEFINED,
                queueIndex,
                vkImage,
                skgpu::VulkanAlloc()
            );

        if (!backendTexture.isValid()) {
            std::cerr << "  Failed to create BackendTexture for image " << i << std::endl;
            return false;
        }

        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        imageData.surface = SkSurfaces::WrapBackendTexture(
            m_impl->recorder.get(),
            backendTexture,
            SkColorSpace::MakeSRGB(),
            &props
        );

        if (!imageData.surface) {
            std::cerr << "  Failed to wrap swapchain image " << i << " as SkSurface" << std::endl;
            return false;
        }

        std::cout << "  Created surface for swapchain image " << i << std::endl;
    }

    m_impl->surfacesCreated = true;
    return true;
}

bool SkiaRenderer::createOffscreenRenderTarget() {
    VkDevice device = m_context->getDevice();
    VkPhysicalDevice physicalDevice = m_context->getPhysicalDevice();
    VkExtent2D extent = m_context->getSwapchainExtent();
    VkFormat format = m_context->getSwapchainFormat();

    std::cout << "  Creating offscreen render target (" << extent.width << "x" << extent.height << ")..." << std::endl;

    // Create image with all the flags Skia Graphite needs
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

    // Request all the flags Skia Graphite needs
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_impl->offscreenRT.image) != VK_SUCCESS) {
        std::cerr << "  Failed to create offscreen image" << std::endl;
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, m_impl->offscreenRT.image, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        std::cerr << "  Failed to find suitable memory type" << std::endl;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_impl->offscreenRT.memory) != VK_SUCCESS) {
        std::cerr << "  Failed to allocate memory for offscreen image" << std::endl;
        return false;
    }

    vkBindImageMemory(device, m_impl->offscreenRT.image, m_impl->offscreenRT.memory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_impl->offscreenRT.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_impl->offscreenRT.imageView) != VK_SUCCESS) {
        std::cerr << "  Failed to create image view" << std::endl;
        return false;
    }

    // Create semaphore for Skia completion
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semInfo, nullptr, &m_impl->offscreenRT.skiaFinishedSemaphore) != VK_SUCCESS) {
        std::cerr << "  Failed to create semaphore" << std::endl;
        return false;
    }

    // Create fence
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceInfo, nullptr, &m_impl->offscreenRT.skiaFinishedFence) != VK_SUCCESS) {
        std::cerr << "  Failed to create fence" << std::endl;
        return false;
    }

    // Wrap as Skia surface
    skgpu::graphite::VulkanTextureInfo textureInfo;
    textureInfo.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
    textureInfo.fFormat = format;
    textureInfo.fImageUsageFlags = imageInfo.usage;
    textureInfo.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    textureInfo.fFlags = 0;

    SkISize dimensions = SkISize::Make(extent.width, extent.height);
    skgpu::graphite::BackendTexture backendTexture =
        skgpu::graphite::BackendTextures::MakeVulkan(
            dimensions,
            textureInfo,
            VK_IMAGE_LAYOUT_UNDEFINED,
            m_context->getGraphicsFamilyIndex(),
            m_impl->offscreenRT.image,
            skgpu::VulkanAlloc()
        );

    if (!backendTexture.isValid()) {
        std::cerr << "  Failed to create BackendTexture" << std::endl;
        return false;
    }

    SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    m_impl->offscreenRT.surface = SkSurfaces::WrapBackendTexture(
        m_impl->recorder.get(),
        backendTexture,
        SkColorSpace::MakeSRGB(),
        &props
    );

    if (!m_impl->offscreenRT.surface) {
        std::cerr << "  Failed to create Skia surface" << std::endl;
        return false;
    }

    m_impl->offscreenCreated = true;
    std::cout << "  Offscreen render target created successfully" << std::endl;
    return true;
}

void SkiaRenderer::destroyOffscreenRenderTarget() {
    VkDevice device = m_context->getDevice();

    m_impl->offscreenRT.surface.reset();

    if (m_impl->offscreenRT.skiaFinishedFence != VK_NULL_HANDLE) {
        vkDestroyFence(device, m_impl->offscreenRT.skiaFinishedFence, nullptr);
        m_impl->offscreenRT.skiaFinishedFence = VK_NULL_HANDLE;
    }

    if (m_impl->offscreenRT.skiaFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, m_impl->offscreenRT.skiaFinishedSemaphore, nullptr);
        m_impl->offscreenRT.skiaFinishedSemaphore = VK_NULL_HANDLE;
    }

    if (m_impl->offscreenRT.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_impl->offscreenRT.imageView, nullptr);
        m_impl->offscreenRT.imageView = VK_NULL_HANDLE;
    }

    if (m_impl->offscreenRT.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_impl->offscreenRT.image, nullptr);
        m_impl->offscreenRT.image = VK_NULL_HANDLE;
    }

    if (m_impl->offscreenRT.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_impl->offscreenRT.memory, nullptr);
        m_impl->offscreenRT.memory = VK_NULL_HANDLE;
    }

    m_impl->offscreenCreated = false;
}

bool SkiaRenderer::createBlitResources() {
    VkDevice device = m_context->getDevice();
    size_t imageCount = m_context->getSwapchain()->getImageCount();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_context->getGraphicsFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_impl->blitCommandPool) != VK_SUCCESS) {
        std::cerr << "  Failed to create blit command pool" << std::endl;
        return false;
    }

    // Allocate one command buffer per swapchain image
    m_impl->blitCommandBuffers.resize(imageCount);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_impl->blitCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(imageCount);

    if (vkAllocateCommandBuffers(device, &allocInfo, m_impl->blitCommandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "  Failed to allocate blit command buffers" << std::endl;
        return false;
    }

    // Create one fence per swapchain image for synchronization
    m_impl->blitFences.resize(imageCount);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled so first frame doesn't wait

    for (size_t i = 0; i < imageCount; i++) {
        if (vkCreateFence(device, &fenceInfo, nullptr, &m_impl->blitFences[i]) != VK_SUCCESS) {
            std::cerr << "  Failed to create blit fence " << i << std::endl;
            return false;
        }
    }

    // Create per-swapchain-image semaphores for blit completion
    m_impl->swapchainImages.resize(imageCount);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageCount; i++) {
        if (vkCreateSemaphore(device, &semInfo, nullptr,
                              &m_impl->swapchainImages[i].renderFinishedSemaphore) != VK_SUCCESS) {
            std::cerr << "  Failed to create semaphore for image " << i << std::endl;
            return false;
        }
    }

    std::cout << "  Blit resources created (" << imageCount << " command buffers, fences)" << std::endl;
    return true;
}

void SkiaRenderer::blitToSwapchain(VkImage srcImage, VkSemaphore waitSemaphore) {
    VkDevice device = m_context->getDevice();
    uint32_t imageIndex = m_context->getCurrentImageIndex();
    VkImage dstImage = m_context->getSwapchain()->getImage(imageIndex);
    VkExtent2D extent = m_context->getSwapchainExtent();

    // Get per-image command buffer and fence
    VkCommandBuffer cmdBuffer = m_impl->blitCommandBuffers[imageIndex];
    VkFence fence = m_impl->blitFences[imageIndex];

    // Wait for previous use of this command buffer to complete
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    // Reset and begin command buffer
    vkResetCommandBuffer(cmdBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Transition src image to TRANSFER_SRC
    VkImageMemoryBarrier2 srcBarrier{};
    srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    srcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    srcBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    srcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    srcBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.image = srcImage;
    srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    srcBarrier.subresourceRange.baseMipLevel = 0;
    srcBarrier.subresourceRange.levelCount = 1;
    srcBarrier.subresourceRange.baseArrayLayer = 0;
    srcBarrier.subresourceRange.layerCount = 1;

    // Transition dst image to TRANSFER_DST
    VkImageMemoryBarrier2 dstBarrier{};
    dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    dstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    dstBarrier.srcAccessMask = 0;
    dstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.image = dstImage;
    dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dstBarrier.subresourceRange.baseMipLevel = 0;
    dstBarrier.subresourceRange.levelCount = 1;
    dstBarrier.subresourceRange.baseArrayLayer = 0;
    dstBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 2;
    VkImageMemoryBarrier2 barriers[] = {srcBarrier, dstBarrier};
    depInfo.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmdBuffer, &depInfo);

    // Blit
    VkImageBlit2 blitRegion{};
    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};

    VkBlitImageInfo2 blitInfo{};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.srcImage = srcImage;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = dstImage;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;
    blitInfo.filter = VK_FILTER_LINEAR;

    vkCmdBlitImage2(cmdBuffer, &blitInfo);

    // Transition dst image to PRESENT_SRC
    VkImageMemoryBarrier2 presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    presentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    presentBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    presentBarrier.dstAccessMask = 0;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.image = dstImage;
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.baseMipLevel = 0;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.baseArrayLayer = 0;
    presentBarrier.subresourceRange.layerCount = 1;

    // Transition src image back to COLOR_ATTACHMENT_OPTIMAL for next frame
    VkImageMemoryBarrier2 srcRestoreBarrier{};
    srcRestoreBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    srcRestoreBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    srcRestoreBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    srcRestoreBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    srcRestoreBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    srcRestoreBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcRestoreBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    srcRestoreBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcRestoreBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcRestoreBarrier.image = srcImage;
    srcRestoreBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    srcRestoreBarrier.subresourceRange.baseMipLevel = 0;
    srcRestoreBarrier.subresourceRange.levelCount = 1;
    srcRestoreBarrier.subresourceRange.baseArrayLayer = 0;
    srcRestoreBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo presentDepInfo{};
    presentDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDepInfo.imageMemoryBarrierCount = 2;
    VkImageMemoryBarrier2 barriers[] = {presentBarrier, srcRestoreBarrier};
    presentDepInfo.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmdBuffer, &presentDepInfo);

    vkEndCommandBuffer(cmdBuffer);

    // Submit with synchronization
    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = waitSemaphore;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmdBuffer;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = m_impl->swapchainImages[imageIndex].renderFinishedSemaphore;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    // Submit with fence for synchronization
    vkQueueSubmit2(m_context->getGraphicsQueue(), 1, &submitInfo, fence);
}

void SkiaRenderer::render() {
    if (!m_impl->recorder || !m_impl->graphiteContext) {
        std::cerr << "Skia not initialized" << std::endl;
        return;
    }

    m_impl->frameCount++;

    uint32_t imageIndex = m_context->getCurrentImageIndex();
    VkExtent2D extent = m_context->getSwapchainExtent();

    // Get the surface to render to
    SkSurface* surface = nullptr;
    if (m_useOffscreenRendering) {
        // Create offscreen target if needed
        if (!m_impl->offscreenCreated) {
            if (!createOffscreenRenderTarget()) {
                std::cerr << "Failed to create offscreen render target" << std::endl;
                return;
            }
        }
        surface = m_impl->offscreenRT.surface.get();
    } else {
        // Create swapchain surfaces if needed
        if (!m_impl->surfacesCreated) {
            if (!createSwapchainSurfaces()) {
                std::cerr << "Failed to create swapchain surfaces" << std::endl;
                return;
            }
        }
        surface = m_impl->swapchainImages[imageIndex].surface.get();
    }

    if (!surface) {
        std::cerr << "No surface available for rendering" << std::endl;
        return;
    }

    // Draw
    SkCanvas* canvas = surface->getCanvas();
    if (!canvas) {
        std::cerr << "Failed to get canvas" << std::endl;
        return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(now - m_impl->startTime).count();

    // Clear
    canvas->clear(SkColorSetRGB(25, 30, 45));

    // Draw rotating rectangle
    SkPaint paint;
    paint.setColor(SkColorSetARGB(255, 100, 180, 255));
    paint.setAntiAlias(true);

    float centerX = extent.width / 2.0f;
    float centerY = extent.height / 2.0f;
    float size = std::min(static_cast<float>(extent.width), static_cast<float>(extent.height)) * 0.15f;

    canvas->save();
    canvas->translate(centerX, centerY);
    canvas->rotate(time * 45.0f);

    SkRect rect = SkRect::MakeXYWH(-size/2, -size/2, size, size);
    canvas->drawRect(rect, paint);
    canvas->restore();

    // Draw pulsing circle
    float pulseScale = 1.0f + 0.3f * std::sin(time * 3.0f);
    paint.setColor(SkColorSetARGB(200, 255, 120, 100));
    canvas->drawCircle(
        centerX + std::sin(time * 1.5f) * 80,
        centerY + std::cos(time * 1.5f) * 80,
        25 * pulseScale,
        paint
    );

    // Draw text
    SkFont font;
    font.setSize(20);
    paint.setColor(SK_ColorWHITE);
    canvas->drawString("Skia Graphite + Vulkan 1.3", 20, 35, font, paint);

    font.setSize(14);
    paint.setColor(SkColorSetARGB(180, 200, 200, 200));

    std::string modeStr = m_useOffscreenRendering ? "Mode: Offscreen + Blit" : "Mode: Direct Rendering";
    canvas->drawString(modeStr.c_str(), 20, extent.height - 65, font, paint);
    canvas->drawString("Renderer: Skia Graphite", 20, extent.height - 45, font, paint);
    canvas->drawString("Press ESC to exit", 20, extent.height - 25, font, paint);

    // Snap recording
    auto recording = m_impl->recorder->snap();
    if (!recording) {
        std::cerr << "Failed to snap recording" << std::endl;
        return;
    }

    // Insert recording
    skgpu::graphite::InsertRecordingInfo insertInfo{};
    insertInfo.fRecording = recording.get();

    // Wait on acquire semaphore
    VkSemaphore acquireSemaphore = m_context->getImageAvailableSemaphore();
    skgpu::graphite::BackendSemaphore backendAcquireSemaphore =
        skgpu::graphite::BackendSemaphores::MakeVulkan(acquireSemaphore);
    insertInfo.fNumWaitSemaphores = 1;
    insertInfo.fWaitSemaphores = &backendAcquireSemaphore;

    if (m_useOffscreenRendering) {
        // Offscreen mode: signal Skia finished semaphore, then blit
        insertInfo.fTargetSurface = surface;
        // Don't transition to PRESENT - we need to blit first

        skgpu::graphite::BackendSemaphore backendSignalSemaphore =
            skgpu::graphite::BackendSemaphores::MakeVulkan(m_impl->offscreenRT.skiaFinishedSemaphore);
        insertInfo.fNumSignalSemaphores = 1;
        insertInfo.fSignalSemaphores = &backendSignalSemaphore;

        m_impl->graphiteContext->insertRecording(insertInfo);
        m_impl->graphiteContext->submit(skgpu::graphite::SyncToCpu::kNo);

        // Now blit to swapchain
        blitToSwapchain(m_impl->offscreenRT.image, m_impl->offscreenRT.skiaFinishedSemaphore);
    } else {
        // Direct mode: render directly to swapchain image
        insertInfo.fTargetSurface = surface;
        skgpu::MutableTextureState presentState = skgpu::MutableTextureStates::MakeVulkan(
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            m_context->getGraphicsFamilyIndex()
        );
        insertInfo.fTargetTextureState = &presentState;

        skgpu::graphite::BackendSemaphore backendRenderSemaphore =
            skgpu::graphite::BackendSemaphores::MakeVulkan(
                m_impl->swapchainImages[imageIndex].renderFinishedSemaphore);
        insertInfo.fNumSignalSemaphores = 1;
        insertInfo.fSignalSemaphores = &backendRenderSemaphore;

        m_impl->graphiteContext->insertRecording(insertInfo);
        m_impl->graphiteContext->submit(skgpu::graphite::SyncToCpu::kNo);
    }
}

VkSemaphore SkiaRenderer::getRenderFinishedSemaphore() const {
    uint32_t imageIndex = m_context->getCurrentImageIndex();
    if (imageIndex < m_impl->swapchainImages.size()) {
        return m_impl->swapchainImages[imageIndex].renderFinishedSemaphore;
    }
    return VK_NULL_HANDLE;
}

} // namespace skia_renderer
