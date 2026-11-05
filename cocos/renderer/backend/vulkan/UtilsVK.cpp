/****************************************************************************
 Vulkan backend: shared helpers and enum conversions.
 ****************************************************************************/
#include "UtilsVK.h"

CC_BACKEND_BEGIN

namespace UtilsVK
{

FormatInfo getFormatInfo(PixelFormat format, VkFormat depthStencilFormat, VkFormat depthOnlyFormat)
{
    FormatInfo info;
    const auto Z = VK_COMPONENT_SWIZZLE_ZERO;
    const auto ONE = VK_COMPONENT_SWIZZLE_ONE;
    const auto R = VK_COMPONENT_SWIZZLE_R;
    const auto G = VK_COMPONENT_SWIZZLE_G;
    switch (format)
    {
    case PixelFormat::RGBA8888:
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case PixelFormat::BGRA8888:
        info.format = VK_FORMAT_B8G8R8A8_UNORM;
        break;
    case PixelFormat::RGB888:
        // 24-bit formats are rarely sampleable on mobile; expand on upload.
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.expandRGBToRGBA = true;
        break;
    case PixelFormat::RGB565:
        // GL_UNSIGNED_SHORT_5_6_5 has the same bit layout.
        info.format = VK_FORMAT_R5G6B5_UNORM_PACK16;
        info.bytesPerBlock = 2;
        break;
    case PixelFormat::RGBA4444:
        info.format = VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        info.bytesPerBlock = 2;
        break;
    case PixelFormat::RGB5A1:
        info.format = VK_FORMAT_R5G5B5A1_UNORM_PACK16;
        info.bytesPerBlock = 2;
        break;
    case PixelFormat::A8:
        info.format = VK_FORMAT_R8_UNORM;
        info.swizzle = { Z, Z, Z, R };
        info.bytesPerBlock = 1;
        break;
    case PixelFormat::I8:
        info.format = VK_FORMAT_R8_UNORM;
        info.swizzle = { R, R, R, ONE };
        info.bytesPerBlock = 1;
        break;
    case PixelFormat::AI88:
        info.format = VK_FORMAT_R8G8_UNORM;
        info.swizzle = { R, R, R, G };
        info.bytesPerBlock = 2;
        break;
    case PixelFormat::RG16F:
        info.format = VK_FORMAT_R16G16_SFLOAT;
        break;
    case PixelFormat::ETC:
        // ETC1 data is valid ETC2 RGB data.
        info.format = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        info.compressed = true;
        info.blockWidth = info.blockHeight = 4;
        info.bytesPerBlock = 8;
        break;
    case PixelFormat::ASTC_RGBA:
        // Matches the GL and Metal backends.
        info.format = VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
        info.compressed = true;
        info.blockWidth = 6;
        info.blockHeight = 5;
        info.bytesPerBlock = 16;
        break;
    case PixelFormat::D24S8:
    case PixelFormat::DEPTH_STENCIL_TEXTURE:
        info.format = depthStencilFormat;
        info.isDepth = true;
        info.hasStencil = true;
        break;
    case PixelFormat::DEPTH_TEXTURE:
        info.format = depthOnlyFormat;
        info.isDepth = true;
        break;
    default:
        // PVRTC, S3TC and ATC have no Vulkan equivalent on Android; the
        // DeviceInfo reports them unsupported so assets fall back.
        info.format = VK_FORMAT_UNDEFINED;
        break;
    }
    return info;
}

VkCompareOp toCompareOp(CompareFunction func)
{
    switch (func)
    {
    case CompareFunction::NEVER: return VK_COMPARE_OP_NEVER;
    case CompareFunction::LESS: return VK_COMPARE_OP_LESS;
    case CompareFunction::LESS_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareFunction::GREATER: return VK_COMPARE_OP_GREATER;
    case CompareFunction::GREATER_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareFunction::EQUAL: return VK_COMPARE_OP_EQUAL;
    case CompareFunction::NOT_EQUAL: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareFunction::ALWAYS: default: return VK_COMPARE_OP_ALWAYS;
    }
}

VkStencilOp toStencilOp(StencilOperation op)
{
    switch (op)
    {
    case StencilOperation::KEEP: return VK_STENCIL_OP_KEEP;
    case StencilOperation::ZERO: return VK_STENCIL_OP_ZERO;
    case StencilOperation::REPLACE: return VK_STENCIL_OP_REPLACE;
    case StencilOperation::INVERT: return VK_STENCIL_OP_INVERT;
    case StencilOperation::INCREMENT_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOperation::DECREMENT_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;
    }
}

VkBlendFactor toBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::ZERO: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::ONE: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::CONSTANT_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case BlendFactor::SRC_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case BlendFactor::ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case BlendFactor::BLEND_CLOLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp toBlendOp(BlendOperation op)
{
    switch (op)
    {
    case BlendOperation::SUBTRACT: return VK_BLEND_OP_SUBTRACT;
    case BlendOperation::RESERVE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOperation::ADD: default: return VK_BLEND_OP_ADD;
    }
}

VkColorComponentFlags toColorWriteMask(ColorWriteMask mask)
{
    // The cocos bits match Vulkan's R=1, G=2, B=4, A=8.
    return (VkColorComponentFlags)((uint32_t)mask & 0xF);
}

VkPrimitiveTopology toTopology(PrimitiveType type)
{
    switch (type)
    {
    case PrimitiveType::POINT: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveType::LINE: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveType::LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveType::TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveType::TRIANGLE: default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkCullModeFlags toCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::BACK: return VK_CULL_MODE_BACK_BIT;
    case CullMode::FRONT: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::NONE: default: return VK_CULL_MODE_NONE;
    }
}

VkFormat toVertexFormat(VertexFormat format, bool normalized)
{
    switch (format)
    {
    case VertexFormat::FLOAT4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::FLOAT3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::FLOAT2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::FLOAT: return VK_FORMAT_R32_SFLOAT;
    // GL converts 32-bit integer attributes to float shader inputs, but
    // Vulkan has no 32-bit SSCALED formats. These only work with integer
    // shader inputs, which GLSL ES 1.00 cannot declare; nothing in the
    // engine or game uses them today.
    case VertexFormat::INT4: return VK_FORMAT_R32G32B32A32_SINT;
    case VertexFormat::INT3: return VK_FORMAT_R32G32B32_SINT;
    case VertexFormat::INT2: return VK_FORMAT_R32G32_SINT;
    case VertexFormat::INT: return VK_FORMAT_R32_SINT;
    case VertexFormat::USHORT4: return normalized ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R16G16B16A16_USCALED;
    case VertexFormat::USHORT2: return normalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_USCALED;
    case VertexFormat::UBYTE4: return normalized ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_USCALED;
    default: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
}

VkFilter toFilter(SamplerFilter filter)
{
    switch (filter)
    {
    case SamplerFilter::NEAREST:
    case SamplerFilter::NEAREST_MIPMAP_NEAREST:
    case SamplerFilter::NEAREST_MIPMAP_LINEAR:
        return VK_FILTER_NEAREST;
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode toMipmapMode(SamplerFilter filter, bool& usesMipmaps)
{
    switch (filter)
    {
    case SamplerFilter::NEAREST_MIPMAP_NEAREST:
    case SamplerFilter::LINEAR_MIPMAP_NEAREST:
        usesMipmaps = true;
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case SamplerFilter::NEAREST_MIPMAP_LINEAR:
    case SamplerFilter::LINEAR_MIPMAP_LINEAR:
        usesMipmaps = true;
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    default:
        usesMipmaps = false;
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
}

VkSamplerAddressMode toAddressMode(SamplerAddressMode mode)
{
    switch (mode)
    {
    case SamplerAddressMode::REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::MIRROR_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::CLAMP_TO_EDGE:
    default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

} // namespace UtilsVK

CC_BACKEND_END
