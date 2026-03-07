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
option(USE_SYSTEM_VMA "Use system VulkanMemoryAllocator" OFF)
option(USE_SYSTEM_SKIA "Use system Skia" OFF)

# ========================================
# Detect build environment
# ========================================
if(WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

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
    # Check for SDL3_DIR from command line or in deps
    if(DEFINED SDL3_DIR)
        find_package(SDL3 REQUIRED HINTS "${SDL3_DIR}")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/SDL3")
        # Check for pre-built SDL3 in deps
        if(EXISTS "${CMAKE_SOURCE_DIR}/deps/SDL3/lib/cmake/SDL3/SDL3Config.cmake")
            find_package(SDL3 REQUIRED HINTS "${CMAKE_SOURCE_DIR}/deps/SDL3/lib/cmake/SDL3")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/SDL3/cmake/SDL3Config.cmake")
            find_package(SDL3 REQUIRED HINTS "${CMAKE_SOURCE_DIR}/deps/SDL3/cmake")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/installed/lib/cmake/SDL3")
            find_package(SDL3 REQUIRED HINTS "${CMAKE_SOURCE_DIR}/deps/installed/lib/cmake/SDL3")
        else()
            # Try to find SDL3 in system paths
            find_package(SDL3 REQUIRED)
        endif()
    else()
        find_package(SDL3 REQUIRED)
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
    # Check for VKBOOTSTRAP_DIR from command line
    if(DEFINED VKBOOTSTRAP_DIR)
        find_path(VK_BOOTSTRAP_INCLUDE_DIR 
            NAMES VkBootstrap.h 
            HINTS "${VKBOOTSTRAP_DIR}/include" 
                  "${VKBOOTSTRAP_DIR}/include/vk-bootstrap"
            PATH_SUFFIXES vk-bootstrap)
        find_library(VK_BOOTSTRAP_LIBRARY 
            NAMES vk-bootstrap vk-bootstrap-static 
            HINTS "${VKBOOTSTRAP_DIR}/lib" 
                  "${VKBOOTSTRAP_DIR}/lib/${CMAKE_BUILD_TYPE}"
            PATH_SUFFIXES Release Debug)
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap")
        find_path(VK_BOOTSTRAP_INCLUDE_DIR 
            NAMES VkBootstrap.h 
            HINTS "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap/include"
                  "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap/include/vk-bootstrap"
            PATH_SUFFIXES vk-bootstrap)
        find_library(VK_BOOTSTRAP_LIBRARY 
            NAMES vk-bootstrap vk-bootstrap-static 
            HINTS "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap/build"
                  "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap/build/${CMAKE_BUILD_TYPE}"
                  "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap/lib"
                  "${CMAKE_SOURCE_DIR}/deps/installed/lib"
            PATH_SUFFIXES Release Debug)
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/installed")
        find_path(VK_BOOTSTRAP_INCLUDE_DIR 
            NAMES VkBootstrap.h 
            HINTS "${CMAKE_SOURCE_DIR}/deps/installed/include"
            PATH_SUFFIXES vk-bootstrap)
        find_library(VK_BOOTSTRAP_LIBRARY 
            NAMES vk-bootstrap vk-bootstrap-static 
            HINTS "${CMAKE_SOURCE_DIR}/deps/installed/lib"
            PATH_SUFFIXES Release Debug)
    else()
        find_package(vk-bootstrap REQUIRED)
    endif()
endif()
message(STATUS "vk-bootstrap Include: ${VK_BOOTSTRAP_INCLUDE_DIR}")
message(STATUS "vk-bootstrap Library: ${VK_BOOTSTRAP_LIBRARY}")

# ========================================
# VMA (Vulkan Memory Allocator) - header only
# ========================================
if(USE_SYSTEM_VMA)
    find_path(VMA_INCLUDE_DIR NAMES vk_mem_alloc.h)
else()
    if(DEFINED VMA_DIR)
        find_path(VMA_INCLUDE_DIR NAMES vk_mem_alloc.h HINTS "${VMA_DIR}/include")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/VulkanMemoryAllocator")
        find_path(VMA_INCLUDE_DIR NAMES vk_mem_alloc.h HINTS "${CMAKE_SOURCE_DIR}/deps/VulkanMemoryAllocator/include")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/installed")
        find_path(VMA_INCLUDE_DIR NAMES vk_mem_alloc.h HINTS "${CMAKE_SOURCE_DIR}/deps/installed/include")
    else()
        find_path(VMA_INCLUDE_DIR NAMES vk_mem_alloc.h)
    endif()
endif()
message(STATUS "VMA Include: ${VMA_INCLUDE_DIR}")

if(NOT VMA_INCLUDE_DIR)
    message(WARNING "VulkanMemoryAllocator not found. Please download to deps/VulkanMemoryAllocator/")
endif()

# ========================================
# Skia
# ========================================
if(USE_SYSTEM_SKIA)
    find_path(SKIA_INCLUDE_DIR NAMES core/SkSurface.h)
    find_library(SKIA_LIBRARY NAMES skia)
else()
    if(DEFINED SKIA_DIR)
        find_path(SKIA_INCLUDE_DIR NAMES core/SkSurface.h HINTS "${SKIA_DIR}")
        find_library(SKIA_LIBRARY NAMES skia HINTS "${SKIA_DIR}/out/Release" "${SKIA_DIR}/out/Debug")
    else()
        find_path(SKIA_INCLUDE_DIR NAMES core/SkSurface.h HINTS "${CMAKE_SOURCE_DIR}/deps/skia")
        find_library(SKIA_LIBRARY NAMES skia 
            HINTS "${CMAKE_SOURCE_DIR}/deps/skia/out/Release" 
                  "${CMAKE_SOURCE_DIR}/deps/skia/out/Debug")
    endif()
endif()
message(STATUS "Skia Include: ${SKIA_INCLUDE_DIR}")
message(STATUS "Skia Library: ${SKIA_LIBRARY}")

if(NOT SKIA_LIBRARY)
    message(WARNING "Skia library not found. Build it with build_deps.bat")
endif()

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
    ${VMA_INCLUDE_DIR}
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
    if(SKIA_LIBRARY)
        list(APPEND LINK_LIBS
            fontsub
            dwrite
            d3d12
            dxgi
            opengl32
        )
    endif()
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
        set_target_properties(${PROJECT_NAME} PROPERTIES STATIC_RUNTIME_LIBRARY_RELEASE "ON")
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
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# ========================================
# Copy DLLs on Windows
# ========================================
if(WIN32)
    # Copy SDL3 DLL
    if(TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3-shared>"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/SDL3/lib/SDL3.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/deps/SDL3/lib/SDL3.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/installed/bin/SDL3.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/deps/installed/bin/SDL3.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
endif()

# ========================================
# RPATH for Unix
# ========================================
if(NOT WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${CMAKE_SOURCE_DIR}/deps/SDL3/lib;${CMAKE_SOURCE_DIR}/deps/skia/out/Release"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# ========================================
# Summary
# ========================================
message(STATUS "")
message(STATUS "==== Configuration Summary ====")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "================================")
message(STATUS "")
