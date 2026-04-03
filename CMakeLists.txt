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
option(VERBOSE_DEPS "Show detailed dependency detection info" OFF)

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
# Vulkan SDK
# ========================================
if(DEFINED VULKAN_SDK AND NOT DEFINED Vulkan_SDK)
    set(Vulkan_SDK ${VULKAN_SDK})
endif()
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan SDK: ${Vulkan_SDK}")

# ========================================
# OpenGL
# ========================================
find_package(OpenGL REQUIRED)
message(STATUS "OpenGL Library: ${OPENGL_LIBRARIES}")

# ========================================
# SDL3
# ========================================
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "[SDL3 Detection]")
endif()

# SDL3 output directory
set(SDL3_OUT_DIR "${DEPS_DIR}/SDL3/out/${CMAKE_BUILD_TYPE}")

if(VERBOSE_DEPS)
    message(STATUS "  SDL3_DIR (from cmdline): \"${SDL3_DIR}\"")
    message(STATUS "  SDL3_OUT_DIR: ${SDL3_OUT_DIR}")
    message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
endif()

if(USE_SYSTEM_SDL)
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: System SDL3")
    endif()
    find_package(SDL3 REQUIRED)
else()
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: Local build")
    endif()
    
    # Define all possible SDL3Config.cmake locations
    # SDL3 can install cmake files in various locations depending on build configuration
    set(SDL3_CMAKE_SEARCH_PATHS
        "${SDL3_DIR}"                           # User-specified via -DSDL3_DIR
        "${SDL3_OUT_DIR}"                       # Direct in output dir (most common on Windows)
        "${SDL3_OUT_DIR}/lib/cmake/SDL3"        # Standard cmake location
        "${SDL3_OUT_DIR}/cmake"                 # Alternative cmake location
        "${SDL3_OUT_DIR}/lib/cmake"             # lib/cmake without SDL3 subdir
        "${DEPS_DIR}/SDL3/lib/cmake/SDL3"       # Source tree cmake
        "${DEPS_DIR}/SDL3/cmake"                # Source tree cmake (alternative)
    )
    
    # Find SDL3Config.cmake
    set(SDL3_CONFIG_PATH "")
    if(VERBOSE_DEPS)
        message(STATUS "  Searching for SDL3Config.cmake:")
    endif()
    
    foreach(_search_path ${SDL3_CMAKE_SEARCH_PATHS})
        if(_search_path)
            # Check for SDL3Config.cmake directly in the path
            if(EXISTS "${_search_path}/SDL3Config.cmake")
                if(VERBOSE_DEPS)
                    message(STATUS "    [FOUND] ${_search_path}/SDL3Config.cmake")
                endif()
                set(SDL3_CONFIG_PATH "${_search_path}")
                break()
            endif()
        endif()
    endforeach()
    
    # If not found, show what we checked
    if(NOT SDL3_CONFIG_PATH)
        if(VERBOSE_DEPS)
            message(STATUS "    SDL3Config.cmake not found in any standard location")
            message(STATUS "  Checking for SDL3.dll and SDL3.lib:")
        endif()
        
        # Check for built artifacts
        set(SDL3_DLL "${SDL3_OUT_DIR}/SDL3.dll")
        set(SDL3_LIB "${SDL3_OUT_DIR}/SDL3.lib")
        set(SDL3_LIB_ALT "${SDL3_OUT_DIR}/lib/SDL3.lib")
        
        if(VERBOSE_DEPS)
            if(EXISTS "${SDL3_DLL}")
                message(STATUS "    [FOUND] SDL3.dll: ${SDL3_DLL}")
            endif()
            if(EXISTS "${SDL3_LIB}")
                message(STATUS "    [FOUND] SDL3.lib: ${SDL3_LIB}")
            elseif(EXISTS "${SDL3_LIB_ALT}")
                message(STATUS "    [FOUND] SDL3.lib: ${SDL3_LIB_ALT}")
            endif()
        endif()
        
        # Try using the SDL3_DIR from command line even if SDL3Config.cmake check failed
        # (it might still work with find_package)
        if(VERBOSE_DEPS AND SDL3_DIR)
            message(STATUS "  Attempting to use SDL3_DIR: ${SDL3_DIR}")
        endif()
    endif()
    
    # Try find_package with hints
    if(SDL3_CONFIG_PATH)
        if(VERBOSE_DEPS)
            message(STATUS "  Using SDL3 cmake path: ${SDL3_CONFIG_PATH}")
        endif()
        find_package(SDL3 REQUIRED CONFIG 
            HINTS "${SDL3_CONFIG_PATH}" 
            NO_DEFAULT_PATH
        )
    elseif(SDL3_DIR)
        if(VERBOSE_DEPS)
            message(STATUS "  Using SDL3_DIR hint: ${SDL3_DIR}")
        endif()
        find_package(SDL3 REQUIRED CONFIG 
            HINTS "${SDL3_DIR}" "${SDL3_OUT_DIR}"
        )
    else()
        if(VERBOSE_DEPS)
            message(STATUS "  Using default search paths")
        endif()
        find_package(SDL3 REQUIRED CONFIG 
            HINTS "${SDL3_OUT_DIR}"
        )
    endif()
