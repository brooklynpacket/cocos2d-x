/****************************************************************************
 Copyright (c) 2015-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2014 GamePlay3D team

 http://www.cocos2d-x.org


 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 Ideas taken from:
 - GamePlay3D: http://gameplay3d.org/
 - OGRE3D: http://www.ogre3d.org/
 - Qt3D: http://qt-project.org/

 ****************************************************************************/

#include "renderer/CCRenderState.h"

#include <string>
#include <cassert>

#include "renderer/CCTexture2D.h"
#include "renderer/CCPass.h"
#include "base/ccUtils.h"
#include "base/CCDirector.h"
#include "renderer/CCRenderer.h"
#include "renderer/CCMaterial.h"

NS_CC_BEGIN


std::string RenderState::getName() const
{
    return _name;
}


void RenderState::bindPass(Pass* pass, MeshCommand* command)
{
    CC_ASSERT(pass);
    assert(pass->_technique && pass->_technique->_material);
    auto *technique = pass->_technique;
    auto *material = technique->_material;
    auto &pipelineDescriptor = command->getPipelineDescriptor();

    //need reset all state
    //pipelineDescriptor.blendDescriptor.blendEnabled = true;

    // Get the combined modified state bits for our RenderState hierarchy.
    long overrideBits = _state._modifiedBits;
    overrideBits |= technique->getStateBlock()._modifiedBits;
    overrideBits |= material->getStateBlock()._modifiedBits;

    // Restore renderer state to its default, except for explicitly specified states
    RenderState::StateBlock::restoreUnmodifiedStates(overrideBits, &pipelineDescriptor);

    material->getStateBlock().apply(&pipelineDescriptor);
    technique->getStateBlock().apply(&pipelineDescriptor);
    _state.apply(&pipelineDescriptor);
    
    

}


RenderState::StateBlock& RenderState::getStateBlock() const
{
    return _state;
}


void RenderState::StateBlock::bind(PipelineDescriptor *pipelineDescriptor)
{
    // When the public bind() is called with no RenderState object passed in,
    // we assume we are being called to bind the state of a single StateBlock,
    // irrespective of whether it belongs to a hierarchy of RenderStates.
    // Therefore, we call restore() here with only this StateBlock's override
    // bits to restore state before applying the new state.
    StateBlock::restoreUnmodifiedStates(_modifiedBits, pipelineDescriptor);

    apply(pipelineDescriptor);
}

void RenderState::StateBlock::apply(PipelineDescriptor *pipelineDescriptor)
{
    //CC_ASSERT(_globalState);

    auto renderer = Director::getInstance()->getRenderer();
    auto &blend = pipelineDescriptor->blendDescriptor;

    // Update any state that differs from _globalState and flip _globalState bits
    if ((_modifiedBits & RS_BLEND))
    {
        blend.blendEnabled = _blendEnabled;
    }

    if ((_modifiedBits & RS_BLEND_FUNC))
    {
        blend.sourceAlphaBlendFactor = blend.sourceRGBBlendFactor = _blendSrc;
        blend.destinationAlphaBlendFactor = blend.destinationRGBBlendFactor = _blendDst;
    }

    if ((_modifiedBits & RS_CULL_FACE))
    {
        if (!_cullFaceEnabled)
             renderer->setCullMode(CullMode::NONE);
    }

    if ((_modifiedBits & RS_CULL_FACE_SIDE))
    {
        renderer->setCullMode(_cullFaceSide);
    }

    if ((_modifiedBits & RS_FRONT_FACE))
    {
        renderer->setWinding(_frontFace);
    }
    if ((_modifiedBits & RS_DEPTH_TEST))
    {
        renderer->setDepthTest(_depthTestEnabled);
    }

    if ((_modifiedBits & RS_DEPTH_WRITE))
    {
        renderer->setDepthWrite(_depthWriteEnabled);
    }

    if ((_modifiedBits & RS_DEPTH_FUNC))
    {
        renderer->setDepthCompareFunction(_depthFunction);
    }
    
    //BPC PATCH
    if ((_modifiedBits & RS_STENCIL_TEST))
    {
        renderer->setStencilTest(_stencilTestEnabled);
    }
    
    if ((_modifiedBits & RS_STENCIL_WRITE))
    {
        renderer->setStencilWriteMask(_stencilWrite);
    }
    
    if ((_modifiedBits & RS_STENCIL_FUNC))
    {
        renderer->setStencilCompareFunction(_stencilFunction, _stencilFunctionRef, _stencilFunctionMask);
    }
    
    if ((_modifiedBits & RS_STENCIL_OP))
    {
        renderer->setStencilOperation(_stencilOpSfail, _stencilOpDpfail, _stencilOpDppass);
    }
    
    if ((_modifiedBits & RS_POLYGON_OFFSET))
    {
        renderer->setPolygonOffsetEnabled(m_polygonOffsetEnabled);
        renderer->setPolygonOffset(m_offset.m_factor, m_offset.m_units, 0.0);
    }
    if ((_modifiedBits & RS_CLIP_BOUNDS))
    {
        renderer->setScissorTest(m_clipEnabled);
        renderer->setScissorRect(m_glBounds.origin.x, m_glBounds.origin.y, m_glBounds.size.width, m_glBounds.size.height);
    }
    //END BPC PATCH
}

