#include "Application.h"
#include "Logger.h"
#include "renderer/VulkanContext.h"
#include "renderer/SkiaRenderer.h"
#include "renderer/Swapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <chrono>

namespace skia_renderer {

struct Application::Impl {
    SDL_Window* window = nullptr;
    std::unique_ptr<VulkanContext> vulkanContext;
    std::unique_ptr<SkiaRenderer> skiaRenderer;
    
    std::string title;
    int width;
    int height;
    VulkanVersionConfig vulkanVersion;
    bool running = false;
    bool initialized = false;
    
    Impl(const std::string& t, int w, int h, const VulkanVersionConfig& v) 
        : title(t), width(w), height(h), vulkanVersion(v) {}
};

Application::Application(const std::string& title, int width, int height,
                         const VulkanVersionConfig& vulkanVersion)
    : m_impl(std::make_unique<Impl>(title, width, height, vulkanVersion)) {
}

Application::~Application() {
    if (m_impl->initialized) {
        shutdown();
    }
}

bool Application::initialize() {
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG_ERROR("Failed to initialize SDL: {}", SDL_GetError());
        return false;
    }

    // Create window with Vulkan support
    m_impl->window = SDL_CreateWindow(
        m_impl->title.c_str(),
        m_impl->width,
        m_impl->height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!m_impl->window) {
        LOG_ERROR("Failed to create SDL window: {}", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Initialize Vulkan context with requested version
    m_impl->vulkanContext = std::make_unique<VulkanContext>();
    if (!m_impl->vulkanContext->initialize(m_impl->window, m_impl->vulkanVersion)) {
        LOG_ERROR("Failed to initialize Vulkan context");
        SDL_DestroyWindow(m_impl->window);
        SDL_Quit();
        return false;
    }

    // Initialize Skia renderer
    m_impl->skiaRenderer = std::make_unique<SkiaRenderer>();
    if (!m_impl->skiaRenderer->initialize(
        m_impl->vulkanContext.get(),
        m_impl->width,
        m_impl->height)) {
        LOG_ERROR("Failed to initialize Skia renderer");
        m_impl->vulkanContext->shutdown();
        SDL_DestroyWindow(m_impl->window);
        SDL_Quit();
        return false;
    }

    m_impl->initialized = true;
    m_impl->running = true;
    
    LOG_INFO("Application initialized successfully!");
    LOG_INFO("  Window: {}x{}", m_impl->width, m_impl->height);
    LOG_INFO("  GPU: {}", m_impl->vulkanContext->getDeviceName());
    LOG_INFO("  Vulkan: {}", m_impl->vulkanContext->getCapabilities().getFeatureLevelString());
    
    return true;
}

void Application::run() {
    if (!m_impl->initialized) {
        LOG_ERROR("Application not initialized!");
        return;
    }

    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    float fpsTimer = 0.0f;
    float currentFPS = 0.0f;

    while (m_impl->running) {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // FPS calculation
        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 0.5f) {  // Update FPS every 0.5 seconds
            currentFPS = frameCount / fpsTimer;
            m_impl->skiaRenderer->setFPS(currentFPS);
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        processEvents();
        update(deltaTime);
        render();

        // Cap frame rate
        SDL_Delay(1);
    }
}

void Application::shutdown() {
    if (!m_impl->initialized) {
        return;
    }

    m_impl->running = false;

    // Wait for GPU to finish
    if (m_impl->vulkanContext) {
        m_impl->vulkanContext->waitIdle();
    }

    // Cleanup Skia first (depends on Vulkan)
    m_impl->skiaRenderer.reset();

    // Cleanup Vulkan
    m_impl->vulkanContext.reset();

    // Cleanup SDL
    if (m_impl->window) {
        SDL_DestroyWindow(m_impl->window);
        m_impl->window = nullptr;
    }
    SDL_Quit();

    m_impl->initialized = false;
    LOG_INFO("Application shut down successfully.");
}

void Application::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_impl->running = false;
                break;
                
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    m_impl->running = false;
                }
                break;
                
            case SDL_EVENT_WINDOW_RESIZED:
                int newWidth = event.window.data1;
                int newHeight = event.window.data2;
                
                m_impl->vulkanContext->waitIdle();
                
                // Recreate swapchain and Skia surface
                m_impl->vulkanContext->resize(newWidth, newHeight);
                m_impl->skiaRenderer->resize(newWidth, newHeight);
                
                m_impl->width = newWidth;
                m_impl->height = newHeight;
                break;
        }
    }
}

void Application::update(float deltaTime) {
    // Update logic here
    (void)deltaTime;
}

void Application::render() {
    // Begin frame - acquire swapchain image
    if (!m_impl->vulkanContext->beginFrame()) {
        return;
    }

    // Let Skia Graphite render to the swapchain image
    m_impl->skiaRenderer->render();

    // End frame - present swapchain image with Skia's semaphore
    m_impl->vulkanContext->endFrame(m_impl->skiaRenderer->getRenderFinishedSemaphore());
}

} // namespace skia_renderer
