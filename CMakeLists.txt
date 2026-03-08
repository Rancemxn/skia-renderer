cmake_minimum_required(VERSION 3.20)
project(skia-renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ========================================
# Options
# ========================================
option(USE_SYSTEM_SDL "Use system SDL3" OFF)
option(USE_SYSTEM_VK_BOOTSTRAP "Use system vk-bootstrap" OFF)
option(USE_SYSTEM_SKIA "Use system Skia" OFF)
# Note: VMA is managed internally by Skia

# ========================================
# Detect build environment
# ========================================
if(WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ========================================
# Build Type Detection
# ========================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LOWER)
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# ========================================
# Vulkan SDK
# ========================================
# Allow custom Vulkan SDK path via command line
if(DEFINED VULKAN_SDK AND NOT DEFINED Vulkan_SDK)
    set(Vulkan_SDK ${VULKAN_SDK})
endif()

find_package(Vulkan REQUIRED)
message(STATUS "Vulkan SDK: ${Vulkan_SDK}")
message(STATUS "Vulkan Include: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan Library: ${Vulkan_LIBRARIES}")

# ========================================
# SDL3
# ========================================
if(USE_SYSTEM_SDL)
    find_package(SDL3 REQUIRED)
else()
    # Priority: command line > build-type-specific install > generic install > source
    if(DEFINED SDL3_DIR)
        find_package(SDL3 REQUIRED HINTS "${SDL3_DIR}")
    else()
        # Build-type-specific install directory (preferred)
        set(SDL3_HINTS 
            "${CMAKE_SOURCE_DIR}/deps/installed/${CMAKE_BUILD_TYPE}/lib/cmake/SDL3"
            "${CMAKE_SOURCE_DIR}/deps/installed/lib/cmake/SDL3"
            "${CMAKE_SOURCE_DIR}/deps/SDL3/lib/cmake/SDL3"
            "${CMAKE_SOURCE_DIR}/deps/SDL3/cmake"
        )
        find_package(SDL3 REQUIRED HINTS ${SDL3_HINTS})
    endif()
endif()
message(STATUS "SDL3 Found: ${SDL3_FOUND}")
message(STATUS "SDL3 Include: ${SDL3_INCLUDE_DIRS}")

# ========================================
# vk-bootstrap
# ========================================
if(USE_SYSTEM_VK_BOOTSTRAP)
    find_package(vk-bootstrap REQUIRED)
else()
    if(DEFINED VKBOOTSTRAP_DIR)
        # Command line specified path
        set(VK_BOOTSTRAP_SEARCH_PATHS "${VKBOOTSTRAP_DIR}")
    else()
        # Build-type-specific paths (priority order)
        set(VK_BOOTSTRAP_SEARCH_PATHS
            "${CMAKE_SOURCE_DIR}/deps/installed/${CMAKE_BUILD_TYPE}"
            "${CMAKE_SOURCE_DIR}/deps/installed"
            "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap"
        )
    endif()
    
    find_path(VK_BOOTSTRAP_INCLUDE_DIR 
        NAMES VkBootstrap.h 
        HINTS ${VK_BOOTSTRAP_SEARCH_PATHS}
        PATH_SUFFIXES include include/vk-bootstrap vk-bootstrap)
    
    # Find library with build-type-specific naming
    find_library(VK_BOOTSTRAP_LIBRARY 
        NAMES vk-bootstrap vk-bootstrap-static 
        HINTS ${VK_BOOTSTRAP_SEARCH_PATHS}
        PATH_SUFFIXES lib lib/${CMAKE_BUILD_TYPE} ${CMAKE_BUILD_TYPE})
endif()

if(NOT VK_BOOTSTRAP_INCLUDE_DIR OR NOT VK_BOOTSTRAP_LIBRARY)
    message(FATAL_ERROR "vk-bootstrap not found. Run: python build_deps.py --build-type ${CMAKE_BUILD_TYPE}")
endif()

message(STATUS "vk-bootstrap Include: ${VK_BOOTSTRAP_INCLUDE_DIR}")
message(STATUS "vk-bootstrap Library: ${VK_BOOTSTRAP_LIBRARY}")

# ========================================
# Skia
# ========================================
# Skia's header structure: headers are directly in root (e.g., skia/core/SkSurface.h)
# Note: VMA is now managed internally by Skia, no need to link separately
if(USE_SYSTEM_SKIA)
    find_path(SKIA_INCLUDE_DIR NAMES core/SkSurface.h)
    find_library(SKIA_LIBRARY NAMES skia)
else()
    if(DEFINED SKIA_DIR)
        set(SKIA_INCLUDE_DIR "${SKIA_DIR}")
        set(SKIA_LIB_HINTS "${SKIA_DIR}/out/${CMAKE_BUILD_TYPE}" "${SKIA_DIR}/out")
    else()
        set(SKIA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/skia")
        set(SKIA_LIB_HINTS 
            "${CMAKE_SOURCE_DIR}/deps/skia/out/${CMAKE_BUILD_TYPE}"
            "${CMAKE_SOURCE_DIR}/deps/skia/out")
    endif()
    
    find_library(SKIA_LIBRARY NAMES skia HINTS ${SKIA_LIB_HINTS})
endif()

if(NOT SKIA_LIBRARY)
    message(FATAL_ERROR "Skia library not found. Run: python build_deps.py --build-type ${CMAKE_BUILD_TYPE}")
endif()

message(STATUS "Skia Include: ${SKIA_INCLUDE_DIR}")
message(STATUS "Skia Library: ${SKIA_LIBRARY}")

# ========================================
# Skia system dependencies (Linux/macOS)
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
    src/renderer/VulkanContext.cpp
    src/renderer/SkiaRenderer.cpp
    src/renderer/Swapchain.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${SDL3_INCLUDE_DIRS}
    ${VK_BOOTSTRAP_INCLUDE_DIR}
    ${SKIA_INCLUDE_DIR}
)

# ========================================
# Link Libraries
# ========================================
set(LINK_LIBS
    Vulkan::Vulkan
    SDL3::SDL3
    ${VK_BOOTSTRAP_LIBRARY}
    ${SKIA_LIBRARY}
)

# Platform-specific libraries
if(WIN32)
    # Windows libraries
    list(APPEND LINK_LIBS
        user32
        gdi32
        shell32
        ole32
        oleaut32
        imm32
        winmm
        version
        setupapi
        dwmapi
    )
    
    # Skia Windows dependencies
    list(APPEND LINK_LIBS
        fontsub
        dwrite
        d3d12
        dxgi
        opengl32
    )
else()
    # Linux/macOS libraries
    list(APPEND LINK_LIBS
        ZLIB::ZLIB
        PNG::PNG
        Freetype::Freetype
        Fontconfig::Fontconfig
        expat
        harfbuzz
        icuuc
        icui18n
        pthread
        dl
        m
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBS})

# ========================================
# Debug Definitions
# ========================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        DEBUG_BUILD
        ENABLE_VULKAN_VALIDATION
        SK_DEBUG
    )
    message(STATUS "Debug build: Vulkan validation enabled")
