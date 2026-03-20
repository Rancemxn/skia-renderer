#include "SkiaRenderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "core/Logger.h"

// Skia core headers
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include "include/core/SkRect.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"

// Skia platform-specific font manager headers
#if defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#elif defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#endif

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

    // Font management
    sk_sp<SkFontMgr> fontMgr;
    sk_sp<SkTypeface> defaultTypeface;
    SkFont defaultFont;
    SkFont smallFont;
    bool fontsInitialized = false;

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

    LOG_INFO("Initializing Skia Graphite renderer...");

    // Check if direct rendering is supported
    m_useOffscreenRendering = !checkDirectRenderingSupported();

    if (m_useOffscreenRendering) {
        LOG_INFO("  Using offscreen rendering + blit (GPU doesn't support required flags)");
    } else {
        LOG_INFO("  Using direct-to-swapchain rendering");
    }

    if (!createSkiaContext()) {
        LOG_ERROR("Failed to create Skia context");
        return false;
    }

    if (m_useOffscreenRendering) {
        if (!createOffscreenRenderTarget()) {
            LOG_ERROR("Failed to create offscreen render target");
            return false;
        }
        if (!createBlitResources()) {
            LOG_ERROR("Failed to create blit resources");
            return false;
        }
    } else {
        if (!createSwapchainSurfaces()) {
            LOG_ERROR("Failed to create swapchain surfaces");
            return false;
        }
    }

    m_initialized = true;
    LOG_INFO("Skia Graphite renderer initialized ({}x{}) ", width, height);
    return true;
}

bool SkiaRenderer::checkDirectRenderingSupported() const {
    // Check if the swapchain supports the required flags for Skia Graphite
    VkImageUsageFlags swapchainUsage = m_context->getSwapchain()->getImageUsageFlags();

    bool hasSampled = (swapchainUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
    bool hasInputAttachment = (swapchainUsage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) != 0;

    LOG_DEBUG("  Swapchain usage flags: 0x{:x}", swapchainUsage);
    LOG_DEBUG("    SAMPLED_BIT: {}", hasSampled ? "yes" : "no");
    LOG_DEBUG("    INPUT_ATTACHMENT_BIT: {}", hasInputAttachment ? "yes" : "no");

    // Skia Graphite needs BOTH SAMPLED_BIT and INPUT_ATTACHMENT_BIT for direct rendering
    return hasSampled && hasInputAttachment;
}

void SkiaRenderer::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down Skia Graphite renderer...");

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
    LOG_INFO("Skia Graphite renderer shut down.");
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

    LOG_INFO("Skia renderer resized to {}x{}", width, height);
}