endif()

# Diagnostic output
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "  SDL3 Detection Results:")
    message(STATUS "    SDL3_FOUND: ${SDL3_FOUND}")
endif()

# Handle SDL3 variables - they might be named differently depending on SDL3 version
# SDL3 3.x uses SDL3_INCLUDE_DIRS, SDL3_LIBRARIES
# Some builds might set SDL3_INCLUDE_DIR instead
if(SDL3_INCLUDE_DIRS)
    if(VERBOSE_DEPS)
        message(STATUS "    SDL3_INCLUDE_DIRS: ${SDL3_INCLUDE_DIRS}")
    endif()
elseif(SDL3_INCLUDE_DIR)
    if(VERBOSE_DEPS)
        message(STATUS "    SDL3_INCLUDE_DIR: ${SDL3_INCLUDE_DIR}")
    endif()
    set(SDL3_INCLUDE_DIRS "${SDL3_INCLUDE_DIR}")
else()
    # Try to find include directory manually
    if(VERBOSE_DEPS)
        message(STATUS "    SDL3_INCLUDE_DIRS: (empty)")
    endif()
    
    # SDL3 include directories - check multiple locations
    find_path(SDL3_INCLUDE_DIR
        NAMES SDL3/SDL.h SDL.h
        PATHS
            "${SDL3_OUT_DIR}/include"
            "${SDL3_OUT_DIR}/include-config-debug"
            "${SDL3_DIR}/include"
            "${SDL3_DIR}/include-config-debug"
            "${DEPS_DIR}/SDL3/include"
        NO_DEFAULT_PATH
    )
    
    if(SDL3_INCLUDE_DIR)
        if(VERBOSE_DEPS)
            message(STATUS "    Found SDL3 include dir: ${SDL3_INCLUDE_DIR}")
        endif()
        set(SDL3_INCLUDE_DIRS "${SDL3_INCLUDE_DIR}")
    else()
        message(WARNING "    SDL3 include directory not found")
    endif()
endif()

if(VERBOSE_DEPS)
    if(SDL3_LIBRARIES)
        message(STATUS "    SDL3_LIBRARIES: ${SDL3_LIBRARIES}")
    else()
        message(STATUS "    SDL3_LIBRARIES: (empty, will use SDL3::SDL3 target)")
    endif()

    # Check if we have the SDL3::SDL3 target (modern CMake)
    if(TARGET SDL3::SDL3)
        message(STATUS "    SDL3::SDL3 target: Available")
    else()
        message(STATUS "    SDL3::SDL3 target: Not available")
    endif()

    message(STATUS "")
endif()

# ========================================
# vk-bootstrap (REQUIRED)
# ========================================
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "[vk-bootstrap Detection]")
endif()

