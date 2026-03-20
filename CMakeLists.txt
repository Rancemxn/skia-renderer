cmake_minimum_required(VERSION 3.20)
project(skia-renderer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ========================================
# Options
# ========================================
option(USE_SYSTEM_SDL "Use system SDL3" OFF)
option(USE_SYSTEM_VK_BOOTSTRAP "Use system vk-bootstrap" OFF)
option(USE_SYSTEM_SKIA "Use system Skia" OFF)
option(USE_SYSTEM_SPDLOG "Use system spdlog" OFF)
option(USE_SYSTEM_CLI11 "Use system CLI11" OFF)

# ========================================
# Build Type
# ========================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# ========================================
# Platform Setup
# ========================================
if(WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ========================================
# Paths
# ========================================
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/deps")

# ========================================
# Vulkan SDK (optional - only for Vulkan backend)
# ========================================
if(DEFINED VULKAN_SDK AND NOT DEFINED Vulkan_SDK)
    set(Vulkan_SDK ${VULKAN_SDK})
endif()
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK: ${Vulkan_SDK}")
else()
    message(STATUS "Vulkan SDK: Not found (OpenGL backend only)")
endif()

# ========================================
# SDL3
# ========================================
if(USE_SYSTEM_SDL)
    find_package(SDL3 REQUIRED)
else()
    # SDL3 build output: deps/SDL3/out/{Debug,Release}/
    set(SDL3_OUT_DIR "${DEPS_DIR}/SDL3/out/${CMAKE_BUILD_TYPE}")
    
    # Find SDL3Config.cmake
    set(SDL3_SEARCH_PATHS
        "${SDL3_DIR}"
        "${SDL3_OUT_DIR}/lib/cmake/SDL3"
        "${SDL3_OUT_DIR}/cmake"
        "${DEPS_DIR}/SDL3/lib/cmake/SDL3"
    )
    
    find_package(SDL3 REQUIRED HINTS ${SDL3_SEARCH_PATHS})
endif()
message(STATUS "SDL3: ${SDL3_INCLUDE_DIRS}")

# ========================================
# vk-bootstrap (VkBootstrap.h) - only needed for Vulkan
# ========================================
if(Vulkan_FOUND)
    if(USE_SYSTEM_VK_BOOTSTRAP)
        find_package(vk-bootstrap REQUIRED)
    else()
        # vk-bootstrap build output: deps/vk-bootstrap/out/{Debug,Release}/
        set(VKB_OUT_DIR "${DEPS_DIR}/vk-bootstrap/out/${CMAKE_BUILD_TYPE}")
        set(VKB_SOURCE "${DEPS_DIR}/vk-bootstrap")
        
        # Search paths
        set(VKB_SEARCH_PATHS "${VKBOOTSTRAP_DIR}" "${VKB_OUT_DIR}" "${VKB_SOURCE}")
        
        find_path(VK_BOOTSTRAP_INCLUDE_DIR 
            NAMES VkBootstrap.h 
            HINTS ${VKB_SEARCH_PATHS}
            PATH_SUFFIXES include)
        
        find_library(VK_BOOTSTRAP_LIBRARY 
            NAMES vk-bootstrap vk-bootstrap-static 
            HINTS ${VKB_SEARCH_PATHS}
            PATH_SUFFIXES lib)
    endif()

    if(VK_BOOTSTRAP_INCLUDE_DIR AND VK_BOOTSTRAP_LIBRARY)
        message(STATUS "vk-bootstrap: ${VK_BOOTSTRAP_LIBRARY}")
    else()
        message(STATUS "vk-bootstrap: Not found (Vulkan backend disabled)")
    endif()
else()
    set(VK_BOOTSTRAP_INCLUDE_DIR "")
    set(VK_BOOTSTRAP_LIBRARY "")
endif()

# ========================================
# Skia
# ========================================
if(USE_SYSTEM_SKIA)
    find_path(SKIA_INCLUDE_DIR NAMES core/SkSurface.h)
    find_library(SKIA_LIBRARY NAMES skia)
else()
    # Skia build output: deps/skia/out/{Debug,Release}/
    set(SKIA_DIR "${SKIA_DIR}" CACHE PATH "Skia directory")
    if(NOT SKIA_DIR)
        set(SKIA_DIR "${DEPS_DIR}/skia")
    endif()
    
    set(SKIA_INCLUDE_DIR "${SKIA_DIR}")
    set(SKIA_LIB_PATHS 
        "${SKIA_DIR}/out/${CMAKE_BUILD_TYPE}"
        "${SKIA_DIR}/out"
    )
    find_library(SKIA_LIBRARY NAMES skia HINTS ${SKIA_LIB_PATHS})
endif()

if(NOT SKIA_LIBRARY)
    message(FATAL_ERROR "Skia not found. Run: python build.py --build-type ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "Skia: ${SKIA_LIBRARY}")

# ========================================
# spdlog
# ========================================
if(USE_SYSTEM_SPDLOG)
    find_package(spdlog REQUIRED)
else()
    set(SPDLOG_DIR "${SPDLOG_DIR}" CACHE PATH "spdlog directory")
    if(NOT SPDLOG_DIR)
        set(SPDLOG_DIR "${DEPS_DIR}/spdlog")
    endif()
    
    set(SPDLOG_INCLUDE_DIR "${SPDLOG_DIR}/include")
    
    if(NOT EXISTS "${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
        message(FATAL_ERROR "spdlog not found. Run: python sync.py")
    endif()
endif()
message(STATUS "spdlog: ${SPDLOG_INCLUDE_DIR}")

# ========================================
# CLI11
# ========================================
if(USE_SYSTEM_CLI11)
    find_path(CLI11_INCLUDE_DIR NAMES CLI/CLI.hpp)
else()
    set(CLI11_DIR "${CLI11_DIR}" CACHE PATH "CLI11 directory")
    if(NOT CLI11_DIR)
        set(CLI11_DIR "${DEPS_DIR}/CLI11")
    endif()
    
    set(CLI11_INCLUDE_DIR "${CLI11_DIR}/include")
    
    if(NOT EXISTS "${CLI11_INCLUDE_DIR}/CLI/CLI.hpp")
        message(FATAL_ERROR "CLI11 not found. Run: python sync.py")
    endif()
endif()
message(STATUS "CLI11: ${CLI11_INCLUDE_DIR}")

# ========================================
# System Dependencies (Linux/macOS)
# ========================================
if(NOT WIN32)
    find_package(ZLIB REQUIRED)
    find_package(PNG REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(Fontconfig REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ICU icu-uc icu-i18n)
    pkg_check_modules(HARFBUZZ harfbuzz)
endif()

# ========================================
# Executable
# ========================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/Application.cpp
    src/renderer/DemoRenderer.cpp
    src/renderer/GLContext.cpp
    src/renderer/GLRenderer.cpp
    src/renderer/RendererFactory.cpp
)

# Add Vulkan-specific files only if Vulkan is available
if(Vulkan_FOUND AND VK_BOOTSTRAP_LIBRARY)
    target_sources(${PROJECT_NAME} PRIVATE
        src/renderer/VulkanContext.cpp
        src/renderer/SkiaRenderer.cpp
        src/renderer/Swapchain.cpp
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE VULKAN_BACKEND_ENABLED=1)
else()
    message(WARNING "Vulkan not available - only OpenGL backend will be supported")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SDL3_INCLUDE_DIRS}
    ${SKIA_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${CLI11_INCLUDE_DIR}
)

# Add Vulkan includes if available
if(Vulkan_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        ${VK_BOOTSTRAP_INCLUDE_DIR}
    )
endif()

# ========================================
# Link Libraries
# ========================================
set(LINK_LIBS
    SDL3::SDL3
    ${SKIA_LIBRARY}
)

# Add Vulkan libraries if available
if(Vulkan_FOUND AND VK_BOOTSTRAP_LIBRARY)
    list(APPEND LINK_LIBS
        Vulkan::Vulkan
        ${VK_BOOTSTRAP_LIBRARY}
    )
endif()

if(WIN32)
    list(APPEND LINK_LIBS
        user32 gdi32 shell32 ole32 oleaut32 imm32 winmm version setupapi dwmapi
        fontsub dwrite d3d12 dxgi opengl32
    )
else()
    list(APPEND LINK_LIBS
        ZLIB::ZLIB PNG::PNG Freetype::Freetype Fontconfig::Fontconfig
        expat harfbuzz icuuc icui18n pthread dl m
        ${CMAKE_DL_LIBS}  # For dlopen/dlsym on Linux
    )
    
    # Add OpenGL library on Linux
    find_package(OpenGL REQUIRED)
    list(APPEND LINK_LIBS OpenGL::GL)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBS})

# ========================================
# Debug Definitions
# ========================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        DEBUG_BUILD SK_DEBUG
    )
    if(Vulkan_FOUND)
        target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_VULKAN_VALIDATION)
        message(STATUS "Debug: Vulkan validation enabled")
    endif()
