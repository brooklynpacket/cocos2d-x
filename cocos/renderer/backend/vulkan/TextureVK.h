/****************************************************************************
 Vulkan backend: textures.

 Every texture rests in SHADER_READ_ONLY_OPTIMAL between operations: uploads
 and render passes transition out of it and back. Uploads are recorded into
 the device's upload command buffer, which runs before the frame's draws.

 GL lets a texture change size, format and mip count after creation; the
 image is recreated when that happens. Mip levels are added on demand the
 first time a level above 0 is uploaded or mipmaps are generated.
 ****************************************************************************/
#pragma once

#include "../Texture.h"
#include "MemoryVK.h"

#include <functional>

CC_BACKEND_BEGIN

class DeviceVK;

// The Vulkan image behind a 2D or cube texture.
class TextureImageVK
{
public:
    TextureImageVK(DeviceVK* device, bool cube);
    ~TextureImageVK();

    /** (Re)creates the image when format, size or usage changed. Contents are lost if it does. */
    void configure(PixelFormat format, uint32_t width, uint32_t height, TextureUsage usage);
    void upload(uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                const uint8_t* data, std::size_t dataLen);
    void generateMipmaps();
    /** Synchronously reads back a region of level 0 / layer 0. */
    void readPixels(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                    const std::function<void(const uint8_t*, std::size_t bytesPerRow)>& callback);

    bool valid() const { return _image != VK_NULL_HANDLE; }
    VkImage getImage() const { return _image; }
    VkFormat getFormat() const { return _info.format; }
    bool isDepth() const { return _info.isDepth; }
    bool hasStencil() const { return _info.hasStencil; }
    uint32_t getWidth() const { return _width; }
    uint32_t getHeight() const { return _height; }
    bool hasMipmaps() const { return _validLevels > 1; }
    /** View for sampling: every level uploaded so far, with the format's swizzle. */
    VkImageView getSampledView();
    /** View for use as a framebuffer attachment: level 0, no swizzle. */
    VkImageView getAttachmentView();

private:
    bool createImage(uint32_t mipLevels);
    void destroyImage();
    void growMipChain();
    void destroyViews();
    VkImageAspectFlags aspect() const;

    DeviceVK* _device = nullptr;
    bool _cube = false;
    PixelFormat _pixelFormat = PixelFormat::NONE;
    TextureUsage _usage = TextureUsage::READ;
    UtilsVK::FormatInfo _info;
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _mipLevels = 1;
    uint32_t _validLevels = 1;

    VkImage _image = VK_NULL_HANDLE;
    AllocationVK _allocation;
    VkImageView _sampledView = VK_NULL_HANDLE;
    uint32_t _sampledViewLevels = 0;
    VkImageView _attachmentView = VK_NULL_HANDLE;
};

class Texture2DVK : public Texture2DBackend
{
public:
    Texture2DVK(DeviceVK* device, const TextureDescriptor& descriptor);
    ~Texture2DVK() override;

    void updateData(uint8_t* data, std::size_t width, std::size_t height, std::size_t level) override;
    void updateCompressedData(uint8_t* data, std::size_t width, std::size_t height, std::size_t dataLen, std::size_t level) override;
    void updateSubData(std::size_t xoffset, std::size_t yoffset, std::size_t width, std::size_t height, std::size_t level, uint8_t* data) override;
    void updateCompressedSubData(std::size_t xoffset, std::size_t yoffset, std::size_t width, std::size_t height, std::size_t dataLen, std::size_t level, uint8_t* data) override;
    void updateSamplerDescriptor(const SamplerDescriptor& sampler) override { _sampler = sampler; }
    void getBytes(std::size_t x, std::size_t y, std::size_t width, std::size_t height, bool flipImage,
                  std::function<void(const unsigned char*, std::size_t, std::size_t)> callback) override;
    void generateMipmaps() override;
    void updateTextureDescriptor(const TextureDescriptor& descriptor) override;

    TextureImageVK& getImage() { return *_image; }
    const SamplerDescriptor& getSamplerDescriptor() const { return _sampler; }

private:
    DeviceVK* _device = nullptr;
    TextureImageVK* _image = nullptr;
    SamplerDescriptor _sampler;
};

class TextureCubeVK : public TextureCubemapBackend
{
public:
    TextureCubeVK(DeviceVK* device, const TextureDescriptor& descriptor);
    ~TextureCubeVK() override;

    void updateFaceData(TextureCubeFace side, void* data) override;
    void updateCompressedFaceData(TextureCubeFace side, void* data, std::size_t dataLen) override;
    void updateSamplerDescriptor(const SamplerDescriptor& sampler) override { _sampler = sampler; }
    void getBytes(std::size_t x, std::size_t y, std::size_t width, std::size_t height, bool flipImage,
                  std::function<void(const unsigned char*, std::size_t, std::size_t)> callback) override;
    void generateMipmaps() override;
    void updateTextureDescriptor(const TextureDescriptor& descriptor) override;

    TextureImageVK& getImage() { return *_image; }
    const SamplerDescriptor& getSamplerDescriptor() const { return _sampler; }

private:
    DeviceVK* _device = nullptr;
    TextureImageVK* _image = nullptr;
    SamplerDescriptor _sampler;
};

// Helpers for code that only has a TextureBackend*.
TextureImageVK* getTextureImageVK(TextureBackend* texture);
const SamplerDescriptor& getSamplerDescriptorVK(TextureBackend* texture);

CC_BACKEND_END
