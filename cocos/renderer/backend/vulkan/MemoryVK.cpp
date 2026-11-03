/****************************************************************************
 Vulkan backend: a small device memory sub-allocator.
 ****************************************************************************/
#include "MemoryVK.h"

CC_BACKEND_BEGIN

namespace
{
    const VkDeviceSize kBlockSize = 32ull * 1024 * 1024;
    // Anything this large gets its own VkDeviceMemory.
    const VkDeviceSize kDedicatedThreshold = 8ull * 1024 * 1024;
}

void MemoryAllocatorVK::init(VkPhysicalDevice physicalDevice, VkDevice device)
{
    _device = device;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &_memoryProperties);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    _bufferImageGranularity = props.limits.bufferImageGranularity;
}

void MemoryAllocatorVK::destroy()
{
    for (auto& block : _blocks)
    {
        if (block.memory != VK_NULL_HANDLE)
            vkFreeMemory(_device, block.memory, nullptr);
    }
    _blocks.clear();
}

int MemoryAllocatorVK::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const
{
    for (uint32_t i = 0; i < _memoryProperties.memoryTypeCount; ++i)
    {
        auto flags = _memoryProperties.memoryTypes[i].propertyFlags;
        if ((typeBits & (1u << i)) && (flags & (required | preferred)) == (required | preferred))
            return (int)i;
    }
    for (uint32_t i = 0; i < _memoryProperties.memoryTypeCount; ++i)
    {
        auto flags = _memoryProperties.memoryTypes[i].propertyFlags;
        if ((typeBits & (1u << i)) && (flags & required) == required)
            return (int)i;
    }
    return -1;
}

bool MemoryAllocatorVK::allocateFromBlock(Block& block, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset)
{
    for (auto it = block.freeRanges.begin(); it != block.freeRanges.end(); ++it)
    {
        VkDeviceSize start = UtilsVK::alignUp(it->first, alignment);
        VkDeviceSize end = it->first + it->second;
        if (start + size > end)
            continue;
        VkDeviceSize rangeStart = it->first;
        block.freeRanges.erase(it);
        if (start > rangeStart)
            block.freeRanges[rangeStart] = start - rangeStart;
        if (start + size < end)
            block.freeRanges[start + size] = end - (start + size);
        offset = start;
        return true;
    }
    return false;
}

bool MemoryAllocatorVK::allocate(const VkMemoryRequirements& requirements, bool hostVisible, AllocationVK& out)
{
    VkMemoryPropertyFlags required = hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryPropertyFlags preferred = hostVisible ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0;
    int type = findMemoryType(requirements.memoryTypeBits, required, preferred);
    if (type < 0 && !hostVisible)
        type = findMemoryType(requirements.memoryTypeBits, 0, 0);
    if (type < 0)
    {
        VK_LOGE("no memory type for bits 0x%x (hostVisible=%d)", requirements.memoryTypeBits, hostVisible);
        return false;
    }

    // Images and buffers share blocks, so honour the worst-case granularity.
    VkDeviceSize alignment = requirements.alignment > _bufferImageGranularity ? requirements.alignment : _bufferImageGranularity;
    VkDeviceSize size = UtilsVK::alignUp(requirements.size, alignment);

    if (size >= kDedicatedThreshold)
    {
        VkMemoryAllocateInfo info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        info.allocationSize = requirements.size;
        info.memoryTypeIndex = (uint32_t)type;
        if (vkAllocateMemory(_device, &info, nullptr, &out.memory) != VK_SUCCESS)
            return false;
        out.offset = 0;
        out.size = requirements.size;
        out.blockIndex = -1;
        out.mapped = nullptr;
        if (hostVisible)
            vkMapMemory(_device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped);
        _dedicatedTypes[out.memory] = (uint32_t)type;
        return true;
    }

    for (size_t i = 0; i < _blocks.size(); ++i)
    {
        Block& block = _blocks[i];
        if (block.memory == VK_NULL_HANDLE || block.memoryType != (uint32_t)type)
            continue;
        VkDeviceSize offset;
        if (allocateFromBlock(block, size, alignment, offset))
        {
            out.memory = block.memory;
            out.offset = offset;
            out.size = size;
            out.blockIndex = (int)i;
            out.mapped = block.mapped ? (char*)block.mapped + offset : nullptr;
            return true;
        }
    }

    Block block;
    block.size = kBlockSize;
    block.memoryType = (uint32_t)type;
    VkMemoryAllocateInfo info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    info.allocationSize = block.size;
    info.memoryTypeIndex = (uint32_t)type;
    if (vkAllocateMemory(_device, &info, nullptr, &block.memory) != VK_SUCCESS)
    {
        VK_LOGE("vkAllocateMemory(%llu) failed", (unsigned long long)block.size);
        return false;
    }
    if (_memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        vkMapMemory(_device, block.memory, 0, VK_WHOLE_SIZE, 0, &block.mapped);
    block.freeRanges[0] = block.size;

    int index = -1;
    for (size_t i = 0; i < _blocks.size(); ++i)
    {
        if (_blocks[i].memory == VK_NULL_HANDLE)
        {
            index = (int)i;
            break;
        }
    }
    if (index < 0)
    {
        index = (int)_blocks.size();
        _blocks.push_back(Block());
    }
    _blocks[index] = std::move(block);

    Block& b = _blocks[index];
    VkDeviceSize offset;
    allocateFromBlock(b, size, alignment, offset);
    out.memory = b.memory;
    out.offset = offset;
    out.size = size;
    out.blockIndex = index;
    out.mapped = b.mapped ? (char*)b.mapped + offset : nullptr;
    return true;
}

void MemoryAllocatorVK::free(AllocationVK& allocation)
{
    if (!allocation.valid())
        return;
    if (allocation.blockIndex < 0)
    {
        _dedicatedTypes.erase(allocation.memory);
        vkFreeMemory(_device, allocation.memory, nullptr);
        allocation = AllocationVK();
        return;
    }

    Block& block = _blocks[allocation.blockIndex];
    VkDeviceSize start = allocation.offset;
    VkDeviceSize size = allocation.size;

    // Merge with the following free range.
    auto next = block.freeRanges.lower_bound(start);
    if (next != block.freeRanges.end() && next->first == start + size)
    {
        size += next->second;
        next = block.freeRanges.erase(next);
    }
    // Merge with the preceding free range.
    if (next != block.freeRanges.begin())
    {
        auto prev = std::prev(next);
        if (prev->first + prev->second == start)
        {
            start = prev->first;
            size += prev->second;
            block.freeRanges.erase(prev);
        }
    }
    block.freeRanges[start] = size;

    // Return fully free blocks to the driver, but keep one per type around
    // to avoid churn.
    if (block.freeRanges.size() == 1 && block.freeRanges.begin()->second == block.size)
    {
        int sameType = 0;
        for (const auto& b : _blocks)
            if (b.memory != VK_NULL_HANDLE && b.memoryType == block.memoryType)
                ++sameType;
        if (sameType > 1)
        {
            vkFreeMemory(_device, block.memory, nullptr);
            block = Block();
        }
    }
    allocation = AllocationVK();
}

bool MemoryAllocatorVK::isCoherent(const AllocationVK& allocation) const
{
    uint32_t type;
    if (allocation.blockIndex >= 0)
        type = _blocks[allocation.blockIndex].memoryType;
    else
    {
        auto it = _dedicatedTypes.find(allocation.memory);
        if (it == _dedicatedTypes.end())
            return true;
        type = it->second;
    }
    return (_memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

CC_BACKEND_END