if(USE_SYSTEM_VK_BOOTSTRAP)
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: System vk-bootstrap")
    endif()
    find_package(vk-bootstrap REQUIRED)
else()
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: Local build")
    endif()
    
    # vk-bootstrap build output: deps/vk-bootstrap/out/{Debug,Release}/
    set(VKB_OUT_DIR "${DEPS_DIR}/vk-bootstrap/out/${CMAKE_BUILD_TYPE}")
    set(VKB_SOURCE "${DEPS_DIR}/vk-bootstrap")
    
    if(VERBOSE_DEPS)
        message(STATUS "  VKBOOTSTRAP_DIR: \"${VKBOOTSTRAP_DIR}\"")
        message(STATUS "  VKB_OUT_DIR: ${VKB_OUT_DIR}")
    endif()
    
    # Search paths
    set(VKB_SEARCH_PATHS 
        "${VKBOOTSTRAP_DIR}" 
        "${VKB_OUT_DIR}" 
        "${VKB_SOURCE}"
    )
    
    # Find header
    if(VERBOSE_DEPS)
        message(STATUS "  Searching for VkBootstrap.h:")
    endif()
    set(VKB_HEADER_FOUND FALSE)
    foreach(_search_path ${VKB_SEARCH_PATHS})
        if(_search_path AND EXISTS "${_search_path}/include/VkBootstrap.h")
            if(VERBOSE_DEPS)
                message(STATUS "    [FOUND] ${_search_path}/include/VkBootstrap.h")
            endif()
            set(VKB_HEADER_FOUND TRUE)
            break()
        elseif(_search_path AND EXISTS "${_search_path}/VkBootstrap.h")
            if(VERBOSE_DEPS)
                message(STATUS "    [FOUND] ${_search_path}/VkBootstrap.h")
            endif()
            set(VKB_HEADER_FOUND TRUE)
            break()
        endif()
    endforeach()
    
    if(NOT VKB_HEADER_FOUND AND VERBOSE_DEPS)
        message(STATUS "    VkBootstrap.h not found in search paths")
    endif()
    
    find_path(VK_BOOTSTRAP_INCLUDE_DIR 
        NAMES VkBootstrap.h 
        HINTS ${VKB_SEARCH_PATHS}
        PATH_SUFFIXES include)
    
    # Find library
    if(VERBOSE_DEPS)
        message(STATUS "  Searching for vk-bootstrap library:")
    endif()
    set(VKB_LIB_NAMES "vk-bootstrap" "vk-bootstrap-static")
    set(VKB_LIB_FOUND FALSE)
    foreach(_lib_name ${VKB_LIB_NAMES})
        foreach(_search_path ${VKB_SEARCH_PATHS})
            if(EXISTS "${_search_path}/lib/${_lib_name}.lib")
                if(VERBOSE_DEPS)
                    message(STATUS "    [FOUND] ${_search_path}/lib/${_lib_name}.lib")
                endif()
                set(VKB_LIB_FOUND TRUE)
                break()
            elseif(EXISTS "${_search_path}/lib/${_lib_name}.a")
                if(VERBOSE_DEPS)
                    message(STATUS "    [FOUND] ${_search_path}/lib/${_lib_name}.a")
                endif()
                set(VKB_LIB_FOUND TRUE)
                break()
            elseif(EXISTS "${_search_path}/lib64/${_lib_name}.a")
                if(VERBOSE_DEPS)
                    message(STATUS "    [FOUND] ${_search_path}/lib64/${_lib_name}.a")
                endif()
                set(VKB_LIB_FOUND TRUE)
                break()
            endif()
        endforeach()
        if(VKB_LIB_FOUND)
            break()
        endif()
    endforeach()
    
    if(NOT VKB_LIB_FOUND AND VERBOSE_DEPS)
        message(STATUS "    vk-bootstrap library not found in search paths")
    endif()
    
    find_library(VK_BOOTSTRAP_LIBRARY 
        NAMES vk-bootstrap vk-bootstrap-static 
        HINTS ${VKB_SEARCH_PATHS}
        PATH_SUFFIXES lib lib64)