void RenderState::StateBlock::restoreUnmodifiedStates(long overrideBits, PipelineDescriptor *programState)
{
    auto renderer = Director::getInstance()->getRenderer();
    auto &blend = programState->blendDescriptor;

    // Update any state that differs from _globalState and flip _globalState bits
    if (!(overrideBits & RS_BLEND))
    {
        blend.blendEnabled = true;
    }

    if (!(overrideBits & RS_BLEND_FUNC))
    {
        blend.sourceAlphaBlendFactor = blend.sourceRGBBlendFactor = backend::BlendFactor::ONE;
        blend.destinationAlphaBlendFactor = blend.destinationRGBBlendFactor = backend::BlendFactor::ZERO;
    }

    if (!(overrideBits & RS_CULL_FACE))
    {
        renderer->setCullMode(CullMode::NONE);
    }

    if (!(overrideBits & RS_CULL_FACE_SIDE))
    {
        renderer->setCullMode(CullMode::BACK);
    }

    if (!(overrideBits & RS_FRONT_FACE))
    {
        renderer->setWinding(FrontFace::COUNTER_CLOCK_WISE);
    }

    if (!(overrideBits & RS_DEPTH_TEST))
    {
        renderer->setDepthTest(true);
    }

    if (!(overrideBits & RS_DEPTH_WRITE))
    {
        renderer->setDepthWrite(false);
    }


    if (!(overrideBits & RS_DEPTH_FUNC))
    {
        renderer->setDepthCompareFunction(DepthFunction::LESS);
    }
    
    //BPC PATCH
    if (!(overrideBits & RS_STENCIL_TEST))
    {
        renderer->setStencilTest(false);
    }
    
    if (!(overrideBits & RS_STENCIL_WRITE))
    {
        renderer->setStencilWriteMask(RS_ALL_ONES);
    }
    
    if (!(overrideBits & RS_STENCIL_FUNC))
    {
        renderer->setStencilCompareFunction(backend::CompareFunction::ALWAYS, 0, RS_ALL_ONES);
    }
    
    if (!(overrideBits & RS_STENCIL_OP))
    {
        renderer->setStencilOperation(backend::StencilOperation::KEEP, backend::StencilOperation::KEEP, backend::StencilOperation::KEEP);
    }
    
    if (!(overrideBits & RS_POLYGON_OFFSET))
    {
        renderer->setPolygonOffsetEnabled(false);
        renderer->setPolygonOffset(0.0, 0.0, 0.0);
    }
    /*if (!(overrideBits & RS_CLIP_BOUNDS))
    {
        renderer->setScissorTest(false);
    }*/
    //END BPC PATCH
}

static bool parseBoolean(const std::string& value)
{
    return (value.compare("true") == 0);
}

static int parseInt(const std::string& value)
{
    // Android NDK 10 doesn't support std::stoi a/ std::stoul
#if CC_TARGET_PLATFORM != CC_PLATFORM_ANDROID
    return std::stoi(value);
#else
    return atoi(value.c_str());
#endif
}

static unsigned int parseUInt(const std::string& value)
{
    // Android NDK 10 doesn't support std::stoi a/ std::stoul
#if CC_TARGET_PLATFORM != CC_PLATFORM_ANDROID
    return (unsigned int)std::stoul(value);
#else
    return (unsigned int)atoi(value.c_str());
#endif

}

