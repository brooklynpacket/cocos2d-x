/****************************************************************************
 Vulkan backend: vertex and index buffers.
 ****************************************************************************/
#include "BufferVK.h"
#include "DeviceVK.h"

#include <cstring>

CC_BACKEND_BEGIN

BufferVK::BufferVK(DeviceVK* device, std::size_t size, BufferType type, BufferUsage usage)
: Buffer(size, type, usage)
, _device(device)
{
    size_t copies = usage == BufferUsage::DYNAMIC ? DeviceVK::kFramesInFlight + 1 : 1;
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size > 0 ? size : 4;
    info.usage = type == BufferType::INDEX ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice vk = device->getDevice();
    for (size_t i = 0; i < copies; ++i)
    {
        Copy copy;
        if (vkCreateBuffer(vk, &info, nullptr, &copy.buffer) != VK_SUCCESS)
            break;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(vk, copy.buffer, &req);
        // Mobile GPUs share memory with the CPU, so host-visible buffers cost
        // nothing extra to read and avoid staging copies.
        if (!device->getMemory().allocate(req, true, copy.allocation) || !copy.allocation.mapped)
        {
            vkDestroyBuffer(vk, copy.buffer, nullptr);
            VK_LOGE("failed to allocate %zu byte buffer", size);
            break;
        }
        vkBindBufferMemory(vk, copy.buffer, copy.allocation.memory, copy.allocation.offset);
        memset(copy.allocation.mapped, 0, size);
        _copies.push_back(copy);
    }
}

BufferVK::~BufferVK()
{
    VkDevice vk = _device->getDevice();
    MemoryAllocatorVK* memory = &_device->getMemory();
    std::vector<Copy> copies = std::move(_copies);
    _device->deferDestroy([vk, memory, copies]() mutable {
        for (auto& copy : copies)
        {
            vkDestroyBuffer(vk, copy.buffer, nullptr);
            memory->free(copy.allocation);
        }
    });
}

void BufferVK::beginWrite()
{
    if (_copies.size() > 1 && _lastWriteFrame != _device->getFrameSerial())
    {
        _current = (_current + 1) % _copies.size();
        _lastWriteFrame = _device->getFrameSerial();
    }
}

void BufferVK::updateData(void* data, std::size_t size)
{
    updateSubData(data, 0, size);
}

void BufferVK::updateSubData(void* data, std::size_t offset, std::size_t size)
{
    if (_copies.empty() || !data || size == 0)
        return;
    if (offset + size > _size)
    {
        VK_LOGE("buffer update out of range: %zu + %zu > %zu", offset, size, _size);
        if (offset >= _size)
            return;
        size = _size - offset;
    }
    beginWrite();
    memcpy((char*)_copies[_current].allocation.mapped + offset, data, size);
}

CC_BACKEND_END
