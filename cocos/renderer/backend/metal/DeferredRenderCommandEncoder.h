//
//  deferredRenderCommandEncoder.h
//  cocos2d_libs
//
//  Created by Jesse Lampert on 7/6/21.
//
#pragma once

#include "DeviceMTL.h"

CC_BACKEND_BEGIN

extern MTLRenderPassDescriptor* toMTLRenderPassDescriptor(const RenderPassDescriptor& descriptor);

class DeferredRenderCommandEncoder
{
public:
  DeferredRenderCommandEncoder(id<MTLCommandQueue> mtlCommandQueue) : _mtlCommandQueue(mtlCommandQueue)
  {
    [_mtlCommandQueue retain];
  }
  
  ~DeferredRenderCommandEncoder()
  {
    if(_mtlRenderEncoder)
    {
      [_mtlRenderEncoder release];
      _mtlRenderEncoder = nil;
    }
    
    if(_mtlCommandBuffer)
    {
      [_mtlCommandBuffer release];
      _mtlCommandBuffer = nil;
    }
    
    [_mtlCommandQueue release];
    _renderPassDescriptor = nullptr;
  }
  
  const RenderPassDescriptor * GetRenderPassDescriptor() const
  {
    return _renderPassDescriptor;
  }
  
  void SetRenderPassDescriptor(const RenderPassDescriptor * renderPassDescriptor)
  {
    _renderPassDescriptor = renderPassDescriptor;
    
    //
    // some render passes are empty and only contain a clear, retain these values and apply them on the next real render pass
    //
    if( _renderPassDescriptor ) {
      if( _renderPassDescriptor->needClearColor ){
        _needClearColor = true;
        _clearColorValue =_renderPassDescriptor->clearColorValue;
      }
      
      if( _renderPassDescriptor->needClearDepth ){
        _needClearDepth = true;
        _clearDepthValue =_renderPassDescriptor->clearDepthValue;
      }
      
      if( _renderPassDescriptor->needClearStencil ){
        _needClearStencil = true;
        _clearStencilValue =_renderPassDescriptor->clearStencilValue;
      }
    }
  }
  
  void Begin()
  {
    _began = true;
    _renderPipelineState = nullptr;
    
    // release old buffer if we're still holding onto it
    if(_mtlCommandBuffer)
    {
      [_mtlCommandBuffer release];
    }
    
    // create a command buffer
    _mtlCommandBuffer = [_mtlCommandQueue commandBuffer];
    [_mtlCommandBuffer retain];
    [_mtlCommandBuffer enqueue];
  }
  
  void End()
  {
    if(_mtlRenderEncoder)
    {
      [_mtlRenderEncoder endEncoding];
      [_mtlRenderEncoder release];
      _mtlRenderEncoder = nil;
    }
    
    _renderPassDescriptor = nullptr;
    _renderPipelineState = nullptr;
  }
  
  void Present(id<MTLTexture> & drawableTexture, void (^block)(id <MTLCommandBuffer>))
  {
    if(!_mtlCommandBuffer)
    {
      block(nil);
    }
    else
    {
      [_mtlCommandBuffer presentDrawable:DeviceMTL::getCurrentDrawable()];
      drawableTexture = DeviceMTL::getCurrentDrawable().texture;
      
      printf("End...\n");
      [_mtlCommandBuffer addCompletedHandler:block];

      [_mtlCommandBuffer commit];
      [_mtlCommandBuffer release];
      //BPC PATCH
      _mtlCommandBuffer = nil;
    }
    
    _began = false;
  }
  
  id<MTLCommandBuffer> commandBuffer()
  {
    return _mtlCommandBuffer;
  }
  
  id<MTLRenderCommandEncoder> encoder() const
  {
    if( _mtlRenderEncoder == nil )
    {
      const_cast<DeferredRenderCommandEncoder *>(this)->CreateRenderCommandEncoder();
    }
    
    return _mtlRenderEncoder;
  }
  
  void setRenderPipelineState(id <MTLRenderPipelineState> renderPipelineState)
  {
    if( _renderPipelineState != renderPipelineState )
    {
      _renderPipelineState = renderPipelineState;
      
      if(_mtlRenderEncoder)
      {
        [_mtlRenderEncoder setRenderPipelineState:renderPipelineState];
      }
    }
    
    _needClearStencil = false;
    _needClearDepth = false;
    _needClearColor = false;
  }
  
private:
  id<MTLRenderCommandEncoder> _mtlRenderEncoder = nil;
  id<MTLCommandBuffer> _mtlCommandBuffer = nil;
  id<MTLCommandQueue> _mtlCommandQueue = nil;
  id <MTLRenderPipelineState> _renderPipelineState = nil;
  const RenderPassDescriptor * _renderPassDescriptor = nullptr;
  unsigned int _renderTargetWidth = 0;
  unsigned int _renderTargetHeight = 0;
  
  bool _needClearColor = false;
  bool _needClearDepth = false;
  bool _needClearStencil = false;
  float _clearDepthValue = 0.f;
  float _clearStencilValue = 0.f;
  std::array<float, 4> _clearColorValue {{0.f, 0.f, 0.f, 0.f}};
    
  bool _began = false;
  
public:
    void preprocessRenderPassDescriptor(const RenderPassDescriptor & source, RenderPassDescriptor & result ) const
    {
      result = source;
      
      if( _needClearColor ) {
        result.needClearColor = true;
        result.clearColorValue = _clearColorValue;
      }
      
      if( _needClearDepth ) {
        result.needClearDepth = true;
        result.clearDepthValue = _clearDepthValue;
      }
      
      if( _needClearStencil ) {
        result.needClearStencil = true;
        result.clearStencilValue = _clearStencilValue;
      }
    }
  
private:
  void CreateRenderCommandEncoder()
  {
    assert(_began);
    assert(_renderPassDescriptor != nullptr);
    
    RenderPassDescriptor renderPassDescriptor = *_renderPassDescriptor;
    
    if( _needClearColor ) {
      renderPassDescriptor.needClearColor = true;
      renderPassDescriptor.clearColorValue = _clearColorValue;
    }
    
    if( _needClearDepth ) {
      renderPassDescriptor.needClearDepth = true;
      renderPassDescriptor.clearDepthValue = _clearDepthValue;
    }
    
    if( _needClearStencil ) {
      renderPassDescriptor.needClearStencil = true;
      renderPassDescriptor.clearStencilValue = _clearStencilValue;
    }
        
    // create the encoder
    auto mtlDescriptor = toMTLRenderPassDescriptor(renderPassDescriptor);
    _renderTargetWidth = (unsigned int)mtlDescriptor.colorAttachments[0].texture.width;
    _renderTargetHeight = (unsigned int)mtlDescriptor.colorAttachments[0].texture.height;
    
    _mtlRenderEncoder = [_mtlCommandBuffer renderCommandEncoderWithDescriptor:mtlDescriptor];
    [_mtlRenderEncoder retain];
    
    if(_renderPipelineState)
    {
      [_mtlRenderEncoder setRenderPipelineState:_renderPipelineState];
      _renderPipelineState = nullptr;
    }
  }
};

CC_BACKEND_END