bool SkiaRenderer::createSkiaContext() {
    auto getProc = [](const char* name, VkInstance instance, VkDevice device) {
        if (device != VK_NULL_HANDLE) {
            return vkGetDeviceProcAddr(device, name);
        }
        return vkGetInstanceProcAddr(instance, name);
    };

    LOG_INFO("  Creating Skia Graphite context...");

    // Initialize font manager (platform-specific)
    LOG_INFO("  Initializing font manager...");
#if defined(_WIN32)
    m_impl->fontMgr = SkFontMgr_New_DirectWrite();
#elif defined(__linux__)
    m_impl->fontMgr = SkFontMgr_New_FontConfig(nullptr, nullptr);
#elif defined(__APPLE__)
    m_impl->fontMgr = SkFontMgr_New_CoreText(nullptr);
#else
    m_impl->fontMgr = SkFontMgr::RefEmpty();
#endif
    if (!m_impl->fontMgr) {
        LOG_WARN("  Failed to create platform font manager, using empty font manager");
        m_impl->fontMgr = SkFontMgr::RefEmpty();
    }

    // Get a default typeface
    m_impl->defaultTypeface = m_impl->fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
    if (!m_impl->defaultTypeface) {
        LOG_WARN("  Failed to match default typeface, trying legacy method");
        // Try to get any available font
        int familyCount = m_impl->fontMgr->countFamilies();
        if (familyCount > 0) {
            for (int i = 0; i < familyCount; ++i) {
                SkString familyName;
                m_impl->fontMgr->getFamilyName(i, &familyName);
                LOG_DEBUG("    Found font family: {}", familyName.c_str());
            }
            // Try to match the first available family
            m_impl->defaultTypeface = m_impl->fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
        }
    }

    if (m_impl->defaultTypeface) {
        LOG_INFO("  Font typeface loaded successfully");
        // Initialize fonts with the typeface
        m_impl->defaultFont = SkFont(m_impl->defaultTypeface, 20.0f);
        m_impl->defaultFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        m_impl->defaultFont.setSubpixel(true);
        m_impl->defaultFont.setHinting(SkFontHinting::kSlight);

        m_impl->smallFont = SkFont(m_impl->defaultTypeface, 14.0f);
        m_impl->smallFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        m_impl->smallFont.setSubpixel(true);
        m_impl->smallFont.setHinting(SkFontHinting::kSlight);

        m_impl->fontsInitialized = true;
    } else {
        LOG_WARN("  No font typeface available, text rendering may fail");
    }

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
    // Use the instance API version (cannot exceed instance version)
    // Device may support higher version but we're limited by instance
    backendContext.fMaxAPIVersion = m_context->getCapabilities().instanceApiVersion;
    backendContext.fVkExtensions = &m_impl->vkExtensions;
    backendContext.fDeviceFeatures = &m_impl->physicalDeviceFeatures;
    backendContext.fGetProc = getProc;

    LOG_INFO("  Creating VMA memory allocator...");
    m_impl->vulkanAllocator = skgpu::VulkanMemoryAllocators::Make(
        backendContext,
        skgpu::ThreadSafe::kNo
    );

    if (!m_impl->vulkanAllocator) {
        LOG_ERROR("  Failed to create Vulkan memory allocator");
        return false;
    }
    LOG_INFO("  VMA memory allocator created");

    backendContext.fMemoryAllocator = m_impl->vulkanAllocator;

    skgpu::graphite::ContextOptions options;
    m_impl->graphiteContext = skgpu::graphite::ContextFactory::MakeVulkan(backendContext, options);

    if (!m_impl->graphiteContext) {
        LOG_ERROR("  Failed to create Skia Graphite Vulkan context");
        return false;
    }

    m_impl->recorder = m_impl->graphiteContext->makeRecorder();
    if (!m_impl->recorder) {
        LOG_ERROR("  Failed to create Graphite recorder");
        return false;
    }

    LOG_INFO("  Skia Graphite context created successfully");
    return true;
}

bool SkiaRenderer::createSwapchainSurfaces() {
    VkDevice device = m_context->getDevice();
    VkFormat swapchainFormat = m_context->getSwapchainFormat();
    VkExtent2D extent = m_context->getSwapchainExtent();
    size_t imageCount = m_context->getSwapchain()->getImageCount();
    uint32_t queueIndex = m_context->getGraphicsFamilyIndex();
    VkImageUsageFlags usageFlags = m_context->getSwapchain()->getImageUsageFlags();

    LOG_INFO("  Creating surfaces for {} swapchain images...", imageCount);

    m_impl->swapchainImages.resize(imageCount);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageCount; i++) {
        auto& imageData = m_impl->swapchainImages[i];

        if (imageData.renderFinishedSemaphore == VK_NULL_HANDLE) {
            if (vkCreateSemaphore(device, &semInfo, nullptr, &imageData.renderFinishedSemaphore) != VK_SUCCESS) {
                LOG_ERROR("  Failed to create semaphore for image {}", i);
                return false;
            }
        }

        VkImage vkImage = m_context->getSwapchain()->getImage(i);
        if (vkImage == VK_NULL_HANDLE) {
            LOG_ERROR("  Failed to get Vulkan image {}", i);
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
            LOG_ERROR("  Failed to create BackendTexture for image {}", i);
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
            LOG_ERROR("  Failed to wrap swapchain image {} as SkSurface", i);
            return false;
        }

        LOG_DEBUG("  Created surface for swapchain image {}", i);
    }

    m_impl->surfacesCreated = true;
    return true;
}

