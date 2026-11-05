/****************************************************************************
 Vulkan backend: device capabilities.
 ****************************************************************************/
#include "DeviceInfoVK.h"
#include "DeviceVK.h"

#include <cstdio>

CC_BACKEND_BEGIN

DeviceInfoVK::DeviceInfoVK(DeviceVK* device)
: _device(device)
{
}

bool DeviceInfoVK::init()
{
    const auto& props = _device->getProperties();
    const auto& features = _device->getFeatures();

    _maxAttributes = (int)props.limits.maxVertexInputAttributes;
    _maxTextureSize = (int)props.limits.maxImageDimension2D;
    _maxTextureUnits = (int)props.limits.maxPerStageDescriptorSamplers;
    _maxSamplesAllowed = 4;

    const VkFormatFeatureFlags sampled = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    _etc2 = features.textureCompressionETC2 && _device->isFormatSupported(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, sampled);
    _astc = features.textureCompressionASTC_LDR && _device->isFormatSupported(VK_FORMAT_ASTC_6x5_UNORM_BLOCK, sampled);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "0x%04x", props.vendorID);
    _vendor = buffer;
    _renderer = props.deviceName;
    snprintf(buffer, sizeof(buffer), "Vulkan %u.%u.%u (driver 0x%x)",
             VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion),
             props.driverVersion);
    _version = buffer;

    // Everything here is core in Vulkan or emulated by the backend.
    _extensions =
        "GL_OES_standard_derivatives GL_EXT_shadow_samplers GL_OES_depth_texture GL_OES_packed_depth_stencil "
        "GL_OES_depth24 GL_EXT_texture_rg GL_OES_texture_half_float GL_OES_texture_half_float_linear "
        "GL_EXT_color_buffer_half_float GL_OES_element_index_uint GL_EXT_discard_framebuffer "
        "GL_OES_vertex_array_object GL_OES_mapbuffer GL_OES_rgb8_rgba8 GL_EXT_shader_texture_lod "
        "GL_EXT_frag_depth GL_IMG_texture_format_BGRA8888 GL_EXT_texture_format_BGRA8888";
    if (_etc2)
        _extensions += " GL_OES_compressed_ETC1_RGB8_texture";
    if (_astc)
        _extensions += " GL_KHR_texture_compression_astc_ldr";
    return true;
}

bool DeviceInfoVK::checkForFeatureSupported(FeatureType feature)
{
    switch (feature)
    {
    case FeatureType::ETC1:
        return _etc2;
    case FeatureType::ASTC:
        return _astc;
    case FeatureType::S3TC:
    case FeatureType::AMD_COMPRESSED_ATC:
    case FeatureType::PVRTC:
        return false;
    case FeatureType::IMG_FORMAT_BGRA8888:
    case FeatureType::DISCARD_FRAMEBUFFER:
    case FeatureType::PACKED_DEPTH_STENCIL:
    case FeatureType::VAO:
    case FeatureType::MAPBUFFER:
    case FeatureType::DEPTH24:
        return true;
    default:
        return false;
    }
}

CC_BACKEND_END
