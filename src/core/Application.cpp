#include "Application.h"
#include "Logger.h"
#include "renderer/GLRenderer.h"
#include "renderer/RendererFactory.h"

#include "renderer/AngleRenderer.h"

#ifdef VULKAN_BACKEND_ENABLED
#include "renderer/VulkanContext.h"
#include "renderer/SkiaRenderer.h"
#include "renderer/Swapchain.h"
#include <SDL3/SDL_vulkan.h>
#endif

#include <SDL3/SDL.h>

#include <chrono>

namespace skia_renderer {

// VulkanVersionConfig implementation
uint32_t VulkanVersionConfig::toVkVersion() const {
#ifdef VULKAN_BACKEND_ENABLED
    return VK_MAKE_API_VERSION(0, major, minor, 0);
#else
    return 0;
#endif
}

VulkanVersionConfig VulkanVersionConfig::fromVkVersion(uint32_t version) {
    VulkanVersionConfig cfg;
#ifdef VULKAN_BACKEND_ENABLED
    cfg.major = VK_API_VERSION_MAJOR(version);
    cfg.minor = VK_API_VERSION_MINOR(version);
#else
    (void)version;
#endif
    return cfg;
}

std::string VulkanVersionConfig::toString() const {
    return std::to_string(major) + "." + std::to_string(minor);
}

struct Application::Impl {
    SDL_Window* window = nullptr;
#ifdef VULKAN_BACKEND_ENABLED
    std::unique_ptr<VulkanContext> vulkanContext;
#endif
    std::unique_ptr<IRenderer> renderer;
    
    std::string title;
    int width;
    int height;
    BackendConfig backendConfig;
    bool running = false;
    bool initialized = false;
    
