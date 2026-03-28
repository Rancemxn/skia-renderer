#pragma once

#ifdef _WIN32

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <wrl/client.h>

// Forward declarations for D3D12 types
interface ID3D12Device;
interface ID3D12CommandQueue;
interface ID3D12CommandAllocator;
interface ID3D12GraphicsCommandList;
interface ID3D12Fence;
interface IDXGISwapChain3;
interface IDXGIAdapter1;
interface ID3D12DescriptorHeap;
interface ID3D12Resource;

namespace skia_renderer {

/**
 * @brief Direct3D 12 context manager
 *
 * Manages D3D12 device, command queue, swap chain and related resources
 * for the Skia Ganesh D3D backend.
 */
class D3DContext {
public:
    D3DContext();
    ~D3DContext();

    // Delete copy and move
    D3DContext(const D3DContext&) = delete;
    D3DContext& operator=(const D3DContext&) = delete;
    D3DContext(D3DContext&&) = delete;
    D3DContext& operator=(D3DContext&&) = delete;

    /**
     * @brief Initialize D3D12 context
     * @param window SDL window
     * @param width Initial width
     * @param height Initial height
     * @return true if initialization succeeded
     */
    bool initialize(SDL_Window* window, int width, int height);

    /**
     * @brief Shutdown D3D12 context
     */
    void shutdown();

    /**
     * @brief Present (swap buffers)
     */
    void present();

    /**
     * @brief Wait for GPU to finish
     */
    void waitForGPU();

    /**
     * @brief Resize swap chain
     */
    void resize(int width, int height);

    /**
     * @brief Transition back buffer to render target state
     */
    void transitionToRenderTarget();

    /**
     * @brief Transition back buffer to present state
     */
    void transitionToPresent();

    /**
     * @brief Get current back buffer index
     */
    UINT getCurrentBackBufferIndex() const { return m_frameIndex; }

    /**
     * @brief Get D3D12 device
     */
    ID3D12Device* getDevice() const { return m_device.Get(); }

    /**
     * @brief Get command queue
     */
    ID3D12CommandQueue* getCommandQueue() const { return m_commandQueue.Get(); }

    /**
     * @brief Get adapter
     */
    IDXGIAdapter1* getAdapter() const { return m_adapter.Get(); }

    /**
     * @brief Get current back buffer resource
     */
    ID3D12Resource* getCurrentBackBuffer() const;

    /**
     * @brief Get RTV descriptor heap
     */
    ID3D12DescriptorHeap* getRTVHeap() const { return m_rtvHeap.Get(); }

    /**
     * @brief Get RTV descriptor size
     */
    UINT getRTVDescriptorSize() const { return m_rtvDescriptorSize; }

    /**
     * @brief Check if context is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get adapter description
     */
    std::string getAdapterDescription() const;

    /**
     * @brief Get window width
     */
    int getWidth() const { return m_width; }

    /**
     * @brief Get window height
     */
    int getHeight() const { return m_height; }

private:
    bool createDevice();
    bool createCommandQueue();
    bool createSwapChain();
    bool createDescriptorHeaps();
    bool createRenderTargetViews();
    void cleanupRenderTargetViews();
    bool createCommandObjects();

    SDL_Window* m_window = nullptr;
    bool m_initialized = false;
    int m_width = 0;
    int m_height = 0;

    // D3D12 core objects
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;

    // Command objects for resource barriers
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    // Descriptor heaps
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    UINT m_rtvDescriptorSize = 0;

    // Render targets
    static const UINT FrameCount = 2;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    UINT m_frameIndex = 0;

    // Synchronization
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;
};

} // namespace skia_renderer

#endif // _WIN32
