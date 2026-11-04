/****************************************************************************
 Vulkan backend: the device. See DeviceVK.h for the frame model.
 ****************************************************************************/
#include "DeviceVK.h"
#include "BufferVK.h"
#include "CommandBufferVK.h"
#include "DepthStencilStateVK.h"
#include "DeviceInfoVK.h"
#include "ProgramVK.h"
#include "RenderPipelineVK.h"
#include "ShaderCompilerVK.h"
#include "TextureVK.h"

#include "platform/CCFileUtils.h"

#include <vulkan/vulkan_android.h>
#include <sys/system_properties.h>

#include <cstdlib>
#include <cstring>

CC_BACKEND_BEGIN

DeviceVK* DeviceVK::s_device = nullptr;

// Device entry points declared in Device.h.
bool Device::tryUseVulkan()
{
    return DeviceVK::probe();
}

Device* Device::createVulkanDevice()
{
    DeviceVK* device = DeviceVK::get();
    if (device)
        setBackendAPI(BackendAPI::VULKAN);
    return device;
}

namespace
{
    const char* kPipelineCacheFile = "vk_pipeline_cache.bin";
    // Written when the GPU device is lost; later launches use OpenGL ES.
    const char* kDeviceLostMarkerFile = "vk_device_lost";
    const VkDeviceSize kUniformChunkSize = 1024 * 1024;
    const VkDeviceSize kStagingChunkSize = 8 * 1024 * 1024;
    // Bounded swapchain acquire: 25 x 20ms before a frame is dropped. Some
    // Android releases ignore the timeout and block until an image is free,
    // which is no worse than eglSwapBuffers.
    const uint64_t kAcquireTimeoutNs = 20ull * 1000 * 1000;
    const int kAcquireAttempts = 25;

    // A trivial shader module so ShaderCache-based code paths keep working;
    // ProgramVK compiles both stages itself.
    class ShaderModuleVK : public ShaderModule
    {
    public:
        ShaderModuleVK(ShaderStage stage, const std::string& source)
        : ShaderModule(stage)
        {
            setHashValue(std::hash<std::string>()(source));
        }
    };

    // Fullscreen triangle that copies the backbuffer onto the swapchain image.
    // The push constants map output UV to source UV, which applies the
    // surface pre-rotation.
    const char* kPresentVertexShader = R"(
layout(push_constant) uniform PresentTransform { vec4 u; vec4 v; } pc;
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    v_uv = vec2(dot(pc.u.xy, p) + pc.u.z, dot(pc.v.xy, p) + pc.v.z);
}
)";
    const char* kPresentFragmentShader = R"(
precision mediump float;
layout(location = 0) in vec2 v_uv;
uniform sampler2D u_texture;
void main()
{
    gl_FragColor = texture2D(u_texture, v_uv);
}
)";

    // "vulkan" or "gl" forces a backend for testing:
    //   adb shell setprop debug.cocos.renderer gl
    std::string rendererOverride()
    {
        char value[PROP_VALUE_MAX] = {0};
        __system_property_get("debug.cocos.renderer", value);
        return value;
    }

    // Drivers known to misbehave with this backend. Matched on the
    // VkPhysicalDeviceProperties of the first GPU. Extend as crash reports
    // come in; each entry should say why it is here.
    bool isDenylisted(const VkPhysicalDeviceProperties& props)
    {
        (void)props;
        return false;
    }

#if COCOS2D_DEBUG > 0
    VKAPI_ATTR VkBool32 VKAPI_CALL debugMessenger(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                  VkDebugUtilsMessageTypeFlagsEXT,
                                                  const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
    {
        // Each distinct message is logged a few times, then suppressed.
        static std::unordered_map<int32_t, int> seen;
        if (++seen[data->messageIdNumber] > 3)
            return VK_FALSE;
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            VK_LOGE("validation: %s", data->pMessage);
        else
            VK_LOGW("validation: %s", data->pMessage);
        return VK_FALSE;
    }
#endif

    std::size_t hashBytes(const void* data, std::size_t size)
    {
        std::size_t h = 1469598103934665603ull;
        const unsigned char* p = (const unsigned char*)data;
        for (std::size_t i = 0; i < size; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }
}

std::size_t RenderPassKeyVK::hash() const
{
    std::size_t h = 0;
    UtilsVK::hashCombine(h, colorFormat);
    UtilsVK::hashCombine(h, depthFormat);
    UtilsVK::hashCombine(h, colorLoad);
    UtilsVK::hashCombine(h, depthLoad);
    UtilsVK::hashCombine(h, stencilLoad);
    UtilsVK::hashCombine(h, depthStore);
    UtilsVK::hashCombine(h, colorLayout);
    UtilsVK::hashCombine(h, depthLayout);
    return h;
}

bool RenderPassKeyVK::operator==(const RenderPassKeyVK& o) const
{
    return colorFormat == o.colorFormat && depthFormat == o.depthFormat && colorLoad == o.colorLoad &&
           depthLoad == o.depthLoad && stencilLoad == o.stencilLoad && depthStore == o.depthStore &&
           colorLayout == o.colorLayout && depthLayout == o.depthLayout;
}

bool DeviceVK::probe()
{
    if (s_device)
        return true;

    std::string forced = rendererOverride();
    if (forced == "gl" || forced == "gles" || forced == "opengl")
    {
        VK_LOGI("debug.cocos.renderer=%s: using OpenGL ES", forced.c_str());
        return false;
    }
    if (forced != "vulkan" &&
        FileUtils::getInstance()->isFileExist(FileUtils::getInstance()->getWritablePath() + kDeviceLostMarkerFile))
    {
        VK_LOGW("Vulkan lost the device on an earlier run; using OpenGL ES (setprop debug.cocos.renderer vulkan to override)");
        return false;
    }

    DeviceVK* device = new (std::nothrow) DeviceVK();
    if (!device)
        return false;
    if (!device->init())
    {
        VK_LOGW("Vulkan unavailable, falling back to OpenGL ES");
        // The destructor tolerates a partially initialised device.
        delete device;
        return false;
    }
    s_device = device;
    return true;
}

DeviceVK::DeviceVK()
{
}

bool DeviceVK::init()
{
    // Instance
    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "cocos2d-x";
    app.pEngineName = "cocos2d-x";
    app.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
    std::vector<const char*> layers;
#if COCOS2D_DEBUG > 0
    {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        for (const auto& layer : available)
        {
            if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                VK_LOGI("enabling validation layer");
            }
        }
    }
#endif

    VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = (uint32_t)extensions.size();
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    instanceInfo.enabledLayerCount = (uint32_t)layers.size();
    instanceInfo.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&instanceInfo, nullptr, &_instance) != VK_SUCCESS)
    {
        VK_LOGW("vkCreateInstance failed");
        return false;
    }