    Impl(const std::string& t, int w, int h, const BackendConfig& b) 
        : title(t), width(w), height(h), backendConfig(b) {}
};

Application::Application(const std::string& title, int width, int height,
                         const BackendConfig& backendConfig)
    : m_impl(std::make_unique<Impl>(title, width, height, backendConfig)) {
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

    // Create window based on backend type
    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;
    
#ifdef VULKAN_BACKEND_ENABLED
    if (m_impl->backendConfig.type == BackendType::Vulkan) {
        windowFlags = static_cast<SDL_WindowFlags>(windowFlags | SDL_WINDOW_VULKAN);
        LOG_INFO("Creating window with Vulkan support");
    } else {
        windowFlags = static_cast<SDL_WindowFlags>(windowFlags | SDL_WINDOW_OPENGL);
        LOG_INFO("Creating window with OpenGL support");
        
        // Set OpenGL attributes BEFORE creating window (critical for proper context creation)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, m_impl->backendConfig.glMajor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, m_impl->backendConfig.glMinor);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    }
#else
    // Only OpenGL/ANGLE is available
    if (m_impl->backendConfig.type == BackendType::Vulkan) {
        LOG_WARN("Vulkan backend requested but not available, falling back to OpenGL");
        m_impl->backendConfig.type = BackendType::OpenGL;
    }
    windowFlags = static_cast<SDL_WindowFlags>(windowFlags | SDL_WINDOW_OPENGL);
    
    // Set OpenGL attributes BEFORE creating window (critical for proper context creation)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, m_impl->backendConfig.glMajor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, m_impl->backendConfig.glMinor);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    
    if (m_impl->backendConfig.type == BackendType::ANGLE) {
        LOG_INFO("Creating window with ANGLE (OpenGL ES) support");
    } else {
        LOG_INFO("Creating window with OpenGL support");
    }
#endif

    m_impl->window = SDL_CreateWindow(
        m_impl->title.c_str(),
        m_impl->width,
        m_impl->height,
        windowFlags
    );

    if (!m_impl->window) {
        LOG_ERROR("Failed to create SDL window: {}", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Initialize the appropriate backend
    bool success = false;
    switch (m_impl->backendConfig.type) {
        case BackendType::Vulkan:
#ifdef VULKAN_BACKEND_ENABLED
            success = initializeVulkanBackend();
#else
            LOG_ERROR("Vulkan backend not available");
            success = false;
#endif
            break;
        case BackendType::OpenGL:
            success = initializeOpenGLBackend();
            break;
        case BackendType::ANGLE:
            success = initializeANGLEBackend();
            break;
    }

    if (!success) {
        SDL_DestroyWindow(m_impl->window);
        SDL_Quit();
        return false;
    }

    m_impl->initialized = true;
    m_impl->running = true;
    
    LOG_INFO("Application initialized successfully!");
    LOG_INFO("  Window: {}x{}", m_impl->width, m_impl->height);
    LOG_INFO("  Backend: {}", m_impl->backendConfig.toString());
    
    return true;
}

#ifdef VULKAN_BACKEND_ENABLED
bool Application::initializeVulkanBackend() {
    LOG_INFO("Initializing Vulkan backend...");
    
    // Create Vulkan context
    VulkanVersionConfig vulkanVersion;
    vulkanVersion.major = m_impl->backendConfig.vulkanMajor;
    vulkanVersion.minor = m_impl->backendConfig.vulkanMinor;
    
    m_impl->vulkanContext = std::make_unique<VulkanContext>();
    if (!m_impl->vulkanContext->initialize(m_impl->window, vulkanVersion)) {
        LOG_ERROR("Failed to initialize Vulkan context");
        return false;
    }

    // Create renderer using factory
    m_impl->renderer = RendererFactory::create(BackendType::Vulkan);
    
    // For Vulkan renderer, we need to pass VulkanContext
    auto* vulkanRenderer = dynamic_cast<SkiaRenderer*>(m_impl->renderer.get());
    if (!vulkanRenderer) {
        LOG_ERROR("Failed to cast to Vulkan renderer");
        return false;
    }
    
    if (!vulkanRenderer->initialize(
        m_impl->vulkanContext.get(),
        m_impl->width,
        m_impl->height)) {
        LOG_ERROR("Failed to initialize Vulkan renderer");
        return false;
    }
    
    LOG_INFO("  GPU: {}", m_impl->vulkanContext->getDeviceName());
    LOG_INFO("  Vulkan: {}", m_impl->vulkanContext->getCapabilities().getFeatureLevelString());
    
    return true;
}
#else
bool Application::initializeVulkanBackend() {
    LOG_ERROR("Vulkan backend not available");
    return false;
}
#endif

bool Application::initializeOpenGLBackend() {
    LOG_INFO("Initializing OpenGL backend...");
    
    // Create renderer using factory
    m_impl->renderer = RendererFactory::create(BackendType::OpenGL);
    
    if (!m_impl->renderer->initialize(
        m_impl->window,
        m_impl->width,
        m_impl->height,
        m_impl->backendConfig)) {
        LOG_ERROR("Failed to initialize OpenGL renderer");
        return false;
    }
    
    return true;
}

 
bool Application::initializeANGLEBackend() {
#ifdef USE_ANGLE
    LOG_INFO("Initializing ANGLE backend...");
    
    // Create renderer using factory
    m_impl->renderer = RendererFactory::create(BackendType::ANGLE);
    
    if (!m_impl->renderer->initialize(
        m_impl->window,
        m_impl->width,
        m_impl->height,
        m_impl->backendConfig)) {
        LOG_ERROR("Failed to initialize ANGLE renderer");
        return false;
    }
    
    return true;
#else
    LOG_ERROR("ANGLE backend not available. Build with -DUSE_ANGLE=ON");
    return false;
#endif
}


void Application::run() {
    if (!m_impl->initialized) {
        LOG_ERROR("Application not initialized!");
        return;
    }

    LOG_INFO("Entering main render loop...");
    LOG_INFO("  Running flag: {}", m_impl->running);
    LOG_INFO("  Backend type: {}", m_impl->backendConfig.toString());

    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    float fpsTimer = 0.0f;
    float currentFPS = 0.0f;
    int totalFrames = 0;

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
            m_impl->renderer->setFPS(currentFPS);
            if (totalFrames < 10 || totalFrames % 100 == 0) {
                LOG_INFO("  Frame {}, FPS: {:.1f}", totalFrames, currentFPS);
            }
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        processEvents();
        update(deltaTime);
        render();
        totalFrames++;

        // Cap frame rate
        SDL_Delay(1);
    }
    
    LOG_INFO("Exited render loop after {} frames", totalFrames);
}

void Application::shutdown() {
    if (!m_impl->initialized) {
        return;
    }

    m_impl->running = false;

#ifdef VULKAN_BACKEND_ENABLED
    // Wait for GPU to finish
    if (m_impl->vulkanContext) {
        m_impl->vulkanContext->waitIdle();
    }
#endif

    // Cleanup renderer first
    m_impl->renderer.reset();

#ifdef VULKAN_BACKEND_ENABLED
    // Cleanup Vulkan if used
    m_impl->vulkanContext.reset();
#endif

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
                LOG_INFO("Received SDL_EVENT_QUIT");
                m_impl->running = false;
                break;
                
            case SDL_EVENT_KEY_DOWN:
                LOG_DEBUG("Key pressed: {}", static_cast<int>(event.key.key));
                if (event.key.key == SDLK_ESCAPE) {
                    LOG_INFO("ESC pressed, exiting");
                    m_impl->running = false;
                }
                break;
                
            case SDL_EVENT_WINDOW_RESIZED: {
                int newWidth = event.window.data1;
                int newHeight = event.window.data2;
                LOG_DEBUG("Window resized to {}x{}", newWidth, newHeight);
                
#ifdef VULKAN_BACKEND_ENABLED
                // Wait for GPU if using Vulkan
                if (m_impl->vulkanContext) {
                    m_impl->vulkanContext->waitIdle();
                    m_impl->vulkanContext->resize(newWidth, newHeight);
                }
#endif
                
                m_impl->renderer->resize(newWidth, newHeight);
                
                m_impl->width = newWidth;
                m_impl->height = newHeight;
                break;
            }
                
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                LOG_INFO("Window close requested");
                m_impl->running = false;
                break;
                
            default:
                break;
        }
    }
}

void Application::update(float deltaTime) {
    // Update logic here
    (void)deltaTime;
}

void Application::render() {
    // Begin frame
    if (!m_impl->renderer->beginFrame()) {
        LOG_WARN("beginFrame returned false");
        return;
    }

#ifdef VULKAN_BACKEND_ENABLED
    // For Vulkan backend, we need to acquire swapchain image first
    if (m_impl->backendConfig.type == BackendType::Vulkan && m_impl->vulkanContext) {
        if (!m_impl->vulkanContext->beginFrame()) {
            return;
        }
    }
#endif

    // Render
    m_impl->renderer->render();

#ifdef VULKAN_BACKEND_ENABLED
    // End frame
    if (m_impl->backendConfig.type == BackendType::Vulkan && m_impl->vulkanContext) {
        // For Vulkan, we need to get the semaphore and present
        auto* vulkanRenderer = dynamic_cast<SkiaRenderer*>(m_impl->renderer.get());
        if (vulkanRenderer) {
            m_impl->vulkanContext->endFrame(vulkanRenderer->getRenderFinishedSemaphore());
        }
    }
#endif
    
    m_impl->renderer->endFrame();
}

} // namespace skia_renderer
