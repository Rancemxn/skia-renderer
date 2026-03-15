#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/ostr.h>

#include <memory>

namespace skia_renderer {

class Logger {
public:
    static void init() {
        if (!s_logger) {
            // Create console logger with color support
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("%^[%l]%$ %v");
            
            s_logger = std::make_shared<spdlog::logger>("skia-renderer", console_sink);
            s_logger->set_level(spdlog::level::trace);
            s_logger->flush_on(spdlog::level::trace);
            
            // Register as default logger
            spdlog::register_logger(s_logger);
            spdlog::set_default_logger(s_logger);
        }
    }
    
    static std::shared_ptr<spdlog::logger>& get() {
        if (!s_logger) {
            init();
        }
        return s_logger;
    }
    
    // Convenience methods
    template<typename... Args>
    static void trace(const char* fmt, Args&&... args) {
        get()->trace(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void debug(const char* fmt, Args&&... args) {
        get()->debug(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void info(const char* fmt, Args&&... args) {
        get()->info(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void warn(const char* fmt, Args&&... args) {
        get()->warn(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void error(const char* fmt, Args&&... args) {
        get()->error(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void critical(const char* fmt, Args&&... args) {
        get()->critical(fmt, std::forward<Args>(args)...);
    }

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

// Macro shortcuts for convenience
#define LOG_TRACE(...)    ::skia_renderer::Logger::trace(__VA_ARGS__)
#define LOG_DEBUG(...)    ::skia_renderer::Logger::debug(__VA_ARGS__)
#define LOG_INFO(...)     ::skia_renderer::Logger::info(__VA_ARGS__)
#define LOG_WARN(...)     ::skia_renderer::Logger::warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::skia_renderer::Logger::error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::skia_renderer::Logger::critical(__VA_ARGS__)

} // namespace skia_renderer
