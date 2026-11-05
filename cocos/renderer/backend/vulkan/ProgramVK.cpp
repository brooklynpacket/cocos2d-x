/****************************************************************************
 Vulkan backend: programs.
 ****************************************************************************/
#include "ProgramVK.h"
#include "DeviceVK.h"

#include "platform/CCFileUtils.h"
#include "base/ccMacros.h"
#include "xxhash.h"

#include <atomic>
#include <cstdio>
#include <cstring>

CC_BACKEND_BEGIN

namespace
{
    std::atomic<uint32_t> s_nextProgramId{ 1 };

    const char* const kBuiltinUniformNames[UNIFORM_MAX] = {
        UNIFORM_NAME_MVP_MATRIX,
        UNIFORM_NAME_TEXTURE,
        UNIFORM_NAME_TEXTURE1,
        UNIFORM_NAME_TEXTURE2,
        UNIFORM_NAME_TEXTURE3,
        UNIFORM_NAME_TEXT_COLOR,
        UNIFORM_NAME_EFFECT_TYPE,
        UNIFORM_NAME_EFFECT_COLOR,
        UNIFORM_NAME_BPC_COLOR,
        UNIFORM_NAME_BPC_ALPHA,
        UNIFORM_NAME_BPC_MATRIX_PALETTE,
        UNIFORM_NAME_BPC_WORLD_VIEW,
        UNIFORM_NAME_BPC_CAMERA_POSITION,
        UNIFORM_NAME_TIME,
    };

    const char* const kBuiltinAttributeNames[ATTRIBUTE_MAX] = {
        ATTRIBUTE_NAME_POSITION,
        ATTRIBUTE_NAME_COLOR,
        ATTRIBUTE_NAME_TEXCOORD,
        ATTRIBUTE_NAME_TEXCOORD1,
        ATTRIBUTE_NAME_TEXCOORD2,
        ATTRIBUTE_NAME_TEXCOORD3,
    };

    std::string cachePath(const std::string& vs, const std::string& fs)
    {
        char name[96];
        snprintf(name, sizeof(name), "%08x%08x%08x%08x_%u.spv",
                 XXH32(vs.data(), (int)vs.size(), 0x5eed), XXH32(fs.data(), (int)fs.size(), 0x5eed),
                 XXH32(vs.data(), (int)vs.size(), 0xbeef), XXH32(fs.data(), (int)fs.size(), 0xbeef),
                 vkshader::kCompilerVersion);
        return FileUtils::getInstance()->getWritablePath() + "vkshaders/" + name;
    }
}

ProgramVK::ProgramVK(DeviceVK* device, const std::string& vertexShader, const std::string& fragmentShader, Program::CompileResult& result)
: Program(vertexShader, fragmentShader)
, _device(device)
, _id(s_nextProgramId++)
{
    std::fill(_builtinAttributes, _builtinAttributes + ATTRIBUTE_MAX, -1);
    if (!compile(vertexShader, fragmentShader, result))
        return;
    buildTables();
    if (!createVulkanObjects())
    {
        result.success = false;
        result.errorMsg = "failed to create Vulkan pipeline layout";
    }
}

ProgramVK::~ProgramVK()
{
    _device->forgetProgram(_id);
    VkDevice vk = _device->getDevice();
    VkShaderModule vs = _vertexModule, fs = _fragmentModule;
    VkDescriptorSetLayout set = _setLayout;
    VkPipelineLayout layout = _pipelineLayout;
    _device->deferDestroy([vk, vs, fs, set, layout] {
        if (vs) vkDestroyShaderModule(vk, vs, nullptr);
        if (fs) vkDestroyShaderModule(vk, fs, nullptr);
        if (layout) vkDestroyPipelineLayout(vk, layout, nullptr);
        if (set) vkDestroyDescriptorSetLayout(vk, set, nullptr);
    });
}

