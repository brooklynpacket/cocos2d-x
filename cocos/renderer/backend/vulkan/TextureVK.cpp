/****************************************************************************
 Vulkan backend: textures.
 ****************************************************************************/
#include "TextureVK.h"
#include "DeviceVK.h"

#include <algorithm>
#include <cstring>
#include <vector>

CC_BACKEND_BEGIN

namespace
{
    const VkPipelineStageFlags kShaderStages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    // Anything that may have touched a resting texture before an upload: reads
    // in shaders, render pass writes, earlier transfers.
    const VkPipelineStageFlags kAnyUseStages = kShaderStages | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                               VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkAccessFlags kAnyWriteAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                          VK_ACCESS_TRANSFER_WRITE_BIT;

    void imageBarrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect,
                      uint32_t baseLevel, uint32_t levelCount, uint32_t baseLayer, uint32_t layerCount,
                      VkImageLayout oldLayout, VkImageLayout newLayout,
                      VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
                      VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
    {
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        b.oldLayout = oldLayout;
        b.newLayout = newLayout;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { aspect, baseLevel, levelCount, baseLayer, layerCount };
        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    uint32_t mipSize(uint32_t size, uint32_t level)
    {
        uint32_t s = size >> level;
        return s ? s : 1;
    }
}

// ---------------------------------------------------------------------------
// TextureImageVK

TextureImageVK::TextureImageVK(DeviceVK* device, bool cube)
: _device(device)
, _cube(cube)
{
}

TextureImageVK::~TextureImageVK()
{
    destroyImage();
}

VkImageAspectFlags TextureImageVK::aspect() const
{
    if (!_info.isDepth)
        return VK_IMAGE_ASPECT_COLOR_BIT;
    return _info.hasStencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT;
}

void TextureImageVK::configure(PixelFormat format, uint32_t width, uint32_t height, TextureUsage usage)
{
    if (_image && format == _pixelFormat && width == _width && height == _height && usage == _usage)
        return;
    destroyImage();
    _pixelFormat = format;
    _width = width;
    _height = height;
    _usage = usage;
    _hasContents = false;
    _info = UtilsVK::getFormatInfo(format, _device->getDepthStencilFormat(), _device->getDepthOnlyFormat());
    if (width == 0 || height == 0)
        return;
    if (_info.format == VK_FORMAT_UNDEFINED)
    {
        VK_LOGE("unsupported texture format %d", (int)format);
        return;
    }
    // Render targets and depth images are cleared when created.
    if (createImage(1) && (_info.isDepth || _usage == TextureUsage::RENDER_TARGET))
        _hasContents = true;
}

bool TextureImageVK::createImage(uint32_t mipLevels)
{
    VkDevice vk = _device->getDevice();
    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.flags = _cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = _info.format;
    info.extent = { _width, _height, 1 };
    info.mipLevels = mipLevels;
    info.arrayLayers = _cube ? 6 : 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (_info.isDepth)
        info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    else if (_usage == TextureUsage::RENDER_TARGET)
        info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &info, nullptr, &_image) != VK_SUCCESS)
    {
        VK_LOGE("vkCreateImage %ux%u format %d failed", _width, _height, (int)_info.format);
        _image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk, _image, &req);
    if (!_device->getMemory().allocate(req, false, _allocation))
    {
        vkDestroyImage(vk, _image, nullptr);
        _image = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(vk, _image, _allocation.memory, _allocation.offset);
    _mipLevels = mipLevels;
    _validLevels = 1;

    VkCommandBuffer cb = _device->getUploadCommandBuffer();
    if (_info.isDepth || _usage == TextureUsage::RENDER_TARGET)
    {
        // GL drivers hand out zeroed render targets and the game relies on
        // it (e.g. sampling a render texture before its first draw); Vulkan
        // images start with garbage, so clear them.
        imageBarrier(cb, _image, aspect(), 0, _mipLevels, 0, info.arrayLayers,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageSubresourceRange range{ aspect(), 0, _mipLevels, 0, info.arrayLayers };
        if (_info.isDepth)
        {
            VkClearDepthStencilValue clear{ 1.0f, 0 };
            vkCmdClearDepthStencilImage(cb, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        }
        else
        {
            VkClearColorValue clear{};
            vkCmdClearColorImage(cb, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        }
        imageBarrier(cb, _image, aspect(), 0, _mipLevels, 0, info.arrayLayers,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kAnyUseStages, VK_ACCESS_SHADER_READ_BIT);
        return true;
    }
    imageBarrier(cb, _image, aspect(), 0, _mipLevels, 0, info.arrayLayers,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                 kAnyUseStages, VK_ACCESS_SHADER_READ_BIT);
    return true;
}

void TextureImageVK::destroyViews()
{
    VkDevice vk = _device->getDevice();
    for (VkImageView* view : { &_sampledView, &_attachmentView })
    {
        if (*view)
        {
            _device->forgetImageView(*view);
            VkImageView v = *view;
            _device->deferDestroy([vk, v] { vkDestroyImageView(vk, v, nullptr); });
            *view = VK_NULL_HANDLE;
        }
    }
    _sampledViewLevels = 0;
}

void TextureImageVK::destroyImage()
{
    destroyViews();
    if (!_image)
        return;
    VkDevice vk = _device->getDevice();
    VkImage image = _image;
    AllocationVK allocation = _allocation;
    MemoryAllocatorVK* memory = &_device->getMemory();
    _device->deferDestroy([vk, image, allocation, memory]() mutable {
        vkDestroyImage(vk, image, nullptr);
        memory->free(allocation);
    });
    _image = VK_NULL_HANDLE;
    _allocation = AllocationVK();
    _mipLevels = 1;
    _validLevels = 1;
}

void TextureImageVK::growMipChain()
{
    uint32_t levels = UtilsVK::mipLevelsFor(_width, _height);
    if (levels <= _mipLevels || !_image)
        return;

    VkImage oldImage = _image;
    AllocationVK oldAllocation = _allocation;
    uint32_t oldLevels = _mipLevels;
    uint32_t oldValid = _validLevels;
    destroyViews();
    _image = VK_NULL_HANDLE;
    _allocation = AllocationVK();
    if (!createImage(levels))
    {
        _image = oldImage;
        _allocation = oldAllocation;
        _mipLevels = oldLevels;
        _validLevels = oldValid;
        return;
    }

    // Copy the levels that already have data into the new image.
    uint32_t layers = _cube ? 6 : 1;
    VkCommandBuffer cb = _device->getUploadCommandBuffer();
    imageBarrier(cb, oldImage, aspect(), 0, oldLevels, 0, layers,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 kAnyUseStages, kAnyWriteAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    imageBarrier(cb, _image, aspect(), 0, oldLevels, 0, layers,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 kAnyUseStages, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    for (uint32_t level = 0; level < oldValid; ++level)
    {
        VkImageCopy region{};
        region.srcSubresource = { aspect(), level, 0, layers };
        region.dstSubresource = region.srcSubresource;
        region.extent = { mipSize(_width, level), mipSize(_height, level), 1 };
        vkCmdCopyImage(cb, oldImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    imageBarrier(cb, _image, aspect(), 0, oldLevels, 0, layers,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kAnyUseStages, VK_ACCESS_SHADER_READ_BIT);
    _validLevels = oldValid;

    VkDevice vk = _device->getDevice();
    MemoryAllocatorVK* memory = &_device->getMemory();
    _device->deferDestroy([vk, oldImage, oldAllocation, memory]() mutable {
        vkDestroyImage(vk, oldImage, nullptr);
        memory->free(oldAllocation);
    });
}

void TextureImageVK::upload(uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                            const uint8_t* data, std::size_t dataLen)
{
    if (!_image || !data || width == 0 || height == 0 || _info.isDepth)
        return;
    if (level >= _mipLevels)
        growMipChain();
    if (level >= _mipLevels || layer >= (_cube ? 6u : 1u))
        return;

    // GL rejects uploads outside the level; in Vulkan they corrupt memory or
    // fault the GPU. Clip to the level (compressed regions stay whole blocks).
    const uint32_t levelWidth = mipSize(_width, level);
    const uint32_t levelHeight = mipSize(_height, level);
    if (x >= levelWidth || y >= levelHeight)
    {
        VK_LOGW("texture upload at %u,%u outside %ux%u level %u", x, y, levelWidth, levelHeight, level);
        return;
    }
    if (x + width > levelWidth || y + height > levelHeight)
    {
        VK_LOGW("texture upload %ux%u at %u,%u clipped to %ux%u level %u", width, height, x, y, levelWidth, levelHeight, level);
        if (!_info.compressed && (x + width > levelWidth))
        {
            // Rows are tightly packed, so a narrower copy needs a row stride.
            // Rare enough to just skip rather than repack.
            return;
        }
        if (_info.compressed)
            return;
        height = levelHeight - y;
    }

    // Bytes the image region needs, and how to fill them from the source.
    std::size_t size;
    std::size_t srcSize;
    if (_info.compressed)
    {
        std::size_t blocksX = (width + _info.blockWidth - 1) / _info.blockWidth;
        std::size_t blocksY = (height + _info.blockHeight - 1) / _info.blockHeight;
        size = blocksX * blocksY * _info.bytesPerBlock;
        srcSize = size;
    }
    else
    {
        size = std::size_t(width) * height * _info.bytesPerBlock;
        srcSize = _info.expandRGBToRGBA ? std::size_t(width) * height * 3 : size;
    }
    if (dataLen != 0 && dataLen < srcSize)
    {
        VK_LOGE("texture upload has %zu bytes, needs %zu", dataLen, srcSize);
        return;
    }

    DeviceVK::TransientAllocation staging;
    VkDeviceSize alignment = _info.compressed ? _info.bytesPerBlock : 4;
    if (!_device->allocateStaging(size, alignment, staging))
    {
        VK_LOGE("out of staging memory for %zu byte upload", size);
        return;
    }
    if (_info.expandRGBToRGBA)
    {
        uint8_t* dst = (uint8_t*)staging.mapped;
        std::size_t pixels = std::size_t(width) * height;
        for (std::size_t i = 0; i < pixels; ++i)
        {
            dst[i * 4 + 0] = data[i * 3 + 0];
            dst[i * 4 + 1] = data[i * 3 + 1];
            dst[i * 4 + 2] = data[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
    }
    else
        memcpy(staging.mapped, data, size);

    VkCommandBuffer cb = _device->getUploadCommandBuffer();
    imageBarrier(cb, _image, aspect(), level, 1, layer, 1,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 kAnyUseStages, kAnyWriteAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.bufferOffset = staging.offset;
    region.imageSubresource = { aspect(), level, layer, 1 };
    region.imageOffset = { (int32_t)x, (int32_t)y, 0 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(cb, staging.buffer, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    imageBarrier(cb, _image, aspect(), level, 1, layer, 1,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kAnyUseStages, VK_ACCESS_SHADER_READ_BIT);

    if (level + 1 > _validLevels)
        _validLevels = level + 1;
    _hasContents = true;
}

void TextureImageVK::generateMipmaps()
{
    if (!_image || _info.compressed || _info.isDepth)
        return;
    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (!_device->isFormatSupported(_info.format, needed))
    {
        VK_LOGW("cannot generate mipmaps for format %d", (int)_info.format);
        return;
    }
    growMipChain();
    if (_mipLevels < 2)
        return;

    uint32_t layers = _cube ? 6 : 1;
    VkCommandBuffer cb = _device->getUploadCommandBuffer();
    for (uint32_t i = 1; i < _mipLevels; ++i)
    {
        imageBarrier(cb, _image, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, layers,
                     i == 1 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     kAnyUseStages, kAnyWriteAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        imageBarrier(cb, _image, VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, layers,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     kAnyUseStages, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, layers };
        blit.srcOffsets[1] = { (int32_t)mipSize(_width, i - 1), (int32_t)mipSize(_height, i - 1), 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, layers };
        blit.dstOffsets[1] = { (int32_t)mipSize(_width, i), (int32_t)mipSize(_height, i), 1 };
        vkCmdBlitImage(cb, _image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);
    }
    imageBarrier(cb, _image, VK_IMAGE_ASPECT_COLOR_BIT, 0, _mipLevels - 1, 0, layers,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, kAnyUseStages, VK_ACCESS_SHADER_READ_BIT);
    imageBarrier(cb, _image, VK_IMAGE_ASPECT_COLOR_BIT, _mipLevels - 1, 1, 0, layers,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kAnyUseStages, VK_ACCESS_SHADER_READ_BIT);
    _validLevels = _mipLevels;
}

void TextureImageVK::readPixels(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                const std::function<void(const uint8_t*, std::size_t)>& callback)
{
    if (!_image || _info.compressed || _info.isDepth || width == 0 || height == 0)
    {
        callback(nullptr, 0);
        return;
    }
    std::size_t rowBytes = std::size_t(width) * _info.bytesPerBlock;
    DeviceVK::TransientAllocation staging;
    if (!_device->allocateStaging(rowBytes * height, 4, staging))
    {
        callback(nullptr, 0);
        return;
    }
    VkImage image = _image;
    VkImageAspectFlags asp = aspect();
    _device->executeNow([&](VkCommandBuffer cb) {
        imageBarrier(cb, image, asp, 0, 1, 0, 1,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     kAnyUseStages, kAnyWriteAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.bufferOffset = staging.offset;
        region.imageSubresource = { asp, 0, 0, 1 };
        region.imageOffset = { (int32_t)x, (int32_t)y, 0 };
        region.imageExtent = { width, height, 1 };
        vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
        imageBarrier(cb, image, asp, 0, 1, 0, 1,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, kAnyUseStages, VK_ACCESS_SHADER_READ_BIT);
        VkMemoryBarrier host{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    });
    callback((const uint8_t*)staging.mapped, rowBytes);
}

VkImageView TextureImageVK::getSampledView()
{
    if (!_image)
        return VK_NULL_HANDLE;
    if (_sampledView && _sampledViewLevels == _validLevels)
        return _sampledView;
    if (_sampledView)
    {
        _device->forgetImageView(_sampledView);
        VkDevice vk = _device->getDevice();
        VkImageView v = _sampledView;
        _device->deferDestroy([vk, v] { vkDestroyImageView(vk, v, nullptr); });
        _sampledView = VK_NULL_HANDLE;
    }
    VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    info.image = _image;
    info.viewType = _cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    info.format = _info.format;
    info.components = _info.swizzle;
    // Samplers read depth only, even from depth/stencil images.
    info.subresourceRange = { _info.isDepth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT,
                              0, _validLevels, 0, _cube ? 6u : 1u };
    VK_CHECK(vkCreateImageView(_device->getDevice(), &info, nullptr, &_sampledView));
    _sampledViewLevels = _validLevels;
    return _sampledView;
}

VkImageView TextureImageVK::getAttachmentView()
{
    if (!_image)
        return VK_NULL_HANDLE;
    if (_attachmentView)
        return _attachmentView;
    VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    info.image = _image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = _info.format;
    info.subresourceRange = { aspect(), 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(_device->getDevice(), &info, nullptr, &_attachmentView));
    return _attachmentView;
}

// ---------------------------------------------------------------------------
// Texture2DVK

Texture2DVK::Texture2DVK(DeviceVK* device, const TextureDescriptor& descriptor)
: Texture2DBackend(descriptor)
, _device(device)
, _sampler(descriptor.samplerDescriptor)
{
    _image = new TextureImageVK(device, false);
    _image->configure(_textureFormat, _width, _height, _textureUsage);
}

Texture2DVK::~Texture2DVK()
{
    delete _image;
}

void Texture2DVK::updateTextureDescriptor(const TextureDescriptor& descriptor)
{
    TextureBackend::updateTextureDescriptor(descriptor);
    _width = descriptor.width;
    _height = descriptor.height;
    _sampler = descriptor.samplerDescriptor;
    if (_image)
        _image->configure(_textureFormat, _width, _height, _textureUsage);
}

void Texture2DVK::updateData(uint8_t* data, std::size_t width, std::size_t height, std::size_t level)
{
    // Like glTexImage2D, a level 0 upload of a new size respecifies the texture.
    if (level == 0 && (width != _width || height != _height || !_image->valid()))
    {
        _width = (uint32_t)width;
        _height = (uint32_t)height;
        _image->configure(_textureFormat, _width, _height, _textureUsage);
    }
    _image->upload((uint32_t)level, 0, 0, 0, (uint32_t)width, (uint32_t)height, data, 0);
    if (level > 0)
        _hasMipmaps = true;
}

void Texture2DVK::updateCompressedData(uint8_t* data, std::size_t width, std::size_t height, std::size_t dataLen, std::size_t level)
{
    if (level == 0 && (width != _width || height != _height || !_image->valid()))
    {
        _width = (uint32_t)width;
        _height = (uint32_t)height;
        _image->configure(_textureFormat, _width, _height, _textureUsage);
    }
    _image->upload((uint32_t)level, 0, 0, 0, (uint32_t)width, (uint32_t)height, data, dataLen);
    if (level > 0)
        _hasMipmaps = true;
}

void Texture2DVK::updateSubData(std::size_t xoffset, std::size_t yoffset, std::size_t width, std::size_t height, std::size_t level, uint8_t* data)
{
    _image->upload((uint32_t)level, 0, (uint32_t)xoffset, (uint32_t)yoffset, (uint32_t)width, (uint32_t)height, data, 0);
}

void Texture2DVK::updateCompressedSubData(std::size_t xoffset, std::size_t yoffset, std::size_t width, std::size_t height,
                                          std::size_t dataLen, std::size_t level, uint8_t* data)
{
    _image->upload((uint32_t)level, 0, (uint32_t)xoffset, (uint32_t)yoffset, (uint32_t)width, (uint32_t)height, data, dataLen);
}

void Texture2DVK::generateMipmaps()
{
    if (_textureUsage == TextureUsage::RENDER_TARGET)
        return;
    _image->generateMipmaps();
    _hasMipmaps = _image->hasMipmaps();
}

void Texture2DVK::getBytes(std::size_t x, std::size_t y, std::size_t width, std::size_t height, bool flipImage,
                           std::function<void(const unsigned char*, std::size_t, std::size_t)> callback)
{
    _image->readPixels((uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height,
                       [&](const uint8_t* pixels, std::size_t rowBytes) {
        if (!pixels)
        {
            callback(nullptr, 0, 0);
            return;
        }
        // Same as the Metal backend: rows come back top first, and are
        // flipped to GL's bottom-first order unless flipImage is set.
        if (flipImage)
        {
            callback(pixels, width, height);
            return;
        }
        std::vector<unsigned char> flipped(rowBytes * height);
        for (std::size_t row = 0; row < height; ++row)
            memcpy(&flipped[row * rowBytes], pixels + (height - row - 1) * rowBytes, rowBytes);
        callback(flipped.data(), width, height);
    });
}

// ---------------------------------------------------------------------------
// TextureCubeVK

TextureCubeVK::TextureCubeVK(DeviceVK* device, const TextureDescriptor& descriptor)
: TextureCubemapBackend(descriptor)
, _device(device)
, _sampler(descriptor.samplerDescriptor)
{
    _image = new TextureImageVK(device, true);
    _image->configure(_textureFormat, _width, _height, _textureUsage);
}

TextureCubeVK::~TextureCubeVK()
{
    delete _image;
}

void TextureCubeVK::updateTextureDescriptor(const TextureDescriptor& descriptor)
{
    TextureBackend::updateTextureDescriptor(descriptor);
    _width = descriptor.width;
    _height = descriptor.height;
    _sampler = descriptor.samplerDescriptor;
    if (_image)
        _image->configure(_textureFormat, _width, _height, _textureUsage);
}

void TextureCubeVK::updateFaceData(TextureCubeFace side, void* data)
{
    _image->upload(0, (uint32_t)side, 0, 0, _width, _height, (const uint8_t*)data, 0);
}

void TextureCubeVK::updateCompressedFaceData(TextureCubeFace side, void* data, std::size_t dataLen)
{
    _image->upload(0, (uint32_t)side, 0, 0, _width, _height, (const uint8_t*)data, dataLen);
}

void TextureCubeVK::generateMipmaps()
{
    _image->generateMipmaps();
    _hasMipmaps = _image->hasMipmaps();
}

void TextureCubeVK::getBytes(std::size_t x, std::size_t y, std::size_t width, std::size_t height, bool flipImage,
                             std::function<void(const unsigned char*, std::size_t, std::size_t)> callback)
{
    _image->readPixels((uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height,
                       [&](const uint8_t* pixels, std::size_t) { callback(pixels, pixels ? width : 0, pixels ? height : 0); });
}

// ---------------------------------------------------------------------------

TextureImageVK* getTextureImageVK(TextureBackend* texture)
{
    if (!texture)
        return nullptr;
    if (texture->getTextureType() == TextureType::TEXTURE_CUBE)
        return &static_cast<TextureCubeVK*>(texture)->getImage();
    return &static_cast<Texture2DVK*>(texture)->getImage();
}

const SamplerDescriptor& getSamplerDescriptorVK(TextureBackend* texture)
{
    if (texture->getTextureType() == TextureType::TEXTURE_CUBE)
        return static_cast<TextureCubeVK*>(texture)->getSamplerDescriptor();
    return static_cast<Texture2DVK*>(texture)->getSamplerDescriptor();
}

CC_BACKEND_END