#if COCOS2D_DEBUG > 0
    if (!layers.empty())
    {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(_instance, "vkCreateDebugUtilsMessengerEXT");
        if (create)
        {
            VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            info.pfnUserCallback = debugMessenger;
            VkDebugUtilsMessengerEXT messenger;
            create(_instance, &info, nullptr, &messenger);
        }
    }
#endif

    // Physical device: the first one with Vulkan 1.1 and a graphics queue.
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(_instance, &gpuCount, nullptr);
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(_instance, &gpuCount, gpus.data());
    for (auto gpu : gpus)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.apiVersion < VK_API_VERSION_1_1)
        {
            VK_LOGW("skipping %s: Vulkan %u.%u", props.deviceName, VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
            continue;
        }
        if (isDenylisted(props))
        {
            VK_LOGW("skipping %s: driver is denylisted", props.deviceName);
            continue;
        }
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                _physicalDevice = gpu;
                _queueFamily = i;
                _properties = props;
                break;
            }
        }
        if (_physicalDevice)
            break;
    }
    if (!_physicalDevice)
    {
        VK_LOGW("no suitable Vulkan GPU");
        return false;
    }
    vkGetPhysicalDeviceFeatures(_physicalDevice, &_features);
    VK_LOGI("using %s, Vulkan %u.%u.%u, driver 0x%x, vendor 0x%x",
            _properties.deviceName,
            VK_VERSION_MAJOR(_properties.apiVersion), VK_VERSION_MINOR(_properties.apiVersion), VK_VERSION_PATCH(_properties.apiVersion),
            _properties.driverVersion, _properties.vendorID);

    if (!createDevice())
        return false;

    // Formats
    const VkFormatFeatureFlags depthFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (VkFormat f : { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT })
    {
        if (isFormatSupported(f, depthFeatures))
        {
            _depthStencilFormat = f;
            break;
        }
    }
    for (VkFormat f : { VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM })
    {
        if (isFormatSupported(f, depthFeatures))
        {
            _depthOnlyFormat = f;
            break;
        }
    }
    if (_depthStencilFormat == VK_FORMAT_UNDEFINED || _depthOnlyFormat == VK_FORMAT_UNDEFINED)
    {
        VK_LOGW("no usable depth formats");
        return false;
    }
    if (!isFormatSupported(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        VK_LOGW("RGBA8 is not renderable");
        return false;
    }

    _memory.init(_physicalDevice, _device);
    loadPipelineCache();
    if (!createFrames())
        return false;
    if (!createPresentPipeline())
        return false;

    _deviceInfo = new (std::nothrow) DeviceInfoVK(this);
    if (!_deviceInfo || !_deviceInfo->init())
        return false;

    beginFrameResources();
    return true;
}

bool DeviceVK::createDevice()
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = _queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    _enabledFeatures = VkPhysicalDeviceFeatures{};
    _enabledFeatures.textureCompressionETC2 = _features.textureCompressionETC2;
    _enabledFeatures.textureCompressionASTC_LDR = _features.textureCompressionASTC_LDR;
    _enabledFeatures.wideLines = _features.wideLines;
    // GL ES drivers tolerate out-of-range vertex/uniform reads; match that
    // instead of faulting the GPU.
    _enabledFeatures.robustBufferAccess = _features.robustBufferAccess;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = deviceExtensions;
    info.pEnabledFeatures = &_enabledFeatures;
    if (vkCreateDevice(_physicalDevice, &info, nullptr, &_device) != VK_SUCCESS)
    {
        VK_LOGW("vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(_device, _queueFamily, 0, &_queue);
    return true;
}

bool DeviceVK::createFrames()
{
    for (auto& frame : _frames)
    {
        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = _queueFamily;
        if (vkCreateCommandPool(_device, &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS)
            return false;

        VkCommandBuffer buffers[2];
        VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        alloc.commandPool = frame.commandPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 2;
        if (vkAllocateCommandBuffers(_device, &alloc, buffers) != VK_SUCCESS)
            return false;
        frame.mainCommandBuffer = buffers[0];
        frame.presentCommandBuffer = buffers[1];

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (vkCreateFence(_device, &fenceInfo, nullptr, &frame.fence) != VK_SUCCESS)
            return false;
        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(_device, &semInfo, nullptr, &frame.acquireSemaphore) != VK_SUCCESS)
            return false;
    }
    return true;
}

bool DeviceVK::createPresentPipeline()
{
    vkshader::CompiledProgram compiled;
    std::string log;
    if (!vkshader::compileProgram(kPresentVertexShader, kPresentFragmentShader, compiled, log))
    {
        VK_LOGE("present shader: %s", log.c_str());
        return false;
    }
    _presentVertexSpirv = compiled.vertexSpirv;
    _presentFragmentSpirv = compiled.fragmentSpirv;
    _presentSamplerBinding = compiled.samplers.empty() ? 1 : compiled.samplers[0].binding;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = _presentSamplerBinding;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_presentSetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange range{ VK_SHADER_STAGE_VERTEX_BIT, 0, 32 };
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &_presentSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    return vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_presentPipelineLayout) == VK_SUCCESS;
}

