/****************************************************************************
 Vulkan backend: GLSL ES 1.00 -> SPIR-V compiler. See ShaderCompilerVK.h.
 ****************************************************************************/
#include "ShaderCompilerVK.h"

#include "glslang/Public/ShaderLang.h"
#include "glslang/Include/ResourceLimits.h"
#include "SPIRV/GlslangToSpv.h"
#include "glslang/MachineIndependent/iomapper.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <sstream>

namespace cocos2d { namespace backend { namespace vkshader {

namespace {

// Values copied, in field order, from the NDK's shaderc
// (libshaderc_util/src/resources.cc), which matches the glslang version we
// build against.
const TBuiltInResource kResources = {
    8, 6, 2, 8, 16, 4096, 60, 16, 80, 16, 1024, 8, 256, 15, 256, 16, 15, -8, 7, 8,
    65535, 65535, 65535, 1024, 1024, 64, 512, 16, 8, 8, 1, 60, 64, 64, 128, 128,
    8, 8, 8, 0, 0, 0, 0, 0, 8, 8, 16, 256, 1024, 512, 60, 128, 128, 16, 1024, 4096,
    128, 128, 16, 1024, 120, 32, 64, 16, 0, 0, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1, 32,
    4, 64, 8, 8, 4,
    256, 512, 32, 1, 1, 32, 1, 1, 4,
    256, 256, 128, 128, 128, 128, 128, 128, 4, 1,
    { 1, 1, 1, 1, 1, 1, 1, 1, 1 },
};

const char* kUniformBlockName = "cc_Uniforms";

void initGlslangOnce()
{
    static std::once_flag flag;
    std::call_once(flag, [] { glslang::InitializeProcess(); });
}

bool startsWithDirective(const std::string& line, const char* directive)
{
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '#')
        return false;
    i = line.find_first_not_of(" \t", i + 1);
    if (i == std::string::npos)
        return false;
    return line.compare(i, strlen(directive), directive) == 0;
}

std::string stripArraySuffix(const std::string& name, bool& isArray)
{
    auto pos = name.find('[');
    isArray = pos != std::string::npos;
    return isArray ? name.substr(0, pos) : name;
}

// The reflection's own glDefineType is used for most things, but samplers
// are classified from the TType so shadow samplers are recognised.
bool isSamplerType(const glslang::TType* type)
{
    return type && type->getBasicType() == glslang::EbtSampler;
}

void appendU32(std::string& out, uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }

void appendString(std::string& out, const std::string& s)
{
    appendU32(out, (uint32_t)s.size());
    out.append(s);
}

void appendWords(std::string& out, const std::vector<uint32_t>& words)
{
    appendU32(out, (uint32_t)words.size());
    out.append(reinterpret_cast<const char*>(words.data()), words.size() * 4);
}

struct Reader
{
    const std::string& data;
    size_t pos = 0;
    bool ok = true;