bool SkiaRenderer::createOffscreenRenderTarget() {
    VkDevice device = m_context->getDevice();
    VkPhysicalDevice physicalDevice = m_context->getPhysicalDevice();
    VkExtent2D extent = m_context->getSwapchainExtent();
    VkFormat format = m_context->getSwapchainFormat();

    LOG_INFO("  Creating offscreen render target ({}x{})...", extent.width, extent.height);

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
        LOG_ERROR("  Failed to create offscreen image");
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
        LOG_ERROR("  Failed to find suitable memory type");
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_impl->offscreenRT.memory) != VK_SUCCESS) {
        LOG_ERROR("  Failed to allocate memory for offscreen image");
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
        LOG_ERROR("  Failed to create image view");
        return false;
    }

    // Create semaphore for Skia completion
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semInfo, nullptr, &m_impl->offscreenRT.skiaFinishedSemaphore) != VK_SUCCESS) {
        LOG_ERROR("  Failed to create semaphore");
        return false;
    }

    // Create fence
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceInfo, nullptr, &m_impl->offscreenRT.skiaFinishedFence) != VK_SUCCESS) {
        LOG_ERROR("  Failed to create fence");
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
        LOG_ERROR("  Failed to create BackendTexture");
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
        LOG_ERROR("  Failed to create Skia surface");
        return false;
    }

    m_impl->offscreenCreated = true;
    LOG_INFO("  Offscreen render target created successfully");
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
        LOG_ERROR("  Failed to create blit command pool");
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
        LOG_ERROR("  Failed to allocate blit command buffers");
        return false;
    }

    // Create one fence per swapchain image for synchronization
    m_impl->blitFences.resize(imageCount);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled so first frame doesn't wait

    for (size_t i = 0; i < imageCount; i++) {
        if (vkCreateFence(device, &fenceInfo, nullptr, &m_impl->blitFences[i]) != VK_SUCCESS) {
            LOG_ERROR("  Failed to create blit fence {}", i);
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
            LOG_ERROR("  Failed to create semaphore for image {}", i);
            return false;
        }
    }

    LOG_INFO("  Blit resources created ({} command buffers, fences)", imageCount);
    return true;
}

void SkiaRenderer::blitToSwapchain(VkImage srcImage, VkSemaphore waitSemaphore) {
    // Use Vulkan 1.3 synchronization2 APIs only if we have Vulkan 1.3 core.
    // Vulkan 1.2 with VK_KHR_synchronization2 extension requires KHR-suffixed functions
    // which we don't load, so we use Vulkan 1.1 legacy path for that case.
    if (m_context->supportsVulkan13()) {
        blitToSwapchainVulkan13(srcImage, waitSemaphore);
    } else {
        blitToSwapchainVulkan11(srcImage, waitSemaphore);
    }
}

void SkiaRenderer::blitToSwapchainVulkan13(VkImage srcImage, VkSemaphore waitSemaphore) {
    VkDevice device = m_context->getDevice();
    uint32_t imageIndex = m_context->getCurrentImageIndex();
    VkImage dstImage = m_context->getSwapchain()->getImage(imageIndex);
    VkExtent2D extent = m_context->getSwapchainExtent();

    VkCommandBuffer cmdBuffer = m_impl->blitCommandBuffers[imageIndex];
    VkFence fence = m_impl->blitFences[imageIndex];

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);
    vkResetCommandBuffer(cmdBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Transition src image to TRANSFER_SRC (using synchronization2)
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

    // Transition src image back to COLOR_ATTACHMENT_OPTIMAL
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
    VkImageMemoryBarrier2 finalBarriers[] = {presentBarrier, srcRestoreBarrier};
    presentDepInfo.pImageMemoryBarriers = finalBarriers;
    vkCmdPipelineBarrier2(cmdBuffer, &presentDepInfo);

    vkEndCommandBuffer(cmdBuffer);

    // Submit with synchronization2 API
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

    vkQueueSubmit2(m_context->getGraphicsQueue(), 1, &submitInfo, fence);
}

