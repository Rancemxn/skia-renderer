#include "SkiaRenderer.h"
#include "VulkanContext.h"

// Skia core headers
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include "include/core/SkRect.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"

// Skia GPU headers
#include "include/gpu/vk/VulkanExtensions.h"

// Skia Graphite headers
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteTypes.h"

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
    sk_sp<SkSurface> surface;
    skgpu::VulkanExtensions vkExtensions;
    VkPhysicalDeviceFeatures physicalDeviceFeatures{};
    sk_sp<skgpu::VulkanMemoryAllocator> vulkanAllocator;
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

    if (!createSkiaContext()) {
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

    m_impl->surface.reset();
    m_impl->recorder.reset();
    m_impl->graphiteContext.reset();
    m_impl->vulkanAllocator.reset();

    m_initialized = false;
    std::cout << "Skia Graphite renderer shut down." << std::endl;
}

void SkiaRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;
}

bool SkiaRenderer::createSkiaContext() {
    // Get proc address function
    auto getProc = [](const char* name, VkInstance instance, VkDevice device) {
        if (device != VK_NULL_HANDLE) {
            return vkGetDeviceProcAddr(device, name);
        }
        return vkGetInstanceProcAddr(instance, name);
    };

    std::cout << "Creating Skia Graphite context..." << std::endl;
    std::cout << "  Instance: " << (m_context->getInstance() ? "valid" : "null") << std::endl;
    std::cout << "  PhysicalDevice: " << (m_context->getPhysicalDevice() ? "valid" : "null") << std::endl;
    std::cout << "  Device: " << (m_context->getDevice() ? "valid" : "null") << std::endl;
    std::cout << "  Queue: " << (m_context->getGraphicsQueue() ? "valid" : "null") << std::endl;

    // Get physical device features
    vkGetPhysicalDeviceFeatures(
        m_context->getPhysicalDevice(),
        &m_impl->physicalDeviceFeatures
    );

    // Initialize Vulkan extensions
    const char* instanceExtensions[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#ifdef ENABLE_VULKAN_VALIDATION
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };
    
    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    std::cout << "  Initializing Vulkan extensions..." << std::endl;
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
    
    // Set required Vulkan 1.3 API version for Graphite
    backendContext.fMaxAPIVersion = VK_API_VERSION_1_3;
    
    // Set extensions and features
    backendContext.fVkExtensions = &m_impl->vkExtensions;
    backendContext.fDeviceFeatures = &m_impl->physicalDeviceFeatures;
    
    backendContext.fGetProc = getProc;

    // Create memory allocator using Skia's internal VMA wrapper
    std::cout << "  Creating VMA memory allocator via Skia..." << std::endl;
    m_impl->vulkanAllocator = skgpu::VulkanMemoryAllocators::Make(
        backendContext,
        skgpu::ThreadSafe::kNo
    );
    
    if (!m_impl->vulkanAllocator) {
        std::cerr << "Failed to create Vulkan memory allocator" << std::endl;
        return false;
    }
    std::cout << "  VMA memory allocator created successfully" << std::endl;
    
    // Set memory allocator in backend context
    backendContext.fMemoryAllocator = m_impl->vulkanAllocator;

    std::cout << "  Backend context configured, creating Graphite context..." << std::endl;

    // Create Graphite Context
    skgpu::graphite::ContextOptions options;
    m_impl->graphiteContext = skgpu::graphite::ContextFactory::MakeVulkan(backendContext, options);
    
    if (!m_impl->graphiteContext) {
        std::cerr << "Failed to create Skia Graphite Vulkan context" << std::endl;
        return false;
    }

    // Create recorder for recording drawing commands
    m_impl->recorder = m_impl->graphiteContext->makeRecorder();
    if (!m_impl->recorder) {
        std::cerr << "Failed to create Graphite recorder" << std::endl;
        return false;
    }

    std::cout << "  Skia Graphite context created successfully!" << std::endl;
    return true;
}

