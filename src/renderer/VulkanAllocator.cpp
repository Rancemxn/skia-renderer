#include "VulkanAllocator.h"

#include <iostream>
#include <cstring>

namespace skia_renderer {

uint64_t VulkanAllocator::s_nextMemoryId = 1;

VulkanAllocator::VulkanAllocator(VkInstance instance,
                                   VkPhysicalDevice physicalDevice,
                                   VkDevice device,
                                   uint32_t apiVersion)
    : m_device(device) {
    
    VmaAllocatorCreateInfo createInfo{};
    createInfo.instance = instance;
    createInfo.physicalDevice = physicalDevice;
    createInfo.device = device;
    createInfo.vulkanApiVersion = apiVersion;
    
    // Don't use VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT - requires device extension
    createInfo.flags = 0;
    
    VkResult result = vmaCreateAllocator(&createInfo, &m_allocator);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create VMA allocator: " << result << std::endl;
        return;
    }
    
    std::cout << "VMA allocator created successfully" << std::endl;
}

VulkanAllocator::~VulkanAllocator() {
    if (m_allocator != VK_NULL_HANDLE) {
        // Free any remaining allocations
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& pair : m_allocations) {
                if (pair.second.mappedData) {
                    vmaUnmapMemory(m_allocator, pair.second.allocation);
                }
                vmaFreeMemory(m_allocator, pair.second.allocation);
            }
            m_allocations.clear();
        }
        
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

uint64_t VulkanAllocator::getNextMemoryId() {
    return s_nextMemoryId++;
}

