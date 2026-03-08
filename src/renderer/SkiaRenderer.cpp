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

    // Per-swapchain-image surfaces and semaphores
    struct SwapchainImageData {
        sk_sp<SkSurface> surface;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    };
    std::vector<SwapchainImageData> swapchainImages;

    // Timing for animation
    std::chrono::high_resolution_clock::time_point startTime;

    // Debug
    uint64_t frameCount = 0;
    bool surfacesCreated = false;
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

    if (!createSkiaContext()) {
        std::cerr << "Failed to create Skia context" << std::endl;
        return false;
    }

    m_initialized = true;
    std::cout << "Skia Graphite renderer initialized (" << width << "x" << height << ")" << std::endl;
    return true;
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

    // Cleanup per-image resources
    VkDevice device = m_context->getDevice();
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

    std::cout << "Skia renderer resized to " << width << "x" << height << std::endl;
}

bool SkiaRenderer::createSkiaContext() {
    // Get proc address function
    auto getProc = [](const char* name, VkInstance instance, VkDevice device) {
        if (device != VK_NULL_HANDLE) {
            return vkGetDeviceProcAddr(device, name);
        }
        return vkGetInstanceProcAddr(instance, name);
    };

    std::cout << "  Creating Skia Graphite context..." << std::endl;

    // Get physical device features
    vkGetPhysicalDeviceFeatures(
        m_context->getPhysicalDevice(),
        &m_impl->physicalDeviceFeatures
    );

    // Initialize Vulkan extensions
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

    // Create Skia Vulkan backend context
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

    // Create memory allocator using Skia's internal VMA wrapper
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

    // Create Graphite Context
    skgpu::graphite::ContextOptions options;
    m_impl->graphiteContext = skgpu::graphite::ContextFactory::MakeVulkan(backendContext, options);

    if (!m_impl->graphiteContext) {
        std::cerr << "  Failed to create Skia Graphite Vulkan context" << std::endl;
        return false;
    }

    // Create recorder
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
    uint32_t presentQueueIndex = m_context->getGraphicsFamilyIndex();  // Same as graphics in our case

    std::cout << "  Creating surfaces for " << imageCount << " swapchain images..." << std::endl;
    std::cout << "  Swapchain format: " << swapchainFormat << std::endl;

    // Resize our array
    m_impl->swapchainImages.resize(imageCount);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageCount; i++) {
        auto& imageData = m_impl->swapchainImages[i];

        // Create render finished semaphore for this image
        if (imageData.renderFinishedSemaphore == VK_NULL_HANDLE) {
            if (vkCreateSemaphore(device, &semInfo, nullptr, &imageData.renderFinishedSemaphore) != VK_SUCCESS) {
                std::cerr << "  Failed to create render finished semaphore for image " << i << std::endl;
                return false;
            }
        }

        // Get the Vulkan image
        VkImage vkImage = m_context->getSwapchain()->getImage(i);
        if (vkImage == VK_NULL_HANDLE) {
            std::cerr << "  Failed to get Vulkan image " << i << std::endl;
            return false;
        }

        // Get actual usage flags from swapchain (includes SAMPLED_BIT, INPUT_ATTACHMENT_BIT if supported)
        VkImageUsageFlags actualUsageFlags = m_context->getSwapchain()->getImageUsageFlags();
        std::cout << "  Image " << i << " usage flags: 0x" << std::hex << actualUsageFlags << std::dec << std::endl;

        // Create VulkanTextureInfo - use actual swapchain usage flags
        skgpu::graphite::VulkanTextureInfo textureInfo;
        textureInfo.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
        textureInfo.fFormat = swapchainFormat;
        textureInfo.fImageUsageFlags = actualUsageFlags;  // Use actual flags from swapchain
        textureInfo.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        textureInfo.fFlags = 0;

        // Create BackendTexture with UNDEFINED layout (Skia manages layout)
        SkISize dimensions = SkISize::Make(extent.width, extent.height);
        skgpu::graphite::BackendTexture backendTexture =
            skgpu::graphite::BackendTextures::MakeVulkan(
                dimensions,
                textureInfo,
                VK_IMAGE_LAYOUT_UNDEFINED,  // Skia will manage the layout
                presentQueueIndex,
                vkImage,
                skgpu::VulkanAlloc()  // Empty alloc, swapchain images are not owned
            );

        if (!backendTexture.isValid()) {
            std::cerr << "  Failed to create BackendTexture for image " << i << std::endl;
            return false;
        }

        // Wrap as SkSurface
        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        imageData.surface = SkSurfaces::WrapBackendTexture(
            m_impl->recorder.get(),
            backendTexture,
            SkColorSpace::MakeSRGB(),
            &props
        );

        if (!imageData.surface) {
            std::cerr << "  Failed to wrap swapchain image " << i << " as SkSurface" << std::endl;
            std::cerr << "  This usually means the format is not supported by Graphite" << std::endl;
            return false;
        }

        std::cout << "  Created surface for swapchain image " << i << std::endl;
    }

    m_impl->surfacesCreated = true;
    return true;
}

