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
#include "include/gpu/vk/VulkanExtensions.h"

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

// Skia Vulkan Memory Allocator (internal)
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

    // Skia-managed render target surface
    sk_sp<SkSurface> renderSurface;

    // Vulkan command buffer for blitting to swapchain
    VkCommandPool blitCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer blitCommandBuffer = VK_NULL_HANDLE;

    // Timing for animation
    std::chrono::high_resolution_clock::time_point startTime;

    // Debug
    uint64_t frameCount = 0;
    bool debugLogged = false;
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

    if (!createBlitResources()) {
        std::cerr << "Failed to create blit resources" << std::endl;
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

    m_impl->renderSurface.reset();
    m_impl->recorder.reset();
    m_impl->graphiteContext.reset();
    m_impl->vulkanAllocator.reset();

    // Cleanup blit resources
    VkDevice device = m_context->getDevice();
    if (m_impl->blitCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_impl->blitCommandPool, nullptr);
        m_impl->blitCommandPool = VK_NULL_HANDLE;
    }

    m_initialized = false;
    std::cout << "Skia Graphite renderer shut down." << std::endl;
}

void SkiaRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;
    m_impl->renderSurface.reset();
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

bool SkiaRenderer::createBlitResources() {
    VkDevice device = m_context->getDevice();

    // Create command pool for blit operations
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_context->getGraphicsFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_impl->blitCommandPool) != VK_SUCCESS) {
        std::cerr << "  Failed to create blit command pool" << std::endl;
        return false;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_impl->blitCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &m_impl->blitCommandBuffer) != VK_SUCCESS) {
        std::cerr << "  Failed to allocate blit command buffer" << std::endl;
        return false;
    }

    std::cout << "  Blit resources created" << std::endl;
    return true;
}

bool SkiaRenderer::createRenderTarget() {
    if (!m_impl->recorder) {
        return false;
    }

    std::cout << "  Creating render target surface (" << m_width << "x" << m_height << ")..." << std::endl;

    // Create a render target surface using Graphite
    // Use BGRA format that matches typical swapchain format
    SkImageInfo imageInfo = SkImageInfo::Make(
        m_width, m_height,
        kBGRA_8888_SkColorType,
        kPremul_SkAlphaType,
        SkColorSpace::MakeSRGB()
    );

    SkSurfaceProps props(0, kUnknown_SkPixelGeometry);

    m_impl->renderSurface = SkSurfaces::RenderTarget(
        m_impl->recorder.get(),
        imageInfo,
        skgpu::graphite::Budgeted::kYes,
        &props
    );

    if (!m_impl->renderSurface) {
        std::cerr << "  Failed to create render target surface" << std::endl;
        return false;
    }

    std::cout << "  Render target surface created" << std::endl;
    return true;
}

void SkiaRenderer::render() {
    if (!m_impl->recorder || !m_impl->graphiteContext) {
        return;
    }

    m_impl->frameCount++;

    // Create render surface if needed
    if (!m_impl->renderSurface) {
        if (!createRenderTarget()) {
            std::cerr << "Frame " << m_impl->frameCount << ": Failed to create render target" << std::endl;
            return;
        }
    }

    // Get canvas and draw
    SkCanvas* canvas = m_impl->renderSurface->getCanvas();
    if (!canvas) {
        std::cerr << "Frame " << m_impl->frameCount << ": Failed to get canvas" << std::endl;
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

    float centerX = m_width / 2.0f;
    float centerY = m_height / 2.0f;
    float size = std::min(m_width, m_height) * 0.15f;

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
    canvas->drawString("Renderer: Skia Graphite", 20, m_height - 45, font, paint);
    canvas->drawString("Press ESC to exit", 20, m_height - 25, font, paint);

    // Get backend texture for the rendered content
    skgpu::graphite::BackendTexture backendTexture = m_impl->renderSurface->getBackendTexture();
    if (!backendTexture.isValid()) {
        std::cerr << "Frame " << m_impl->frameCount << ": Invalid backend texture" << std::endl;
        return;
    }

    // Get Vulkan image from backend texture
    VkImage skiaImage = skgpu::graphite::BackendTextures::GetVkImage(backendTexture);
    if (skiaImage == VK_NULL_HANDLE) {
        std::cerr << "Frame " << m_impl->frameCount << ": Failed to get Vulkan image from Skia texture" << std::endl;
        return;
    }

    // Log once for debugging
    if (!m_impl->debugLogged) {
        std::cout << "Skia render texture VkImage: " << skiaImage << std::endl;
        m_impl->debugLogged = true;
    }

    // Snap recording
    auto recording = m_impl->recorder->snap();
    if (!recording) {
        std::cerr << "Frame " << m_impl->frameCount << ": Failed to snap recording" << std::endl;
        return;
    }

    // Insert recording with synchronization
    skgpu::graphite::InsertRecordingInfo insertInfo{};
    insertInfo.fRecording = recording.get();

    // Wait on acquire semaphore, signal when done
    VkSemaphore acquireSemaphore = m_context->getImageAvailableSemaphore();
    VkSemaphore renderFinishedSemaphore = m_context->getRenderFinishedSemaphore();

    skgpu::graphite::BackendSemaphore waitSemaphore;
    waitSemaphore.initVulkan(acquireSemaphore);

    skgpu::graphite::BackendSemaphore signalSemaphore;
    signalSemaphore.initVulkan(renderFinishedSemaphore);

    insertInfo.fNumWaitSemaphores = 1;
    insertInfo.fWaitSemaphores = &waitSemaphore;
    insertInfo.fNumSignalSemaphores = 1;
    insertInfo.fSignalSemaphores = &signalSemaphore;

    m_impl->graphiteContext->insertRecording(insertInfo);

    // Submit Skia work
    m_impl->graphiteContext->submit();

    // Now blit from Skia's texture to swapchain image
    blitToSwapchain(skiaImage);
}

void SkiaRenderer::blitToSwapchain(VkImage srcImage) {
    VkDevice device = m_context->getDevice();
    VkCommandBuffer cmd = m_impl->blitCommandBuffer;

    uint32_t imageIndex = m_context->getCurrentImageIndex();
    VkImage dstImage = m_context->getSwapchain()->getImage(imageIndex);
    VkExtent2D extent = m_context->getSwapchainExtent();

    // Reset command buffer
    vkResetCommandBuffer(cmd, 0);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        std::cerr << "Failed to begin blit command buffer" << std::endl;
        return;
    }

    // Transition Skia's image to transfer src
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

    // Transition swapchain image to transfer dst
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

    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Perform blit
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

    vkCmdBlitImage2(cmd, &blitInfo);

    // Transition swapchain image to present
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

    VkDependencyInfo presentDepInfo{};
    presentDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDepInfo.imageMemoryBarrierCount = 1;
    presentDepInfo.pImageMemoryBarriers = &presentBarrier;

    vkCmdPipelineBarrier2(cmd, &presentDepInfo);

    // End command buffer
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        std::cerr << "Failed to end blit command buffer" << std::endl;
        return;
    }

    // Submit command buffer
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;

    // Wait on render finished semaphore from Skia
    VkSemaphore renderFinished = m_context->getRenderFinishedSemaphore();
    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = renderFinished;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;

    // Signal same semaphore when done (for present)
    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = renderFinished;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    VkQueue graphicsQueue = m_context->getGraphicsQueue();
    if (vkQueueSubmit2(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        std::cerr << "Failed to submit blit command buffer" << std::endl;
    }
}

} // namespace skia_renderer
