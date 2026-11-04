/****************************************************************************
 Vulkan backend: command buffer.
 ****************************************************************************/
#include "CommandBufferVK.h"
#include "BufferVK.h"
#include "DepthStencilStateVK.h"
#include "DeviceVK.h"
#include "ProgramVK.h"
#include "RenderPipelineVK.h"
#include "TextureVK.h"

#include "base/CCDirector.h"
#include "platform/CCGLView.h"
#include "platform/CCFileUtils.h"

#include <algorithm>
#include <cstring>

CC_BACKEND_BEGIN

namespace
{
    // Same rules as CommandBufferMTL.mm, so passes merge identically on iOS
    // and Android.
    bool needsClear(const RenderPassDescriptor& d)
    {
        return d.needClearColor || d.needClearDepth || d.needClearStencil;
    }

    bool switchingRenderTargets(const RenderPassDescriptor& oldD, const RenderPassDescriptor& newD)
    {
        return newD.colorAttachmentsTexture[0] != oldD.colorAttachmentsTexture[0];
    }

    bool needNewPass(const RenderPassDescriptor& oldD, const RenderPassDescriptor& newD)
    {
        bool addingAttachments = (!oldD.needColorAttachment && newD.needColorAttachment) ||
                                 (!oldD.needDepthStencilAttachment() && newD.needDepthStencilAttachment());
        return needsClear(newD) || switchingRenderTargets(oldD, newD) || addingAttachments;
    }

    RenderPassDescriptor mergePassDescriptor(const RenderPassDescriptor& oldD, const RenderPassDescriptor& newD)
    {
        RenderPassDescriptor d = oldD;
        d.depthTestEnabled = d.depthTestEnabled || newD.depthTestEnabled;
        d.depthWriteEnabled = d.depthWriteEnabled || newD.depthWriteEnabled;
        d.stencilTestEnabled = d.stencilTestEnabled || newD.stencilTestEnabled;
        d.stencilWriteEnabled = d.stencilWriteEnabled || newD.stencilWriteEnabled;
        // The clear (if any) already happened when the pass started.
        d.needClearColor = d.needClearDepth = d.needClearStencil = false;
        return d;
    }

    VkStencilOpState toStencilState(const StencilDescriptor& s)
    {
        VkStencilOpState op{};
        op.failOp = UtilsVK::toStencilOp(s.stencilFailureOperation);
        op.passOp = UtilsVK::toStencilOp(s.depthStencilPassOperation);
        op.depthFailOp = UtilsVK::toStencilOp(s.depthFailureOperation);
        op.compareOp = UtilsVK::toCompareOp(s.stencilCompareFunction);
        op.compareMask = s.readMask;
        op.writeMask = s.writeMask;
        return op;
    }
}

