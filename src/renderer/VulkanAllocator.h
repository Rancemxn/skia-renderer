#pragma once

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