endif()

# Verify vk-bootstrap was found
if(NOT VK_BOOTSTRAP_INCLUDE_DIR OR NOT VK_BOOTSTRAP_LIBRARY)
    message(FATAL_ERROR "vk-bootstrap not found")
endif()

# Results
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "  vk-bootstrap Detection Results:")
    message(STATUS "    VK_BOOTSTRAP_INCLUDE_DIR: ${VK_BOOTSTRAP_INCLUDE_DIR}")
    message(STATUS "    VK_BOOTSTRAP_LIBRARY: ${VK_BOOTSTRAP_LIBRARY}")
    message(STATUS "    Status: Found")
    message(STATUS "")
endif()

# ========================================
# Skia
# ========================================
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "[Skia Detection]")
endif()

if(USE_SYSTEM_SKIA)
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: System Skia")
    endif()
    find_path(SKIA_INCLUDE_DIR NAMES core/SkSurface.h)
    find_library(SKIA_LIBRARY NAMES skia)
else()
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: Local build")
    endif()
    
    # Skia build output: deps/skia/out/{Debug,Release}/
    set(SKIA_DIR "${SKIA_DIR}" CACHE PATH "Skia directory")
    if(NOT SKIA_DIR)
        set(SKIA_DIR "${DEPS_DIR}/skia")
    endif()
    
    if(VERBOSE_DEPS)
        message(STATUS "  SKIA_DIR: ${SKIA_DIR}")
        message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
    endif()
    
    set(SKIA_INCLUDE_DIR "${SKIA_DIR}")
    set(SKIA_LIB_PATHS 
        "${SKIA_DIR}/out/${CMAKE_BUILD_TYPE}"
        "${SKIA_DIR}/out"
    )
    
    # Check for Skia header
    if(VERBOSE_DEPS)
        message(STATUS "  Searching for Skia headers:")
        if(EXISTS "${SKIA_DIR}/include/core/SkSurface.h")
            message(STATUS "    [FOUND] ${SKIA_DIR}/include/core/SkSurface.h")
        elseif(EXISTS "${SKIA_DIR}/core/SkSurface.h")
            message(STATUS "    [FOUND] ${SKIA_DIR}/core/SkSurface.h")
        else()
            message(STATUS "    Skia headers not found (expected at ${SKIA_DIR}/include/core/SkSurface.h)")
        endif()
    endif()
    
    # Check for Skia library
    if(VERBOSE_DEPS)
        message(STATUS "  Searching for Skia library:")
    endif()
    set(SKIA_LIB_FOUND FALSE)
    foreach(_lib_path ${SKIA_LIB_PATHS})
        if(EXISTS "${_lib_path}/skia.lib")
            if(VERBOSE_DEPS)
                message(STATUS "    [FOUND] ${_lib_path}/skia.lib")
            endif()
            set(SKIA_LIB_FOUND TRUE)
            break()
        elseif(EXISTS "${_lib_path}/libskia.a")
            if(VERBOSE_DEPS)
                message(STATUS "    [FOUND] ${_lib_path}/libskia.a")
            endif()
            set(SKIA_LIB_FOUND TRUE)
            break()
        endif()
    endforeach()
    
    if(NOT SKIA_LIB_FOUND AND VERBOSE_DEPS)
        message(STATUS "    Skia library not found")
        message(STATUS "    Searched paths:")
        foreach(_lib_path ${SKIA_LIB_PATHS})
            message(STATUS "      - ${_lib_path}")
        endforeach()
    endif()
    
    find_library(SKIA_LIBRARY NAMES skia HINTS ${SKIA_LIB_PATHS})
endif()

# Results
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "  Skia Detection Results:")
    if(SKIA_INCLUDE_DIR)
        message(STATUS "    SKIA_INCLUDE_DIR: ${SKIA_INCLUDE_DIR}")
    else()
        message(STATUS "    SKIA_INCLUDE_DIR: (not set)")
    endif()

    if(SKIA_LIBRARY)
        message(STATUS "    SKIA_LIBRARY: ${SKIA_LIBRARY}")
    else()
        message(FATAL_ERROR "    SKIA_LIBRARY: Not found")
    endif()
    message(STATUS "")
