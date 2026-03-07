cmake_minimum_required(VERSION 3.20)
project(skia-renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(USE_SYSTEM_SDL "Use system SDL3" OFF)
option(USE_SYSTEM_VK_BOOTSTRAP "Use system vk-bootstrap" OFF)

# Vulkan SDK
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan SDK: ${VULKAN_SDK}")
message(STATUS "Vulkan Include: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan Library: ${Vulkan_LIBRARIES}")

# SDL3
if(USE_SYSTEM_SDL)
    find_package(SDL3 REQUIRED)
else()
    set(SDL3_DIR "${CMAKE_SOURCE_DIR}/deps/SDL3")
    if(EXISTS "${SDL3_DIR}/cmake/SDL3Config.cmake")
        set(SDL3_DIR "${SDL3_DIR}/cmake")
    elseif(EXISTS "${SDL3_DIR}/lib/cmake/SDL3/SDL3Config.cmake")
        set(SDL3_DIR "${SDL3_DIR}/lib/cmake/SDL3")
    endif()
    find_package(SDL3 REQUIRED HINTS "${SDL3_DIR}")
endif()
message(STATUS "SDL3 Found: ${SDL3_FOUND}")

# vk-bootstrap
if(USE_SYSTEM_VK_BOOTSTRAP)
    find_package(vk-bootstrap REQUIRED)
else()
    set(VK_BOOTSTRAP_DIR "${CMAKE_SOURCE_DIR}/deps/vk-bootstrap")
    find_path(VK_BOOTSTRAP_INCLUDE_DIR NAMES VkBootstrap.h PATHS "${VK_BOOTSTRAP_DIR}/include")
    find_library(VK_BOOTSTRAP_LIBRARY NAMES vk-bootstrap PATHS "${VK_BOOTSTRAP_DIR}/lib" "${VK_BOOTSTRAP_DIR}/build")
endif()
message(STATUS "vk-bootstrap Include: ${VK_BOOTSTRAP_INCLUDE_DIR}")
message(STATUS "vk-bootstrap Library: ${VK_BOOTSTRAP_LIBRARY}")

# VMA (Vulkan Memory Allocator) - header only
set(VMA_DIR "${CMAKE_SOURCE_DIR}/deps/VulkanMemoryAllocator")
find_path(VMA_INCLUDE_DIR NAMES vk_mem_alloc.h PATHS "${VMA_DIR}/include")
message(STATUS "VMA Include: ${VMA_INCLUDE_DIR}")

# Skia
set(SKIA_DIR "${CMAKE_SOURCE_DIR}/deps/skia")
find_path(SKIA_INCLUDE_DIR NAMES core/SkSurface.h PATHS "${SKIA_DIR}")
find_library(SKIA_LIBRARY NAMES skia PATHS "${SKIA_DIR}/out/Release")
message(STATUS "Skia Include: ${SKIA_INCLUDE_DIR}")
message(STATUS "Skia Library: ${SKIA_LIBRARY}")

# Skia system dependencies
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)

# Executable
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
    ${Freetype_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    ${VK_BOOTSTRAP_LIBRARY}
    ${SKIA_LIBRARY}
    ZLIB::ZLIB
    PNG::PNG
    Freetype::Freetype
    Fontconfig::Fontconfig
    
    # Skia dependencies
    expat
    harfbuzz
    icuuc
    icui18n
    zlib
    pthread
    dl
    m
)

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-g -O0>
)

# Copy SDL3 library to output directory for runtime
if(NOT USE_SYSTEM_SDL AND EXISTS "${CMAKE_SOURCE_DIR}/deps/SDL3/lib/libSDL3.so.0")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/SDL3/lib/libSDL3.so.0"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# Set RPATH for finding libraries at runtime
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${CMAKE_SOURCE_DIR}/deps/SDL3/lib;${CMAKE_SOURCE_DIR}/deps/skia/out/Release"
    INSTALL_RPATH "$ORIGIN"
)
