#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/ostr.h>

#include <memory>
#include <string>

namespace skia_renderer {

class Logger {
public:
    static void init() {
        if (!s_logger) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("%^[%l]%$ %v");
            
            s_logger = std::make_shared<spdlog::logger>("skia-renderer", console_sink);
            s_logger->set_level(spdlog::level::info);
            s_logger->flush_on(spdlog::level::info);
            
            spdlog::register_logger(s_logger);
            spdlog::set_default_logger(s_logger);
        }
    }
    
    static void set_level(spdlog::level::level_enum level) {
        if (s_logger) {
            s_logger->set_level(level);
            s_logger->flush_on(level);
        }
    }
    
    static std::shared_ptr<spdlog::logger> get() {
        if (!s_logger) {
            init();
        }
        return s_logger;
    }

private:
    static inline std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace skia_renderer

// Macros using spdlog's default logger (set by Logger::init)
#define LOG_TRACE(...)    SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
