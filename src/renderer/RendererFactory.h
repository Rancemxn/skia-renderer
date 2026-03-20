#pragma once

#include "IRenderer.h"
#include <memory>

namespace skia_renderer {

/**
 * @brief Factory for creating renderer instances
 */
class RendererFactory {
public:
    /**
     * @brief Create a renderer based on backend type
     * @param type The backend type (Vulkan or OpenGL)
     * @return Unique pointer to the created renderer
     */
    static std::unique_ptr<IRenderer> create(BackendType type);

    /**
     * @brief Get backend type name
     * @param type The backend type
     * @return Human-readable backend name
     */
    static std::string getBackendName(BackendType type);

    /**
     * @brief Check if a backend is available on this system
     * @param type The backend type to check
     * @return true if the backend is available
     */
    static bool isBackendAvailable(BackendType type);
};

} // namespace skia_renderer
