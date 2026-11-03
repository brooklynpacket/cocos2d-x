/****************************************************************************
 Vulkan backend: a small device memory sub-allocator.

 Android drivers often cap vkAllocateMemory at 4096 live allocations, so
 buffers and images are packed into large blocks. First-fit with coalescing
 is enough for this engine's allocation pattern (mostly long-lived textures
 and a fixed set of streaming buffers).
 ****************************************************************************/
#pragma once

#include "UtilsVK.h"

#include <map>
#include <vector>

CC_BACKEND_BEGIN

struct AllocationVK
{
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped = nullptr; // non-null for host-visible memory
    int blockIndex = -1;    // -1 for dedicated allocations
    bool valid() const { return memory != VK_NULL_HANDLE; }
};

class MemoryAllocatorVK
{
public:
    void init(VkPhysicalDevice physicalDevice, VkDevice device);
    void destroy();

    // hostVisible: memory must be mappable (and is kept mapped). Coherent
    // memory is preferred so writes need no flush.
    bool allocate(const VkMemoryRequirements& requirements, bool hostVisible, AllocationVK& out);
    void free(AllocationVK& allocation);

    bool isCoherent(const AllocationVK& allocation) const;

private:
    struct Block
    {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        uint32_t memoryType = 0;
        void* mapped = nullptr;
        std::map<VkDeviceSize, VkDeviceSize> freeRanges; // offset -> size
    };

    int findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const;
    bool allocateFromBlock(Block& block, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset);

    VkDevice _device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties _memoryProperties{};
    VkDeviceSize _bufferImageGranularity = 1;
    std::vector<Block> _blocks;
    std::map<VkDeviceMemory, uint32_t> _dedicatedTypes;
};

CC_BACKEND_END
