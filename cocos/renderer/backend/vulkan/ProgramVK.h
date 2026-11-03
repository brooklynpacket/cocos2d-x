/****************************************************************************
 Vulkan backend: programs.

 Uniform locations: non-sampler uniforms get locations 0..N-1 and samplers
 N..N+M-1. UniformLocation::location[1] holds the uniform's std140 offset, so
 ProgramState's CPU-side uniform buffer has exactly the layout of the
 uniform block and is uploaded as-is for each draw. writeUniform() expands
 the engine's tightly packed data (vec3 arrays, mat3, ...) into std140.

 Compiled SPIR-V and reflection are cached on disk, keyed by the sources,
 since compiling with glslang on device is much slower than a GL driver.
 ****************************************************************************/
#pragma once

#include "../Program.h"
#include "ShaderCompilerVK.h"
#include "UtilsVK.h"

#include <unordered_map>
#include <vector>

CC_BACKEND_BEGIN

class DeviceVK;

class ProgramVK : public Program
{
public:
    ProgramVK(DeviceVK* device, const std::string& vertexShader, const std::string& fragmentShader, Program::CompileResult& result);
    ~ProgramVK() override;

    UniformLocation getUniformLocation(const std::string& uniform) const override;
    UniformLocation getUniformLocation(backend::Uniform name) const override;
    int getAttributeLocation(const std::string& name) const override;
    int getAttributeLocation(backend::Attribute name) const override;
    int getMaxVertexLocation() const override { return (int)(_compiled.uniforms.size() + _compiled.samplers.size()); }
    int getMaxFragmentLocation() const override { return getMaxVertexLocation(); }
    const std::unordered_map<std::string, AttributeBindInfo> getActiveAttributes() const override;
    std::size_t getUniformBufferSize(ShaderStage stage) const override;
    const UniformInfo& getActiveUniformInfo(ShaderStage stage, int location) const override;
    const std::unordered_map<std::string, UniformInfo>& getAllActiveUniformInfo(ShaderStage stage) const override;
    void writeUniform(int location, std::size_t offset, const void* data, std::size_t size, char* buffer) const override;
#ifdef ANDROID
    void getProgramBinary(unsigned int& format, std::string& binary) override
    {
        format = 0;
        binary.clear();
    }
#endif

    // Vulkan
    bool isValid() const { return _pipelineLayout != VK_NULL_HANDLE; }
    uint32_t getId() const { return _id; }
    VkShaderModule getVertexModule() const { return _vertexModule; }
    VkShaderModule getFragmentModule() const { return _fragmentModule; }
    VkDescriptorSetLayout getSetLayout() const { return _setLayout; }
    VkPipelineLayout getPipelineLayout() const { return _pipelineLayout; }
    uint32_t getUniformBlockBinding() const { return _compiled.uniformBlockBinding; }
    uint32_t getUniformBlockSize() const { return _compiled.uniformBlockSize; }

    struct SamplerSlot
    {
        uint32_t binding;
        uint32_t arraySize;
        bool cube;
        bool shadow;
    };
    /** The sampler for a location from getUniformLocation(), or null. */
    const SamplerSlot* getSamplerForLocation(int location) const;
    const std::vector<SamplerSlot>& getSamplers() const { return _samplers; }
    const std::vector<vkshader::AttributeBinding>& getAttributes() const { return _compiled.attributes; }

protected:
#if CC_ENABLE_CACHE_TEXTURE_DATA
    // Vulkan never loses its resources, so locations never change.
    int getMappedLocation(int location) const override { return location; }
    int getOriginalLocation(int location) const override { return location; }
    const std::unordered_map<std::string, int> getAllUniformsLocation() const override;
#endif

private:
    bool compile(const std::string& vertexShader, const std::string& fragmentShader, Program::CompileResult& result);
    bool createVulkanObjects();
    void buildTables();

    DeviceVK* _device = nullptr;
    uint32_t _id = 0;
    vkshader::CompiledProgram _compiled;
    std::unordered_map<std::string, UniformInfo> _uniformInfos;
    std::vector<UniformInfo> _uniformInfoByLocation;
    std::vector<SamplerSlot> _samplers;
    UniformLocation _builtinUniforms[UNIFORM_MAX];
    int _builtinAttributes[ATTRIBUTE_MAX];

    VkShaderModule _vertexModule = VK_NULL_HANDLE;
    VkShaderModule _fragmentModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout _setLayout = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
};

CC_BACKEND_END
