#pragma once

// VMA (Vulkan Memory Allocator) configuration macros
// These MUST be defined before including vk_mem_alloc.h
// VMA requires these to match the Vulkan API version used at runtime
#ifndef VMA_VULKAN_VERSION
#define VMA_VULKAN_VERSION 1003000  // Vulkan 1.3 for Skia Graphite
#endif

// Enable required VMA features for Vulkan 1.2+
#ifndef VMA_DEDICATED_ALLOCATION
#define VMA_DEDICATED_ALLOCATION 1
#endif

#ifndef VMA_BIND_MEMORY2
#define VMA_BIND_MEMORY2 1
#endif

#ifndef VMA_MEMORY_BUDGET
#define VMA_MEMORY_BUDGET 1
#endif

#ifndef VMA_BUFFER_DEVICE_ADDRESS
#define VMA_BUFFER_DEVICE_ADDRESS 1
#endif

#include "include/gpu/vk/VulkanMemoryAllocator.h"
#include "include/gpu/vk/VulkanTypes.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <mutex>
#include <unordered_map>
#include <utility>

namespace skia_renderer {

/**
 * VulkanMemoryAllocator implementation using VMA (Vulkan Memory Allocator)
 * This wraps VMA to provide the interface Skia Graphite requires.
 */
class VulkanAllocator : public skgpu::VulkanMemoryAllocator {
public:
    VulkanAllocator(VkInstance instance,
                    VkPhysicalDevice physicalDevice,
                    VkDevice device,
                    uint32_t apiVersion);
    
    ~VulkanAllocator() override;

    // skgpu::VulkanMemoryAllocator interface
    VkResult allocateImageMemory(VkImage image,
                                  uint32_t allocationPropertyFlags,
                                  skgpu::VulkanBackendMemory* memory) override;

    VkResult allocateBufferMemory(VkBuffer buffer,
                                   BufferUsage usage,
                                   uint32_t allocationPropertyFlags,
                                   skgpu::VulkanBackendMemory* memory) override;

    void getAllocInfo(const skgpu::VulkanBackendMemory& memory,
                      skgpu::VulkanAlloc* alloc) const override;

    void* mapMemory(const skgpu::VulkanBackendMemory& memory) override;
    
    void unmapMemory(const skgpu::VulkanBackendMemory& memory) override;

    void flushMappedMemory(const skgpu::VulkanBackendMemory& memory,
                           VkDeviceSize offset,
                           VkDeviceSize size) override;

    void invalidateMappedMemory(const skgpu::VulkanBackendMemory& memory,
                                VkDeviceSize offset,
                                VkDeviceSize size) override;

    void freeMemory(const skgpu::VulkanBackendMemory& memory) override;

    std::pair<uint64_t, uint64_t> totalAllocatedAndUsedMemory() const override;

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    
    // Track allocations for cleanup and info queries
    struct AllocationInfo {
        VmaAllocation allocation;
        VmaAllocationInfo info;
        VkDeviceMemory deviceMemory;
        VkDeviceSize offset;
        VkDeviceSize size;
        void* mappedData = nullptr;
    };
    
    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, AllocationInfo> m_allocations;
    static uint64_t s_nextMemoryId;
    
    uint64_t getNextMemoryId();
    
    // Helper to convert our usage to VMA usage
    static VmaMemoryUsage getVmaMemoryUsage(BufferUsage usage);
    static VmaAllocationCreateFlags getVmaAllocationFlags(uint32_t propertyFlags);
};

} // namespace skia_renderer