CommandBufferVK::CommandBufferVK(DeviceVK* device)
: _device(device)
{
    _defaultAttributeBuffer = new BufferVK(device, 16, BufferType::VERTEX, BufferUsage::STATIC);
    float defaults[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    _defaultAttributeBuffer->updateData(defaults, sizeof(defaults));
}

CommandBufferVK::~CommandBufferVK()
{
    releaseDrawResources();
    CC_SAFE_RELEASE_NULL(_renderPipeline);
    CC_SAFE_RELEASE_NULL(_defaultAttributeBuffer);
}

void CommandBufferVK::beginFrame()
{
    auto glView = Director::getInstance()->getOpenGLView();
    if (glView)
    {
        Size size = glView->getFrameSize();
        _device->ensureBackbuffer((uint32_t)(size.width + 0.5f), (uint32_t)(size.height + 0.5f));
    }
}

void CommandBufferVK::beginRenderPass(const RenderPassDescriptor& descriptor)
{
    if (_passOpen)
    {
        if (!needNewPass(_passDescriptor, descriptor))
        {
            _passDescriptor = mergePassDescriptor(_passDescriptor, descriptor);
            return;
        }
        closePass();
    }
    openPass(descriptor);
}

void CommandBufferVK::openPass(const RenderPassDescriptor& descriptor)
{
    if (!_device->getBackbufferView())
        beginFrame();

    RenderPassKeyVK key;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    uint32_t width = 0, height = 0;

    if (descriptor.needColorAttachment)
    {
        TextureImageVK* color = getTextureImageVK(descriptor.colorAttachmentsTexture[0]);
        if (color && color->valid())
        {
            colorView = color->getAttachmentView();
            key.colorFormat = color->getFormat();
            key.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            width = color->getWidth();
            height = color->getHeight();
        }
        else
        {
            colorView = _device->getBackbufferView();
            key.colorFormat = _device->getBackbufferFormat();
            key.colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            width = _device->getBackbufferWidth();
            height = _device->getBackbufferHeight();
        }
        key.colorLoad = descriptor.needClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    }

    if (descriptor.needDepthStencilAttachment())
    {
        TextureImageVK* depth = getTextureImageVK(descriptor.depthAttachmentTexture);
        bool custom = depth && depth->valid() && depth->isDepth();
        if (custom)
        {
            depthView = depth->getAttachmentView();
            key.depthFormat = depth->getFormat();
            key.depthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            key.depthStore = VK_ATTACHMENT_STORE_OP_STORE;
            if (!colorView)
            {
                width = depth->getWidth();
                height = depth->getHeight();
            }
            else
            {
                width = std::min(width, depth->getWidth());
                height = std::min(height, depth->getHeight());
            }
        }
        else
        {
            // The default depth buffer lives for the whole frame, as in GL.
            // UI views clear depth (View::visit -> Renderer::clear) and then
            // draw 3D models later in the same frame; if anything in between
            // switches render targets (e.g. a SpriteMasker render texture),
            // the pass ends and the next one must see that cleared depth.
            depthView = _device->getDefaultDepthView();
            key.depthFormat = _device->getDepthStencilFormat();
            key.depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            key.depthStore = VK_ATTACHMENT_STORE_OP_STORE;
            if (!colorView)
            {
                width = _device->getBackbufferWidth();
                height = _device->getBackbufferHeight();
            }
            else if (width > _device->getBackbufferWidth() || height > _device->getBackbufferHeight())
            {
                VK_LOGW("render target %ux%u is larger than the default depth buffer", width, height);
                width = std::min(width, _device->getBackbufferWidth());
                height = std::min(height, _device->getBackbufferHeight());
            }
        }
        VkAttachmentLoadOp keep = VK_ATTACHMENT_LOAD_OP_LOAD;
        key.depthLoad = descriptor.needClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : keep;
        bool hasStencil = key.depthFormat != _device->getDepthOnlyFormat();
        key.stencilLoad = !hasStencil ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                      : (descriptor.needClearStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : keep);
    }

    if (!colorView && !depthView)
    {
        VK_LOGE("render pass with no attachments");
        _passOpen = false;
        return;
    }
    if (width == 0 || height == 0)
        return;

    _renderPass = _device->getRenderPass(key);
    VkFramebuffer framebuffer = _device->getFramebuffer(_renderPass, colorView, depthView, width, height);

    VkClearValue clears[2]{};
    uint32_t clearCount = 0;
    if (colorView)
    {
        const auto& c = descriptor.clearColorValue;
        clears[clearCount++].color = { { c[0], c[1], c[2], c[3] } };
    }
    if (depthView)
        clears[clearCount++].depthStencil = { descriptor.clearDepthValue, (uint32_t)descriptor.clearStencilValue };

    VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin.renderPass = _renderPass;
    begin.framebuffer = framebuffer;
    begin.renderArea.extent = { width, height };
    begin.clearValueCount = clearCount;
    begin.pClearValues = clears;
    vkCmdBeginRenderPass(_device->getFrameCommandBuffer(), &begin, VK_SUBPASS_CONTENTS_INLINE);

    _passDescriptor = descriptor;
    _passOpen = true;
    _passColorFormat = key.colorFormat;
    _passDepthFormat = key.depthFormat;
    _targetWidth = width;
    _targetHeight = height;
    _boundPipeline = VK_NULL_HANDLE;
}

void CommandBufferVK::closePass()
{
    if (!_passOpen)
        return;
    vkCmdEndRenderPass(_device->getFrameCommandBuffer());
    _passOpen = false;
    _boundPipeline = VK_NULL_HANDLE;
}

void CommandBufferVK::endFrame()
{
    closePass();
    releaseDrawResources();
    auto capture = std::move(_captureCallback);
    _captureCallback = nullptr;
    _device->endFrame(capture);
}

void CommandBufferVK::setRenderPipeline(RenderPipeline* renderPipeline)
{
    if (renderPipeline == _renderPipeline)
        return;
    CC_SAFE_RETAIN(renderPipeline);
    CC_SAFE_RELEASE(_renderPipeline);
    _renderPipeline = static_cast<RenderPipelineVK*>(renderPipeline);
}

void CommandBufferVK::setViewport(int x, int y, unsigned int w, unsigned int h)
{
    _viewportX = x;
    _viewportY = y;
    _viewportW = w;
    _viewportH = h;
}

void CommandBufferVK::setScissorRect(bool isEnabled, float x, float y, float width, float height)
{
    _scissorEnabled = isEnabled;
    _scissorX = x;
    _scissorY = y;
    _scissorW = width;
    _scissorH = height;
}

void CommandBufferVK::setDepthStencilState(DepthStencilState* depthStencilState)
{
    _depthStencilEnabled = depthStencilState != nullptr;
    if (depthStencilState)
        _depthStencil = static_cast<DepthStencilStateVK*>(depthStencilState)->getDescriptor();
}

void CommandBufferVK::setPolygonOffset(bool enabled, double slope, double constant, double clamp)
{
    _polygonOffsetEnabled = enabled;
    _polygonOffsetSlope = (float)slope;
    _polygonOffsetConstant = (float)constant;
    _polygonOffsetClamp = (float)clamp;
}

void CommandBufferVK::captureScreen(std::function<void(const unsigned char*, int, int)> callback)
{
    _captureCallback = std::move(callback);
}

void CommandBufferVK::setVertexBuffer(Buffer* buffer)
{
    CC_SAFE_RETAIN(buffer);
    CC_SAFE_RELEASE(_vertexBuffer);
    _vertexBuffer = static_cast<BufferVK*>(buffer);
}

void CommandBufferVK::setIndexBuffer(Buffer* buffer)
{
    CC_SAFE_RETAIN(buffer);
    CC_SAFE_RELEASE(_indexBuffer);
    _indexBuffer = static_cast<BufferVK*>(buffer);
}

void CommandBufferVK::setProgramState(ProgramState* programState)
{
    CC_SAFE_RETAIN(programState);
    CC_SAFE_RELEASE(_programState);
    _programState = programState;
}

void CommandBufferVK::releaseDrawResources()
{
    CC_SAFE_RELEASE_NULL(_vertexBuffer);
    CC_SAFE_RELEASE_NULL(_indexBuffer);
    CC_SAFE_RELEASE_NULL(_programState);
}

VkPipeline CommandBufferVK::getPipeline(ProgramVK* program, PrimitiveType primitiveType)
{
    const auto& layout = *_programState->getVertexLayout();
    const BlendDescriptor blend = _renderPipeline ? _renderPipeline->getBlendDescriptor() : BlendDescriptor();
    const bool hasColor = _passColorFormat != VK_FORMAT_UNDEFINED;
    const bool hasDepth = _passDepthFormat != VK_FORMAT_UNDEFINED;
    const bool depthStencil = hasDepth && _depthStencilEnabled;

    // Which shader inputs the vertex layout leaves unset.
    uint32_t missingMask = 0;
    for (const auto& attr : program->getAttributes())
    {
        bool found = false;
        for (const auto& it : layout.getAttributes())
            if (it.second.index == attr.location)
                found = true;
        if (!found && attr.location < 32)
            missingMask |= 1u << attr.location;
    }

    std::size_t key = 0;
    UtilsVK::hashCombine(key, program->getId());
    UtilsVK::hashCombine(key, (std::size_t)primitiveType);
    UtilsVK::hashCombine(key, layout.getStride());
    UtilsVK::hashCombine(key, (std::size_t)layout.getVertexStepMode());
    for (const auto& it : layout.getAttributes())
    {
        // unordered_map iteration order is stable for an unchanged map, but
        // combine order-independently anyway.
        std::size_t a = 0;
        UtilsVK::hashCombine(a, it.second.index);
        UtilsVK::hashCombine(a, (std::size_t)it.second.format);
        UtilsVK::hashCombine(a, it.second.offset);
        UtilsVK::hashCombine(a, it.second.needToBeNormallized);
        key ^= a * 0x9e3779b97f4a7c15ull;
    }
    UtilsVK::hashCombine(key, missingMask);
    UtilsVK::hashCombine(key, _passColorFormat);
    UtilsVK::hashCombine(key, _passDepthFormat);
    if (hasColor)
    {
        UtilsVK::hashCombine(key, blend.blendEnabled);
        UtilsVK::hashCombine(key, (std::size_t)blend.writeMask);
        if (blend.blendEnabled)
        {
            UtilsVK::hashCombine(key, (std::size_t)blend.rgbBlendOperation);
            UtilsVK::hashCombine(key, (std::size_t)blend.alphaBlendOperation);
            UtilsVK::hashCombine(key, (std::size_t)blend.sourceRGBBlendFactor);
            UtilsVK::hashCombine(key, (std::size_t)blend.destinationRGBBlendFactor);
            UtilsVK::hashCombine(key, (std::size_t)blend.sourceAlphaBlendFactor);
            UtilsVK::hashCombine(key, (std::size_t)blend.destinationAlphaBlendFactor);
        }
    }
    UtilsVK::hashCombine(key, depthStencil);
    if (depthStencil)
    {
        const auto& d = _depthStencil;
        UtilsVK::hashCombine(key, d.depthTestEnabled);
        UtilsVK::hashCombine(key, d.depthWriteEnabled);
        UtilsVK::hashCombine(key, (std::size_t)d.depthCompareFunction);
        UtilsVK::hashCombine(key, d.stencilTestEnabled);
        for (const StencilDescriptor* s : { &d.frontFaceStencil, &d.backFaceStencil })
        {
            UtilsVK::hashCombine(key, (std::size_t)s->stencilFailureOperation);
            UtilsVK::hashCombine(key, (std::size_t)s->depthFailureOperation);
            UtilsVK::hashCombine(key, (std::size_t)s->depthStencilPassOperation);
            UtilsVK::hashCombine(key, (std::size_t)s->stencilCompareFunction);
            UtilsVK::hashCombine(key, s->readMask);
            UtilsVK::hashCombine(key, s->writeMask);
        }
    }
    UtilsVK::hashCombine(key, (std::size_t)_cullMode);
    UtilsVK::hashCombine(key, (std::size_t)_winding);
    UtilsVK::hashCombine(key, _polygonOffsetEnabled);

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (_device->hasPipeline(key, pipeline))
        return pipeline;

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = program->getVertexModule();
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = program->getFragmentModule();
    stages[1].pName = "main";

    // Vertex input
    VkVertexInputBindingDescription bindings[2]{};
    uint32_t bindingCount = 0;
    std::vector<VkVertexInputAttributeDescription> attributes;
    if (layout.isValid())
    {
        bindings[bindingCount].binding = 0;
        bindings[bindingCount].stride = (uint32_t)layout.getStride();
        bindings[bindingCount].inputRate = layout.getVertexStepMode() == VertexStepMode::INSTANCE
                                               ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        ++bindingCount;
        for (const auto& it : layout.getAttributes())
        {
            const auto& a = it.second;
            VkVertexInputAttributeDescription desc{};
            desc.location = (uint32_t)a.index;
            desc.binding = 0;
            desc.format = UtilsVK::toVertexFormat(a.format, a.needToBeNormallized);
            desc.offset = (uint32_t)a.offset;
            attributes.push_back(desc);
        }
    }
    if (missingMask)
    {
        bindings[bindingCount].binding = 1;
        bindings[bindingCount].stride = 0;
        bindings[bindingCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        ++bindingCount;
        for (uint32_t loc = 0; loc < 32; ++loc)
        {
            if (missingMask & (1u << loc))
                attributes.push_back({ loc, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
        }
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = bindingCount;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    assembly.topology = UtilsVK::toTopology(primitiveType);

    VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = UtilsVK::toCullMode(_cullMode);
    // The flipped viewport keeps GL's winding rules, so GL's setting carries over.
    raster.frontFace = _winding == Winding::CLOCK_WISE ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = _polygonOffsetEnabled ? VK_TRUE : VK_FALSE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    if (depthStencil)
    {
        const auto& d = _depthStencil;
        depth.depthTestEnable = d.depthTestEnabled;
        depth.depthWriteEnable = d.depthWriteEnabled;
        depth.depthCompareOp = UtilsVK::toCompareOp(d.depthCompareFunction);
        depth.stencilTestEnable = d.stencilTestEnabled;
        depth.front = toStencilState(d.frontFaceStencil);
        depth.back = toStencilState(d.backFaceStencil);
    }
    depth.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = blend.blendEnabled;
    blendAttachment.srcColorBlendFactor = UtilsVK::toBlendFactor(blend.sourceRGBBlendFactor);
    blendAttachment.dstColorBlendFactor = UtilsVK::toBlendFactor(blend.destinationRGBBlendFactor);
    blendAttachment.colorBlendOp = UtilsVK::toBlendOp(blend.rgbBlendOperation);
    blendAttachment.srcAlphaBlendFactor = UtilsVK::toBlendFactor(blend.sourceAlphaBlendFactor);
    blendAttachment.dstAlphaBlendFactor = UtilsVK::toBlendFactor(blend.destinationAlphaBlendFactor);
    blendAttachment.alphaBlendOp = UtilsVK::toBlendOp(blend.alphaBlendOperation);
    blendAttachment.colorWriteMask = UtilsVK::toColorWriteMask(blend.writeMask);
    VkPipelineColorBlendStateCreateInfo blendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blendState.attachmentCount = hasColor ? 1 : 0;
    blendState.pAttachments = &blendAttachment;

    VkDynamicState dynamics[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_LINE_WIDTH,
    };
    VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = sizeof(dynamics) / sizeof(dynamics[0]);
    dynamic.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = hasDepth ? &depth : nullptr;
    info.pColorBlendState = &blendState;
    info.pDynamicState = &dynamic;
    info.layout = program->getPipelineLayout();
    info.renderPass = _renderPass;
    VkResult result = vkCreateGraphicsPipelines(_device->getDevice(), _device->getPipelineCache(), 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        VK_LOGE("vkCreateGraphicsPipelines failed (%d) for program %u, topology %d, stride %zu, missing 0x%x, color %d, depth %d",
                (int)result, program->getId(), (int)primitiveType, layout.getStride(), missingMask,
                (int)_passColorFormat, (int)_passDepthFormat);
        for (const auto& a : attributes)
            VK_LOGE("  attribute location %u binding %u format %d offset %u", a.location, a.binding, (int)a.format, a.offset);
        for (const auto& a : program->getAttributes())
            VK_LOGE("  shader input %s location %u type 0x%x", a.name.c_str(), a.location, a.glType);
        // Logcat truncates long lines; keep the full sources for debugging.
        std::string dump = FileUtils::getInstance()->getWritablePath() + "vk_failed_program_" + std::to_string(program->getId());
        FileUtils::getInstance()->writeStringToFile(program->getVertexShader(), dump + ".vert");
        FileUtils::getInstance()->writeStringToFile(program->getFragmentShader(), dump + ".frag");
        VK_LOGE("  sources written to %s.{vert,frag}", dump.c_str());
        // Remember the failure so it isn't retried (and logged) every draw.
        _device->addPipeline(key, program->getId(), VK_NULL_HANDLE);
        return VK_NULL_HANDLE;
    }
    _device->addPipeline(key, program->getId(), pipeline);
    return pipeline;
}

bool CommandBufferVK::prepareDraw(PrimitiveType primitiveType)
{
    if (!_passOpen || !_programState)
        return false;
    auto program = static_cast<ProgramVK*>(_programState->getProgram());
    if (!program || !program->isValid())
        return false;

    VkCommandBuffer cb = _device->getFrameCommandBuffer();

    VkPipeline pipeline = getPipeline(program, primitiveType);
    if (!pipeline)
        return false;
    if (pipeline != _boundPipeline)
    {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        _boundPipeline = pipeline;
    }

    // Dynamic state. Viewport and scissor arrive in GL's bottom-left
    // coordinates; the negative-height viewport flips clip space to match.
    VkViewport vp;
    vp.x = (float)_viewportX;
    vp.y = (float)((int)_targetHeight - _viewportY);
    vp.width = (float)_viewportW;
    vp.height = -(float)_viewportH;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    if (_viewportW == 0 || _viewportH == 0)
    {
        vp.x = 0;
        vp.y = (float)_targetHeight;
        vp.width = (float)_targetWidth;
        vp.height = -(float)_targetHeight;
    }
    vkCmdSetViewport(cb, 0, 1, &vp);

    VkRect2D scissor{ { 0, 0 }, { _targetWidth, _targetHeight } };
    if (_scissorEnabled)
    {
        int x0 = (int)_scissorX;
        int y0 = (int)_targetHeight - (int)(_scissorY + _scissorH);
        int x1 = x0 + (int)_scissorW;
        int y1 = y0 + (int)_scissorH;
        x0 = std::max(0, std::min(x0, (int)_targetWidth));
        x1 = std::max(0, std::min(x1, (int)_targetWidth));
        y0 = std::max(0, std::min(y0, (int)_targetHeight));
        y1 = std::max(0, std::min(y1, (int)_targetHeight));
        scissor.offset = { x0, y0 };
        scissor.extent = { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) };
    }
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_BIT, _stencilReferenceValueFront);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_BACK_BIT, _stencilReferenceValueBack);
    float clamp = _device->getFeatures().depthBiasClamp ? _polygonOffsetClamp : 0.0f;
    vkCmdSetDepthBias(cb, _polygonOffsetConstant, clamp, _polygonOffsetSlope);
    vkCmdSetLineWidth(cb, _device->getFeatures().wideLines ? _lineWidth : 1.0f);

    // Uniforms
    for (auto& callback : _programState->getCallbackUniforms())
        callback.second(_programState, callback.first);

    VkDescriptorSet set = _device->allocateDescriptorSet(program->getSetLayout());
    if (!set)
        return false;

    std::vector<VkWriteDescriptorSet> writes;
    VkDescriptorBufferInfo uniformInfo{};
    if (program->getUniformBlockSize() > 0)
    {
        char* data = nullptr;
        std::size_t size = 0;
        _programState->getVertexUniformBuffer(&data, size);
        DeviceVK::TransientAllocation uniforms;
        if (!_device->allocateUniform(program->getUniformBlockSize(), uniforms))
            return false;
        std::size_t n = std::min<std::size_t>(size, program->getUniformBlockSize());
        if (data && n)
            memcpy(uniforms.mapped, data, n);
        if (n < program->getUniformBlockSize())
            memset((char*)uniforms.mapped + n, 0, program->getUniformBlockSize() - n);
        uniformInfo.buffer = uniforms.buffer;
        uniformInfo.offset = uniforms.offset;
        uniformInfo.range = program->getUniformBlockSize();

        VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = set;
        w.dstBinding = program->getUniformBlockBinding();
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &uniformInfo;
        writes.push_back(w);
    }

    // Textures. Start every sampler on the dummy texture, then fill in what
    // the program state set.
    const auto& samplers = program->getSamplers();
    std::vector<std::vector<VkDescriptorImageInfo>> images(samplers.size());
    for (size_t i = 0; i < samplers.size(); ++i)
    {
        const auto& slot = samplers[i];
        TextureBackend* dummy = slot.shadow ? _device->getDummyDepthTexture() : _device->getDummyTexture(slot.cube);
        TextureImageVK* image = getTextureImageVK(dummy);
        VkDescriptorImageInfo info{};
        info.sampler = _device->getSampler(getSamplerDescriptorVK(dummy), false, slot.shadow);
        info.imageView = image->getSampledView();
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[i].assign(slot.arraySize, info);
    }
    for (const auto* infos : { &_programState->getVertexTextureInfos(), &_programState->getFragmentTextureInfos() })
    {
        for (const auto& it : *infos)
        {
            int location = it.first;
#if CC_ENABLE_CACHE_TEXTURE_DATA
            if (it.second.location >= 0)
                location = it.second.location;
#endif
            const ProgramVK::SamplerSlot* slot = program->getSamplerForLocation(location);
            if (!slot)
                continue;
            size_t index = slot - samplers.data();
            const auto& textures = it.second.textures;
            for (size_t e = 0; e < textures.size() && e < slot->arraySize; ++e)
            {
                TextureBackend* texture = textures[e];
                TextureImageVK* image = getTextureImageVK(texture);
                if (!image || !image->valid())
                    continue;
                // Sampling with the wrong view type is invalid; leave the dummy.
                if ((texture->getTextureType() == TextureType::TEXTURE_CUBE) != slot->cube)
                    continue;
                // Depth-compare sampling of a color image is undefined; leave the dummy.
                if (slot->shadow && !image->isDepth())
                    continue;
                VkDescriptorImageInfo& info = images[index][e];
                info.imageView = image->getSampledView();
                info.sampler = _device->getSampler(getSamplerDescriptorVK(texture), image->hasMipmaps(), slot->shadow);
            }
        }
    }
    for (size_t i = 0; i < samplers.size(); ++i)
    {
        VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = set;
        w.dstBinding = samplers[i].binding;
        w.descriptorCount = samplers[i].arraySize;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = images[i].data();
        writes.push_back(w);
    }
    vkUpdateDescriptorSets(_device->getDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, program->getPipelineLayout(), 0, 1, &set, 0, nullptr);

    // Vertex buffers
    VkBuffer buffers[2] = { _vertexBuffer ? _vertexBuffer->getVkBuffer() : VK_NULL_HANDLE,
                            _defaultAttributeBuffer->getVkBuffer() };
    VkDeviceSize offsets[2] = { 0, 0 };
    if (buffers[0])
        vkCmdBindVertexBuffers(cb, 0, 1, &buffers[0], &offsets[0]);
    vkCmdBindVertexBuffers(cb, 1, 1, &buffers[1], &offsets[1]);
    return true;
}

void CommandBufferVK::drawArrays(PrimitiveType primitiveType, std::size_t start, std::size_t count)
{
    if (prepareDraw(primitiveType) && count > 0)
        vkCmdDraw(_device->getFrameCommandBuffer(), (uint32_t)count, 1, (uint32_t)start, 0);
    releaseDrawResources();
}

void CommandBufferVK::drawElements(PrimitiveType primitiveType, IndexFormat indexType, std::size_t count, std::size_t offset)
{
    if (_indexBuffer && prepareDraw(primitiveType) && count > 0)
    {
        VkCommandBuffer cb = _device->getFrameCommandBuffer();
        bool u16 = indexType == IndexFormat::U_SHORT;
        vkCmdBindIndexBuffer(cb, _indexBuffer->getVkBuffer(), 0, u16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        uint32_t firstIndex = (uint32_t)(offset / (u16 ? 2 : 4));
        vkCmdDrawIndexed(cb, (uint32_t)count, 1, firstIndex, 0, 0);
    }
    releaseDrawResources();
}

CC_BACKEND_END