DeviceVK::~DeviceVK()
{
    if (_device)
        vkDeviceWaitIdle(_device);
    if (s_device == this)
        s_device = nullptr;

    ProgramCache::destroyInstance();
    CC_SAFE_RELEASE_NULL(_dummyTexture2D);
    CC_SAFE_RELEASE_NULL(_dummyTextureCube);
    CC_SAFE_RELEASE_NULL(_dummyDepthTexture);
    delete _deviceInfo;
    _deviceInfo = nullptr;

    if (_device)
    {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            destroySwapchainLocked();
            if (_surface)
                vkDestroySurfaceKHR(_instance, _surface, nullptr);
            _surface = VK_NULL_HANDLE;
            if (_window)
                ANativeWindow_release(_window);
            _window = nullptr;
        }
        destroyImageDeferred(_backbuffer);
        destroyImageDeferred(_defaultDepth);
        for (auto& frame : _frames)
        {
            for (auto& fn : frame.deferred)
                fn();
            frame.deferred.clear();
            destroyRing(frame.uniforms);
            destroyRing(frame.staging);
            for (auto pool : frame.descriptorPools)
                vkDestroyDescriptorPool(_device, pool, nullptr);
            if (frame.commandPool)
                vkDestroyCommandPool(_device, frame.commandPool, nullptr);
            if (frame.fence)
                vkDestroyFence(_device, frame.fence, nullptr);
            if (frame.acquireSemaphore)
                vkDestroySemaphore(_device, frame.acquireSemaphore, nullptr);
        }
        for (auto& it : _pipelines)
            vkDestroyPipeline(_device, it.second, nullptr);
        for (auto& it : _framebuffers)
            vkDestroyFramebuffer(_device, it.second.framebuffer, nullptr);
        for (auto& it : _renderPasses)
            vkDestroyRenderPass(_device, it.second.second, nullptr);
        for (auto& it : _samplers)
            vkDestroySampler(_device, it.second, nullptr);
        if (_presentPipeline)
            vkDestroyPipeline(_device, _presentPipeline, nullptr);
        if (_presentRenderPass)
            vkDestroyRenderPass(_device, _presentRenderPass, nullptr);
        if (_presentPipelineLayout)
            vkDestroyPipelineLayout(_device, _presentPipelineLayout, nullptr);
        if (_presentSetLayout)
            vkDestroyDescriptorSetLayout(_device, _presentSetLayout, nullptr);
        savePipelineCache();
        if (_pipelineCache)
            vkDestroyPipelineCache(_device, _pipelineCache, nullptr);
        _memory.destroy();
        vkDestroyDevice(_device, nullptr);
    }
    if (_instance)
        vkDestroyInstance(_instance, nullptr);
}

// ---------------------------------------------------------------------------
// Device interface

CommandBuffer* DeviceVK::newCommandBuffer()
{
    return new (std::nothrow) CommandBufferVK(this);
}

Buffer* DeviceVK::newBuffer(std::size_t size, BufferType type, BufferUsage usage)
{
    return new (std::nothrow) BufferVK(this, size, type, usage);
}

TextureBackend* DeviceVK::newTexture(const TextureDescriptor& descriptor)
{
    switch (descriptor.textureType)
    {
    case TextureType::TEXTURE_2D:
        return new (std::nothrow) Texture2DVK(this, descriptor);
    case TextureType::TEXTURE_CUBE:
        return new (std::nothrow) TextureCubeVK(this, descriptor);
    default:
        return nullptr;
    }
}

DepthStencilState* DeviceVK::createDepthStencilState(const DepthStencilDescriptor& descriptor)
{
    auto ret = new (std::nothrow) DepthStencilStateVK(descriptor);
    if (ret)
        ret->autorelease();
    return ret;
}

RenderPipeline* DeviceVK::newRenderPipeline()
{
    return new (std::nothrow) RenderPipelineVK();
}

Program* DeviceVK::newProgram(const std::string& vertexShader, const std::string& fragmentShader, Program::CompileResult& result)
{
    return new (std::nothrow) ProgramVK(this, vertexShader, fragmentShader, result);
}

Program* DeviceVK::newProgram(unsigned int format, const std::string binary, Program::CompileResult& result)
{
    // GL program binaries don't apply; ProgramVK keeps its own SPIR-V cache.
    result.success = false;
    result.errorMsg = "program binaries are not supported by the Vulkan backend";
    return nullptr;
}

ShaderModule* DeviceVK::newShaderModule(ShaderStage stage, const std::string& source, Program::CompileResult& result)
{
    return new (std::nothrow) ShaderModuleVK(stage, source);
}

bool DeviceVK::isFormatSupported(VkFormat format, VkFormatFeatureFlags features) const
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &props);
    return (props.optimalTilingFeatures & features) == features;
}

// ---------------------------------------------------------------------------
// Frames

void DeviceVK::waitForFrame(uint32_t index)
{
    Frame& frame = _frames[index];
    if (frame.submitted && !_deviceLost)
    {
        VK_CHECK(vkWaitForFences(_device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        vkResetFences(_device, 1, &frame.fence);
        frame.submitted = false;
    }
    // Run in order; later entries may depend on earlier ones having run.
    for (size_t i = 0; i < frame.deferred.size(); ++i)
        frame.deferred[i]();
    frame.deferred.clear();
}

void DeviceVK::beginFrameResources()
{
    waitForFrame(_frameIndex);
    Frame& frame = _frames[_frameIndex];
    vkResetCommandPool(_device, frame.commandPool, 0);
    for (auto pool : frame.descriptorPools)
        vkResetDescriptorPool(_device, pool, 0);
    frame.descriptorPoolIndex = 0;
    frame.uploadUsed = 0;
    frame.uploadRecording = false;
    resetRing(frame.uniforms);
    resetRing(frame.staging);

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.mainCommandBuffer, &begin));
}

VkCommandBuffer DeviceVK::getUploadCommandBuffer()
{
    Frame& frame = _frames[_frameIndex];
    if (!frame.uploadRecording)
    {
        if (frame.uploadUsed >= frame.uploadCommandBuffers.size())
        {
            VkCommandBuffer cb;
            VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            alloc.commandPool = frame.commandPool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            VK_CHECK(vkAllocateCommandBuffers(_device, &alloc, &cb));
            frame.uploadCommandBuffers.push_back(cb);
        }
        VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(frame.uploadCommandBuffers[frame.uploadUsed], &begin));
        frame.uploadRecording = true;
    }
    return frame.uploadCommandBuffers[frame.uploadUsed];
}

