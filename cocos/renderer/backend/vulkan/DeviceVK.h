/****************************************************************************
 Vulkan backend: the device.

 Frame model
 -----------
 Frames are recorded into one main command buffer per frame, with up to
 kFramesInFlight frames queued on the GPU. Texture and buffer uploads are
 recorded into a separate upload command buffer that is submitted ahead of
 the frame's main command buffer, so uploads never have to happen inside a
 render pass.

 The engine renders into an offscreen "backbuffer" image rather than straight
 into swapchain images. At the end of each frame the backbuffer is drawn onto
 the swapchain image (applying the surface's pre-rotation). This means the
 Android surface can be created, resized or destroyed from the UI thread at
 any time without waiting for the render thread to finish a frame: the
 swapchain is only touched for a moment at present time, under a lock.

 Resources freed while frames are in flight are destroyed once every frame
 that might reference them has finished (deferDestroy).
 ****************************************************************************/
#pragma once

#include <atomic>

#include "../Device.h"
#include "MemoryVK.h"

#include <android/native_window.h>

#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

CC_BACKEND_BEGIN

class ProgramVK;

struct RenderPassKeyVK
{
    VkFormat colorFormat = VK_FORMAT_UNDEFINED; // UNDEFINED: no color attachment
    VkFormat depthFormat = VK_FORMAT_UNDEFINED; // UNDEFINED: no depth/stencil attachment
    VkAttachmentLoadOp colorLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentLoadOp depthLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentLoadOp stencilLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp depthStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // layout before and after the pass
    VkImageLayout depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::size_t hash() const;
    bool operator==(const RenderPassKeyVK& o) const;
};

class DeviceVK : public Device
{
public:
    static const uint32_t kFramesInFlight = 2;

    /** Creates the singleton if Vulkan is usable on this device. */
    static bool probe();
    /** The Vulkan device singleton, or null when Vulkan is not in use. */
    static DeviceVK* get() { return s_device; }

    ~DeviceVK() override;

    // Device
    CommandBuffer* newCommandBuffer() override;
    Buffer* newBuffer(std::size_t size, BufferType type, BufferUsage usage) override;
    TextureBackend* newTexture(const TextureDescriptor& descriptor) override;
    DepthStencilState* createDepthStencilState(const DepthStencilDescriptor& descriptor) override;
    RenderPipeline* newRenderPipeline() override;
    void setFrameBufferOnly(bool frameBufferOnly) override {}
    Program* newProgram(const std::string& vertexShader, const std::string& fragmentShader, Program::CompileResult& result) override;
    Program* newProgram(unsigned int format, const std::string binary, Program::CompileResult& result) override;

    // Vulkan objects
    VkInstance getInstanceVK() const { return _instance; }
    VkPhysicalDevice getPhysicalDevice() const { return _physicalDevice; }
    VkDevice getDevice() const { return _device; }
    uint32_t getQueueFamily() const { return _queueFamily; }
    const VkPhysicalDeviceProperties& getProperties() const { return _properties; }
    const VkPhysicalDeviceFeatures& getFeatures() const { return _features; }
    MemoryAllocatorVK& getMemory() { return _memory; }
    VkPipelineCache getPipelineCache() const { return _pipelineCache; }
    VkFormat getDepthStencilFormat() const { return _depthStencilFormat; }
    VkFormat getDepthOnlyFormat() const { return _depthOnlyFormat; }
    bool isFormatSupported(VkFormat format, VkFormatFeatureFlags features) const;

    // Frames
    /** Serial of the frame currently being recorded; starts at 1. */
    uint64_t getFrameSerial() const { return _frameSerial; }
    VkCommandBuffer getFrameCommandBuffer() const { return _frames[_frameIndex].mainCommandBuffer; }
    /** Command buffer for copies that must run before this frame's draws. */
    VkCommandBuffer getUploadCommandBuffer();
    /**
     * Submits everything recorded for the current frame and presents the
     * backbuffer if a window is attached. captureScreen, when set, receives
     * the backbuffer's pixels (RGBA8, top row first) once the frame is done.
     */
    void endFrame(std::function<void(const unsigned char*, int, int)> captureScreen);
    /** Records commands into a one-off command buffer and waits for them to finish. */
    void executeNow(const std::function<void(VkCommandBuffer)>& record);
    /** Runs fn once all work submitted so far and the current frame have finished on the GPU. */
    void deferDestroy(std::function<void()> fn);

    // Per-frame transient memory, valid until the frame finishes on the GPU.
    struct TransientAllocation
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        void* mapped = nullptr;
    };
    bool allocateUniform(VkDeviceSize size, TransientAllocation& out);
    bool allocateStaging(VkDeviceSize size, VkDeviceSize alignment, TransientAllocation& out);
    VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout layout);

    // Caches
    VkRenderPass getRenderPass(const RenderPassKeyVK& key);
    VkFramebuffer getFramebuffer(VkRenderPass renderPass, VkImageView color, VkImageView depth, uint32_t width, uint32_t height);
    /** Called when an image view is destroyed so framebuffers using it are dropped. */
    void forgetImageView(VkImageView view);
    VkSampler getSampler(const SamplerDescriptor& descriptor, bool mipmapped, bool compare);
    /** True if the key is cached; pipeline is VK_NULL_HANDLE for a pipeline that failed to build. */
    bool hasPipeline(std::size_t key, VkPipeline& pipeline) const;
    void addPipeline(std::size_t key, uint32_t programId, VkPipeline pipeline);
    void forgetProgram(uint32_t programId);
    /** 1x1 opaque black textures bound to samplers the game left unset (what GL returns for an incomplete texture). */
    TextureBackend* getDummyTexture(bool cube);
    /** 1x1 depth texture (depth 1.0) for shadow samplers; compare-sampling a color image is undefined. */
    TextureBackend* getDummyDepthTexture();

    // Default framebuffer
    /** Makes sure the backbuffer and default depth buffer match the given size. */
    void ensureBackbuffer(uint32_t width, uint32_t height);
    VkImageView getBackbufferView() const { return _backbuffer.view; }
    VkImageView getDefaultDepthView() const { return _defaultDepth.view; }
    VkFormat getBackbufferFormat() const { return VK_FORMAT_R8G8B8A8_UNORM; }
    uint32_t getBackbufferWidth() const { return _backbufferWidth; }
    uint32_t getBackbufferHeight() const { return _backbufferHeight; }

    // Presentation. Safe to call from any thread.
    /** Attaches (or with null, detaches) the window to present into. Takes its own reference. */
    void setWindow(ANativeWindow* window);
    /** The attached window changed size or orientation; the swapchain is rebuilt before the next present. */
    void onWindowResized();
    /** Number of frames handed to the compositor so far. Safe to read from any thread. */
    uint64_t getPresentCount() const { return _presentCount.load(std::memory_order_acquire); }