static backend::BlendFactor parseBlend(const std::string& value)
{
    // Convert the string to uppercase for comparison.
    std::string upper(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), (int(*)(int))toupper);
    if (upper == "ZERO")
        return backend::BlendFactor::ZERO;
    else if (upper == "ONE")
        return backend::BlendFactor::ONE;
    else if (upper == "SRC_COLOR")
        return backend::BlendFactor::SRC_COLOR;
    else if (upper == "ONE_MINUS_SRC_COLOR")
        return backend::BlendFactor::ONE_MINUS_SRC_COLOR;
    else if (upper == "DST_COLOR")
        return backend::BlendFactor::DST_COLOR;
    else if (upper == "ONE_MINUS_DST_COLOR")
        return backend::BlendFactor::ONE_MINUS_DST_COLOR;
    else if (upper == "SRC_ALPHA")
        return backend::BlendFactor::SRC_ALPHA;
    else if (upper == "ONE_MINUS_SRC_ALPHA")
        return backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
    else if (upper == "DST_ALPHA")
        return backend::BlendFactor::DST_ALPHA;
    else if (upper == "ONE_MINUS_DST_ALPHA")
        return backend::BlendFactor::ONE_MINUS_DST_ALPHA;
    else if (upper == "CONSTANT_ALPHA")
        return backend::BlendFactor::CONSTANT_ALPHA;
    else if (upper == "ONE_MINUS_CONSTANT_ALPHA")
        return backend::BlendFactor::ONE_MINUS_CONSTANT_ALPHA;
    else if (upper == "SRC_ALPHA_SATURATE")
        return backend::BlendFactor::SRC_ALPHA_SATURATE;
    else
    {
        CCLOG("Unsupported blend value (%s). (Will default to BLEND_ONE if errors are treated as warnings)", value.c_str());
        return backend::BlendFactor::ONE;
    }
}

static DepthFunction parseDepthFunc(const std::string& value)
{
    // Convert string to uppercase for comparison
    std::string upper(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), (int(*)(int))toupper);
    if (upper == "NEVER")
        return DepthFunction::NEVER;
    else if (upper == "LESS")
        return DepthFunction::LESS;
    else if (upper == "EQUAL")
        return DepthFunction::EQUAL;
    else if (upper == "LEQUAL")
        return DepthFunction::LESS_EQUAL;
    else if (upper == "GREATER")
        return DepthFunction::GREATER;
    else if (upper == "NOTEQUAL")
        return DepthFunction::NOT_EQUAL;
    else if (upper == "GEQUAL")
        return DepthFunction::GREATER_EQUAL;
    else if (upper == "ALWAYS")
        return DepthFunction::ALWAYS;
    else
    {
        CCLOG("Unsupported depth function value (%s). Will default to DEPTH_LESS if errors are treated as warnings)", value.c_str());
        return DepthFunction::LESS;
    }
}

static CullFaceSide parseCullFaceSide(const std::string& value)
{
    // Convert string to uppercase for comparison
    std::string upper(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), (int(*)(int))toupper);
    if (upper == "BACK")
        return CullFaceSide::BACK;
    else if (upper == "FRONT")
        return CullFaceSide::FRONT;
// XXX: metal doesn't support back&front culling. Is it needed, since it will draw nothing.
//    else if (upper == "FRONT_AND_BACK")
//        return RenderState::CULL_FACE_SIDE_FRONT_AND_BACK;
    else
    {
        CCLOG("Unsupported cull face side value (%s). Will default to BACK if errors are treated as warnings.", value.c_str());
        return CullFaceSide::BACK;
    }
}

static FrontFace parseFrontFace(const std::string& value)
{
    // Convert string to uppercase for comparison
    std::string upper(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), (int(*)(int))toupper);
    if (upper == "CCW")
        return FrontFace::COUNTER_CLOCK_WISE;
    else if (upper == "CW")
        return FrontFace::CLOCK_WISE;
    else
    {
        CCLOG("Unsupported front face side value (%s). Will default to CCW if errors are treated as warnings.", value.c_str());
        return FrontFace::COUNTER_CLOCK_WISE;
    }
}