elseif(NOT SKIA_LIBRARY)
    message(FATAL_ERROR "Skia library not found")
endif()

# ========================================
# spdlog
# ========================================
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "[spdlog Detection]")
endif()

if(USE_SYSTEM_SPDLOG)
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: System spdlog")
    endif()
    find_package(spdlog REQUIRED)
else()
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: Local build")
    endif()
    
    set(SPDLOG_DIR "${SPDLOG_DIR}" CACHE PATH "spdlog directory")
    if(NOT SPDLOG_DIR)
        set(SPDLOG_DIR "${DEPS_DIR}/spdlog")
    endif()
    
    if(VERBOSE_DEPS)
        message(STATUS "  SPDLOG_DIR: ${SPDLOG_DIR}")
    endif()
    
    set(SPDLOG_INCLUDE_DIR "${SPDLOG_DIR}/include")
    
    # Check for spdlog header
    if(VERBOSE_DEPS)
        message(STATUS "  Searching for spdlog header:")
        if(EXISTS "${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
            message(STATUS "    [FOUND] ${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
        else()
            message(STATUS "    spdlog.h not found at ${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
        endif()
    endif()
    
    if(NOT EXISTS "${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
        message(FATAL_ERROR "spdlog not found")
    endif()
endif()

# Results
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "  spdlog Detection Results:")
    message(STATUS "    SPDLOG_INCLUDE_DIR: ${SPDLOG_INCLUDE_DIR}")
    message(STATUS "")
endif()

# ========================================
# CLI11
# ========================================
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "[CLI11 Detection]")
endif()

if(USE_SYSTEM_CLI11)
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: System CLI11")
    endif()
    find_path(CLI11_INCLUDE_DIR NAMES CLI/CLI.hpp)
else()
    if(VERBOSE_DEPS)
        message(STATUS "  Mode: Local build")
    endif()
    
    set(CLI11_DIR "${CLI11_DIR}" CACHE PATH "CLI11 directory")
    if(NOT CLI11_DIR)
        set(CLI11_DIR "${DEPS_DIR}/CLI11")
    endif()
    
    if(VERBOSE_DEPS)
        message(STATUS "  CLI11_DIR: ${CLI11_DIR}")
    endif()
    
    set(CLI11_INCLUDE_DIR "${CLI11_DIR}/include")
    
    # Check for CLI11 header
    if(VERBOSE_DEPS)
        message(STATUS "  Searching for CLI11 header:")
        if(EXISTS "${CLI11_INCLUDE_DIR}/CLI/CLI.hpp")
            message(STATUS "    [FOUND] ${CLI11_INCLUDE_DIR}/CLI/CLI.hpp")
        else()
            message(STATUS "    CLI.hpp not found at ${CLI11_INCLUDE_DIR}/CLI/CLI.hpp")
        endif()
    endif()
    
    if(NOT EXISTS "${CLI11_INCLUDE_DIR}/CLI/CLI.hpp")
        message(FATAL_ERROR "CLI11 not found. Run: python sync.py")
    endif()
endif()

# Results
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "  CLI11 Detection Results:")
    message(STATUS "    CLI11_INCLUDE_DIR: ${CLI11_INCLUDE_DIR}")
    message(STATUS "")
endif()

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
# ANGLE (REQUIRED)
# ========================================
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "[ANGLE Detection]")
endif()

# ANGLE build output: deps/angle/out/{Debug,Release}/
set(ANGLE_DIR "${ANGLE_DIR}" CACHE PATH "ANGLE directory")
if(NOT ANGLE_DIR)
    set(ANGLE_DIR "${DEPS_DIR}/angle")
endif()

set(ANGLE_OUT_DIR "${ANGLE_DIR}/out/${CMAKE_BUILD_TYPE}")

