/****************************************************************************
 Vulkan backend: shared helpers and enum conversions.
 ****************************************************************************/
#pragma once

#include <vulkan/vulkan.h>

#include "../Macros.h"
#include "../Types.h"

#include <android/log.h>

#define VK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "cocos2d-vulkan", __VA_ARGS__)
#define VK_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "cocos2d-vulkan", __VA_ARGS__)
#define VK_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "cocos2d-vulkan", __VA_ARGS__)

#define VK_CHECK(expr)                                                                   \
    do {                                                                                 \
        VkResult vkCheckResult_ = (expr);                                                \
        if (vkCheckResult_ != VK_SUCCESS)                                                \
            VK_LOGE("%s failed: %d (%s:%d)", #expr, (int)vkCheckResult_, __FILE__, __LINE__); \
    } while (0)

CC_BACKEND_BEGIN

namespace UtilsVK
{
    // How a cocos PixelFormat is stored in a Vulkan image. Formats Vulkan
    // lacks are emulated: RGB888 is expanded to RGBA on upload, and the GL
    // alpha/luminance formats use a single/dual channel image with a swizzle.
    struct FormatInfo
    {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkComponentMapping swizzle = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        bool expandRGBToRGBA = false;
        bool compressed = false;
        uint32_t blockWidth = 1;
        uint32_t blockHeight = 1;
        uint32_t bytesPerBlock = 4; // bytes per texel for uncompressed formats
        bool isDepth = false;
        bool hasStencil = false;
    };

    // depthStencilFormat/depthOnlyFormat are chosen per device at startup.
    FormatInfo getFormatInfo(PixelFormat format, VkFormat depthStencilFormat, VkFormat depthOnlyFormat);

    VkCompareOp toCompareOp(CompareFunction func);
    VkStencilOp toStencilOp(StencilOperation op);
    VkBlendFactor toBlendFactor(BlendFactor factor);
    VkBlendOp toBlendOp(BlendOperation op);
    VkColorComponentFlags toColorWriteMask(ColorWriteMask mask);
    VkPrimitiveTopology toTopology(PrimitiveType type);
    VkCullModeFlags toCullMode(CullMode mode);
    VkFormat toVertexFormat(VertexFormat format, bool normalized);
    VkFilter toFilter(SamplerFilter filter);
    VkSamplerMipmapMode toMipmapMode(SamplerFilter filter, bool& usesMipmaps);
    VkSamplerAddressMode toAddressMode(SamplerAddressMode mode);

    inline uint32_t mipLevelsFor(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        uint32_t size = width > height ? width : height;
        while (size > 1) { size >>= 1; ++levels; }
        return levels;
    }

    inline VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
    }

    // Combines hashes; used for pipeline/render pass/sampler cache keys.
    inline void hashCombine(std::size_t& seed, std::size_t value)
    {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    }
}

CC_BACKEND_END
