#include "Application.h"
#include "renderer/VulkanContext.h"
#include "renderer/SkiaRenderer.h"
#include "renderer/Swapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <chrono>
#include <iostream>

namespace skia_renderer {

struct Application::Impl {
    SDL_Window* window = nullptr;
    std::unique_ptr<VulkanContext> vulkanContext;
    std::unique_ptr<SkiaRenderer> skiaRenderer;
    
    std::string title;
    int width;
    int height;
    bool running = false;
    bool initialized = false;
    
    Impl(const std::string& t, int w, int h) : title(t), width(w), height(h) {}
};

Application::Application(const std::string& title, int width, int height)
    : m_impl(std::make_unique<Impl>(title, width, height)) {
}

Application::~Application() {
    if (m_impl->initialized) {
        shutdown();
    }
}

bool Application::initialize() {
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
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
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }

    // Initialize Vulkan context
    m_impl->vulkanContext = std::make_unique<VulkanContext>();
    if (!m_impl->vulkanContext->initialize(m_impl->window)) {
        std::cerr << "Failed to initialize Vulkan context" << std::endl;
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
        std::cerr << "Failed to initialize Skia renderer" << std::endl;
        m_impl->vulkanContext->shutdown();
        SDL_DestroyWindow(m_impl->window);
        SDL_Quit();
        return false;
    }

    m_impl->initialized = true;
    m_impl->running = true;
    
    std::cout << "Application initialized successfully!" << std::endl;
    std::cout << "  Window: " << m_impl->width << "x" << m_impl->height << std::endl;
    std::cout << "  GPU: " << m_impl->vulkanContext->getDeviceName() << std::endl;
    
    return true;
}

void Application::run() {
    if (!m_impl->initialized) {
        std::cerr << "Application not initialized!" << std::endl;
        return;
    }

    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    float fpsTimer = 0.0f;

    while (m_impl->running) {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // FPS calculation
        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 1.0f) {
            // Could output FPS here if desired
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
    std::cout << "Application shut down successfully." << std::endl;
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