if(VERBOSE_DEPS)
    message(STATUS "  ANGLE_DIR: ${ANGLE_DIR}")
    message(STATUS "  ANGLE_OUT_DIR: ${ANGLE_OUT_DIR}")
    message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
endif()

# Find ANGLE include directory
set(ANGLE_INCLUDE_DIR "${ANGLE_DIR}/include")

# Check for ANGLE headers
if(VERBOSE_DEPS)
    message(STATUS "  Searching for ANGLE headers:")
    if(EXISTS "${ANGLE_INCLUDE_DIR}/EGL/egl.h")
        message(STATUS "    [FOUND] ${ANGLE_INCLUDE_DIR}/EGL/egl.h")
    else()
        message(STATUS "    EGL/egl.h not found at ${ANGLE_INCLUDE_DIR}/EGL/egl.h")
    endif()
    if(EXISTS "${ANGLE_INCLUDE_DIR}/GLES3/gl3.h")
        message(STATUS "    [FOUND] ${ANGLE_INCLUDE_DIR}/GLES3/gl3.h")
    else()
        message(STATUS "    GLES3/gl3.h not found at ${ANGLE_INCLUDE_DIR}/GLES3/gl3.h")
    endif()
endif()

# Find ANGLE libraries
if(VERBOSE_DEPS)
    message(STATUS "  Searching for ANGLE libraries:")
endif()

# libEGL and libGLESv2
if(WIN32)
    set(ANGLE_EGL_LIB_NAMES "libEGL.dll" "libEGL" "EGL")
    set(ANGLE_GLES_LIB_NAMES "libGLESv2.dll" "libGLESv2" "GLESv2")
    set(ANGLE_LIB_EXT ".lib")
else()
    set(ANGLE_EGL_LIB_NAMES "libEGL")
    set(ANGLE_GLES_LIB_NAMES "libGLESv2")
    set(ANGLE_LIB_EXT ".a")
endif()

set(ANGLE_EGL_FOUND FALSE)
set(ANGLE_GLES_FOUND FALSE)

# Search for EGL library
foreach(_lib_name ${ANGLE_EGL_LIB_NAMES})
    if(EXISTS "${ANGLE_OUT_DIR}/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_EGL_LIBRARY "${ANGLE_OUT_DIR}/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_EGL_FOUND TRUE)
        if(VERBOSE_DEPS)
            message(STATUS "    [FOUND] EGL: ${ANGLE_EGL_LIBRARY}")
        endif()
        break()
    elseif(EXISTS "${ANGLE_OUT_DIR}/lib/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_EGL_LIBRARY "${ANGLE_OUT_DIR}/lib/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_EGL_FOUND TRUE)
        if(VERBOSE_DEPS)
            message(STATUS "    [FOUND] EGL: ${ANGLE_EGL_LIBRARY}")
        endif()
        break()
    endif()
endforeach()