    uint32_t u32()
    {
        if (pos + 4 > data.size()) { ok = false; return 0; }
        uint32_t v;
        memcpy(&v, data.data() + pos, 4);
        pos += 4;
        return v;
    }
    std::string str()
    {
        uint32_t n = u32();
        if (!ok || pos + n > data.size()) { ok = false; return {}; }
        std::string s = data.substr(pos, n);
        pos += n;
        return s;
    }
    std::vector<uint32_t> words()
    {
        uint32_t n = u32();
        if (!ok || pos + size_t(n) * 4 > data.size()) { ok = false; return {}; }
        std::vector<uint32_t> w(n);
        memcpy(w.data(), data.data() + pos, size_t(n) * 4);
        pos += size_t(n) * 4;
        return w;
    }
};

} // namespace

uint32_t packedSizeOf(uint32_t glType)
{
    switch (glType)
    {
    case GLT_FLOAT: case GLT_INT: case GLT_BOOL: return 4;
    case GLT_FLOAT_VEC2: case GLT_INT_VEC2: case GLT_BOOL_VEC2: return 8;
    case GLT_FLOAT_VEC3: case GLT_INT_VEC3: case GLT_BOOL_VEC3: return 12;
    case GLT_FLOAT_VEC4: case GLT_INT_VEC4: case GLT_BOOL_VEC4: return 16;
    case GLT_FLOAT_MAT2: return 16;
    case GLT_FLOAT_MAT3: return 36;
    case GLT_FLOAT_MAT4: return 64;
    default: return 0;
    }
}

uint32_t matrixColumns(uint32_t glType)
{
    switch (glType)
    {
    case GLT_FLOAT_MAT2: return 2;
    case GLT_FLOAT_MAT3: return 3;
    case GLT_FLOAT_MAT4: return 4;
    default: return 0;
    }
}

std::string translateSource(const std::string& source, bool isVertex)
{
    std::string out;
    out.reserve(source.size() + 1024);
    out += "#version 310 es\n";
    out += "#define CC_VULKAN 1\n";
    // Vulkan leaves the point size undefined unless the vertex shader writes
    // it; GL drivers use 1.0. Wrap main() to supply that default.
    const bool addPointSize = isVertex && source.find("gl_PointSize") == std::string::npos;
    if (isVertex)
    {
        out += "#define attribute in\n";
        out += "#define varying out\n";
        if (addPointSize)
            out += "#define main cc_shader_main\n";
    }
    else
    {
        out += "#define varying in\n";
        out += "layout(location = 0) out mediump vec4 cc_FragColor;\n";
        out += "#define gl_FragColor cc_FragColor\n";
    }
    out += "#define texture2D texture\n";
    out += "#define texture2DProj textureProj\n";
    out += "#define texture2DLod textureLod\n";
    out += "#define texture2DLodEXT textureLod\n";
    out += "#define texture2DProjLod textureProjLod\n";
    out += "#define texture2DProjLodEXT textureProjLod\n";
    out += "#define texture2DGradEXT textureGrad\n";
    out += "#define textureCube texture\n";
    out += "#define textureCubeLod textureLod\n";
    out += "#define textureCubeLodEXT textureLod\n";
    out += "#define shadow2DEXT texture\n";
    out += "#define shadow2DProjEXT textureProj\n";
    out += "#define gl_FragDepthEXT gl_FragDepth\n";
    // GL_EXT_shadow_samplers gave sampler2DShadow a default precision of lowp;
    // GLSL ES 3.10 has none.
    out += "precision lowp sampler2DShadow;\n";
    // Words that were ordinary identifiers in GLSL ES 1.00 but are keywords or
    // reserved in 3.10 (e.g. cocos' label_outline.frag declares "sample").
    static const char* const kRenamed[] = {
        "sample", "patch", "buffer", "shared", "smooth", "centroid", "coherent",
        "readonly", "writeonly", "restrict", "resource", "subroutine", "precise",
        "noperspective", "active", "filter", "common", "partition",
    };
    for (const char* word : kRenamed)
    {
        out += "#define ";
        out += word;
        out += " cc_renamed_";
        out += word;
        out += "\n";
    }
    // Keep the original line numbers in compiler errors.
    out += "#line 1\n";

    // Drop #version and #extension lines. Everything those extensions provided
    // in GLSL ES 1.00 is core in 3.10.
    std::istringstream in(source);
    std::string line;
    while (std::getline(in, line))
    {
        if (startsWithDirective(line, "version") || startsWithDirective(line, "extension"))
            out += "\n";
        else
        {
            out += line;
            out += '\n';
        }
    }
    if (addPointSize)
        out += "\n#undef main\nvoid main()\n{\n    gl_PointSize = 1.0;\n    cc_shader_main();\n}\n";
    return out;
}

bool compileProgram(const std::string& vertexSource,
                    const std::string& fragmentSource,
                    CompiledProgram& out,
                    std::string& errorLog)
{
    initGlslangOnce();
    out = CompiledProgram();

    const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    const std::string sources[2] = {
        translateSource(vertexSource, true),
        translateSource(fragmentSource, false),
    };
    const EShLanguage stages[2] = { EShLangVertex, EShLangFragment };

    glslang::TShader vs(EShLangVertex);
    glslang::TShader fs(EShLangFragment);
    glslang::TShader* shaders[2] = { &vs, &fs };

    for (int i = 0; i < 2; ++i)
    {
        glslang::TShader& shader = *shaders[i];
        const char* src = sources[i].c_str();
        shader.setStrings(&src, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stages[i], glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
        shader.setEnvInputVulkanRulesRelaxed();
        shader.setGlobalUniformBlockName(kUniformBlockName);
        shader.setGlobalUniformSet(0);
        shader.setGlobalUniformBinding(0);
        // Samplers are numbered after the uniform block.
        shader.setShiftBinding(glslang::EResSampler, 1);
        shader.setShiftBinding(glslang::EResTexture, 1);
        shader.setAutoMapBindings(true);
        shader.setAutoMapLocations(true);
        if (!shader.parse(&kResources, 310, EEsProfile, false, false, messages))
        {
            errorLog = std::string(i == 0 ? "[vertex] " : "[fragment] ") + shader.getInfoLog();
            return false;
        }
    }

    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&fs);
    if (!program.link(messages))
    {
        errorLog = std::string("[link] ") + program.getInfoLog();
        return false;
    }
    // The GLSL IO mapper assigns varying locations by name across both stages.
    // (The default mapper numbers each stage on its own, so a fragment shader
    // that declares a different set of varyings would read the wrong ones.)
    // One resolver shared by both stages; it takes the auto-map and binding
    // shift settings from the vertex stage (both stages use the same ones).
    glslang::TDefaultGlslIoResolver resolver(*program.getIntermediate(EShLangVertex));
    glslang::TGlslIoMapper ioMapper;
    if (!program.mapIO(&resolver, &ioMapper))
    {
        errorLog = std::string("[mapIO] ") + program.getInfoLog();
        return false;
    }
    // Include inputs the shader declares but never reads: Vulkan still needs
    // a vertex attribute for every declared input.
    if (!program.buildReflection(EShReflectionAllIOVariables))
    {
        errorLog = "[reflection] failed";
        return false;
    }

    // Uniform block (there is at most one: the merged default block).
    // Push constant blocks are only used by the backend's own shaders.
    int defaultBlockIndex = -1;
    for (int i = 0; i < program.getNumUniformBlocks(); ++i)
    {
        const auto& block = program.getUniformBlock(i);
        if (block.getType() && block.getType()->getQualifier().isPushConstant())
            continue;
        if (block.name.find(kUniformBlockName) == 0)
        {
            defaultBlockIndex = i;
            out.uniformBlockBinding = (uint32_t)std::max(0, block.getBinding());
            out.uniformBlockSize = (uint32_t)block.size;
        }
        else
        {
            errorLog = "[reflection] unexpected uniform block " + block.name;
            return false;
        }
    }

    for (int i = 0; i < program.getNumUniformVariables(); ++i)
    {
        const auto& u = program.getUniform(i);
        bool isArray = false;
        std::string name = stripArraySuffix(u.name, isArray);
        auto dot = name.rfind('.');
        if (dot != std::string::npos)
            name = name.substr(dot + 1);

        if (isSamplerType(u.getType()))
        {
            SamplerBinding s;
            s.name = name;
            s.glType = (uint32_t)u.glDefineType;
            s.binding = (uint32_t)std::max(0, u.getBinding());
            s.arraySize = (uint32_t)std::max(1, u.size);
            out.samplers.push_back(s);
        }
        else if (u.index >= 0 && u.index == defaultBlockIndex)
        {
            UniformMember m;
            m.name = name;
            m.glType = (uint32_t)u.glDefineType;
            m.isArray = isArray || (u.getType() && u.getType()->isArray());
            m.arraySize = (uint32_t)std::max(1, u.size);
            m.offset = (uint32_t)u.offset;
            m.arrayStride = (uint32_t)std::max(0, u.arrayStride);
            out.uniforms.push_back(m);
        }
    }

    for (int i = 0; i < program.getNumPipeInputs(); ++i)
    {
        const auto& in = program.getPipeInput(i);
        if (in.name.compare(0, 3, "gl_") == 0)
            continue;
        AttributeBinding a;
        a.name = in.name;
        a.glType = (uint32_t)in.glDefineType;
        const auto* type = in.getType();
        a.location = (type && type->getQualifier().hasLocation()) ? type->getQualifier().layoutLocation : 0;
        out.attributes.push_back(a);
    }

    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.generateDebugInfo = false;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), out.vertexSpirv, &logger, &options);
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), out.fragmentSpirv, &logger, &options);
    if (out.vertexSpirv.empty() || out.fragmentSpirv.empty())
    {
        errorLog = "[spirv] " + logger.getAllMessages();
        return false;
    }
    return true;
}

