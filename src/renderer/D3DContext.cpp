#ifdef _WIN32

#include "D3DContext.h"
#include "core/Logger.h"

#include <SDL3/SDL_syswm.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <string>

namespace skia_renderer {

D3DContext::D3DContext() = default;

D3DContext::~D3DContext() {
    if (m_initialized) {
        shutdown();
    }
}

bool D3DContext::initialize(SDL_Window* window, int width, int height) {
    if (m_initialized) {
        LOG_WARN("D3DContext already initialized");
        return true;
    }

    LOG_INFO("Initializing D3D12 context...");

    m_window = window;
    m_width = width;
    m_height = height;

    if (!createDevice()) {
        LOG_ERROR("Failed to create D3D12 device");
        return false;
    }

    if (!createCommandQueue()) {
        LOG_ERROR("Failed to create command queue");
        return false;
    }

    if (!createSwapChain()) {
        LOG_ERROR("Failed to create swap chain");
        return false;
    }

    if (!createDescriptorHeaps()) {
        LOG_ERROR("Failed to create descriptor heaps");
        return false;
    }

    if (!createRenderTargetViews()) {
        LOG_ERROR("Failed to create render target views");
        return false;
    }

    // Create fence for synchronization
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create fence: 0x{:08X}", hr);
        return false;
    }

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        LOG_ERROR("Failed to create fence event");
        return false;
    }

    m_initialized = true;
    LOG_INFO("D3D12 context initialized successfully");
    LOG_INFO("  Adapter: {}", getAdapterDescription());

    return true;
}

bool D3DContext::createDevice() {
    // Enable debug layer in debug builds
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        LOG_INFO("  D3D12 debug layer enabled");
    }
#endif

    // Create DXGI factory
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create DXGI factory: 0x{:08X}", hr);
        return false;
    }

    // Enumerate adapters
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterIndex = 0; 
         DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter); 
         ++adapterIndex) {
        
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Check if adapter supports D3D12
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, 
                                        _uuidof(ID3D12Device), nullptr))) {
            m_adapter = adapter;
            break;
        }
    }

    if (!m_adapter) {
        // Try WARP adapter
        Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
        hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
        if (SUCCEEDED(hr)) {
            warpAdapter.As(&m_adapter);
            LOG_WARN("Using WARP software adapter");
        }
    }

    if (!m_adapter) {
        LOG_ERROR("No suitable D3D12 adapter found");
        return false;
    }

    // Create D3D12 device
    hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 device: 0x{:08X}", hr);
        return false;
    }

    LOG_INFO("  D3D12 device created successfully");
    return true;
}

bool D3DContext::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    HRESULT hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create command queue: 0x{:08X}", hr);
        return false;
    }

    LOG_INFO("  Command queue created successfully");
    return true;
}

bool D3DContext::createSwapChain() {
    // Get HWND from SDL window
    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    HWND hwnd = (HWND)SDL_GetProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd) {
        LOG_ERROR("Failed to get HWND from SDL window");
        return false;
    }

    // Create DXGI factory for swap chain
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create DXGI factory for swap chain: 0x{:08X}", hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = static_cast<UINT>(m_width);
    swapChainDesc.Height = static_cast<UINT>(m_height);
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create swap chain: 0x{:08X}", hr);
        return false;
    }

    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get IDXGISwapChain3: 0x{:08X}", hr);
        return false;
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    LOG_INFO("  Swap chain created successfully ({}x{}, {} buffers)", 
             m_width, m_height, FrameCount);
    return true;
}

bool D3DContext::createDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    HRESULT hr = m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create RTV descriptor heap: 0x{:08X}", hr);
        return false;
    }

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    LOG_INFO("  Descriptor heaps created successfully");
    return true;
}

bool D3DContext::createRenderTargetViews() {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FrameCount; i++) {
        HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get swap chain buffer {}: 0x{:08X}", i, hr);
            return false;
        }

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    LOG_INFO("  Render target views created successfully");
    return true;
}

void D3DContext::cleanupRenderTargetViews() {
    for (UINT i = 0; i < FrameCount; i++) {
        m_renderTargets[i].Reset();
    }
}

void D3DContext::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down D3D12 context...");

    waitForGPU();

    cleanupRenderTargetViews();

    m_rtvHeap.Reset();
    m_swapChain.Reset();
    m_commandQueue.Reset();
    m_device.Reset();
    m_adapter.Reset();
    m_fence.Reset();

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_initialized = false;
    LOG_INFO("D3D12 context shut down.");
}

void D3DContext::present() {
    if (!m_swapChain) {
        return;
    }

    HRESULT hr = m_swapChain->Present(1, 0);  // VSync enabled
    if (FAILED(hr)) {
        LOG_ERROR("Present failed: 0x{:08X}", hr);
    }

    // Update frame index
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3DContext::waitForGPU() {
    if (!m_commandQueue || !m_fence) {
        return;
    }

    // Signal and increment the fence value
    const UINT64 fenceValue = m_fenceValue + 1;
    HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to signal fence: 0x{:08X}", hr);
        return;
    }

    // Wait until the previous frame is finished
    if (m_fence->GetCompletedValue() < fenceValue) {
        hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to set fence event: 0x{:08X}", hr);
            return;
        }
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_fenceValue = fenceValue;
}

void D3DContext::resize(int width, int height) {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Resizing D3D12 context to {}x{}", width, height);

    waitForGPU();

    m_width = width;
    m_height = height;

    // Cleanup render targets
    cleanupRenderTargetViews();

    // Resize swap chain
    HRESULT hr = m_swapChain->ResizeBuffers(
        FrameCount,
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to resize swap chain: 0x{:08X}", hr);
        return;
    }

    // Recreate render target views
    createRenderTargetViews();
}

ID3D12Resource* D3DContext::getCurrentBackBuffer() const {
    if (m_frameIndex < FrameCount) {
        return m_renderTargets[m_frameIndex].Get();
    }
    return nullptr;
}

std::string D3DContext::getAdapterDescription() const {
    if (!m_adapter) {
        return "Unknown";
    }

    DXGI_ADAPTER_DESC1 desc;
    m_adapter->GetDesc1(&desc);

    // Convert wide string to narrow string
    char description[128] = {};
    WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, description, sizeof(description), nullptr, nullptr);
    
    return std::string(description);
}

} // namespace skia_renderer

#endif // _WIN32