# Search for GLESv2 library
foreach(_lib_name ${ANGLE_GLES_LIB_NAMES})
    if(EXISTS "${ANGLE_OUT_DIR}/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_GLES_LIBRARY "${ANGLE_OUT_DIR}/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_GLES_FOUND TRUE)
        if(VERBOSE_DEPS)
            message(STATUS "    [FOUND] GLESv2: ${ANGLE_GLES_LIBRARY}")
        endif()
        break()
    elseif(EXISTS "${ANGLE_OUT_DIR}/lib/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_GLES_LIBRARY "${ANGLE_OUT_DIR}/lib/${_lib_name}${ANGLE_LIB_EXT}")
        set(ANGLE_GLES_FOUND TRUE)
        if(VERBOSE_DEPS)
            message(STATUS "    [FOUND] GLESv2: ${ANGLE_GLES_LIBRARY}")
        endif()
        break()
    endif()
endforeach()

# ANGLE is REQUIRED - fail if not found
if(NOT ANGLE_EGL_FOUND)
    if(VERBOSE_DEPS)
        message(STATUS "    EGL library not found")
    endif()
    message(FATAL_ERROR "ANGLE EGL library not found")
endif()

if(NOT ANGLE_GLES_FOUND)
    if(VERBOSE_DEPS)
        message(STATUS "    GLESv2 library not found")
    endif()
    message(FATAL_ERROR "ANGLE GLESv2 library not found")
endif()

# Results
if(VERBOSE_DEPS)
    message(STATUS "")
    message(STATUS "  ANGLE Detection Results:")
    message(STATUS "    ANGLE_INCLUDE_DIR: ${ANGLE_INCLUDE_DIR}")
    message(STATUS "    ANGLE_EGL_LIBRARY: ${ANGLE_EGL_LIBRARY}")
    message(STATUS "    ANGLE_GLES_LIBRARY: ${ANGLE_GLES_LIBRARY}")
    message(STATUS "")
endif()

# ========================================
# DirectX-Headers (REQUIRED on Windows)
# ========================================
if(WIN32)
    if(VERBOSE_DEPS)
        message(STATUS "")
        message(STATUS "[DirectX-Headers Detection]")
    endif()

    set(DIRECTX_HEADERS_DIR "${DIRECTX_HEADERS_DIR}" CACHE PATH "DirectX-Headers directory")
    if(NOT DIRECTX_HEADERS_DIR)
        set(DIRECTX_HEADERS_DIR "${DEPS_DIR}/DirectX-Headers")
    endif()

    if(VERBOSE_DEPS)
        message(STATUS "  DIRECTX_HEADERS_DIR: ${DIRECTX_HEADERS_DIR}")
    endif()

    # Check for DirectX-Headers
    set(DIRECTX_HEADERS_INCLUDE_DIR "${DIRECTX_HEADERS_DIR}/include")
    set(DIRECTX_HEADERS_FOUND FALSE)

    if(EXISTS "${DIRECTX_HEADERS_INCLUDE_DIR}/directx/d3d12.h")
        set(DIRECTX_HEADERS_FOUND TRUE)
        if(VERBOSE_DEPS)
            message(STATUS "  [FOUND] ${DIRECTX_HEADERS_INCLUDE_DIR}/directx/d3d12.h")
        endif()
    elseif(VERBOSE_DEPS)
        message(STATUS "  DirectX-Headers not found at ${DIRECTX_HEADERS_INCLUDE_DIR}/directx/d3d12.h")
    endif()

    if(NOT DIRECTX_HEADERS_FOUND)
        message(FATAL_ERROR "DirectX-Headers not found. Run: python sync.py")
    endif()

    # Results
    if(VERBOSE_DEPS)
        message(STATUS "")
        message(STATUS "  DirectX-Headers Detection Results:")
        message(STATUS "    DIRECTX_HEADERS_INCLUDE_DIR: ${DIRECTX_HEADERS_INCLUDE_DIR}")
        message(STATUS "")
    endif()
endif()

# ========================================
# Executable
# ========================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/Application.cpp
    src/renderer/SceneRenderer.cpp
    src/renderer/GLContext.cpp
    src/renderer/GLRenderer.cpp
    src/renderer/RendererFactory.cpp
    # Vulkan backend files
    src/renderer/VulkanContext.cpp
    src/renderer/SkiaRenderer.cpp
    src/renderer/Swapchain.cpp
    # ANGLE backend files
    src/renderer/AngleContext.cpp
    src/renderer/AngleRenderer.cpp
    # D3D12 backend files (Windows only)
    $<${WIN32}:src/renderer/D3DContext.cpp>
    $<${WIN32}:src/renderer/D3DRenderer.cpp>
    # DirectX-Headers source files (Windows only)
    $<${WIN32}:${DIRECTX_HEADERS_DIR}/src/dxguids.cpp>
    $<${WIN32}:${DIRECTX_HEADERS_DIR}/src/d3dx12_property_format_table.cpp>
)

# Enable Vulkan backend
target_compile_definitions(${PROJECT_NAME} PRIVATE VULKAN_BACKEND_ENABLED=1)

