#include "SkiaRenderer.h"
#include "VulkanContext.h"

// Skia core headers (relative to skia root)
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include "include/core/SkRect.h"

// Skia GPU headers - Use standard Ganesh paths for chrome/m146
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/vk/GrVkBackendContext.h"

#include <iostream>
#include <chrono>
#include <cmath>

namespace skia_renderer {

struct SkiaRenderer::Impl {
    sk_sp<GrDirectContext> grContext;
    sk_sp<SkSurface> surface;
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

    if (!createSkiaSurface()) {
        return false;
    }

    m_initialized = true;
    std::cout << "Skia renderer initialized (" << width << "x" << height << ")" << std::endl;
    return true;
}

void SkiaRenderer::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_impl->surface.reset();
    m_impl->grContext.reset();

    m_initialized = false;
    std::cout << "Skia renderer shut down." << std::endl;
}

void SkiaRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;
    
    // Recreate surface with new dimensions
    m_impl->surface.reset();
    createSkiaSurface();
}

bool SkiaRenderer::createSkiaContext() {
    // Create Skia Vulkan backend context
    GrVkBackendContext backendContext{};
    
    backendContext.fInstance = m_context->getInstance();
    backendContext.fPhysicalDevice = m_context->getPhysicalDevice();
    backendContext.fDevice = m_context->getDevice();
    backendContext.fQueue = m_context->getGraphicsQueue();
    backendContext.fGraphicsQueueIndex = m_context->getGraphicsFamilyIndex();
    
    // Get device proc address
    backendContext.fGetProc = [](const char* name, VkInstance instance, VkDevice device) {
        // Use vkGetInstanceProcAddr for instance functions, vkGetDeviceProcAddr for device functions
        if (device != VK_NULL_HANDLE) {
            return vkGetDeviceProcAddr(device, name);
        } else if (instance != VK_NULL_HANDLE) {
            return vkGetInstanceProcAddr(instance, name);
        }
        return (PFN_vkVoidFunction)nullptr;
    };

    // Create GrDirectContext
    m_impl->grContext = GrDirectContext::MakeVulkan(backendContext);
    
    if (!m_impl->grContext) {
        std::cerr << "Failed to create Skia Vulkan context" << std::endl;
        return false;
    }

    return true;
}

bool SkiaRenderer::createSkiaSurface() {
    if (!m_impl->grContext) {
        return false;
    }

    // Create render target for current swapchain image
    // Note: In a production renderer, we'd create one surface per swapchain image
    // For simplicity, we're creating surfaces on-demand in render()
    
    return true;
}

void SkiaRenderer::render(VkFramebuffer framebuffer) {
    if (!m_impl->grContext) {
        return;
    }

    // Get swapchain image info
    VkExtent2D extent = m_context->getSwapchainExtent();
    VkFormat format = m_context->getSwapchainFormat();
    
    // Map Vulkan format to Skia color type
    SkColorType colorType = kRGBA_8888_SkColorType;
    if (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM) {
        colorType = kBGRA_8888_SkColorType;
    }

    // Create render target for this frame's swapchain image
    GrVkRenderTargetInfo rtInfo{};
    rtInfo.fImage = VK_NULL_HANDLE; // We'll get this from the swapchain
    rtInfo.fFormat = format;
    rtInfo.fSampleCount = 1;
    rtInfo.fLevelCount = 1;
    rtInfo.fOrigin = kTopLeft_GrSurfaceOrigin;
    
    // For now, use the simpler approach: wrap the framebuffer
    // In a real implementation, we'd need the VkImage from the swapchain
    
    // Create surface from render target properties
    SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    
    // Note: This is a simplified rendering approach
    // In production, we'd use proper Vulkan interop with Graphite
    // For now, we'll demonstrate basic rendering concept
    
    // Get or create surface
    if (!m_impl->surface) {
        // Create a CPU surface for demonstration
        // In real Graphite usage, this would be a GPU surface from Vulkan
        SkImageInfo imageInfo = SkImageInfo::Make(extent.width, extent.height, 
                                                   colorType, kPremul_SkAlphaType);
        m_impl->surface = SkSurfaces::Raster(imageInfo, &props);
    }

    if (!m_impl->surface) {
        std::cerr << "Failed to create Skia surface" << std::endl;
        return;
    }

    // Draw content
    drawContent();

    // Flush and submit
    m_impl->surface->flushAndSubmit();
    
    (void)framebuffer; // Will be used in full implementation
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

    // Draw some text
    paint.setColor(SK_ColorWHITE);
    paint.setTextSize(24);
    canvas->drawString("Skia Graphite + Vulkan", 20, 40, paint);
    
    // Draw FPS indicator area
    paint.setTextSize(16);
    paint.setColor(SkColorSetARGB(180, 180, 180, 180));
    canvas->drawString("Renderer: Skia + Vulkan 1.3", 20, m_height - 40, paint);
    canvas->drawString("Press ESC to exit", 20, m_height - 20, paint);
}

} // namespace skia_renderer
