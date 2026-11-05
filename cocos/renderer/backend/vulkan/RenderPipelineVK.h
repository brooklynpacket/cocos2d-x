/****************************************************************************
 Vulkan backend: render pipeline.

 A VkPipeline also depends on state the command buffer only knows at draw
 time (topology, cull mode, depth/stencil, attachment formats), so this just
 remembers the blend state; CommandBufferVK builds and caches the pipelines.
 ****************************************************************************/
#pragma once

#include "../RenderPipeline.h"

CC_BACKEND_BEGIN

class RenderPipelineVK : public RenderPipeline
{
public:
    void update(const PipelineDescriptor& pipelineDescriptor, const RenderPassDescriptor& renderPassDescriptor) override
    {
        _blend = pipelineDescriptor.blendDescriptor;
    }

    const BlendDescriptor& getBlendDescriptor() const { return _blend; }

private:
    BlendDescriptor _blend;
};

CC_BACKEND_END