void SkiaRenderer::blitToSwapchainVulkan11(VkImage srcImage, VkSemaphore waitSemaphore) {
    // Vulkan 1.1 fallback using legacy synchronization APIs
    VkDevice device = m_context->getDevice();
    uint32_t imageIndex = m_context->getCurrentImageIndex();
    VkImage dstImage = m_context->getSwapchain()->getImage(imageIndex);
    VkExtent2D extent = m_context->getSwapchainExtent();

    VkCommandBuffer cmdBuffer = m_impl->blitCommandBuffers[imageIndex];
    VkFence fence = m_impl->blitFences[imageIndex];

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);
    vkResetCommandBuffer(cmdBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Transition src image to TRANSFER_SRC (using legacy barrier)
    VkImageMemoryBarrier srcBarrier{};
    srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    srcBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
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
    VkImageMemoryBarrier dstBarrier{};
    dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dstBarrier.srcAccessMask = 0;
    dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
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

    VkImageMemoryBarrier barriers[] = {srcBarrier, dstBarrier};
    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        2, barriers
    );

    // Blit using legacy API
    VkImageBlit blitRegion{};
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

    vkCmdBlitImage(
        cmdBuffer,
        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blitRegion,
        VK_FILTER_LINEAR
    );

    // Transition dst image to PRESENT_SRC
    VkImageMemoryBarrier presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    presentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
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

    // Transition src image back to COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier srcRestoreBarrier{};
    srcRestoreBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    srcRestoreBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcRestoreBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
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

    VkImageMemoryBarrier finalBarriers[] = {presentBarrier, srcRestoreBarrier};
    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        2, finalBarriers
    );

    vkEndCommandBuffer(cmdBuffer);

    // Submit using legacy API
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_impl->swapchainImages[imageIndex].renderFinishedSemaphore;

    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, fence);
}

