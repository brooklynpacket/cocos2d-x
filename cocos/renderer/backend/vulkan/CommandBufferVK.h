/****************************************************************************
 Vulkan backend: command buffer.

 Records into the device's per-frame command buffer. Render passes follow the
 Metal backend's rules: consecutive passes on the same target that don't
 clear and don't add attachments are merged into one Vulkan render pass,
 and endRenderPass() does not end the Vulkan pass.

 Coordinates follow Metal's conventions (see Device::isTopLeftOrigin): the
 viewport is flipped with a negative height so clip space is y-up while
 images keep Vulkan's top-left origin, and clip-space depth is 0..1. The
 engine's projection matrices already produce 0..1 depth in that mode.
 ****************************************************************************/
#pragma once

#include "../CommandBuffer.h"
#include "../DepthStencilState.h"
#include "UtilsVK.h"

#include <functional>

CC_BACKEND_BEGIN

class DeviceVK;
class BufferVK;
class ProgramVK;
class RenderPipelineVK;

class CommandBufferVK : public CommandBuffer
{
public:
    explicit CommandBufferVK(DeviceVK* device);
    ~CommandBufferVK() override;

    void beginFrame() override;
    void beginRenderPass(const RenderPassDescriptor& descriptor) override;
    void setRenderPipeline(RenderPipeline* renderPipeline) override;
    void setViewport(int x, int y, unsigned int w, unsigned int h) override;
    void setCullMode(CullMode mode) override { _cullMode = mode; }
    void setWinding(Winding winding) override { _winding = winding; }
    void setVertexBuffer(Buffer* buffer) override;
    void setProgramState(ProgramState* programState) override;
    void setIndexBuffer(Buffer* buffer) override;
    void drawArrays(PrimitiveType primitiveType, std::size_t start, std::size_t count) override;
    void drawElements(PrimitiveType primitiveType, IndexFormat indexType, std::size_t count, std::size_t offset) override;
    void endRenderPass() override {}
    void endFrame() override;
    void setLineWidth(float lineWidth) override { _lineWidth = lineWidth > 0.0f ? lineWidth : 1.0f; }
    void setScissorRect(bool isEnabled, float x, float y, float width, float height) override;
    void setDepthStencilState(DepthStencilState* depthStencilState) override;
    void setPolygonOffset(bool enabled, double slope, double constant, double clamp) override;
    void captureScreen(std::function<void(const unsigned char*, int, int)> callback) override;
    const RenderPassDescriptor& getRenderPassDescriptor() const override { return _passDescriptor; }

private:
    void openPass(const RenderPassDescriptor& descriptor);
    void closePass();
    bool prepareDraw(PrimitiveType primitiveType);
    VkPipeline getPipeline(ProgramVK* program, PrimitiveType primitiveType);
    void releaseDrawResources();

    DeviceVK* _device = nullptr;
    VkCommandBuffer _cb = VK_NULL_HANDLE;

    // Render pass in use.
    RenderPassDescriptor _passDescriptor;
    bool _passOpen = false;
    VkRenderPass _renderPass = VK_NULL_HANDLE;
    VkFormat _passColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat _passDepthFormat = VK_FORMAT_UNDEFINED;
    uint32_t _targetWidth = 0;
    uint32_t _targetHeight = 0;

    // Pipeline state.
    RenderPipelineVK* _renderPipeline = nullptr;
    bool _depthStencilEnabled = false;
    DepthStencilDescriptor _depthStencil;
    CullMode _cullMode = CullMode::NONE;
    Winding _winding = Winding::COUNTER_CLOCK_WISE;
    float _lineWidth = 1.0f;
    bool _polygonOffsetEnabled = false;
    float _polygonOffsetSlope = 0.0f;
    float _polygonOffsetConstant = 0.0f;
    float _polygonOffsetClamp = 0.0f;

    // Dynamic state.
    int _viewportX = 0, _viewportY = 0;
    unsigned int _viewportW = 0, _viewportH = 0;
    bool _scissorEnabled = false;
    float _scissorX = 0, _scissorY = 0, _scissorW = 0, _scissorH = 0;
    VkPipeline _boundPipeline = VK_NULL_HANDLE;

    // Per-draw resources (released after each draw, like the GL backend).
    BufferVK* _vertexBuffer = nullptr;
    BufferVK* _indexBuffer = nullptr;
    ProgramState* _programState = nullptr;

    // Supplies (0,0,0,1) to shader inputs the vertex layout doesn't provide,
    // which is what GL returns for a disabled attribute array.
    BufferVK* _defaultAttributeBuffer = nullptr;

    std::function<void(const unsigned char*, int, int)> _captureCallback;
};

CC_BACKEND_END
