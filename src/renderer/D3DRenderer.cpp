#ifdef _WIN32

#include "D3DRenderer.h"
#include "D3DContext.h"
#include "SceneRenderer.h"
#include "core/Logger.h"

// Skia Ganesh (D3D) headers
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"

namespace skia_renderer {

struct D3DRenderer::Impl {
    sk_sp<GrDirectContext> grContext;
    sk_sp<SkSurface> surface;
    GrD3DTextureResourceInfo textureInfo;
};

D3DRenderer::D3DRenderer()
    : m_impl(std::make_unique<Impl>())
    , m_d3dContext(std::make_unique<D3DContext>())
    , m_sceneRenderer(std::make_unique<SceneRenderer>()) {
}

D3DRenderer::~D3DRenderer() {
    if (m_initialized) {
        shutdown();
    }
}

bool D3DRenderer::initialize(SDL_Window* window, int width, int height, const BackendConfig& config) {
    (void)config;  // Suppress unused parameter warning
    
    m_window = window;
    m_width = width;
    m_height = height;

    LOG_INFO("Initializing Skia Ganesh D3D12 renderer...");

    // Initialize D3D12 context
    if (!m_d3dContext->initialize(window, width, height)) {
        LOG_ERROR("Failed to create D3D12 context");
        return false;
    }

    if (!createSkiaContext()) {
        LOG_ERROR("Failed to create Skia context");
        return false;
    }

    if (!createSurface()) {
        LOG_ERROR("Failed to create Skia surface");
        return false;
    }

    // Initialize scene renderer fonts
    if (!m_sceneRenderer->initializeFonts()) {
        LOG_WARN("Failed to initialize fonts for scene renderer");
    }

    m_initialized = true;
    LOG_INFO("Skia Ganesh D3D12 renderer initialized ({}x{})", width, height);
    return true;
}

bool D3DRenderer::createSkiaContext() {
    LOG_INFO("  Creating Skia Ganesh D3D12 context...");

    // Create backend context
    GrD3DBackendContext backendContext;
    
    // Use gr_cp constructor to adopt the pointers
    // Note: gr_cp constructor does NOT call AddRef, so we need to do it manually
    ID3D12Device* device = m_d3dContext->getDevice();
    ID3D12CommandQueue* queue = m_d3dContext->getCommandQueue();
    IDXGIAdapter1* adapter = m_d3dContext->getAdapter();
    
    // AddRef since gr_cp will Release when destroyed
    adapter->AddRef();
    device->AddRef();
    queue->AddRef();
    
    backendContext.fAdapter = gr_cp<IDXGIAdapter1>(adapter);
    backendContext.fDevice = gr_cp<ID3D12Device>(device);
    backendContext.fQueue = gr_cp<ID3D12CommandQueue>(queue);
    backendContext.fProtectedContext = GrProtected::kNo;

    // Create context options
    GrContextOptions options;

    // Create Ganesh context
    m_impl->grContext = GrDirectContexts::MakeD3D(backendContext, options);
    if (!m_impl->grContext) {
        LOG_ERROR("  Failed to create Skia Ganesh D3D12 context");
        return false;
    }

    LOG_INFO("  Skia Ganesh D3D12 context created successfully");
    return true;
}

bool D3DRenderer::createSurface() {
    if (!m_impl->grContext) {
        LOG_ERROR("  No Skia context available");
        return false;
    }

    ID3D12Resource* backBuffer = m_d3dContext->getCurrentBackBuffer();
    if (!backBuffer) {
        LOG_ERROR("  Failed to get back buffer");
        return false;
    }

    LOG_INFO("  Creating Skia surface ({}x{})...", m_width, m_height);

    // Setup texture resource info
    m_impl->textureInfo = GrD3DTextureResourceInfo();
    
    // AddRef the back buffer since gr_cp will Release it
    backBuffer->AddRef();
    m_impl->textureInfo.fResource = gr_cp<ID3D12Resource>(backBuffer);
    m_impl->textureInfo.fResourceState = D3D12_RESOURCE_STATE_PRESENT;
    m_impl->textureInfo.fFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_impl->textureInfo.fSampleCount = 1;
    m_impl->textureInfo.fLevelCount = 1;

    // Create backend render target
    GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeD3D(
        m_width, m_height, m_impl->textureInfo);

    if (!backendRT.isValid()) {
        LOG_ERROR("  Failed to create backend render target");
        return false;
    }

    // Create Skia surface with top-left origin (D3D convention)
    SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    m_impl->surface = SkSurfaces::WrapBackendRenderTarget(
        m_impl->grContext.get(),
        backendRT,
        kTopLeft_GrSurfaceOrigin,  // D3D uses top-left origin
        kRGBA_8888_SkColorType,
        SkColorSpace::MakeSRGB(),
        &props
    );

    if (!m_impl->surface) {
        LOG_ERROR("  Failed to wrap back buffer as Skia surface");
        return false;
    }

    LOG_INFO("  Skia surface created successfully");

    // Verify we can get a canvas
    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("  Failed to get canvas from surface");
        return false;
    }

    LOG_INFO("  Canvas obtained successfully");
    return true;
}

void D3DRenderer::destroySurface() {
    m_impl->surface.reset();
    m_impl->textureInfo.fResource.reset();
}

void D3DRenderer::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down Skia Ganesh D3D12 renderer...");

    // Wait for GPU to finish
    if (m_impl->grContext) {
        m_impl->grContext->flushAndSubmit();
    }

    destroySurface();
    m_impl->grContext.reset();
    m_d3dContext.reset();

    m_initialized = false;
    LOG_INFO("Skia Ganesh D3D12 renderer shut down.");
}

void D3DRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;

    // Recreate surface for new size
    if (m_initialized && m_impl->grContext) {
        destroySurface();

        // Flush before resize
        m_impl->grContext->flushAndSubmit();

        // Resize D3D context
        m_d3dContext->resize(width, height);

        createSurface();
    }

    LOG_INFO("D3DRenderer resized to {}x{}", width, height);
}

bool D3DRenderer::beginFrame() {
    if (!m_initialized) {
        LOG_ERROR("D3DRenderer::beginFrame called but not initialized");
        return false;
    }

    // Wait for previous frame to complete
    m_d3dContext->waitForGPU();

    return true;
}

void D3DRenderer::endFrame() {
    if (!m_impl->grContext) {
        return;
    }

    // Flush and submit Skia rendering
    m_impl->grContext->flushAndSubmit();

    // Present
    m_d3dContext->present();
}

void D3DRenderer::render() {
    if (!m_impl->grContext || !m_impl->surface) {
        LOG_ERROR("D3DRenderer not initialized");
        return;
    }

    SkCanvas* canvas = m_impl->surface->getCanvas();
    if (!canvas) {
        LOG_ERROR("Failed to get canvas");
        return;
    }

    // Update scene renderer state
    m_sceneRenderer->setFPS(m_fps);

    // Build backend info string
    std::string backendInfo = m_d3dContext->getAdapterDescription();

    // Use shared scene renderer
    m_sceneRenderer->render(canvas, m_width, m_height, backendInfo, "Skia Ganesh D3D12");

    // Flush after each render call
    m_impl->grContext->flush();
}

void D3DRenderer::setFPS(float fps) {
    m_fps = fps;
}

} // namespace skia_renderer

#endif // _WIN32