void DeviceVK::submitUploadsLocked(Frame& frame)
{
    if (!frame.uploadRecording)
        return;
    VkCommandBuffer cb = frame.uploadCommandBuffers[frame.uploadUsed];
    VK_CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    VK_CHECK(vkQueueSubmit(_queue, 1, &submit, VK_NULL_HANDLE));
    frame.uploadRecording = false;
    ++frame.uploadUsed;
}

void DeviceVK::executeNow(const std::function<void(VkCommandBuffer)>& record)
{
    Frame& frame = _frames[_frameIndex];
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = frame.commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(_device, &alloc, &cb));
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &begin));
    record(cb);
    VK_CHECK(vkEndCommandBuffer(cb));

    VkFence fence;
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &fence));
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        submitUploadsLocked(frame);
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cb;
        VK_CHECK(vkQueueSubmit(_queue, 1, &submit, fence));
    }
    VK_CHECK(vkWaitForFences(_device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(_device, fence, nullptr);
    // The command buffer goes back to the pool when this frame slot is reset.
}

void DeviceVK::deferDestroy(std::function<void()> fn)
{
    _frames[_frameIndex].deferred.push_back(std::move(fn));
}

void DeviceVK::onDeviceLost()
{
    if (_deviceLost)
        return;
    _deviceLost = true;
    VK_LOGE("Vulkan device lost; OpenGL ES will be used from the next launch");
    FileUtils::getInstance()->writeStringToFile("1", FileUtils::getInstance()->getWritablePath() + kDeviceLostMarkerFile);
}

void DeviceVK::endFrame(std::function<void(const unsigned char*, int, int)> captureScreen)
{
    if (_deviceLost)
    {
        // Nothing submitted can run any more; keep recording into fresh
        // command buffers so the engine keeps ticking until the next launch.
        vkEndCommandBuffer(_frames[_frameIndex].mainCommandBuffer);
        ++_frameSerial;
        _frameIndex = (_frameIndex + 1) % kFramesInFlight;
        beginFrameResources();
        return;
    }

    Frame& frame = _frames[_frameIndex];
    VkCommandBuffer cb = frame.mainCommandBuffer;

    // Optional readback of the finished backbuffer for captureScreen.
    TransientAllocation readback;
    bool capturing = false;
    if (captureScreen && _backbuffer.image)
    {
        VkDeviceSize size = VkDeviceSize(_backbufferWidth) * _backbufferHeight * 4;
        if (allocateStaging(size, 16, readback))
        {
            capturing = true;
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = _backbuffer.image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy region{};
            region.bufferOffset = readback.offset;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent = { _backbufferWidth, _backbufferHeight, 1 };
            vkCmdCopyImageToBuffer(cb, _backbuffer.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hostBarrier, 0, nullptr, 0, nullptr);
        }
    }

    VK_CHECK(vkEndCommandBuffer(cb));

    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        submitUploadsLocked(frame);
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cb;
        if (vkQueueSubmit(_queue, 1, &submit, VK_NULL_HANDLE) == VK_ERROR_DEVICE_LOST)
            onDeviceLost();
    }
    if (_deviceLost)
    {
        frame.submitted = false;
        ++_frameSerial;
        _frameIndex = (_frameIndex + 1) % kFramesInFlight;
        beginFrameResources();
        return;
    }

    // Present. The surface lock is held only for one bounded acquire attempt
    // at a time, so setWindow() on the UI thread never waits long even when
    // the compositor holds on to every swapchain image (e.g. while the window
    // is being hidden).
    bool presented = false;
    for (int attempt = 0; attempt < kAcquireAttempts && !presented; ++attempt)
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        if (!_surface || !_backbuffer.image)
            break;
        if ((_swapchainDirty || !_swapchain) && !createSwapchainLocked())
            break;

        uint32_t imageIndex = 0;
        VkResult r = vkAcquireNextImageKHR(_device, _swapchain, kAcquireTimeoutNs, frame.acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (r == VK_TIMEOUT || r == VK_NOT_READY)
            continue; // drop the lock and try again
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
        {
            _swapchainDirty = true;
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        {
            VK_LOGE("vkAcquireNextImageKHR: %d", (int)r);
            _swapchainDirty = true;
            break;
        }
        if (r == VK_SUBOPTIMAL_KHR)
            _swapchainDirty = true;

        VkCommandBuffer present = frame.presentCommandBuffer;
        VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(present, &begin));
        recordPresent(present, imageIndex);
        VK_CHECK(vkEndCommandBuffer(present));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore renderDone = _renderDoneSemaphores[imageIndex];
        VkSubmitInfo presentSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        presentSubmit.waitSemaphoreCount = 1;
        presentSubmit.pWaitSemaphores = &frame.acquireSemaphore;
        presentSubmit.pWaitDstStageMask = &waitStage;
        presentSubmit.commandBufferCount = 1;
        presentSubmit.pCommandBuffers = &present;
        presentSubmit.signalSemaphoreCount = 1;
        presentSubmit.pSignalSemaphores = &renderDone;
        VK_CHECK(vkQueueSubmit(_queue, 1, &presentSubmit, frame.fence));
        frame.submitted = true;
        presented = true;

        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderDone;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &_swapchain;
        presentInfo.pImageIndices = &imageIndex;
        r = vkQueuePresentKHR(_queue, &presentInfo);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            _presentCount.fetch_add(1, std::memory_order_release);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            _swapchainDirty = true;
        else if (r != VK_SUCCESS)
            VK_LOGE("vkQueuePresentKHR: %d", (int)r);
    }

    if (!presented)
    {
        // No window (or it never freed an image): still signal the frame fence.
        std::lock_guard<std::mutex> lock(_queueMutex);
        VK_CHECK(vkQueueSubmit(_queue, 0, nullptr, frame.fence));
        frame.submitted = true;
    }

    if (capturing)
    {
        VK_CHECK(vkWaitForFences(_device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        captureScreen((const unsigned char*)readback.mapped, (int)_backbufferWidth, (int)_backbufferHeight);
    }

    // Save new pipelines once a burst of creation settles (or every 16 during
    // a long one). Android usually kills the app rather than shutting it down,
    // so pipelines that wait for a later save are typically lost and get
    // rebuilt (~20-60 ms each) the next time that screen opens. A save is ~1 ms.
    if (_pipelinesSinceSave > 0 &&
        (_pipelinesSinceSave >= 16 || _frameSerial - _lastPipelineFrame >= kPipelineSaveQuietFrames))
    {
        savePipelineCache();
        _pipelinesSinceSave = 0;
    }

    ++_frameSerial;
    _frameIndex = (_frameIndex + 1) % kFramesInFlight;
    beginFrameResources();
}

// ---------------------------------------------------------------------------
// Transient memory

bool DeviceVK::allocateFromRing(Ring& ring, VkDeviceSize size, VkDeviceSize alignment, VkBufferUsageFlags usage,
                                VkDeviceSize chunkSize, TransientAllocation& out)
{
    for (; ring.current < ring.chunks.size(); ++ring.current)
    {
        Chunk& chunk = ring.chunks[ring.current];
        VkDeviceSize offset = UtilsVK::alignUp(chunk.used, alignment);
        if (offset + size <= chunk.size)
        {
            chunk.used = offset + size;
            out.buffer = chunk.buffer;
            out.offset = offset;
            out.mapped = (char*)chunk.allocation.mapped + offset;
            return true;
        }
    }

    Chunk chunk;
    chunk.size = size > chunkSize ? UtilsVK::alignUp(size, 4096) : chunkSize;
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = chunk.size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(_device, &info, nullptr, &chunk.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(_device, chunk.buffer, &req);
    if (!_memory.allocate(req, true, chunk.allocation) || !chunk.allocation.mapped)
    {
        vkDestroyBuffer(_device, chunk.buffer, nullptr);
        return false;
    }
    vkBindBufferMemory(_device, chunk.buffer, chunk.allocation.memory, chunk.allocation.offset);
    chunk.used = size;
    out.buffer = chunk.buffer;
    out.offset = 0;
    out.mapped = chunk.allocation.mapped;
    ring.chunks.push_back(chunk);
    ring.current = ring.chunks.size() - 1;
    return true;
}

void DeviceVK::resetRing(Ring& ring)
{
    // Drop oversized one-off chunks (e.g. a large texture upload) so a single
    // spike doesn't pin memory forever.
    for (size_t i = ring.chunks.size(); i-- > 1;)
    {
        Chunk& chunk = ring.chunks[i];
        if (chunk.size > kStagingChunkSize)
        {
            vkDestroyBuffer(_device, chunk.buffer, nullptr);
            _memory.free(chunk.allocation);
            ring.chunks.erase(ring.chunks.begin() + i);
        }
    }
    for (auto& chunk : ring.chunks)
        chunk.used = 0;
    ring.current = 0;
}

void DeviceVK::destroyRing(Ring& ring)
{
    for (auto& chunk : ring.chunks)
    {
        vkDestroyBuffer(_device, chunk.buffer, nullptr);
        _memory.free(chunk.allocation);
    }
    ring.chunks.clear();
    ring.current = 0;
}

bool DeviceVK::allocateUniform(VkDeviceSize size, TransientAllocation& out)
{
    return allocateFromRing(_frames[_frameIndex].uniforms, size, _properties.limits.minUniformBufferOffsetAlignment,
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kUniformChunkSize, out);
}

bool DeviceVK::allocateStaging(VkDeviceSize size, VkDeviceSize alignment, TransientAllocation& out)
{
    return allocateFromRing(_frames[_frameIndex].staging, size, alignment < 16 ? 16 : alignment,
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kStagingChunkSize, out);
}

VkDescriptorSet DeviceVK::allocateDescriptorSet(VkDescriptorSetLayout layout)
{
    Frame& frame = _frames[_frameIndex];
    for (;;)
    {
        if (frame.descriptorPoolIndex >= frame.descriptorPools.size())
        {
            VkDescriptorPoolSize sizes[] = {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048 },
            };
            VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            info.maxSets = 1024;
            info.poolSizeCount = 2;
            info.pPoolSizes = sizes;
            VkDescriptorPool pool;
            if (vkCreateDescriptorPool(_device, &info, nullptr, &pool) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            frame.descriptorPools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc.descriptorPool = frame.descriptorPools[frame.descriptorPoolIndex];
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout;
        VkDescriptorSet set;
        VkResult r = vkAllocateDescriptorSets(_device, &alloc, &set);
        if (r == VK_SUCCESS)
            return set;
        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL)
        {
            VK_LOGE("vkAllocateDescriptorSets: %d", (int)r);
            return VK_NULL_HANDLE;
        }
        ++frame.descriptorPoolIndex;
    }
}

// ---------------------------------------------------------------------------
// Caches

VkRenderPass DeviceVK::getRenderPass(const RenderPassKeyVK& key)
{
    std::size_t h = key.hash();
    auto it = _renderPasses.find(h);
    if (it != _renderPasses.end() && it->second.first == key)
        return it->second.second;

    VkAttachmentDescription attachments[2]{};
    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    uint32_t count = 0;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (key.colorFormat != VK_FORMAT_UNDEFINED)
    {
        auto& a = attachments[count];
        a.format = key.colorFormat;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = key.colorLoad;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = key.colorLoad == VK_ATTACHMENT_LOAD_OP_LOAD ? key.colorLayout : VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = key.colorLayout;
        colorRef.attachment = count++;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
    }
    if (key.depthFormat != VK_FORMAT_UNDEFINED)
    {
        auto& a = attachments[count];
        a.format = key.depthFormat;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = key.depthLoad;
        a.storeOp = key.depthStore;
        a.stencilLoadOp = key.stencilLoad;
        a.stencilStoreOp = key.depthStore;
        bool loads = key.depthLoad == VK_ATTACHMENT_LOAD_OP_LOAD || key.stencilLoad == VK_ATTACHMENT_LOAD_OP_LOAD;
        a.initialLayout = loads ? key.depthLayout : VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = key.depthLayout;
        depthRef.attachment = count++;
        subpass.pDepthStencilAttachment = &depthRef;
    }

    // One conservative pair of dependencies for every pass, so all passes
    // with the same attachment formats stay compatible.
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_SHADER_READ_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    info.attachmentCount = count;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    VkRenderPass pass = VK_NULL_HANDLE;
    VK_CHECK(vkCreateRenderPass(_device, &info, nullptr, &pass));
    _renderPasses[h] = { key, pass };
    return pass;
}

VkFramebuffer DeviceVK::getFramebuffer(VkRenderPass renderPass, VkImageView color, VkImageView depth, uint32_t width, uint32_t height)
{
    std::size_t h = 0;
    UtilsVK::hashCombine(h, (std::size_t)renderPass);
    UtilsVK::hashCombine(h, (std::size_t)color);
    UtilsVK::hashCombine(h, (std::size_t)depth);
    UtilsVK::hashCombine(h, width);
    UtilsVK::hashCombine(h, height);
    auto it = _framebuffers.find(h);
    if (it != _framebuffers.end())
        return it->second.framebuffer;

    VkImageView views[2];
    uint32_t count = 0;
    if (color)
        views[count++] = color;
    if (depth)
        views[count++] = depth;
    VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    info.renderPass = renderPass;
    info.attachmentCount = count;
    info.pAttachments = views;
    info.width = width;
    info.height = height;
    info.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VK_CHECK(vkCreateFramebuffer(_device, &info, nullptr, &fb));
    _framebuffers[h] = { fb, color, depth };
    return fb;
}

void DeviceVK::forgetImageView(VkImageView view)
{
    for (auto it = _framebuffers.begin(); it != _framebuffers.end();)
    {
        if (it->second.color == view || it->second.depth == view)
        {
            VkFramebuffer fb = it->second.framebuffer;
            VkDevice device = _device;
            deferDestroy([device, fb] { vkDestroyFramebuffer(device, fb, nullptr); });
            it = _framebuffers.erase(it);
        }
        else
            ++it;
    }
}

VkSampler DeviceVK::getSampler(const SamplerDescriptor& d, bool mipmapped, bool compare)
{
    std::size_t h = 0;
    UtilsVK::hashCombine(h, (std::size_t)d.magFilter);
    UtilsVK::hashCombine(h, (std::size_t)d.minFilter);
    UtilsVK::hashCombine(h, (std::size_t)d.sAddressMode);
    UtilsVK::hashCombine(h, (std::size_t)d.tAddressMode);
    UtilsVK::hashCombine(h, mipmapped);
    UtilsVK::hashCombine(h, compare);
    auto it = _samplers.find(h);
    if (it != _samplers.end())
        return it->second;

    bool usesMipmaps = false;
    VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    info.magFilter = UtilsVK::toFilter(d.magFilter);
    info.minFilter = UtilsVK::toFilter(d.minFilter);
    info.mipmapMode = UtilsVK::toMipmapMode(d.minFilter, usesMipmaps);
    info.addressModeU = UtilsVK::toAddressMode(d.sAddressMode);
    info.addressModeV = UtilsVK::toAddressMode(d.tAddressMode);
    info.addressModeW = info.addressModeV;
    info.minLod = 0.0f;
    // Without mipmaps, behave like a GL non-mipmapped filter.
    info.maxLod = (usesMipmaps && mipmapped) ? VK_LOD_CLAMP_NONE : 0.25f;
    info.compareEnable = compare ? VK_TRUE : VK_FALSE;
    // Matches the GL_LESS compare func the shadow map code sets.
    info.compareOp = VK_COMPARE_OP_LESS;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(_device, &info, nullptr, &sampler));
    _samplers[h] = sampler;
    return sampler;
}

bool DeviceVK::hasPipeline(std::size_t key, VkPipeline& pipeline) const
{
    auto it = _pipelines.find(key);
    if (it == _pipelines.end())
        return false;
    pipeline = it->second;
    return true;
}

void DeviceVK::addPipeline(std::size_t key, uint32_t programId, VkPipeline pipeline)
{
    _pipelines[key] = pipeline;
    _pipelinesByProgram[programId].push_back(key);
    ++_pipelinesSinceSave;
    _lastPipelineFrame = _frameSerial;
}

void DeviceVK::forgetProgram(uint32_t programId)
{
    auto it = _pipelinesByProgram.find(programId);
    if (it == _pipelinesByProgram.end())
        return;
    for (auto key : it->second)
    {
        auto p = _pipelines.find(key);
        if (p == _pipelines.end())
            continue;
        VkPipeline pipeline = p->second;
        VkDevice device = _device;
        if (pipeline)
            deferDestroy([device, pipeline] { vkDestroyPipeline(device, pipeline, nullptr); });
        _pipelines.erase(p);
    }
    _pipelinesByProgram.erase(it);
}

TextureBackend* DeviceVK::getDummyDepthTexture()
{
    if (!_dummyDepthTexture)
    {
        TextureDescriptor desc;
        desc.textureFormat = PixelFormat::DEPTH_TEXTURE;
        desc.textureUsage = TextureUsage::RENDER_TARGET;
        desc.width = desc.height = 1;
        desc.samplerDescriptor.minFilter = desc.samplerDescriptor.magFilter = SamplerFilter::NEAREST;
        // Depth textures are cleared to 1.0 on creation.
        _dummyDepthTexture = new Texture2DVK(this, desc);
    }
    return _dummyDepthTexture;
}

TextureBackend* DeviceVK::getDummyTexture(bool cube)
{
    TextureBackend*& slot = cube ? _dummyTextureCube : _dummyTexture2D;
    if (!slot)
    {
        TextureDescriptor desc;
        desc.textureType = cube ? TextureType::TEXTURE_CUBE : TextureType::TEXTURE_2D;
        desc.textureFormat = PixelFormat::RGBA8888;
        desc.width = desc.height = 1;
        desc.samplerDescriptor.minFilter = desc.samplerDescriptor.magFilter = SamplerFilter::NEAREST;
        uint8_t black[4] = { 0, 0, 0, 255 };
        if (cube)
        {
            auto t = new TextureCubeVK(this, desc);
            for (int face = 0; face < 6; ++face)
                t->updateFaceData((TextureCubeFace)face, black);
            slot = t;
        }
        else
        {
            auto t = new Texture2DVK(this, desc);
            t->updateData(black, 1, 1, 0);
            slot = t;
        }
    }
    return slot;
}

void DeviceVK::loadPipelineCache()
{
    std::string path = FileUtils::getInstance()->getWritablePath() + kPipelineCacheFile;
    Data data = FileUtils::getInstance()->getDataFromFile(path);
    VkPipelineCacheCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    // The driver validates the header and ignores data from another device or driver.
    info.initialDataSize = data.getSize();
    info.pInitialData = data.getBytes();
    if (vkCreatePipelineCache(_device, &info, nullptr, &_pipelineCache) != VK_SUCCESS)
    {
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        vkCreatePipelineCache(_device, &info, nullptr, &_pipelineCache);
    }
}

void DeviceVK::savePipelineCache()
{
    if (!_pipelineCache)
        return;
    size_t size = 0;
    if (vkGetPipelineCacheData(_device, _pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0)
        return;
    Data data;
    unsigned char* bytes = (unsigned char*)malloc(size);
    if (!bytes)
        return;
    if (vkGetPipelineCacheData(_device, _pipelineCache, &size, bytes) == VK_SUCCESS)
    {
        data.fastSet(bytes, size);
        FileUtils::getInstance()->writeDataToFile(data, FileUtils::getInstance()->getWritablePath() + kPipelineCacheFile);
    }
    else
        free(bytes);
}

// ---------------------------------------------------------------------------
// Default framebuffer

bool DeviceVK::createImage(ImageVK& out, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(_device, &info, nullptr, &out.image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(_device, out.image, &req);
    if (!_memory.allocate(req, false, out.allocation))
        return false;
    vkBindImageMemory(_device, out.image, out.allocation.memory, out.allocation.offset);

    VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = out.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { aspect, 0, 1, 0, 1 };
    return vkCreateImageView(_device, &view, nullptr, &out.view) == VK_SUCCESS;
}

void DeviceVK::destroyImageDeferred(ImageVK& image)
{
    if (image.view)
        forgetImageView(image.view);
    VkDevice device = _device;
    VkImage img = image.image;
    VkImageView view = image.view;
    AllocationVK allocation = image.allocation;
    MemoryAllocatorVK* memory = &_memory;
    deferDestroy([device, img, view, allocation, memory]() mutable {
        if (view)
            vkDestroyImageView(device, view, nullptr);
        if (img)
            vkDestroyImage(device, img, nullptr);
        memory->free(allocation);
    });
    image = ImageVK();
}

void DeviceVK::ensureBackbuffer(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    if (_backbuffer.image && width == _backbufferWidth && height == _backbufferHeight)
        return;

    destroyImageDeferred(_backbuffer);
    destroyImageDeferred(_defaultDepth);
    _backbufferWidth = width;
    _backbufferHeight = height;

    bool ok = createImage(_backbuffer, getBackbufferFormat(), width, height,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT);
    ok = ok && createImage(_defaultDepth, _depthStencilFormat, width, height,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    if (!ok)
    {
        VK_LOGE("failed to create %ux%u backbuffer", width, height);
        destroyImageDeferred(_backbuffer);
        destroyImageDeferred(_defaultDepth);
        return;
    }

    // Move both into their resting layouts before anything renders to them.
    VkCommandBuffer cb = getUploadCommandBuffer();
    VkImageMemoryBarrier barriers[2]{};
    for (auto& b : barriers)
    {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    barriers[0].image = _backbuffer.image;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barriers[1].image = _defaultDepth.image;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);
    VK_LOGI("backbuffer %ux%u", width, height);
}

// ---------------------------------------------------------------------------
// Presentation

void DeviceVK::setWindow(ANativeWindow* window)
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    if (window == _window)
    {
        _swapchainDirty = true;
        return;
    }
    // Nothing on the queue may still use the old swapchain's images.
    vkQueueWaitIdle(_queue);
    destroySwapchainLocked();
    if (_surface)
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
    _surface = VK_NULL_HANDLE;
    if (_window)
        ANativeWindow_release(_window);
    _window = window;
    if (!_window)
        return;

    ANativeWindow_acquire(_window);
    VkAndroidSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
    info.window = _window;
    if (vkCreateAndroidSurfaceKHR(_instance, &info, nullptr, &_surface) != VK_SUCCESS)
    {
        VK_LOGE("vkCreateAndroidSurfaceKHR failed");
        _surface = VK_NULL_HANDLE;
        return;
    }
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, _queueFamily, _surface, &supported);
    if (!supported)
        VK_LOGE("graphics queue cannot present to this surface");
    _swapchainDirty = true;
}

void DeviceVK::onWindowResized()
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    _swapchainDirty = true;
}

void DeviceVK::destroySwapchainLocked()
{
    if (!_swapchain && _swapchainViews.empty())
        return;
    vkQueueWaitIdle(_queue);
    for (auto fb : _swapchainFramebuffers)
        vkDestroyFramebuffer(_device, fb, nullptr);
    for (auto view : _swapchainViews)
        vkDestroyImageView(_device, view, nullptr);
    for (auto sem : _renderDoneSemaphores)
        vkDestroySemaphore(_device, sem, nullptr);
    _swapchainFramebuffers.clear();
    _swapchainViews.clear();
    _renderDoneSemaphores.clear();
    _swapchainImages.clear();
    if (_swapchain)
        vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    _swapchain = VK_NULL_HANDLE;
}

bool DeviceVK::createSwapchainLocked()
{
    destroySwapchainLocked();
    _swapchainDirty = false;
    if (!_surface)
        return false;

    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _surface, &caps) != VK_SUCCESS)
        return false;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu)
    {
        extent.width = (uint32_t)ANativeWindow_getWidth(_window);
        extent.height = (uint32_t)ANativeWindow_getHeight(_window);
    }
    if (extent.width == 0 || extent.height == 0)
        return false;

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, formats.data());
    if (formats.empty())
        return false;
    // GL renders without sRGB conversion, so present to a UNORM surface.
    VkSurfaceFormatKHR format = formats[0];
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            format = f;
            break;
        }
    }

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR a : { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                           VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR })
    {
        if (caps.supportedCompositeAlpha & a)
        {
            alpha = a;
            break;
        }
    }

    uint32_t imageCount = caps.minImageCount < 3 ? 3 : caps.minImageCount;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    // Pre-rotate in the present pass instead of making the compositor do it.
    // Android reports currentExtent in the window's orientation, but a
    // pre-rotated swapchain must use the display's native orientation;
    // without the swap the compositor stretches a landscape-shaped buffer
    // onto the portrait panel (blurring everything along one axis).
    _swapchainTransform = caps.currentTransform;
    if (_swapchainTransform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR))
        std::swap(extent.width, extent.height);

    VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = _surface;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = _swapchainTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    VkResult r = vkCreateSwapchainKHR(_device, &info, nullptr, &_swapchain);
    if (r != VK_SUCCESS)
    {
        VK_LOGE("vkCreateSwapchainKHR: %d", (int)r);
        _swapchain = VK_NULL_HANDLE;
        _swapchainDirty = true;
        return false;
    }
    _swapchainFormat = format.format;
    _swapchainExtent = extent;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(_device, _swapchain, &count, nullptr);
    _swapchainImages.resize(count);
    vkGetSwapchainImagesKHR(_device, _swapchain, &count, _swapchainImages.data());

    // Present render pass and pipeline depend only on the surface format.
    if (_presentRenderPassFormat != _swapchainFormat)
    {
        if (_presentPipeline)
            vkDestroyPipeline(_device, _presentPipeline, nullptr);
        if (_presentRenderPass)
            vkDestroyRenderPass(_device, _presentRenderPass, nullptr);
        _presentPipeline = VK_NULL_HANDLE;
        _presentRenderPass = VK_NULL_HANDLE;

        VkAttachmentDescription attachment{};
        attachment.format = _swapchainFormat;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rp.attachmentCount = 1;
        rp.pAttachments = &attachment;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;
        VK_CHECK(vkCreateRenderPass(_device, &rp, nullptr, &_presentRenderPass));

        VkShaderModule modules[2] = {};
        const std::vector<uint32_t>* code[2] = { &_presentVertexSpirv, &_presentFragmentSpirv };
        for (int i = 0; i < 2; ++i)
        {
            VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            sm.codeSize = code[i]->size() * 4;
            sm.pCode = code[i]->data();
            VK_CHECK(vkCreateShaderModule(_device, &sm, nullptr, &modules[i]));
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = modules[0];
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = modules[1];
        stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
        VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamics;
        VkGraphicsPipelineCreateInfo pipe{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipe.stageCount = 2;
        pipe.pStages = stages;
        pipe.pVertexInputState = &vertexInput;
        pipe.pInputAssemblyState = &assembly;
        pipe.pViewportState = &viewport;
        pipe.pRasterizationState = &raster;
        pipe.pMultisampleState = &ms;
        pipe.pColorBlendState = &blend;
        pipe.pDynamicState = &dynamic;
        pipe.layout = _presentPipelineLayout;
        pipe.renderPass = _presentRenderPass;
        VK_CHECK(vkCreateGraphicsPipelines(_device, _pipelineCache, 1, &pipe, nullptr, &_presentPipeline));
        vkDestroyShaderModule(_device, modules[0], nullptr);
        vkDestroyShaderModule(_device, modules[1], nullptr);
        _presentRenderPassFormat = _swapchainFormat;
    }

    for (auto image : _swapchainImages)
    {
        VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = _swapchainFormat;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageView v = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(_device, &view, nullptr, &v));
        _swapchainViews.push_back(v);

        VkFramebufferCreateInfo fb{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fb.renderPass = _presentRenderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &v;
        fb.width = extent.width;
        fb.height = extent.height;
        fb.layers = 1;
        VkFramebuffer f = VK_NULL_HANDLE;
        VK_CHECK(vkCreateFramebuffer(_device, &fb, nullptr, &f));
        _swapchainFramebuffers.push_back(f);

        VkSemaphoreCreateInfo sem{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkSemaphore s = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSemaphore(_device, &sem, nullptr, &s));
        _renderDoneSemaphores.push_back(s);
    }
    VK_LOGI("swapchain %ux%u format %d transform 0x%x images %u", extent.width, extent.height,
            (int)_swapchainFormat, (unsigned)_swapchainTransform, count);
    return true;
}

void DeviceVK::recordPresent(VkCommandBuffer cb, uint32_t imageIndex)
{
    // Backbuffer: color attachment -> sampled.
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = _backbuffer.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin.renderPass = _presentRenderPass;
    begin.framebuffer = _swapchainFramebuffers[imageIndex];
    begin.renderArea.extent = _swapchainExtent;
    vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0, 0, (float)_swapchainExtent.width, (float)_swapchainExtent.height, 0, 1 };
    VkRect2D scissor{ { 0, 0 }, _swapchainExtent };
    vkCmdSetViewport(cb, 0, 1, &viewport);
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _presentPipeline);

    VkDescriptorSet set = allocateDescriptorSet(_presentSetLayout);
    VkDescriptorImageInfo image{};
    SamplerDescriptor linear;
    image.sampler = getSampler(linear, false, false);
    image.imageView = _backbuffer.view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set;
    write.dstBinding = _presentSamplerBinding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _presentPipelineLayout, 0, 1, &set, 0, nullptr);

    // Output UV (p, top-left origin) -> backbuffer UV. VK_SURFACE_TRANSFORM_ROTATE_90
    // means the content must be rotated 90 degrees clockwise, and so on.
    float t[8] = { 1, 0, 0, 0, 0, 1, 0, 0 };
    switch (_swapchainTransform)
    {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
        { float m[8] = { 0, 1, 0, 0, -1, 0, 1, 0 }; memcpy(t, m, sizeof(t)); }
        break;
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
        { float m[8] = { -1, 0, 1, 0, 0, -1, 1, 0 }; memcpy(t, m, sizeof(t)); }
        break;
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
        { float m[8] = { 0, -1, 1, 0, 1, 0, 0, 0 }; memcpy(t, m, sizeof(t)); }
        break;
    default:
        break;
    }
    vkCmdPushConstants(cb, _presentPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(t), t);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderPass(cb);

    // Backbuffer back to its resting layout for the next frame.
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

CC_BACKEND_END