bool ProgramVK::compile(const std::string& vertexShader, const std::string& fragmentShader, Program::CompileResult& result)
{
    auto fileUtils = FileUtils::getInstance();
    std::string path = cachePath(vertexShader, fragmentShader);
    if (fileUtils->isFileExist(path))
    {
        Data data = fileUtils->getDataFromFile(path);
        std::string blob((const char*)data.getBytes(), data.getSize());
        if (_compiled.deserialize(blob))
            return true;
        fileUtils->removeFile(path);
    }

    std::string log;
    if (!vkshader::compileProgram(vertexShader, fragmentShader, _compiled, log))
    {
        result.success = false;
        result.errorMsg = log;
        result.filename = log.compare(0, 8, "[vertex]") == 0 ? "vert" : "frag";
        CCLOG("cocos2d: Vulkan shader compile failed: %s", log.c_str());
        return false;
    }

    std::string dir = fileUtils->getWritablePath() + "vkshaders/";
    if (!fileUtils->isDirectoryExist(dir))
        fileUtils->createDirectory(dir);
    std::string blob = _compiled.serialize();
    Data data;
    data.copy((const unsigned char*)blob.data(), (ssize_t)blob.size());
    fileUtils->writeDataToFile(data, path);
    return true;
}

void ProgramVK::buildTables()
{
    const auto& uniforms = _compiled.uniforms;
    const auto& samplers = _compiled.samplers;
    _uniformInfoByLocation.resize(uniforms.size() + samplers.size());

    for (size_t i = 0; i < uniforms.size(); ++i)
    {
        const auto& u = uniforms[i];
        UniformInfo info;
        info.count = (int)u.arraySize;
        info.location = (int)i;
        info.type = u.glType;
        info.isArray = u.isArray;
        info.size = vkshader::packedSizeOf(u.glType);
        info.bufferOffset = u.offset;
        info.isMatrix = vkshader::matrixColumns(u.glType) != 0;
        info.needConvert = info.isMatrix || (u.isArray && u.arrayStride != info.size);
        _uniformInfos[u.name] = info;
        _uniformInfoByLocation[i] = info;
    }
    for (size_t j = 0; j < samplers.size(); ++j)
    {
        const auto& s = samplers[j];
        UniformInfo info;
        info.count = (int)s.arraySize;
        info.location = (int)(uniforms.size() + j);
        info.type = s.glType;
        info.isArray = s.arraySize > 1;
        info.size = 4;
        info.isSampler = true;
        _uniformInfos[s.name] = info;
        _uniformInfoByLocation[info.location] = info;

        SamplerSlot slot;
        slot.binding = s.binding;
        slot.arraySize = s.arraySize;
        slot.cube = s.glType == vkshader::GLT_SAMPLER_CUBE;
        slot.shadow = s.glType == vkshader::GLT_SAMPLER_2D_SHADOW;
        _samplers.push_back(slot);
    }

    for (int i = 0; i < UNIFORM_MAX; ++i)
        _builtinUniforms[i] = getUniformLocation(std::string(kBuiltinUniformNames[i]));
    for (int i = 0; i < ATTRIBUTE_MAX; ++i)
        _builtinAttributes[i] = getAttributeLocation(std::string(kBuiltinAttributeNames[i]));
}