protected:
    ShaderModule* newShaderModule(ShaderStage stage, const std::string& source, Program::CompileResult& result) override;

private:
    DeviceVK();
    bool init();
    bool createDevice();
    bool createFrames();
    bool createPresentPipeline();
    void destroySwapchainLocked();
    bool createSwapchainLocked();
    void recordPresent(VkCommandBuffer cb, uint32_t imageIndex);
    void beginFrameResources();
    void onDeviceLost();
    bool _deviceLost = false;
    void waitForFrame(uint32_t index);
    void loadPipelineCache();
    void savePipelineCache();

    struct Chunk
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        AllocationVK allocation;
        VkDeviceSize size = 0;
        VkDeviceSize used = 0;
    };
    struct Ring
    {
        std::vector<Chunk> chunks;
        size_t current = 0;
    };
    bool allocateFromRing(Ring& ring, VkDeviceSize size, VkDeviceSize alignment, VkBufferUsageFlags usage,
                          VkDeviceSize chunkSize, TransientAllocation& out);
    void resetRing(Ring& ring);
    void destroyRing(Ring& ring);

    struct Frame
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer mainCommandBuffer = VK_NULL_HANDLE;
        VkCommandBuffer presentCommandBuffer = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> uploadCommandBuffers; // reused across frames
        size_t uploadUsed = 0;
        bool uploadRecording = false;
        VkFence fence = VK_NULL_HANDLE;
        bool submitted = false;
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> descriptorPools;
        size_t descriptorPoolIndex = 0;
        Ring uniforms;
        Ring staging;
        std::vector<std::function<void()>> deferred;
    };

    struct ImageVK
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        AllocationVK allocation;
    };
    bool createImage(ImageVK& out, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void destroyImageDeferred(ImageVK& image);
    void submitUploadsLocked(Frame& frame);

    static DeviceVK* s_device;

    VkInstance _instance = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _queue = VK_NULL_HANDLE;
    uint32_t _queueFamily = 0;
    VkPhysicalDeviceProperties _properties{};
    VkPhysicalDeviceFeatures _features{};
    VkPhysicalDeviceFeatures _enabledFeatures{};
    MemoryAllocatorVK _memory;
    VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
    VkFormat _depthStencilFormat = VK_FORMAT_UNDEFINED;
    VkFormat _depthOnlyFormat = VK_FORMAT_UNDEFINED;

    Frame _frames[kFramesInFlight];
    uint32_t _frameIndex = 0;
    uint64_t _frameSerial = 1;
    std::atomic<uint64_t> _presentCount{ 0 };
    uint32_t _pipelinesSinceSave = 0;

    // Guards the queue and everything below it that the UI thread touches.
    std::mutex _queueMutex;

    std::unordered_map<std::size_t, std::pair<RenderPassKeyVK, VkRenderPass>> _renderPasses;
    struct FramebufferEntry
    {
        VkFramebuffer framebuffer;
        VkImageView color;
        VkImageView depth;
    };
    std::unordered_map<std::size_t, FramebufferEntry> _framebuffers;
    std::unordered_map<std::size_t, VkSampler> _samplers;
    std::unordered_map<std::size_t, VkPipeline> _pipelines;
    std::unordered_map<uint32_t, std::vector<std::size_t>> _pipelinesByProgram;
    TextureBackend* _dummyTexture2D = nullptr;
    TextureBackend* _dummyTextureCube = nullptr;
    TextureBackend* _dummyDepthTexture = nullptr;

    ImageVK _backbuffer;
    ImageVK _defaultDepth;
    uint32_t _backbufferWidth = 0;
    uint32_t _backbufferHeight = 0;

    // Presentation; guarded by _queueMutex.
    ANativeWindow* _window = nullptr;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D _swapchainExtent{};
    VkSurfaceTransformFlagBitsKHR _swapchainTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainViews;
    std::vector<VkFramebuffer> _swapchainFramebuffers;
    std::vector<VkSemaphore> _renderDoneSemaphores; // one per swapchain image
    bool _swapchainDirty = true;
    VkRenderPass _presentRenderPass = VK_NULL_HANDLE;
    VkFormat _presentRenderPassFormat = VK_FORMAT_UNDEFINED;
    VkPipeline _presentPipeline = VK_NULL_HANDLE;
    VkPipelineLayout _presentPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _presentSetLayout = VK_NULL_HANDLE;
    uint32_t _presentSamplerBinding = 1;
    std::vector<uint32_t> _presentVertexSpirv;
    std::vector<uint32_t> _presentFragmentSpirv;
};

CC_BACKEND_END