std::string CompiledProgram::serialize() const
{
    std::string s;
    appendU32(s, 0x50534B56u); // 'VKSP'
    appendU32(s, kCompilerVersion);
    appendWords(s, vertexSpirv);
    appendWords(s, fragmentSpirv);
    appendU32(s, uniformBlockBinding);
    appendU32(s, uniformBlockSize);
    appendU32(s, (uint32_t)uniforms.size());
    for (const auto& u : uniforms)
    {
        appendString(s, u.name);
        appendU32(s, u.glType);
        appendU32(s, u.arraySize);
        appendU32(s, u.isArray ? 1 : 0);
        appendU32(s, u.offset);
        appendU32(s, u.arrayStride);
    }
    appendU32(s, (uint32_t)samplers.size());
    for (const auto& sm : samplers)
    {
        appendString(s, sm.name);
        appendU32(s, sm.glType);
        appendU32(s, sm.binding);
        appendU32(s, sm.arraySize);
    }
    appendU32(s, (uint32_t)attributes.size());
    for (const auto& a : attributes)
    {
        appendString(s, a.name);
        appendU32(s, a.glType);
        appendU32(s, a.location);
    }
    return s;
}

bool CompiledProgram::deserialize(const std::string& data)
{
    Reader r{data};
    if (r.u32() != 0x50534B56u || r.u32() != kCompilerVersion)
        return false;
    CompiledProgram p;
    p.vertexSpirv = r.words();
    p.fragmentSpirv = r.words();
    p.uniformBlockBinding = r.u32();
    p.uniformBlockSize = r.u32();
    uint32_t n = r.u32();
    for (uint32_t i = 0; r.ok && i < n; ++i)
    {
        UniformMember u;
        u.name = r.str();
        u.glType = r.u32();
        u.arraySize = r.u32();
        u.isArray = r.u32() != 0;
        u.offset = r.u32();
        u.arrayStride = r.u32();
        p.uniforms.push_back(u);
    }
    n = r.u32();
    for (uint32_t i = 0; r.ok && i < n; ++i)
    {
        SamplerBinding s;
        s.name = r.str();
        s.glType = r.u32();
        s.binding = r.u32();
        s.arraySize = r.u32();
        p.samplers.push_back(s);
    }
    n = r.u32();
    for (uint32_t i = 0; r.ok && i < n; ++i)
    {
        AttributeBinding a;
        a.name = r.str();
        a.glType = r.u32();
        a.location = r.u32();
        p.attributes.push_back(a);
    }
    if (!r.ok || p.vertexSpirv.empty() || p.fragmentSpirv.empty())
        return false;
    *this = std::move(p);
    return true;
}

}}} // namespace cocos2d::backend::vkshader