bool ProgramVK::createVulkanObjects()
{
    VkDevice vk = _device->getDevice();
    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = _compiled.vertexSpirv.size() * 4;
    info.pCode = _compiled.vertexSpirv.data();
    if (vkCreateShaderModule(vk, &info, nullptr, &_vertexModule) != VK_SUCCESS)
        return false;
    info.codeSize = _compiled.fragmentSpirv.size() * 4;
    info.pCode = _compiled.fragmentSpirv.data();
    if (vkCreateShaderModule(vk, &info, nullptr, &_fragmentModule) != VK_SUCCESS)
        return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (_compiled.uniformBlockSize > 0)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = _compiled.uniformBlockBinding;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = stages;
        bindings.push_back(b);
    }
    for (const auto& s : _samplers)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = s.binding;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = s.arraySize;
        b.stageFlags = stages;
        bindings.push_back(b);
    }
    VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    setInfo.bindingCount = (uint32_t)bindings.size();
    setInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(vk, &setInfo, nullptr, &_setLayout) != VK_SUCCESS)
        return false;

    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &_setLayout;
    if (vkCreatePipelineLayout(vk, &layoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        _pipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

UniformLocation ProgramVK::getUniformLocation(const std::string& uniform) const
{
    UniformLocation location;
    auto it = _uniformInfos.find(uniform);
    if (it != _uniformInfos.end())
    {
        location.location[0] = it->second.location;
        location.location[1] = it->second.isSampler ? 0 : (int)it->second.bufferOffset;
    }
    return location;
}

UniformLocation ProgramVK::getUniformLocation(backend::Uniform name) const
{
    return _builtinUniforms[name];
}

int ProgramVK::getAttributeLocation(const std::string& name) const
{
    for (const auto& a : _compiled.attributes)
        if (a.name == name)
            return (int)a.location;
    return -1;
}

int ProgramVK::getAttributeLocation(backend::Attribute name) const
{
    return _builtinAttributes[name];
}

const std::unordered_map<std::string, AttributeBindInfo> ProgramVK::getActiveAttributes() const
{
    std::unordered_map<std::string, AttributeBindInfo> attributes;
    for (const auto& a : _compiled.attributes)
    {
        AttributeBindInfo info;
        info.attributeName = a.name;
        info.location = (int)a.location;
        info.type = (int)a.glType;
        info.size = (int)vkshader::packedSizeOf(a.glType);
        attributes[a.name] = info;
    }
    return attributes;
}

std::size_t ProgramVK::getUniformBufferSize(ShaderStage stage) const
{
    // Everything lives in the vertex-stage buffer, as on GL.
    return stage == ShaderStage::FRAGMENT ? 0 : _compiled.uniformBlockSize;
}

const UniformInfo& ProgramVK::getActiveUniformInfo(ShaderStage stage, int location) const
{
    static const UniformInfo empty;
    if (location < 0 || location >= (int)_uniformInfoByLocation.size())
        return empty;
    return _uniformInfoByLocation[location];
}

const std::unordered_map<std::string, UniformInfo>& ProgramVK::getAllActiveUniformInfo(ShaderStage stage) const
{
    return _uniformInfos;
}

#if CC_ENABLE_CACHE_TEXTURE_DATA
const std::unordered_map<std::string, int> ProgramVK::getAllUniformsLocation() const
{
    std::unordered_map<std::string, int> locations;
    for (const auto& it : _uniformInfos)
        locations[it.first] = it.second.location;
    return locations;
}
#endif

void ProgramVK::writeUniform(int location, std::size_t offset, const void* data, std::size_t size, char* buffer) const
{
    if (location < 0 || location >= (int)_compiled.uniforms.size() || !data || !buffer)
        return;
    const auto& u = _compiled.uniforms[location];
    const std::size_t blockSize = _compiled.uniformBlockSize;
    const uint32_t packed = vkshader::packedSizeOf(u.glType);
    const uint32_t columns = vkshader::matrixColumns(u.glType);
    const char* src = (const char*)data;

    if (packed == 0)
        return;

    // Fast path: the packed and std140 layouts agree.
    if (!columns && (!u.isArray || u.arrayStride == packed))
    {
        std::size_t n = size;
        if (u.offset + n > blockSize)
            n = u.offset < blockSize ? blockSize - u.offset : 0;
        memcpy(buffer + u.offset, src, n);
        return;
    }

    std::size_t count = size / packed;
    if (!u.isArray && count > 1)
        count = 1;
    const std::size_t stride = u.isArray ? u.arrayStride : 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        std::size_t dst = u.offset + i * stride;
        if (columns)
        {
            // std140 pads every matrix column to 16 bytes.
            const std::size_t columnBytes = columns * 4;
            for (uint32_t c = 0; c < columns; ++c)
            {
                std::size_t d = dst + c * 16;
                if (d + columnBytes > blockSize)
                    return;
                memcpy(buffer + d, src + (i * columns + c) * columnBytes, columnBytes);
            }
        }
        else
        {
            if (dst + packed > blockSize)
                return;
            memcpy(buffer + dst, src + i * packed, packed);
        }
    }
}

const ProgramVK::SamplerSlot* ProgramVK::getSamplerForLocation(int location) const
{
    int index = location - (int)_compiled.uniforms.size();
    if (index < 0 || index >= (int)_samplers.size())
        return nullptr;
    return &_samplers[index];
}

CC_BACKEND_END