static backend::CompareFunction parseStencilFunc(const std::string& value)
{
    // Convert string to uppercase for comparison
    std::string upper(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), (int(*)(int))toupper);
    if (upper == "NEVER")
        return backend::CompareFunction::NEVER;
    else if (upper == "LESS")
        return backend::CompareFunction::LESS;
    else if (upper == "EQUAL")
        return backend::CompareFunction::EQUAL;
    else if (upper == "LEQUAL")
        return backend::CompareFunction::LESS_EQUAL;
    else if (upper == "GREATER")
        return backend::CompareFunction::GREATER;
    else if (upper == "NOTEQUAL")
        return backend::CompareFunction::NOT_EQUAL;
    else if (upper == "GEQUAL")
        return backend::CompareFunction::GREATER_EQUAL;
    else if (upper == "ALWAYS")
        return backend::CompareFunction::ALWAYS;
    else
    {
        CCLOG("Unsupported stencil function value (%s). Will default to ALWAYS if errors are treated as warnings)", value.c_str());
        return backend::CompareFunction::ALWAYS;
    }
}

static backend::StencilOperation parseStencilOp(const std::string& value)
{
    // Convert string to uppercase for comparison
    std::string upper(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), (int(*)(int))toupper);
    if (upper == "KEEP")
        return backend::StencilOperation::KEEP;
    else if (upper == "ZERO")
        return backend::StencilOperation::ZERO;
    else if (upper == "REPLACE")
        return backend::StencilOperation::REPLACE;
    /*else if (upper == "INCR")
        return backend::StencilOperation::INCR;
    else if (upper == "DECR")
        return backend::StencilOperation::DECR;*/
    else if (upper == "INVERT")
        return backend::StencilOperation::INVERT;
    else if (upper == "INCR_WRAP")
        return backend::StencilOperation::INCREMENT_WRAP;
    else if (upper == "DECR_WRAP")
        return backend::StencilOperation::DECREMENT_WRAP;
    else
    {
        CCLOG("Unsupported stencil operation value (%s). Will default to STENCIL_OP_KEEP if errors are treated as warnings)", value.c_str());
        return backend::StencilOperation::KEEP;
    }
}

void RenderState::StateBlock::setState(const std::string& name, const std::string& value)
{
    if (name.compare("blend") == 0)
    {
        setBlend(parseBoolean(value));
    }
    else if (name.compare("blendSrc") == 0)
    {
        setBlendSrc(parseBlend(value));
    }
    else if (name.compare("blendDst") == 0)
    {
        setBlendDst(parseBlend(value));
    }
    else if (name.compare("cullFace") == 0)
    {
        setCullFace(parseBoolean(value));
    }
    else if (name.compare("cullFaceSide") == 0)
    {
        setCullFaceSide(parseCullFaceSide(value));
    }
    else if (name.compare("frontFace") == 0)
    {
        setFrontFace(parseFrontFace(value));
    }
    else if (name.compare("depthTest") == 0)
    {
        setDepthTest(parseBoolean(value));
    }
    else if (name.compare("depthWrite") == 0)
    {
        setDepthWrite(parseBoolean(value));
    }
    else if (name.compare("depthFunc") == 0)
    {
        setDepthFunction(parseDepthFunc(value));
    }
    else if (name.compare("stencilTest") == 0)
    {
        setStencilTest(parseBoolean(value));
    }
    else if (name.compare("stencilWrite") == 0)
    {
        setStencilWrite(parseUInt(value));
    }
    else if (name.compare("stencilFunc") == 0)
    {
        setStencilFunction(parseStencilFunc(value), _stencilFunctionRef, _stencilFunctionMask);
    }
    else if (name.compare("stencilFuncRef") == 0)
    {
        setStencilFunction(_stencilFunction, parseInt(value), _stencilFunctionMask);
    }
    else if (name.compare("stencilFuncMask") == 0)
    {
        setStencilFunction(_stencilFunction, _stencilFunctionRef, parseUInt(value));
    }
    else if (name.compare("stencilOpSfail") == 0)
    {
        setStencilOperation(parseStencilOp(value), _stencilOpDpfail, _stencilOpDppass);
    }
    else if (name.compare("stencilOpDpfail") == 0)
    {
        setStencilOperation(_stencilOpSfail, parseStencilOp(value), _stencilOpDppass);
    }
    else if (name.compare("stencilOpDppass") == 0)
    {
        setStencilOperation(_stencilOpSfail, _stencilOpDpfail, parseStencilOp(value));
    }
    else
    {
        CCLOG("Unsupported render state string '%s'.", name.c_str());
    }
}

bool RenderState::StateBlock::isDirty() const
{
    // XXX
    return true;
}

uint32_t RenderState::StateBlock::getHash() const
{
    // XXX
    return 0x12345678;
}

void RenderState::StateBlock::setBlend(bool enabled)
{
    _blendEnabled = enabled;
    _modifiedBits |= RS_BLEND;
}