endif()

# ========================================
# Compiler Flags
# ========================================

# Detect if using Clang on Windows
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "Using Clang on Windows")
    set(USING_CLANG_WIN TRUE)
endif()

if(MSVC)
    # MSVC flags
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /utf-8
        /GR
        /EHsc
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(${PROJECT_NAME} PRIVATE /LTCG)
    endif()
elseif(USING_CLANG_WIN)
    # Clang on Windows
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -frtti
        -fexceptions
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
else()
    # GCC or Clang on Unix
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -frtti
        -fPIC
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# ========================================
# RPATH for Unix
# ========================================
if(NOT WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${CMAKE_SOURCE_DIR}/deps/installed/${CMAKE_BUILD_TYPE}/lib;${CMAKE_SOURCE_DIR}/deps/skia/out/${CMAKE_BUILD_TYPE}"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# ========================================
# Summary
# ========================================
message(STATUS "")
message(STATUS "==== Configuration Summary ====")
message(STATUS "Build Type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:  ${CMAKE_CXX_STANDARD}")
message(STATUS "Generator:     ${CMAKE_GENERATOR}")
message(STATUS "SDL3:          ${SDL3_INCLUDE_DIRS}")
message(STATUS "vk-bootstrap:  ${VK_BOOTSTRAP_LIBRARY}")
message(STATUS "Skia:          ${SKIA_LIBRARY}")
message(STATUS "================================")
message(STATUS "")
