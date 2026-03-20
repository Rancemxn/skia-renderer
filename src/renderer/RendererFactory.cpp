#include "RendererFactory.h"
#include "GLRenderer.h"
#include "core/Logger.h"

#ifdef VULKAN_BACKEND_ENABLED
#include "SkiaRenderer.h"
#endif

namespace skia_renderer {

std::unique_ptr<IRenderer> RendererFactory::create(BackendType type) {
    switch (type) {
        case BackendType::Vulkan:
#ifdef VULKAN_BACKEND_ENABLED
            LOG_INFO("Creating Vulkan (Graphite) renderer");
            return std::make_unique<SkiaRenderer>();
#else
            LOG_WARN("Vulkan backend not available, falling back to OpenGL");
            return std::make_unique<GLRenderer>();
#endif
        case BackendType::OpenGL:
            LOG_INFO("Creating OpenGL (Ganesh) renderer");
            return std::make_unique<GLRenderer>();
    }
    
    LOG_ERROR("Unknown backend type, defaulting to OpenGL");
    return std::make_unique<GLRenderer>();
}

std::string RendererFactory::getBackendName(BackendType type) {
    switch (type) {
        case BackendType::Vulkan:
            return "Vulkan (Graphite)";
        case BackendType::OpenGL:
            return "OpenGL (Ganesh)";
    }
    return "Unknown";
}

bool RendererFactory::isBackendAvailable(BackendType type) {
    switch (type) {
        case BackendType::Vulkan:
#ifdef VULKAN_BACKEND_ENABLED
            return true;
#else
            return false;
#endif
        case BackendType::OpenGL:
            return true;
    }
    return false;
}

} // namespace skia_renderer