void RenderState::StateBlock::setBlendFunc(const BlendFunc& blendFunc)
{
    setBlendSrc(blendFunc.src);
    setBlendDst(blendFunc.dst);
}

void RenderState::StateBlock::setBlendSrc(backend::BlendFactor blend)
{
    _blendSrc = blend;
    _modifiedBits |= RS_BLEND_FUNC;
}

void RenderState::StateBlock::setBlendDst(backend::BlendFactor blend)
{
    _blendDst = blend;
    _modifiedBits |= RS_BLEND_FUNC;
}

void RenderState::StateBlock::setCullFace(bool enabled)
{
    _cullFaceEnabled = enabled;
    _modifiedBits |= RS_CULL_FACE;
}

void RenderState::StateBlock::setCullFaceSide(CullFaceSide side)
{
    _cullFaceSide = side;
    _modifiedBits |= RS_CULL_FACE_SIDE;
}

void RenderState::StateBlock::setFrontFace(FrontFace winding)
{
    _frontFace = winding;
    _modifiedBits |= RS_FRONT_FACE;
}

void RenderState::StateBlock::setDepthTest(bool enabled)
{
    _depthTestEnabled = enabled;
    _modifiedBits |= RS_DEPTH_TEST;
}

void RenderState::StateBlock::setDepthWrite(bool enabled)
{
    _depthWriteEnabled = enabled;
    _modifiedBits |= RS_DEPTH_WRITE;
}

void RenderState::StateBlock::setDepthFunction(DepthFunction func)
{
    _depthFunction = func;
    _modifiedBits |= RS_DEPTH_FUNC;
}

void RenderState::StateBlock::setStencilTest(bool enabled)
{
    _stencilTestEnabled = enabled;
    if (!enabled)
    {
        _modifiedBits &= ~RS_STENCIL_TEST;
    }
    else
    {
        _modifiedBits |= RS_STENCIL_TEST;
    }
}

void RenderState::StateBlock::setStencilWrite(unsigned int mask)
{
    _stencilWrite = mask;
    if (mask == RS_ALL_ONES)
    {
        // Default stencil write
        _modifiedBits &= ~RS_STENCIL_WRITE;
    }
    else
    {
        _modifiedBits |= RS_STENCIL_WRITE;
    }
}

void RenderState::StateBlock::setStencilFunction(backend::CompareFunction func, int ref, unsigned int mask)
{
    _stencilFunction = func;
    _stencilFunctionRef = ref;
    _stencilFunctionMask = mask;
    if (func == backend::CompareFunction::ALWAYS && ref == 0 && mask == RS_ALL_ONES)
    {
        // Default stencil function
        _modifiedBits &= ~RS_STENCIL_FUNC;
    }
    else
    {
        _modifiedBits |= RS_STENCIL_FUNC;
    }
}

void RenderState::StateBlock::setStencilOperation(backend::StencilOperation sfail, backend::StencilOperation dpfail, backend::StencilOperation dppass)
{
    _stencilOpSfail = sfail;
    _stencilOpDpfail = dpfail;
    _stencilOpDppass = dppass;
    if (sfail == backend::StencilOperation::KEEP && dpfail == backend::StencilOperation::KEEP && dppass == backend::StencilOperation::KEEP)
    {
        // Default stencil operation
        _modifiedBits &= ~RS_STENCIL_OP;
    }
    else
    {
        _modifiedBits |= RS_STENCIL_OP;
    }
}

/*BPC PATCH*/

void RenderState::StateBlock::setShouldUsePolygonOffset(bool enabled)
{
    m_polygonOffsetEnabled = enabled;
    if (!enabled)
    {
        _modifiedBits &= ~RS_POLYGON_OFFSET;
    }
    else
    {
        _modifiedBits |= RS_POLYGON_OFFSET;
    }
}

void RenderState::StateBlock::setOffset(PolygonOffset const & offset)
{
    m_offset = offset;
}

void RenderState::StateBlock::setShouldClip(bool shouldClip)
{
    m_clipEnabled = shouldClip;
    if (!shouldClip)
    {
        _modifiedBits &= ~RS_CLIP_BOUNDS;
    }
    else
    {
        _modifiedBits |= RS_CLIP_BOUNDS;
    }
}

void RenderState::StateBlock::setGlBounds(Rect glBounds)
{
    m_glBounds = glBounds;
}

/*BPC PATCH END*/

NS_CC_END
