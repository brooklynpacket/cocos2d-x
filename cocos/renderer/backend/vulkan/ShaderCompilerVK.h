/****************************************************************************
 Vulkan backend: GLSL ES 1.00 -> SPIR-V compiler.

 The engine and game shaders are written in GLSL ES 1.00 (attribute/varying,
 loose uniforms, gl_FragColor, texture2D). This module rewrites them to
 GLSL ES 3.10 and compiles both stages together with glslang's "relaxed
 Vulkan rules", which packs every loose non-sampler uniform from both stages
 into one std140 uniform block. The result is one uniform buffer per program
 plus one combined image sampler binding per sampler uniform.

 This file deliberately has no Vulkan or cocos2d dependencies so it can be
 built and tested on a desktop host.
 ****************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cocos2d { namespace backend { namespace vkshader {

// GL type enums, used so UniformInfo::type keeps the same meaning as on GL.
enum : uint32_t
{
    GLT_FLOAT = 0x1406,
    GLT_FLOAT_VEC2 = 0x8B50,
    GLT_FLOAT_VEC3 = 0x8B51,
    GLT_FLOAT_VEC4 = 0x8B52,
    GLT_INT = 0x1404,
    GLT_INT_VEC2 = 0x8B53,
    GLT_INT_VEC3 = 0x8B54,
    GLT_INT_VEC4 = 0x8B55,
    GLT_BOOL = 0x8B56,
    GLT_BOOL_VEC2 = 0x8B57,
    GLT_BOOL_VEC3 = 0x8B58,
    GLT_BOOL_VEC4 = 0x8B59,
    GLT_FLOAT_MAT2 = 0x8B5A,
    GLT_FLOAT_MAT3 = 0x8B5B,
    GLT_FLOAT_MAT4 = 0x8B5C,
    GLT_SAMPLER_2D = 0x8B5E,
    GLT_SAMPLER_CUBE = 0x8B60,
    GLT_SAMPLER_2D_SHADOW = 0x8B62,
};

struct UniformMember
{
    std::string name;       // without any "[0]" suffix
    uint32_t glType = 0;
    uint32_t arraySize = 1; // 1 for non-arrays
    bool isArray = false;
    uint32_t offset = 0;    // std140 byte offset in the uniform block
    uint32_t arrayStride = 0;
};

struct SamplerBinding
{
    std::string name;
    uint32_t glType = 0;
    uint32_t binding = 0;
    uint32_t arraySize = 1;
};

struct AttributeBinding
{
    std::string name;
    uint32_t glType = 0;
    uint32_t location = 0;
};

struct CompiledProgram
{
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    // Binding of the merged uniform block, valid when uniformBlockSize > 0.
    uint32_t uniformBlockBinding = 0;
    uint32_t uniformBlockSize = 0;
    std::vector<UniformMember> uniforms;
    std::vector<SamplerBinding> samplers;
    std::vector<AttributeBinding> attributes;

    std::string serialize() const;
    bool deserialize(const std::string& data);
};

// Bumped whenever the translation or the output format changes, so cached
// SPIR-V from an older build is ignored.
static const uint32_t kCompilerVersion = 4;

// Rewrites GLSL ES 1.00 source to GLSL ES 3.10. Exposed for testing.
std::string translateSource(const std::string& source, bool isVertex);

// Compiles a vertex/fragment pair. Returns false and fills errorLog on failure.
bool compileProgram(const std::string& vertexSource,
                    const std::string& fragmentSource,
                    CompiledProgram& out,
                    std::string& errorLog);

// Byte size of one element of the given GL type when tightly packed (as the
// engine passes uniform data), and its std140 size/alignment helpers.
uint32_t packedSizeOf(uint32_t glType);
uint32_t matrixColumns(uint32_t glType); // 0 for non-matrices

}}} // namespace cocos2d::backend::vkshader
