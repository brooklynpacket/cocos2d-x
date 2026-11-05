/****************************************************************************
 Vulkan backend: vertex and index buffers.

 Same semantics as the Metal backend: a DYNAMIC buffer has one copy per
 frame in flight (plus one) and moves to the next copy on its first update
 in a frame, so the CPU never writes memory the GPU may still be reading
 from an earlier frame. Updating the same region twice within one frame
 overwrites it for every draw in that frame, as on Metal; the Renderer
 avoids this by appending batches at increasing offsets.
 ****************************************************************************/
#pragma once

#include "../Buffer.h"
#include "MemoryVK.h"

#include <vector>

CC_BACKEND_BEGIN

class DeviceVK;

class BufferVK : public Buffer
{
public:
    BufferVK(DeviceVK* device, std::size_t size, BufferType type, BufferUsage usage);
    ~BufferVK() override;

    void updateData(void* data, std::size_t size) override;
    void updateSubData(void* data, std::size_t offset, std::size_t size) override;
    void usingDefaultStoredData(bool needDefaultStoredData) override {}

    VkBuffer getVkBuffer() const { return _copies.empty() ? VK_NULL_HANDLE : _copies[_current].buffer; }

private:
    struct Copy
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        AllocationVK allocation;
    };
    void beginWrite();

    DeviceVK* _device = nullptr;
    std::vector<Copy> _copies;
    size_t _current = 0;
    uint64_t _lastWriteFrame = 0;
};

CC_BACKEND_END