VkResult VulkanAllocator::allocateImageMemory(VkImage image,
                                                uint32_t allocationPropertyFlags,
                                                skgpu::VulkanBackendMemory* memory) {
    if (m_allocator == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocCreateInfo.flags = getVmaAllocationFlags(allocationPropertyFlags);
    
    // For dedicated allocations
    if (allocationPropertyFlags & kDedicatedAllocation_AllocationPropertyFlag) {
        allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    
    VmaAllocation allocation;
    VmaAllocationInfo allocInfo;
    
    VkResult result = vmaAllocateMemoryForImage(m_allocator, image, &allocCreateInfo,
                                                 &allocation, &allocInfo);
    if (result != VK_SUCCESS) {
        return result;
    }
    
    // Bind the memory
    result = vmaBindImageMemory(m_allocator, allocation, image);
    if (result != VK_SUCCESS) {
        vmaFreeMemory(m_allocator, allocation);
        return result;
    }
    
    // Track the allocation
    uint64_t memoryId = getNextMemoryId();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        AllocationInfo& info = m_allocations[memoryId];
        info.allocation = allocation;
        info.info = allocInfo;
        info.deviceMemory = allocInfo.deviceMemory;
        info.offset = allocInfo.offset;
        info.size = allocInfo.size;
    }
    
    *memory = static_cast<skgpu::VulkanBackendMemory>(memoryId);
    return VK_SUCCESS;
}

VkResult VulkanAllocator::allocateBufferMemory(VkBuffer buffer,
                                                 BufferUsage usage,
                                                 uint32_t allocationPropertyFlags,
                                                 skgpu::VulkanBackendMemory* memory) {
    if (m_allocator == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = getVmaMemoryUsage(usage);
    allocCreateInfo.flags = getVmaAllocationFlags(allocationPropertyFlags);
    
    // For persistent mapping
    if (allocationPropertyFlags & kPersistentlyMapped_AllocationPropertyFlag) {
        allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    
    // For dedicated allocations
    if (allocationPropertyFlags & kDedicatedAllocation_AllocationPropertyFlag) {
        allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    
    VmaAllocation allocation;
    VmaAllocationInfo allocInfo;
    
    VkResult result = vmaAllocateMemoryForBuffer(m_allocator, buffer, &allocCreateInfo,
                                                  &allocation, &allocInfo);
    if (result != VK_SUCCESS) {
        return result;
    }
    
    // Bind the memory
    result = vmaBindBufferMemory(m_allocator, allocation, buffer);
    if (result != VK_SUCCESS) {
        vmaFreeMemory(m_allocator, allocation);
        return result;
    }
    
    // Track the allocation
    uint64_t memoryId = getNextMemoryId();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        AllocationInfo& info = m_allocations[memoryId];
        info.allocation = allocation;
        info.info = allocInfo;
        info.deviceMemory = allocInfo.deviceMemory;
        info.offset = allocInfo.offset;
        info.size = allocInfo.size;
        
        // If already mapped, store the pointer
        if (allocCreateInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
            info.mappedData = allocInfo.pMappedData;
        }
    }
    
    *memory = static_cast<skgpu::VulkanBackendMemory>(memoryId);
    return VK_SUCCESS;
}

void VulkanAllocator::getAllocInfo(const skgpu::VulkanBackendMemory& memory,
                                    skgpu::VulkanAlloc* alloc) const {
    uint64_t memoryId = static_cast<uint64_t>(memory);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(memoryId);
    if (it == m_allocations.end()) {
        return;
    }
    
    const AllocationInfo& info = it->second;
    alloc->fMemory = info.deviceMemory;
    alloc->fOffset = info.offset;
    alloc->fSize = info.size;
    alloc->fFlags = 0;
    alloc->fBackendMemory = memory;
}

void* VulkanAllocator::mapMemory(const skgpu::VulkanBackendMemory& memory) {
    uint64_t memoryId = static_cast<uint64_t>(memory);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(memoryId);
    if (it == m_allocations.end()) {
        return nullptr;
    }
    
    AllocationInfo& info = it->second;
    
    // Already mapped?
    if (info.mappedData) {
        return info.mappedData;
    }
    
    void* data = nullptr;
    VkResult result = vmaMapMemory(m_allocator, info.allocation, &data);
    if (result == VK_SUCCESS) {
        info.mappedData = data;
        return data;
    }
    
    return nullptr;
}

void VulkanAllocator::unmapMemory(const skgpu::VulkanBackendMemory& memory) {
    uint64_t memoryId = static_cast<uint64_t>(memory);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(memoryId);
    if (it == m_allocations.end()) {
        return;
    }
    
    AllocationInfo& info = it->second;
    
    if (info.mappedData) {
        vmaUnmapMemory(m_allocator, info.allocation);
        info.mappedData = nullptr;
    }
}

void VulkanAllocator::flushMappedMemory(const skgpu::VulkanBackendMemory& memory,
                                         VkDeviceSize offset,
                                         VkDeviceSize size) {
    uint64_t memoryId = static_cast<uint64_t>(memory);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(memoryId);
    if (it == m_allocations.end()) {
        return;
    }
    
    vmaFlushAllocation(m_allocator, it->second.allocation, offset, size);
}

void VulkanAllocator::invalidateMappedMemory(const skgpu::VulkanBackendMemory& memory,
                                              VkDeviceSize offset,
                                              VkDeviceSize size) {
    uint64_t memoryId = static_cast<uint64_t>(memory);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(memoryId);
    if (it == m_allocations.end()) {
        return;
    }
    
    vmaInvalidateAllocation(m_allocator, it->second.allocation, offset, size);
}

void VulkanAllocator::freeMemory(const skgpu::VulkanBackendMemory& memory) {
    uint64_t memoryId = static_cast<uint64_t>(memory);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(memoryId);
    if (it == m_allocations.end()) {
        return;
    }
    
    AllocationInfo& info = it->second;
    
    if (info.mappedData) {
        vmaUnmapMemory(m_allocator, info.allocation);
    }
    
    vmaFreeMemory(m_allocator, info.allocation);
    m_allocations.erase(it);
}

std::pair<uint64_t, uint64_t> VulkanAllocator::totalAllocatedAndUsedMemory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint64_t totalAllocated = 0;
    uint64_t totalUsed = 0;
    
    for (const auto& pair : m_allocations) {
        totalAllocated += pair.second.size;
        totalUsed += pair.second.size;
    }
    
    return {totalAllocated, totalUsed};
}

// Static helper methods
VmaMemoryUsage VulkanAllocator::getVmaMemoryUsage(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::kGpuOnly:
            return VMA_MEMORY_USAGE_GPU_ONLY;
        case BufferUsage::kCpuWritesGpuReads:
            return VMA_MEMORY_USAGE_CPU_TO_GPU;
        case BufferUsage::kTransfersFromCpuToGpu:
            return VMA_MEMORY_USAGE_CPU_ONLY;
        case BufferUsage::kTransfersFromGpuToCpu:
            return VMA_MEMORY_USAGE_GPU_TO_CPU;
        default:
            return VMA_MEMORY_USAGE_UNKNOWN;
    }
}

VmaAllocationCreateFlags VulkanAllocator::getVmaAllocationFlags(uint32_t propertyFlags) {
    VmaAllocationCreateFlags flags = 0;
    
    if (propertyFlags & kDedicatedAllocation_AllocationPropertyFlag) {
        flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    
    if (propertyFlags & kLazyAllocation_AllocationPropertyFlag) {
        flags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
    }
    
    if (propertyFlags & kPersistentlyMapped_AllocationPropertyFlag) {
        flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    
    return flags;
}

} // namespace skia_renderer