void SkiaRenderer::renderToSwapchainImage(VkImage swapchainImage, VkFormat format, 
                                           VkImageLayout currentLayout, uint32_t imageIndex) {
    if (!m_impl->recorder || !m_impl->graphiteContext) {
        return;
    }

    // Determine color type from swapchain format
    SkColorType colorType = kRGBA_8888_SkColorType;
    if (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM) {
        colorType = kBGRA_8888_SkColorType;
    }

    // Create VulkanTextureInfo for the swapchain image
    skgpu::graphite::VulkanTextureInfo vulkanTextureInfo(
        VK_SAMPLE_COUNT_1_BIT,                    // sampleCount
        skgpu::Mipmapped::kNo,                    // mipmapped
        0,                                        // flags
        format,                                   // format
        VK_IMAGE_TILING_OPTIMAL,                  // imageTiling
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,  // imageUsageFlags
        VK_SHARING_MODE_EXCLUSIVE,                // sharingMode
        VK_IMAGE_ASPECT_COLOR_BIT,                // aspectMask
        skgpu::VulkanYcbcrConversionInfo()        // ycbcrConversionInfo
    );

    // Create TextureInfo from VulkanTextureInfo
    skgpu::graphite::TextureInfo textureInfo = 
        skgpu::graphite::TextureInfos::MakeVulkan(vulkanTextureInfo);

    // Create an empty VulkanAlloc (swapchain images are borrowed, not owned by Skia)
    skgpu::VulkanAlloc vulkanAlloc{};
    vulkanAlloc.fMemory = VK_NULL_HANDLE;  // Borrowed image
    vulkanAlloc.fOffset = 0;
    vulkanAlloc.fSize = 0;

    // Create BackendTexture from the swapchain image
    SkISize dimensions = SkISize::Make(m_width, m_height);
    skgpu::graphite::BackendTexture backendTexture = 
        skgpu::graphite::BackendTextures::MakeVulkan(
            dimensions,
            vulkanTextureInfo,
            currentLayout,
            m_context->getGraphicsFamilyIndex(),
            swapchainImage,
            vulkanAlloc
        );

    if (!backendTexture.isValid()) {
        std::cerr << "Failed to create BackendTexture for swapchain image" << std::endl;
        return;
    }

    // Wrap the swapchain image as an SkSurface
    SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    m_impl->surface = SkSurfaces::WrapBackendTexture(
        m_impl->recorder.get(),
        backendTexture,
        SkColorSpace::MakeSRGB(),
        &props
    );

    if (!m_impl->surface) {
        std::cerr << "Failed to wrap swapchain image as SkSurface" << std::endl;
        return;
    }

    // Draw content to the surface
    drawContent();

    // Snap recording
    auto recording = m_impl->recorder->snap();
    if (!recording) {
        std::cerr << "Failed to snap recording" << std::endl;
        return;
    }

    // Insert recording into context
    skgpu::graphite::InsertRecordingInfo insertInfo;
    insertInfo.fRecording = recording.get();
    m_impl->graphiteContext->insertRecording(insertInfo);

    // Submit to GPU
    m_impl->graphiteContext->submit();

    // Reset surface for next frame
    m_impl->surface.reset();
}

void SkiaRenderer::drawContent() {
    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        return;
    }

    // Clear background with a dark color
    canvas->clear(SkColorSetRGB(30, 30, 40));

    // Draw animated content
    static float time = 0.0f;
    time += 0.016f; // Approximate 60fps increment

    // Draw a rotating rectangle
    SkPaint paint;
    paint.setColor(SkColorSetARGB(255, 100, 200, 255));
    paint.setAntiAlias(true);

    float centerX = m_width / 2.0f;
    float centerY = m_height / 2.0f;
    float size = std::min(m_width, m_height) * 0.2f;

    canvas->save();
    canvas->translate(centerX, centerY);
    canvas->rotate(time * 60.0f);
    
    SkRect rect = SkRect::MakeXYWH(-size/2, -size/2, size, size);
    canvas->drawRect(rect, paint);
    canvas->restore();

    // Draw a circle
    paint.setColor(SkColorSetARGB(200, 255, 100, 100));
    canvas->drawCircle(
        centerX + std::sin(time) * 100,
        centerY + std::cos(time) * 100,
        30,
        paint
    );

    // Draw some text using SkFont (new Skia API)
    SkFont font;
    font.setSize(24);
    paint.setColor(SK_ColorWHITE);
    canvas->drawString("Skia Graphite + Vulkan 1.3", 20, 40, font, paint);
    
    // Draw FPS indicator area
    font.setSize(16);
    paint.setColor(SkColorSetARGB(180, 180, 180, 180));
    canvas->drawString("Renderer: Skia Graphite + Vulkan 1.3", 20, m_height - 40, font, paint);
    canvas->drawString("Press ESC to exit", 20, m_height - 20, font, paint);
}

} // namespace skia_renderer