void SkiaRenderer::render() {
    if (!m_impl->recorder || !m_impl->graphiteContext) {
        LOG_ERROR("Skia not initialized");
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
                LOG_ERROR("Failed to create offscreen render target");
                return;
            }
        }
        surface = m_impl->offscreenRT.surface.get();
    } else {
        // Create swapchain surfaces if needed
        if (!m_impl->surfacesCreated) {
            if (!createSwapchainSurfaces()) {
                LOG_ERROR("Failed to create swapchain surfaces");
                return;
            }
        }
        surface = m_impl->swapchainImages[imageIndex].surface.get();
    }

    if (!surface) {
        LOG_ERROR("No surface available for rendering");
        return;
    }

    // Draw
    SkCanvas* canvas = surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("Failed to get canvas");
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

    // Draw text (use pre-loaded fonts with valid typeface)
    paint.setColor(SK_ColorWHITE);

    // Draw title and FPS with default font (20px)
    if (m_impl->fontsInitialized) {
        // Show Vulkan version dynamically
        std::string vulkanStr = std::string("Skia Graphite + ") + m_context->getCapabilities().getFeatureLevelString();
        canvas->drawString(vulkanStr.c_str(), 20, 35, m_impl->defaultFont, paint);

        // Draw FPS
        std::string fpsStr = "FPS: " + std::to_string(static_cast<int>(m_fps));
        canvas->drawString(fpsStr.c_str(), 20, 60, m_impl->defaultFont, paint);

        // Draw info with small font (14px)
        paint.setColor(SkColorSetARGB(180, 200, 200, 200));

        std::string modeStr = m_useOffscreenRendering ? "Mode: Offscreen + Blit" : "Mode: Direct Rendering";
        canvas->drawString(modeStr.c_str(), 20, extent.height - 85, m_impl->smallFont, paint);
        
        // Display present mode
        const char* presentModeStr = "Unknown";
        VkPresentModeKHR presentMode = m_context->getSwapchain()->getPresentMode();
        switch (presentMode) {
            case VK_PRESENT_MODE_MAILBOX_KHR: presentModeStr = "Mailbox"; break;
            case VK_PRESENT_MODE_IMMEDIATE_KHR: presentModeStr = "Immediate"; break;
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: presentModeStr = "FIFO Relaxed"; break;
            case VK_PRESENT_MODE_FIFO_KHR: presentModeStr = "FIFO (VSync)"; break;
            default: break;
        }
        std::string presentStr = std::string("Present: ") + presentModeStr;
        canvas->drawString(presentStr.c_str(), 20, extent.height - 65, m_impl->smallFont, paint);
        
        canvas->drawString("Renderer: Skia Graphite", 20, extent.height - 45, m_impl->smallFont, paint);
        canvas->drawString("Press ESC to exit", 20, extent.height - 25, m_impl->smallFont, paint);
    } else {
        // Fallback: try to draw without pre-loaded font (may not work)
        SkFont fallbackFont;
        fallbackFont.setSize(20);
        // Show Vulkan version dynamically
        std::string vulkanStr = std::string("Skia Graphite + ") + m_context->getCapabilities().getFeatureLevelString();
        canvas->drawString(vulkanStr.c_str(), 20, 35, fallbackFont, paint);
        std::string fpsStr = "FPS: " + std::to_string(static_cast<int>(m_fps));
        canvas->drawString(fpsStr.c_str(), 20, 60, fallbackFont, paint);
        fallbackFont.setSize(14);
        paint.setColor(SkColorSetARGB(180, 200, 200, 200));
        std::string modeStr = m_useOffscreenRendering ? "Mode: Offscreen + Blit" : "Mode: Direct Rendering";
        canvas->drawString(modeStr.c_str(), 20, extent.height - 85, fallbackFont, paint);
        
        const char* presentModeStr = "Unknown";
        VkPresentModeKHR presentMode = m_context->getSwapchain()->getPresentMode();
        switch (presentMode) {
            case VK_PRESENT_MODE_MAILBOX_KHR: presentModeStr = "Mailbox"; break;
            case VK_PRESENT_MODE_IMMEDIATE_KHR: presentModeStr = "Immediate"; break;
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: presentModeStr = "FIFO Relaxed"; break;
            case VK_PRESENT_MODE_FIFO_KHR: presentModeStr = "FIFO (VSync)"; break;
            default: break;
        }
        std::string presentStr = std::string("Present: ") + presentModeStr;
        canvas->drawString(presentStr.c_str(), 20, extent.height - 65, fallbackFont, paint);
        
        canvas->drawString("Renderer: Skia Graphite", 20, extent.height - 45, fallbackFont, paint);
        canvas->drawString("Press ESC to exit", 20, extent.height - 25, fallbackFont, paint);
    }

    // Snap recording
    auto recording = m_impl->recorder->snap();
    if (!recording) {
        LOG_ERROR("Failed to snap recording");
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

// IRenderer interface implementation (SDL_Window* version not used for Vulkan backend)
bool SkiaRenderer::initialize(SDL_Window* window, int width, int height, const BackendConfig& config) {
    (void)window;
    (void)width;
    (void)height;
    (void)config;
    LOG_ERROR("SkiaRenderer::initialize(SDL_Window*) not supported. Use initialize(VulkanContext*) instead.");
    return false;
}

bool SkiaRenderer::beginFrame() {
    return m_initialized;
}

void SkiaRenderer::endFrame() {
    // Frame ending is handled internally by the Vulkan rendering flow
}

} // namespace skia_renderer
