/****************************************************************************
 Copyright (c) 2018-2019 Xiamen Yaji Software Co., Ltd.

 http://www.cocos2d-x.org

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
 
#include "BufferManager.h"
#include "BufferMTL.h"
#include "DeviceMTL.h"
#include <unordered_map>
#include <mach/mach.h>

CC_BACKEND_BEGIN

std::vector<BufferMTL*> BufferManager::_buffers;

static std::unordered_map<void *, unsigned> _autoBuffers;
static void * _currentAutoBuffer = nullptr;
static char * _currentAutoDataBuffer = nullptr;
static size_t _currentAutoBufferSize = 0;
static size_t _currentAutoBufferOffset = 0;

void BufferManager::addBuffer(BufferMTL* buffer)
{
    _buffers.push_back(buffer);
}

void BufferManager::removeBuffer(BufferMTL* buffer)
{
    auto iter = std::find(_buffers.begin(), _buffers.end(), buffer);
    if (_buffers.end() != iter)
        _buffers.erase(iter);
}

void BufferManager::beginFrame()
{
    for (auto& buffer : _buffers)
        buffer->beginFrame();
}

static void createNewDeviceBuffer()
{
    size_t size = getpagesize();
    vm_address_t address;
  
    vm_allocate((vm_map_t)mach_task_self(),
                &address,
                size,
                VM_FLAGS_ANYWHERE);
  
    id<MTLDevice> device = const_cast<id<MTLDevice>>(((DeviceMTL *)DeviceMTL::getInstance())->getMTLDevice());
    
    _currentAutoDataBuffer = (char *)address;
    _currentAutoBufferSize = size;
    _currentAutoBufferOffset = 0;
    _currentAutoBuffer = [device newBufferWithBytesNoCopy:(void *)address
                                                   length:size
                                                  options:MTLResourceStorageModeShared
                                              deallocator:[](void *pointer, NSUInteger length){
        vm_deallocate((vm_map_t)mach_task_self(),
                      (vm_address_t)pointer,
                      length);
    }];
  
    _autoBuffers.insert({_currentAutoBuffer, 0});
}

void BufferManager::allocateDeviceBuffer(size_t size, void ** deviceBuffer, char ** dataBuffer, size_t * offset)
{
  if(size == 0) {
    *dataBuffer = nullptr;
    *deviceBuffer = nullptr;
    *offset = 0;
  } else {
    // create a new buffer if we don't have enough space left
    if(_currentAutoBufferOffset + size > _currentAutoBufferSize) {
      createNewDeviceBuffer();
    }
    
    *dataBuffer = _currentAutoDataBuffer + _currentAutoBufferOffset;
    *deviceBuffer = _currentAutoBuffer;
    *offset = _currentAutoBufferOffset;

    _autoBuffers[_currentAutoBuffer] += 1;
    _currentAutoBufferOffset += size;
  }
}

void BufferManager::releaseDeviceBuffer(void * buffer)
{
  if(buffer) {
    auto iterator = _autoBuffers.find(buffer);
    
    if(iterator != _autoBuffers.end()) {
      iterator->second -= 1;
      
      if(iterator->second == 0) {
        id<MTLBuffer> mtlBuffer = (id<MTLBuffer>)iterator->first;
        [mtlBuffer release];
      }
    }
  }
}

CC_BACKEND_END