void SkiaRenderer::render() {
    if (!m_impl->recorder || !m_impl->graphiteContext) {
        std::cerr << "Skia not initialized" << std::endl;
        return;
    }

    m_impl->frameCount++;

    // Create surfaces if needed
    if (!m_impl->surfacesCreated) {
        if (!createSwapchainSurfaces()) {
            std::cerr << "Failed to create swapchain surfaces" << std::endl;
            return;
        }
    }

    uint32_t imageIndex = m_context->getCurrentImageIndex();
    VkExtent2D extent = m_context->getSwapchainExtent();

    // Get the surface for this image
    auto& imageData = m_impl->swapchainImages[imageIndex];
    if (!imageData.surface) {
        std::cerr << "No surface for image " << imageIndex << std::endl;
        return;
    }

    // Draw to the surface
    SkCanvas* canvas = imageData.surface->getCanvas();
    if (!canvas) {
        std::cerr << "Failed to get canvas" << std::endl;
        return;
    }

    // Calculate animation time
    auto now = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(now - m_impl->startTime).count();

    // Clear background
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
    canvas->drawString("Renderer: Skia Graphite", 20, extent.height - 45, font, paint);
    canvas->drawString("Press ESC to exit", 20, extent.height - 25, font, paint);

    // Snap recording
    auto recording = m_impl->recorder->snap();
    if (!recording) {
        std::cerr << "Failed to snap recording" << std::endl;
        return;
    }

    // Set up insertion info following Skia's example
    skgpu::graphite::InsertRecordingInfo insertInfo{};
    insertInfo.fRecording = recording.get();

    // Set target surface and layout transition state (key for proper presentation!)
    insertInfo.fTargetSurface = imageData.surface.get();
    skgpu::MutableTextureState presentState = skgpu::MutableTextureStates::MakeVulkan(
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        m_context->getGraphicsFamilyIndex()
    );
    insertInfo.fTargetTextureState = &presentState;

    // Wait on acquire semaphore
    VkSemaphore acquireSemaphore = m_context->getImageAvailableSemaphore();
    skgpu::graphite::BackendSemaphore backendAcquireSemaphore =
        skgpu::graphite::BackendSemaphores::MakeVulkan(acquireSemaphore);
    insertInfo.fNumWaitSemaphores = 1;
    insertInfo.fWaitSemaphores = &backendAcquireSemaphore;

    // Signal render finished semaphore for this image
    skgpu::graphite::BackendSemaphore backendRenderSemaphore =
        skgpu::graphite::BackendSemaphores::MakeVulkan(imageData.renderFinishedSemaphore);
    insertInfo.fNumSignalSemaphores = 1;
    insertInfo.fSignalSemaphores = &backendRenderSemaphore;

    // Insert recording
    m_impl->graphiteContext->insertRecording(insertInfo);

    // Submit (async)
    m_impl->graphiteContext->submit(skgpu::graphite::SyncToCpu::kNo);
}

VkSemaphore SkiaRenderer::getRenderFinishedSemaphore() const {
    uint32_t imageIndex = m_context->getCurrentImageIndex();
    if (imageIndex < m_impl->swapchainImages.size()) {
        return m_impl->swapchainImages[imageIndex].renderFinishedSemaphore;
    }
    return VK_NULL_HANDLE;
}

} // namespace skia_renderer
