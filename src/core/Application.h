#pragma once

#include <memory>
#include <string>

namespace skia_renderer {

class VulkanContext;
class SkiaRenderer;

class Application {
public:
    Application(const std::string& title, int width, int height);
    ~Application();

    // Delete copy and move
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    bool initialize();
    void run();
    void shutdown();

private:
    void processEvents();
    void update(float deltaTime);
    void render();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace skia_renderer