# ANGLE is always enabled
target_compile_definitions(${PROJECT_NAME} PRIVATE USE_ANGLE=1)

# Use DirectX-Headers on Windows (MUST be first to override Windows SDK)
target_include_directories(${PROJECT_NAME} PRIVATE
    # DirectX-Headers include (MUST be first on Windows to override Windows SDK)
    $<$<PLATFORM_ID:Windows>:${DIRECTX_HEADERS_INCLUDE_DIR}>
    $<$<PLATFORM_ID:Windows>:${DIRECTX_HEADERS_INCLUDE_DIR}/directx>
    $<$<PLATFORM_ID:Windows>:${DIRECTX_HEADERS_INCLUDE_DIR}/dxguids>
    # Project source
    ${CMAKE_SOURCE_DIR}/src
    ${SDL3_INCLUDE_DIRS}
    ${SKIA_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${CLI11_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${VK_BOOTSTRAP_INCLUDE_DIR}
    # ANGLE include directories
    ${ANGLE_INCLUDE_DIR}
    ${ANGLE_INCLUDE_DIR}/EGL
    ${ANGLE_INCLUDE_DIR}/GLES2
    ${ANGLE_INCLUDE_DIR}/GLES3
    ${ANGLE_INCLUDE_DIR}/KHR
)

# ========================================
# Link Libraries
# ========================================
set(LINK_LIBS
    SDL3::SDL3
    ${SKIA_LIBRARY}
    Vulkan::Vulkan
    ${VK_BOOTSTRAP_LIBRARY}
    # ANGLE libraries
    ${ANGLE_EGL_LIBRARY}
    ${ANGLE_GLES_LIBRARY}
)

if(WIN32)
    list(APPEND LINK_LIBS
        user32 gdi32 shell32 ole32 oleaut32 imm32 winmm version setupapi dwmapi
        fontsub dwrite d3d12 dxgi d3dcompiler opengl32
    )
else()
    list(APPEND LINK_LIBS
        ZLIB::ZLIB PNG::PNG Freetype::Freetype Fontconfig::Fontconfig
        expat harfbuzz icuuc icui18n pthread dl m
        ${CMAKE_DL_LIBS}  # For dlopen/dlsym on Linux
        OpenGL::GL
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBS})

# ========================================
# Debug Definitions
# ========================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        DEBUG_BUILD SK_DEBUG
        ENABLE_VULKAN_VALIDATION
    )
    message(STATUS "Debug: Vulkan validation enabled")
endif()

# ========================================
# Compiler Flags
# ========================================
# Detect clang-cl (Clang with MSVC frontend)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR
       CMAKE_CXX_COMPILER MATCHES "clang-cl")
        set(USING_CLANG_CL TRUE)
        message(STATUS "Using clang-cl on Windows (MSVC-compatible)")
    else()
        set(USING_CLANG_GNU TRUE)
        message(STATUS "Using Clang on Windows (GNU-compatible)")
    endif()
endif()

# Note: Check USING_CLANG_CL first because MSVC is also TRUE for clang-cl
if(USING_CLANG_CL)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4 /utf-8 /GR /EHsc
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    # Note: /GL (LTCG) is not supported by clang-cl, only by MSVC
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4 /utf-8 /GR /EHsc
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(${PROJECT_NAME} PRIVATE /LTCG)
    endif()
elseif(USING_CLANG_GNU)
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
message(STATUS "")
message(STATUS "Graphics APIs:")
message(STATUS "  Vulkan:     ${Vulkan_SDK}")
message(STATUS "  OpenGL:     ${OPENGL_LIBRARIES}")
message(STATUS "  ANGLE:      ${ANGLE_INCLUDE_DIR}")
if(WIN32)
message(STATUS "  D3D12:      ${DIRECTX_HEADERS_INCLUDE_DIR}")
endif()
message(STATUS "")
message(STATUS "Renderers:")
message(STATUS "  Vulkan:     Skia Graphite")
message(STATUS "  OpenGL:     Skia Ganesh")
message(STATUS "================================")
message(STATUS "")
