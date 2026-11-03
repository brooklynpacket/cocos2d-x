/****************************************************************************
 Vulkan backend: depth/stencil state. Vulkan bakes this into the pipeline,
 so it is only a descriptor that the command buffer hashes into the
 pipeline key.
 ****************************************************************************/
#pragma once

#include "../DepthStencilState.h"

CC_BACKEND_BEGIN

class DepthStencilStateVK : public DepthStencilState
{
public:
    explicit DepthStencilStateVK(const DepthStencilDescriptor& descriptor)
    : DepthStencilState(descriptor)
    {}

    const DepthStencilDescriptor& getDescriptor() const { return _depthStencilInfo; }
};

CC_BACKEND_END