endif()

# ========================================
# Compiler Flags
# ========================================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(USING_CLANG_WIN TRUE)
    message(STATUS "Using Clang on Windows")
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4 /utf-8 /GR /EHsc
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(${PROJECT_NAME} PRIVATE /LTCG)
    endif()
elseif(USING_CLANG_WIN)
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic -frtti -fexceptions
        # Disable warnings from Skia headers (GNU extensions)
        -Wno-gnu-zero-variadic-macro-arguments
        -Wno-nested-anon-types
        -Wno-gnu-anonymous-struct
        # Disable MSVC extension warnings from spdlog on Windows
        -Wno-language-extension-token
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic -frtti -fPIC
        # Disable warnings from Skia headers (GNU extensions)
        -Wno-gnu-zero-variadic-macro-arguments
        -Wno-nested-anon-types
        -Wno-gnu-anonymous-struct
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# ========================================
# RPATH (Unix)
# ========================================
if(NOT WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${DEPS_DIR}/SDL3/out/${CMAKE_BUILD_TYPE}/lib;${DEPS_DIR}/skia/out/${CMAKE_BUILD_TYPE}"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# ========================================
# Summary
# ========================================
message(STATUS "")
message(STATUS "==== Configuration Summary ====")
message(STATUS "Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Output:       ${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}")
if(Vulkan_FOUND)
    message(STATUS "Vulkan:       Available")
else()
    message(STATUS "Vulkan:       Not available (OpenGL only)")
endif()
message(STATUS "================================")
message(STATUS "")
