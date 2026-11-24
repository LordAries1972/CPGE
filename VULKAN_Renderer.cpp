// ---------------------------------------------------------------------------------------------------------------
// VULKAN_Renderer.cpp  —  Vulkan Rendering Backend Implementation
// ---------------------------------------------------------------------------------------------------------------
// Full implementation of VulkanRenderer: device creation, swap chain, pipelines, texture loading,
// 2D overlay (D2D on Windows / stub on Linux-Android), and all Renderer interface methods.
//
// Platform guards appear at every API boundary:
//   #if defined(PLATFORM_WINDOWS)  /  #elif defined(PLATFORM_LINUX)  /  #elif defined(PLATFORM_ANDROID)
// ---------------------------------------------------------------------------------------------------------------
#include "Includes.h"
#include "VectorAA.h"

#if defined(__USE_VULKAN__)

#include "VULKAN_Renderer.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "WinSystem.h"
#include "ThreadManager.h"
#include "Configuration.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "GUIManager.h"
#include "FXManager.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"
#include "assembly.h"

// shaderc — optional runtime GLSL → SPIR-V (see CreateShaderModuleFromGLSL
// below). File scope only: the header has extern "C" at top level and cannot
// be included inside a function body.
#if __has_include(<shaderc/shaderc.hpp>)
    #include <shaderc/shaderc.hpp>
    #define HAS_SHADERC 1
#else
    #define HAS_SHADERC 0
#endif

#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    // stb_image declarations for LoadTexture's Linux/Android branch below.
    // The implementation lives in VulkanModels.cpp (which defines
    // STB_IMAGE_IMPLEMENTATION) — defining it here too would duplicate every
    // stb_* symbol at link time. Must be at file scope: including the header
    // inside LoadTexture's function body is a compile error (extern "C" and
    // typedefs are not allowed at block scope).
    #if __has_include("stb_image.h")
        #include "stb_image.h"
    #endif
#endif

// ---------------------------------------------------------------------------------------------------------------
// Externals
// ---------------------------------------------------------------------------------------------------------------
extern Debug              debug;
extern ExceptionHandler   exceptionHandler;
extern SystemUtils        sysUtils;
extern ThreadManager      threadManager;
extern SceneManager       scene;
extern Configuration      config;
extern GUIManager         guiManager;
extern FXManager          fxManager;
extern WindowMetrics      winMetrics;
extern LightsManager      lightsManager;
extern Model              models[MAX_MODELS];
extern ScreenRecorder     screenRecorder;
extern MoviePlayer        moviePlayer;
extern Vector2            myMouseCoords;
extern std::atomic<bool>  bResizeInProgress;
extern std::atomic<bool>  bFullScreenTransition;

// ---------------------------------------------------------------------------------------------------------------
// Static member definitions
// s_loaderMutex is defined in IOLoaderThread.cpp inside the __USE_VULKAN__ block.
// ---------------------------------------------------------------------------------------------------------------
std::mutex VulkanRenderer::s_renderMutex;

// ---------------------------------------------------------------------------------------------------------------
// Validation layer list (debug builds only)
// ---------------------------------------------------------------------------------------------------------------
#if defined(_DEBUG)
static const std::vector<const char*> k_validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
static constexpr bool k_enableValidation = true;
#else
static const std::vector<const char*> k_validationLayers;
static constexpr bool k_enableValidation = false;
#endif

static const std::vector<const char*> k_deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// ---------------------------------------------------------------------------------------------------------------
// Debug messenger helpers
// ---------------------------------------------------------------------------------------------------------------
static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    return fn ? fn(instance, pCreateInfo, pAllocator, pDebugMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator)
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn) fn(instance, debugMessenger, pAllocator);
}

// ---------------------------------------------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------------------------------------------
VulkanRenderer::VulkanRenderer()
{
    RenderType = RendererType::RT_Vulkan;
    for (auto& fd : m_frames) {
        fd.commandBuffer  = VK_NULL_HANDLE;
        fd.imageAvailable = VK_NULL_HANDLE;
        fd.inFlightFence  = VK_NULL_HANDLE;
    }
    InitPlatformDetails();
    for (auto& t : m_textures2D) t = {};
    for (auto& t : m_textures3D) t = {};
    std::memset(My2DBlitQueue, 0, sizeof(My2DBlitQueue));

}

VulkanRenderer::~VulkanRenderer()
{
    if (!m_bHasCleanedUp) Cleanup();
}

// ---------------------------------------------------------------------------------------------------------------
// RendererName
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::RendererName(std::string sThisName)
{
    m_rendererName = sThisName;
    sName = std::wstring(sThisName.begin(), sThisName.end()).c_str();
}

// ---------------------------------------------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::Initialize(NativeWindowHandle hwnd, NativeInstanceHandle hInstance)
{
    exceptionHandler.RecordFunctionCall("VulkanRenderer::Initialize");

    InitPlatformWindow(hwnd, hInstance);

    CreateInstance();
    if (k_enableValidation) SetupDebugMessenger();
    CreateSurface(hwnd);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain(static_cast<uint32_t>(m_renderTargetWidth),
                    static_cast<uint32_t>(m_renderTargetHeight));
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateImageViews");
    #endif
    CreateImageViews();
    ChooseMsaaSamples();                // needs the swapchain format; fixes m_msaaSamples for the renderer's lifetime
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateRenderPass");
    #endif
    CreateRenderPass();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateDescriptorSetLayouts");
    #endif
    CreateDescriptorSetLayouts();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateCommandPool");
    #endif
    CreateCommandPool();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateDepthResources");
    #endif
    CreateDepthResources();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateFramebuffers");
    #endif
    CreateFramebuffers();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateDescriptorPool");
    #endif
    CreateDescriptorPool();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateGraphicsPipelines");
    #endif
    CreateGraphicsPipelines();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateQuadVertexBuffer");
    #endif
    CreateQuadVertexBuffer();
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateSyncObjects");
    #endif
    CreateSyncObjects();

    // Create fallback textures used when a model has no material textures.
    // m_defaultTexture:  1×1 white (diffuse fallback — also used by 2D pipeline)
    // m_defaultNormalTex: 1×1 flat normal in tangent space (0.5,0.5,1.0)
    // m_defaultOrmTex:   1×1 default ORM (AO=1, roughness=0.5, metallic=0)
    // m_defaultAoTex:    1×1 white AO (full ambient)
    {
        const uint8_t white[4]      = { 255, 255, 255, 255 };
        const uint8_t flatNormal[4] = { 128, 128, 255, 255 }; // (0.5,0.5,1.0) tangent-space flat
        const uint8_t defaultOrm[4] = { 255, 128,   0, 255 }; // R=AO(1.0), G=roughness(0.5), B=metallic(0)
        m_defaultTexture    = CreateTextureFromRGBA(white,      1, 1);
        m_defaultNormalTex  = CreateTextureFromRGBA(flatNormal, 1, 1);
        m_defaultOrmTex     = CreateTextureFromRGBA(defaultOrm, 1, 1);
        m_defaultAoTex      = CreateTextureFromRGBA(white,      1, 1);

        // Old 1-slot descriptor set for 2D pipeline fallback
        if (m_defaultTexture.isValid && m_descriptorPool != VK_NULL_HANDLE &&
            m_textureDescSetLayout != VK_NULL_HANDLE)
        {
            VkDescriptorSetAllocateInfo dsai{};
            dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsai.descriptorPool     = m_descriptorPool;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts        = &m_textureDescSetLayout;
            if (vkAllocateDescriptorSets(m_device, &dsai, &m_defaultTextureDescSet) == VK_SUCCESS)
            {
                VkDescriptorImageInfo imgInfo{};
                imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imgInfo.imageView   = m_defaultTexture.view;
                imgInfo.sampler     = m_defaultTexture.sampler;
                VkWriteDescriptorSet write{};
                write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet          = m_defaultTextureDescSet;
                write.dstBinding      = 0;
                write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo      = &imgInfo;
                vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
            }
        }

        // 4-slot fallback set for 3D PBR pipeline (all slots → white/flat-normal/ORM/white)
        if (m_defaultTexture.isValid  && m_defaultNormalTex.isValid &&
            m_defaultOrmTex.isValid   && m_defaultAoTex.isValid     &&
            m_3dTexSetLayout != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE)
        {
            VkDescriptorSetAllocateInfo dsai{};
            dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsai.descriptorPool     = m_descriptorPool;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts        = &m_3dTexSetLayout;
            if (vkAllocateDescriptorSets(m_device, &dsai, &m_defaultTexSetDescSet) == VK_SUCCESS)
            {
                // All 6 bindings must be written to match m_3dTexSetLayout (6-slot):
                //   0=diffuse, 1=normal, 2=ORM, 3=AO, 4=gloss, 5=emissive
                // Gloss/emissive fall back to the white diffuse texture; the fragment
                // shader only samples them when useGlossMap/useEmissiveMap > 0.5.
                VkImageView fallbackViews[6] = {
                    m_defaultTexture.view,   // binding 0: diffuse  (1x1 white)
                    m_defaultNormalTex.view, // binding 1: normal   (flat 0.5,0.5,1.0)
                    m_defaultOrmTex.view,    // binding 2: ORM      (no metallic, mid roughness)
                    m_defaultAoTex.view,     // binding 3: AO       (1x1 white)
                    m_defaultTexture.view,   // binding 4: gloss    (white fallback)
                    m_defaultTexture.view    // binding 5: emissive (white fallback)
                };
                std::array<VkWriteDescriptorSet, 6>    writes{};
                std::array<VkDescriptorImageInfo, 6>   imgInfos{};
                for (uint32_t b = 0; b < 6; ++b) {
                    imgInfos[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfos[b].imageView   = fallbackViews[b];
                    imgInfos[b].sampler     = m_defaultTexture.sampler;
                    writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[b].dstSet          = m_defaultTexSetDescSet;
                    writes[b].dstBinding      = b;
                    writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[b].descriptorCount = 1;
                    writes[b].pImageInfo      = &imgInfos[b];
                }
                vkUpdateDescriptorSets(m_device, 6, writes.data(), 0, nullptr);
            }
        }
    }

    // Per-frame lighting UBOs + shadow maps (3D pipeline set = 2).  The lighting half is required
    // for 3D rendering; the shadow depth pass is optional and falls back to "no shadows".
    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateShadowResourcesVK");
    #endif
    if (!CreateShadowResourcesVK())
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] Lighting/shadow resources unavailable - 3D models will not render.");

    #if defined(_DEBUG_VULKANRENDERER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[VulkanRenderer] Init step: CreateOverlayResources");
    #endif
    CreateOverlayResources(static_cast<uint32_t>(m_renderTargetWidth),
                           static_cast<uint32_t>(m_renderTargetHeight));

    HidePlatformMouseCursor();

    bIsInitialized.store(true);

    if (threadManager.threadVars.bIsResizing.load())
    {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Initialized and Activated.");
    }
    else
    {
        threadManager.ResumeThread(THREAD_LOADER);
    }
    threadManager.threadVars.bIsResizing.store(false);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Initialized successfully.");
}

// ---------------------------------------------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::Cleanup()
{
    if (m_bHasCleanedUp) return;
    m_bHasCleanedUp = true;
    bHasCleanedUp.store(true);

    // Restore the specific monitor we changed to its registry settings.
    RestorePlatformDisplayMode();

    WaitForGPUToFinish();

    // Per-frame lighting UBOs, shadow maps, shadow pass (before the descriptor pool is destroyed)
    ReleaseShadowResourcesVK();

    // Destroy textures
    for (auto& t : m_textures2D) DestroyVulkanTexture(t);
    for (auto& t : m_textures3D) DestroyVulkanTexture(t);
    DestroyVulkanTexture(m_overlayTexture);
    DestroyVulkanTexture(m_bgOverlayTexture);
    DestroyVulkanTexture(m_defaultTexture);
    m_defaultTextureDescSet = VK_NULL_HANDLE; // freed with descriptor pool

    // UV settings: destroy the lazily-created wrap-mode sampler cache.
    // GPU is already idle (WaitForGPUToFinish above), so destruction is safe.
    for (int wu = 0; wu < 3; ++wu)
        for (int wv = 0; wv < 3; ++wv)
            if (m_wrapSamplers[wu][wv] != VK_NULL_HANDLE) {
                vkDestroySampler(m_device, m_wrapSamplers[wu][wv], nullptr);
                m_wrapSamplers[wu][wv] = VK_NULL_HANDLE;
            }

    ReleasePlatformOverlayResources();

    CleanupSwapChain();

    // Pipelines
    if (m_2dPipelineMS     != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_2dPipelineMS, nullptr); m_2dPipelineMS = VK_NULL_HANDLE; }
    if (m_3dPipelineMS     != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_3dPipelineMS, nullptr); m_3dPipelineMS = VK_NULL_HANDLE; }
    if (m_2dPipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_2dPipeline, nullptr);
    if (m_2dPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_2dPipelineLayout, nullptr);
    if (m_3dPipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_3dPipeline, nullptr);
    if (m_3dPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_3dPipelineLayout, nullptr);

    // Fallback textures (freed via pool when pool is destroyed, but destroy images/views/samplers)
    DestroyVulkanTexture(m_defaultNormalTex);
    DestroyVulkanTexture(m_defaultOrmTex);
    DestroyVulkanTexture(m_defaultAoTex);
    m_defaultTexSetDescSet = VK_NULL_HANDLE; // freed with descriptor pool

    // Descriptor set layouts
    if (m_3dFrameSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_3dFrameSetLayout, nullptr);
        m_3dFrameSetLayout = VK_NULL_HANDLE;
    }
    if (m_3dTexSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_3dTexSetLayout, nullptr);
    if (m_3dUboSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_3dUboSetLayout, nullptr);
    if (m_textureDescSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_textureDescSetLayout, nullptr);
    if (m_uniformDescSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_uniformDescSetLayout, nullptr);

    // Quad geometry
    if (m_quadVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_quadVertexBuffer, nullptr);
    if (m_quadVertexMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_quadVertexMemory, nullptr);

    // Descriptor pool
    if (m_descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    // Sync objects (renderFinished semaphores already destroyed via CleanupSwapChain)
    for (auto& fd : m_frames) {
        if (fd.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(m_device, fd.imageAvailable, nullptr);
        if (fd.inFlightFence  != VK_NULL_HANDLE) vkDestroyFence(m_device, fd.inFlightFence, nullptr);
    }
    ReleasePlatformD2DTextures();

    // Command pools
    if (m_loaderCommandPool != VK_NULL_HANDLE) { vkDestroyCommandPool(m_device, m_loaderCommandPool, nullptr); m_loaderCommandPool = VK_NULL_HANDLE; }
    if (m_commandPool       != VK_NULL_HANDLE) { vkDestroyCommandPool(m_device, m_commandPool,       nullptr); m_commandPool       = VK_NULL_HANDLE; }

    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;

    if (k_enableValidation && m_debugMessenger != VK_NULL_HANDLE)
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);

    if (m_surface   != VK_NULL_HANDLE) { vkDestroySurfaceKHR(m_instance, m_surface, nullptr);  m_surface  = VK_NULL_HANDLE; }
    if (m_instance  != VK_NULL_HANDLE) { vkDestroyInstance(m_instance, nullptr);                m_instance = VK_NULL_HANDLE; }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Cleaned up.");
}

// ---------------------------------------------------------------------------------------------------------------
// CreateInstance
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateInstance()
{
    if (k_enableValidation && !CheckValidationLayerSupport())
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] Validation layers requested but not available.");

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "CPGE";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "CPGE Engine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME };
    AppendPlatformSurfaceExtension(extensions);
    if (k_enableValidation)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    ProbePlatformInstanceExtensions(extensions);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount       = k_enableValidation
                                          ? static_cast<uint32_t>(k_validationLayers.size()) : 0;
    createInfo.ppEnabledLayerNames     = k_enableValidation ? k_validationLayers.data() : nullptr;

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create Vulkan instance.");

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Instance created.");
}

// ---------------------------------------------------------------------------------------------------------------
// SetupDebugMessenger
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::SetupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugCallback;
    CreateDebugUtilsMessengerEXT(m_instance, &ci, nullptr, &m_debugMessenger);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*)
{
    // Vulkan validation-layer messages are development diagnostics, not application
    // errors.  Mapping them to LOG_ERROR would trigger PostQuitMessage on every
    // validation warning during normal scene transitions.  Use LOG_WARNING instead.
    LogLevel lvl = (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                    ? LogLevel::LOG_WARNING : LogLevel::LOG_DEBUG;
    std::string msg = std::string("[Vulkan] ") + pData->pMessage;
    debug.logLevelMessage(lvl, std::wstring(msg.begin(), msg.end()));
    return VK_FALSE;
}

// ---------------------------------------------------------------------------------------------------------------
// CreateSurface  (platform-dispatched)
// ---------------------------------------------------------------------------------------------------------------
// CreateSurface — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// ---------------------------------------------------------------------------------------------------------------
// PickPhysicalDevice
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        throw std::runtime_error("[VulkanRenderer] No Vulkan-capable GPU found.");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Score all devices, prefer discrete GPU
    int bestScore = -1;
    for (auto& dev : devices) {
        int score = RateDeviceSuitability(dev);
        if (score > bestScore) { bestScore = score; m_physicalDevice = dev; }
    }
    if (m_physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("[VulkanRenderer] No suitable GPU found.");

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    std::string gpuName = props.deviceName;
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[VulkanRenderer] Selected GPU: " + std::wstring(gpuName.begin(), gpuName.end()));

    // GPU capability detection — mirrors DX12 logging for parity
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                m_dedicatedVRAMMB += static_cast<UINT64>(memProps.memoryHeaps[i].size / (1024 * 1024));

        m_isUMA       = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
        m_isLowEndGPU = (m_dedicatedVRAMMB < 2048 || m_isUMA);

        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"VulkanRenderer: GPU Caps: VRAM: %llu MB, UMA: %s, LowEnd: %s",
            m_dedicatedVRAMMB,
            m_isUMA       ? L"Yes" : L"No",
            m_isLowEndGPU ? L"Yes" : L"No");
        if (m_isLowEndGPU)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"VulkanRenderer: Low-end GPU detected — reduced VRAM or integrated/UMA architecture.");
    }
}

int VulkanRenderer::RateDeviceSuitability(VkPhysicalDevice device) const
{
    if (!IsDeviceSuitable(device)) return -1;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);
    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1000;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score +=  500;
    score += static_cast<int>(props.limits.maxImageDimension2D);
    return score;
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device) const
{
    auto qfi = FindQueueFamilies(device);
    if (!qfi.IsComplete()) return false;

    // Check required extensions
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> avail(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, avail.data());
    std::unordered_set<std::string> required(k_deviceExtensions.begin(), k_deviceExtensions.end());
    for (auto& ext : avail) required.erase(ext.extensionName);
    if (!required.empty()) return false;

    auto sc = QuerySwapChainSupport(device);
    return !sc.formats.empty() && !sc.presentModes.empty();
}

QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices idx{};
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) idx.graphicsFamily = i;
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) idx.presentFamily = i;
        if (idx.IsComplete()) break;
    }
    return idx;
}

// ---------------------------------------------------------------------------------------------------------------
// CreateLogicalDevice
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateLogicalDevice()
{
    auto idx = FindQueueFamilies(m_physicalDevice);
    m_graphicsQueueFamily = idx.graphicsFamily;
    m_presentQueueFamily  = idx.presentFamily;

    std::vector<VkDeviceQueueCreateInfo> queueCIs;
    std::unordered_set<uint32_t> uniqueFamilies = { idx.graphicsFamily, idx.presentFamily };
    float priority = 1.0f;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        ci.queueFamilyIndex = family;
        ci.queueCount       = 1;
        ci.pQueuePriorities = &priority;
        queueCIs.push_back(ci);
    }

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;
    features.fillModeNonSolid  = VK_TRUE;  // wireframe support
    {
        // Sample-rate shading (shades every MSAA sample for the 3D pipeline) is optional; only enabled when present.
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(m_physicalDevice, &supported);
        m_sampleShadingOk = (supported.sampleRateShading == VK_TRUE);
        features.sampleRateShading = m_sampleShadingOk ? VK_TRUE : VK_FALSE;
    }

    // Build device extension list — start with required extensions, then add optional ones.
    std::vector<const char*> enabledExtensions(k_deviceExtensions.begin(), k_deviceExtensions.end());
    AppendPlatformDeviceExtensions(enabledExtensions);

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queueCIs.size());
    ci.pQueueCreateInfos       = queueCIs.data();
    ci.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensions.size());
    ci.ppEnabledExtensionNames = enabledExtensions.data();
    ci.pEnabledFeatures        = &features;
    // Device layers have been deprecated since Vulkan 1.0; only instance layers are valid.
    // enabledLayerCount must be 0 per VUID-VkDeviceCreateInfo-enabledLayerCount-12384.

    if (vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create logical device.");

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily,  0, &m_presentQueue);
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Logical device created.");
}

// ---------------------------------------------------------------------------------------------------------------
// Swap chain
// ---------------------------------------------------------------------------------------------------------------
SwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device) const
{
    SwapChainSupportDetails details;
    VkResult capResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);
    if (capResult != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Surface lost or unavailable (vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed).");
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &count, nullptr);
    if (count) { details.formats.resize(count); vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &count, details.formats.data()); }
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &count, nullptr);
    if (count) { details.presentModes.resize(count); vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &count, details.presentModes.data()); }
    return details;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& avail) const
{
    for (auto& f : avail)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return avail[0];
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& avail) const
{
    // Respect VSync config
    bool vsync = GetPlatformVSyncEnabled();
    if (!vsync)
        for (auto& m : avail)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h) const
{
    // Prefer the caller-supplied dimensions when they are valid.
    // Win32 surfaces always report a fixed currentExtent (never UINT32_MAX) but the driver may
    // not update it synchronously after SetWindowPos, returning the old window size. Using the
    // stale currentExtent would create a swapchain at the previous (windowed) resolution and
    // cause only that fraction of the screen to be rendered — appearing as a "quarter of screen"
    // when the old window was half the monitor size. Using w/h directly avoids this race.
    if (w == 0 || h == 0) {
        if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    }
    VkExtent2D ext = { w, h };
    ext.width  = std::clamp(ext.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return ext;
}

void VulkanRenderer::CreateSwapChain(uint32_t width, uint32_t height)
{
    auto sc = QuerySwapChainSupport(m_physicalDevice);
    auto fmt  = ChooseSwapSurfaceFormat(sc.formats);
    auto mode = ChooseSwapPresentMode(sc.presentModes);
    auto ext  = ChooseSwapExtent(sc.capabilities, width, height);

    if (ext.width == 0 || ext.height == 0)
        throw std::runtime_error("[VulkanRenderer] Swap chain extent is zero; deferring creation.");

    // Triple buffering: request minImageCount + 1 (typically 3).
    // Double buffering: request minImageCount (typically 2).
    uint32_t imgCount = config.myConfig.buffering
        ? sc.capabilities.minImageCount + 1   // triple buffering
        : sc.capabilities.minImageCount;       // double buffering
    if (sc.capabilities.maxImageCount > 0)
        imgCount = std::min(imgCount, sc.capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = m_surface;
    ci.minImageCount    = imgCount;
    ci.imageFormat      = fmt.format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    uint32_t families[] = { m_graphicsQueueFamily, m_presentQueueFamily };
    if (m_graphicsQueueFamily != m_presentQueueFamily) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = sc.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = mode;
    ci.clipped        = VK_TRUE;

#if defined(PLATFORM_WINDOWS)
    // Win32 surfaces require BOTH structs in the pNext chain when using
    // VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT.
    // VUID-VkSwapchainCreateInfoKHR-pNext-02679 mandates VkSurfaceFullScreenExclusiveWin32InfoEXT
    // alongside VkSurfaceFullScreenExclusiveInfoEXT to supply the target HMONITOR.
    VkSurfaceFullScreenExclusiveInfoEXT      exclusiveInfo{};
    VkSurfaceFullScreenExclusiveWin32InfoEXT exclusiveWin32Info{};
    if (m_requestExclusiveMode && m_supportsVkExclusiveFS) {
        exclusiveWin32Info.sType    = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
        exclusiveWin32Info.pNext    = nullptr;
        exclusiveWin32Info.hmonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);

        exclusiveInfo.sType             = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
        exclusiveInfo.pNext             = &exclusiveWin32Info;
        exclusiveInfo.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;

        ci.pNext = &exclusiveInfo;
    }
#endif

    if (vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create swap chain.");

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, nullptr);
    m_swapchainImages.resize(imgCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, m_swapchainImages.data());
    m_swapchainFormat = fmt.format;
    m_swapchainExtent = ext;
    m_renderTargetWidth  = static_cast<int>(ext.width);
    m_renderTargetHeight = static_cast<int>(ext.height);
    iOrigWidth  = m_renderTargetWidth;
    iOrigHeight = m_renderTargetHeight;

    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[VulkanRenderer] Swap chain created (" + std::to_wstring(ext.width) +
        L"x" + std::to_wstring(ext.height) + L").");
}

void VulkanRenderer::CreateImageViews()
{
    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); ++i)
        m_swapchainImageViews[i] = CreateImageView(m_swapchainImages[i], m_swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT);
}

// ---------------------------------------------------------------------------------------------------------------
// Render pass
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateRenderPass()
{
    VkAttachmentDescription color{};
    color.format         = m_swapchainFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format         = FindDepthFormat();
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { color, depth };
    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments    = attachments.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create render pass.");

    // ---- Multisampled main pass: [0] MSAA colour (cleared, not stored), [1] MSAA depth, [2] swapchain resolve target ----
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        VkAttachmentDescription msColor = color;
        msColor.samples     = m_msaaSamples;
        msColor.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;                 // only the resolved image is kept
        msColor.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription msDepth = depth;
        msDepth.samples = m_msaaSamples;

        VkAttachmentDescription resolve{};
        resolve.format         = m_swapchainFormat;
        resolve.samples        = VK_SAMPLE_COUNT_1_BIT;
        resolve.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolve.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        resolve.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolve.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        resolve.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference resolveRef{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription msSubpass = subpass;
        msSubpass.pResolveAttachments = &resolveRef;

        std::array<VkAttachmentDescription, 3> msAttachments = { msColor, msDepth, resolve };
        VkRenderPassCreateInfo msRpci = rpci;
        msRpci.attachmentCount = static_cast<uint32_t>(msAttachments.size());
        msRpci.pAttachments    = msAttachments.data();
        msRpci.pSubpasses      = &msSubpass;

        if (vkCreateRenderPass(m_device, &msRpci, nullptr, &m_renderPassMS) != VK_SUCCESS)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] MSAA render pass creation failed; falling back to 1 sample.");
            m_renderPassMS = VK_NULL_HANDLE;
            m_msaaSamples  = VK_SAMPLE_COUNT_1_BIT;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// MSAA sample selection.  Anti-Aliasing (master) + MSAA + MSAA Samples from the Video settings; the requested
// count is lowered to the highest count the device supports for BOTH the swapchain colour format and the depth
// format as framebuffer attachments.  Called once from Initialize (changing it needs a video restart).
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::ChooseMsaaSamples()
{
    m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    if (!config.myConfig.antiAliasingEnabled || !config.myConfig.msaaEnabled)
        return;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    VkSampleCountFlags supported = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts & props.limits.framebufferStencilSampleCounts;

    // Per-format check (limits are only an upper bound for the formats actually used)
    auto formatCounts = [&](VkFormat fmt, VkImageUsageFlags usage) -> VkSampleCountFlags {
        VkImageFormatProperties ifp{};
        if (vkGetPhysicalDeviceImageFormatProperties(m_physicalDevice, fmt, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                     usage, 0, &ifp) != VK_SUCCESS)
            return VK_SAMPLE_COUNT_1_BIT;
        return ifp.sampleCounts;
    };
    supported &= formatCounts(m_swapchainFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    supported &= formatCounts(FindDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    const int want = std::clamp(config.myConfig.msaaSamples, 2, 8);
    const VkSampleCountFlagBits ladder[3] = { VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT };
    const int                   counts[3] = { 8, 4, 2 };
    for (int i = 0; i < 3; ++i)
    {
        if (counts[i] <= want && (supported & ladder[i]))
        {
            m_msaaSamples = ladder[i];
            break;
        }
    }

    if (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] MSAA requested but no supported multisample count was found; rendering 1 sample.");
    else
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] MSAA enabled: %dx (requested %dx)", static_cast<int>(m_msaaSamples), want);
}

// ---------------------------------------------------------------------------------------------------------------
// Descriptor set layouts
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateDescriptorSetLayouts()
{
    // ---- 2D pipeline: set=0, binding=0 — single combined image sampler ----
    VkDescriptorSetLayoutBinding texBinding{};
    texBinding.binding            = 0;
    texBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount    = 1;
    texBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo texCI{};
    texCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    texCI.bindingCount = 1;
    texCI.pBindings    = &texBinding;
    vkCreateDescriptorSetLayout(m_device, &texCI, nullptr, &m_textureDescSetLayout);

    // ---- Legacy single-UBO layout (kept for backward compat) ----
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo uboCI{};
    uboCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    uboCI.bindingCount = 1;
    uboCI.pBindings    = &uboBinding;
    vkCreateDescriptorSetLayout(m_device, &uboCI, nullptr, &m_uniformDescSetLayout);

    // ---- Full PBR 3D: set=0 — binding0=transform UBO (vert+frag), binding1=material UBO (frag) ----
    std::array<VkDescriptorSetLayoutBinding, 2> ubo3DBindings{};
    ubo3DBindings[0].binding         = 0;
    ubo3DBindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;   // dynamic offset: slot 0 = main pass, slot 1 = planar mirror pass
    ubo3DBindings[0].descriptorCount = 1;
    ubo3DBindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    ubo3DBindings[1].binding         = 1;
    ubo3DBindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo3DBindings[1].descriptorCount = 1;
    ubo3DBindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ubo3DCI{};
    ubo3DCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ubo3DCI.bindingCount = static_cast<uint32_t>(ubo3DBindings.size());
    ubo3DCI.pBindings    = ubo3DBindings.data();
    vkCreateDescriptorSetLayout(m_device, &ubo3DCI, nullptr, &m_3dUboSetLayout);

    // ---- Full PBR 3D: set=1 — bindings 0-3 = diffuse/normal/ORM/AO samplers (frag) ----
    // 6 texture slots: 0=diffuse, 1=normal, 2=ORM, 3=AO, 4=gloss, 5=emissive
    std::array<VkDescriptorSetLayoutBinding, 6> tex3DBindings{};
    for (uint32_t b = 0; b < 6; ++b) {
        tex3DBindings[b].binding         = b;
        tex3DBindings[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex3DBindings[b].descriptorCount = 1;
        tex3DBindings[b].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo tex3DCI{};
    tex3DCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tex3DCI.bindingCount = static_cast<uint32_t>(tex3DBindings.size());
    tex3DCI.pBindings    = tex3DBindings.data();
    vkCreateDescriptorSetLayout(m_device, &tex3DCI, nullptr, &m_3dTexSetLayout);

    // ---- Full PBR 3D: set=2 — per-frame lighting + shadows (frag) ----
    // binding 0 = GlobalLightBuffer UBO, binding 1 = ShadowBuffer UBO,
    // binding 2 = directional shadow map (sampler2DShadow), binding 3 = spot/point array (sampler2DArrayShadow),
    // binding 4 = scene reflection probe (samplerCube), binding 5 = planar mirror render (sampler2D)
    std::array<VkDescriptorSetLayoutBinding, 6> frameBindings{};
    for (uint32_t b = 0; b < 6; ++b) {
        frameBindings[b].binding         = b;
        frameBindings[b].descriptorType  = (b < 2) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                   : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        frameBindings[b].descriptorCount = 1;
        frameBindings[b].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo frameCI{};
    frameCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameCI.bindingCount = static_cast<uint32_t>(frameBindings.size());
    frameCI.pBindings    = frameBindings.data();
    vkCreateDescriptorSetLayout(m_device, &frameCI, nullptr, &m_3dFrameSetLayout);
}

// ---------------------------------------------------------------------------------------------------------------
// Pipelines — GLSL compiled via shaderc if available, otherwise use pre-compiled SPIR-V stubs
// ---------------------------------------------------------------------------------------------------------------

// Inline GLSL source for the 2D textured quad pipeline
static const char* k_glsl2DVert = R"(
#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(push_constant) uniform PC {
    vec2 screenSize;
    vec2 position;
    vec2 size;
    float opacity;
} pc;
void main() {
    // pixel-space → NDC: x maps (-1,+1 left-to-right), y maps (-1 top, +1 bottom).
    // In Vulkan clip-space Y is already positive-down, so the formula is correct as-is;
    // a negation here would flip the entire overlay (images upside-down, mouse reversed).
    vec2 ndcPos = ((inPos * pc.size + pc.position) / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndcPos, 0.0, 1.0);
    fragUV    = inUV;
    fragColor = inColor * vec4(1.0, 1.0, 1.0, pc.opacity);
})";

static const char* k_glsl2DFrag = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = texture(tex, fragUV) * fragColor;
})";

// Inline GLSL source for the full PBR 3D geometry pipeline
// set=0 binding=0: transform UBO (model/view/proj/camPos/scale) - camPos.w = receiveShadows flag
// set=0 binding=1: material UBO (Kd/Ka/metallic/roughness/emissive/flags)
// set=1 binding=0..5: diffuse/normal/ORM/AO/gloss/emissive samplers
// set=2 binding=0: GlobalLightBuffer (8 lights: directional / point / spot) - same layout as DX/GL b3
// set=2 binding=1: ShadowBuffer (ShadowBufferData in Lights.h)
// set=2 binding=2: directional shadow map (sampler2DShadow)
// set=2 binding=3: spot slices + point cube faces (sampler2DArrayShadow)
// set=2 binding=4: scene reflection probe (samplerCube, mip chain)
// set=2 binding=5: planar mirror render (sampler2D)
static const char* k_glsl3DVert = R"(
#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vTexCoord;
layout(location = 3) out vec3 vViewDir;
layout(location = 4) out vec3 vTangent;
layout(location = 5) out vec3 vBitangent;
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 scale;
} ubo;
void main() {
    vec3 scaledPos = inPos * ubo.scale.xyz;
    vec4 worldPos  = ubo.model * vec4(scaledPos, 1.0);
    vWorldPos      = worldPos.xyz;
    mat3 normalMat = transpose(inverse(mat3(ubo.model)));
    vNormal    = normalize(normalMat * inNormal);
    vTangent   = normalize(normalMat * inTangent);
    vBitangent = cross(vNormal, vTangent);
    vViewDir   = normalize(ubo.camPos.xyz - worldPos.xyz);
    vTexCoord  = inUV;
    gl_Position = ubo.proj * ubo.view * worldPos;
})";

static const char* k_glsl3DFrag = R"(
#version 450
#define PI 3.14159265359
#define MAX_GLOBAL_LIGHTS       8
#define MAX_LOCAL_SHADOW_SLICES 32
#define LIGHT_TYPE_DIRECTIONAL  0
#define LIGHT_TYPE_POINT        1
#define LIGHT_TYPE_SPOT         2
#define SHADOW_KIND_DIRECTIONAL 1
#define SHADOW_KIND_SPOT        2
#define SHADOW_KIND_POINT       3
layout(location = 0) out vec4 fragColor;
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;
layout(location = 3) in vec3 vViewDir;
layout(location = 4) in vec3 vTangent;
layout(location = 5) in vec3 vBitangent;
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model; mat4 view; mat4 proj;
    vec4 camPos;    // xyz = camera position, w = receiveShadows (1.0 = model is shadowed)
    vec4 scale;
} ubo;
layout(set = 0, binding = 1) uniform MaterialUBO {
    vec3  Kd;       float metallic;
    vec3  Ka;       float roughness;
    vec3  emissive; float emissiveStrength;
    float normalScale; float useNormal;  float useORM;        float useAO;
    float useDiffuseMap; float useGlossMap; float useEmissiveMap; float planarStrength;   // planarStrength: reflector mix, 0 = none
    float planarIndex;   float _pm1;         float _pm2;          float _pm3;             // planarIndex: layer of planarMap this reflector samples
} mat;
layout(set = 1, binding = 0) uniform sampler2D diffuseTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;
layout(set = 1, binding = 2) uniform sampler2D ormTex;
layout(set = 1, binding = 3) uniform sampler2D aoTex;
layout(set = 1, binding = 4) uniform sampler2D glossTex;
layout(set = 1, binding = 5) uniform sampler2D emissiveTex;

// Matches CPU LightStruct (160 bytes) - identical to ModelPixel.glsl.
// 'active' is a reserved GLSL word, hence lActive.
struct LightStruct {
    vec3  position;      float _pad0;
    vec3  direction;     float _pad1;
    vec3  color;         float _pad2;
    vec3  ambient;       float intensity;
    vec3  specularColor; float _pad3;
    float range;  float angle;  int type;  int lActive;
    int   animMode; float animTimer; float animSpeed; float baseIntensity;
    float animAmplitude; float _pad4; float innerCone; float outerCone;
    float lightFalloff; float Shiningness; float Reflection; float _pad5;
    vec4  _pad6;
};
layout(std140, set = 2, binding = 0) uniform GlobalLightBuffer {
    int   globalLightCount;
    float _padGL0; float _padGL1; float _padGL2;
    LightStruct globalLights[MAX_GLOBAL_LIGHTS];
};
// MUST match ShadowBufferData in Lights.h (2368 bytes).
layout(std140, set = 2, binding = 1) uniform ShadowBuffer {
    mat4  lightViewProj;
    float shadowBias; float shadowStrength; float useShadowMap; float shadowMapSize;
    mat4  localViewProj[MAX_LOCAL_SHADOW_SLICES];
    ivec4 lightShadowInfo[MAX_GLOBAL_LIGHTS];
    float localBias; float useLocalShadows; float localMapSize; float displayBrightness;
    float displayContrast; float reflectionScale; float reflectionMaxMip; float reflectionBlur;
    vec4  planarParams;     // x = planar scale (0 = off), y = distortion, zw = 1/screen size
    vec4  planarPlanes[4];  // per planar layer: xyz = plane normal (faces the viewer), w = d
};
layout(set = 2, binding = 2) uniform sampler2DShadow      shadowMap;
layout(set = 2, binding = 3) uniform sampler2DArrayShadow localShadowMaps;
layout(set = 2, binding = 4) uniform samplerCube          sceneProbe;       // renderer-owned scene reflection probe
layout(set = 2, binding = 5) uniform sampler2DArray       planarMap;        // planar mirror renders, one layer per plane (reflector surfaces only)

vec3 FresnelSchlick(float c, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0);
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.001);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

// Same light model as ModelPixel.hlsl / ModelPixel.glsl ProcessLight().
vec3 ProcessLight(LightStruct light, vec3 N, vec3 V, vec3 worldPos,
                  float roughness, float metallic, vec3 albedo, vec3 F0)
{
    if (light.lActive == 0) return vec3(0.0);
    vec3  L = vec3(0.0);
    float attenuation = 1.0;
    if (light.type == LIGHT_TYPE_DIRECTIONAL) {
        L = normalize(-light.direction);
    } else {
        vec3  lightVec = light.position - worldPos;
        float dist     = length(lightVec);
        L = normalize(lightVec);
        if (light.type == LIGHT_TYPE_POINT) {
            attenuation = clamp(1.0 - dist / light.range, 0.0, 1.0) / (1.0 + dist * dist);
        } else if (light.type == LIGHT_TYPE_SPOT) {
            vec3  spotDir  = normalize(-light.direction);
            float spotCos  = dot(spotDir, -L);
            float spotFall = smoothstep(cos(light.outerCone), cos(light.innerCone), spotCos);
            float distFall = 1.0 / (1.0 + pow(dist, light.lightFalloff));
            attenuation = spotFall * distFall;
        }
    }
    attenuation *= max(light.baseIntensity + light.intensity, 0.0);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0001) return vec3(0.0);
    vec3  H       = normalize(V + L);
    float reflAdj = 1.0 + light.Reflection;
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0 * reflAdj);
    float NDF = DistributionGGX(N, H, roughness / (1.0 + light.Shiningness));
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.001);
    vec3  kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3  diffuseColor  = kD * albedo / PI;
    vec3  specularColor = specular * light.specularColor * reflAdj;
    return (diffuseColor + specularColor) * light.color * NdotL * attenuation;
}

// ── Shadows (raw visibility: 1 = lit, 0 = shadowed) ──
// Light matrices are D3D-style (depth 0..1, same as Vulkan).  Vulkan NDC y = -1 is the
// top row of the framebuffer, so uv = ndc * 0.5 + 0.5 with no flip.
float SampleDirShadow(vec3 worldPos) {
    if (useShadowMap < 0.5) return 1.0;
    vec4 lc = lightViewProj * vec4(worldPos, 1.0);
    vec3 p  = lc.xyz / lc.w;
    if (p.z > 1.0 || p.z < 0.0 || abs(p.x) > 1.0 || abs(p.y) > 1.0) return 1.0;
    vec2  uv    = p.xy * 0.5 + 0.5;
    float ref   = p.z - shadowBias;
    float texel = 1.0 / max(shadowMapSize, 1.0);
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            s += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * texel, ref));
    return s / 9.0;
}
float SampleLocalShadow(int slice, vec3 worldPos) {
    if (useLocalShadows < 0.5 || slice < 0 || slice >= MAX_LOCAL_SHADOW_SLICES) return 1.0;
    vec4 lc = localViewProj[slice] * vec4(worldPos, 1.0);
    if (lc.w <= 0.0001) return 1.0;
    vec3 p = lc.xyz / lc.w;
    if (p.z > 1.0 || p.z < 0.0 || abs(p.x) > 1.0 || abs(p.y) > 1.0) return 1.0;
    vec2  uv    = p.xy * 0.5 + 0.5;
    float ref   = p.z - localBias;
    float texel = 1.0 / max(localMapSize, 1.0);
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            s += texture(localShadowMaps, vec4(uv + vec2(float(x), float(y)) * texel, float(slice), ref));
    return s / 9.0;
}
// Face order MUST match BuildShadowFrame() in Lights.cpp: +X -X +Y -Y +Z -Z.
int PointShadowFace(vec3 v) {
    vec3 a = abs(v);
    if (a.x >= a.y && a.x >= a.z) return (v.x >= 0.0) ? 0 : 1;
    if (a.y >= a.z)               return (v.y >= 0.0) ? 2 : 3;
    return (v.z >= 0.0) ? 4 : 5;
}
float ShadowForGlobalLight(int li, vec3 lightPos, vec3 worldPos) {
    if (ubo.camPos.w < 0.5) return 1.0;
    ivec4 info = lightShadowInfo[li];
    float vis  = 1.0;
    if (info.x == SHADOW_KIND_DIRECTIONAL)  vis = SampleDirShadow(worldPos);
    else if (info.x == SHADOW_KIND_SPOT)    vis = SampleLocalShadow(info.y, worldPos);
    else if (info.x == SHADOW_KIND_POINT)   vis = SampleLocalShadow(info.y + PointShadowFace(worldPos - lightPos), worldPos);
    return mix(1.0 - shadowStrength, 1.0, vis);
}

void main() {
    // Diffuse albedo: sample texture only when useDiffuseMap is set, otherwise use material colour.
    vec4 albedo = mat.useDiffuseMap > 0.5 ? texture(diffuseTex, vTexCoord) : vec4(1.0);
    albedo.rgb *= mat.Kd;
    float metallicV  = mat.metallic;
    float roughnessV = mat.roughness;
    float aoV        = 1.0;
    if (mat.useORM > 0.5) {
        vec3 orm = texture(ormTex, vTexCoord).rgb;
        aoV = orm.r; roughnessV = orm.g; metallicV = orm.b;
    }
    // Gloss map: roughness = 1 - gloss.r (overrides ORM roughness when both are set).
    if (mat.useGlossMap > 0.5) roughnessV = 1.0 - texture(glossTex, vTexCoord).r;
    if (mat.useAO > 0.5) aoV = texture(aoTex, vTexCoord).r;
    vec3 N = normalize(vNormal);
    if (mat.useNormal > 0.5) {
        vec3 nTs = texture(normalTex, vTexCoord).xyz * 2.0 - 1.0;
        nTs.xy  *= mat.normalScale;
        N = normalize(mat3(normalize(vTangent), normalize(vBitangent), N) * nTs);
    }
    vec3 V  = normalize(vViewDir);
    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallicV);
    // Ambient: material Ka, or 15% of Kd when the importer left Ka at zero (GLTF has no Ka)
    // - same rule the DX11/DX12/OpenGL paths apply on the CPU.
    vec3 KaEff   = (mat.Ka.x + mat.Ka.y + mat.Ka.z <= 0.0003) ? mat.Kd * 0.15 : mat.Ka;
    vec3 ambient = KaEff * albedo.rgb * aoV;
    // Direct lighting: every global light, each attenuated by its own shadow map.
    vec3 direct = vec3(0.0);
    int  lightCount = min(globalLightCount, MAX_GLOBAL_LIGHTS);
    for (int gi = 0; gi < lightCount; ++gi) {
        vec3 contrib = ProcessLight(globalLights[gi], N, V, vWorldPos, roughnessV, metallicV, albedo.rgb, F0);
        direct += contrib * ShadowForGlobalLight(gi, globalLights[gi].position, vWorldPos);
    }
    // Emissive: sample emissive texture when flag is set, else use material emissive factor.
    vec3 emissive = mat.useEmissiveMap > 0.5
        ? texture(emissiveTex, vTexCoord).rgb * mat.emissiveStrength
        : mat.emissive * mat.emissiveStrength;
    // Scene reflection probe (set=2 binding 4); reflectionScale == 0 means the probe is off.
    // Roughness picks the mip; roughness-aware Fresnel keeps rough dielectrics from over-reflecting.
    vec3 reflection = vec3(0.0);
    if (reflectionScale > 0.0) {
        vec3  R     = reflect(-V, N);
        float mip   = clamp(roughnessV * reflectionMaxMip + reflectionBlur, 0.0, reflectionMaxMip);
        vec3  probe = textureLod(sceneProbe, R, mip).rgb;
        float NoV   = clamp(dot(N, V), 0.0, 1.0);
        float gloss = 1.0 - roughnessV;
        vec3  Fr    = F0 + (max(vec3(gloss), F0) - F0) * pow(1.0 - NoV, 5.0);
        reflection  = probe * Fr * reflectionScale * aoV;
    }
    vec3 color = ambient + direct + emissive + reflection;
    // Planar reflection (reflector surfaces only).  The mirror render uses an x-flipped projection, so u is
    // mirrored; gl_FragCoord (origin top-left in Vulkan) matches the image row order of the planar target.
    // planarIndex picks the layer of the plane this surface reflects.
    if (mat.planarStrength > 0.0 && planarParams.x > 0.0) {
        int  planarSlice = clamp(int(mat.planarIndex + 0.5), 0, 3);
        vec3 planarN     = planarPlanes[planarSlice].xyz;
        vec2 planarUV    = gl_FragCoord.xy * planarParams.zw;
        planarUV.x       = 1.0 - planarUV.x;
        // Ripple: normal-map detail (N vs the geometric normal) along the plane's two tangents.
        vec3 planarT1    = normalize(cross(planarN, (abs(planarN.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
        vec3 planarT2    = cross(planarN, planarT1);
        vec3 planarDelta = N - normalize(vNormal);
        planarUV        += vec2(dot(planarDelta, planarT1), dot(planarDelta, planarT2)) * (planarParams.y * 0.25);
        planarUV         = clamp(planarUV, 0.0, 1.0);
        vec3  planarCol  = textureLod(planarMap, vec3(planarUV, float(planarSlice)), 0.0).rgb;
        float planarNoV  = clamp(dot(N, V), 0.0, 1.0);
        float planarW    = clamp(mat.planarStrength * planarParams.x * (1.0 - roughnessV)
                                 * (0.4 + 0.6 * pow(1.0 - planarNoV, 2.0)), 0.0, 1.0);
        color = mix(color, planarCol, planarW);
    }
    // Video settings brightness / contrast (a zeroed ShadowBuffer reads as neutral).
    float dispB = (displayBrightness > 0.0) ? displayBrightness : 1.0;
    float dispC = (displayContrast   > 0.0) ? displayContrast   : 1.0;
    // Clamp first so emissive / over-lit pixels (> 1.0) still respond instead of clipping to white.
    color = clamp(color, 0.0, 1.0);
    color = clamp((color - 0.5) * dispC + 0.5, 0.0, 1.0) * dispB;
    // Linear output — the sRGB swapchain (VK_FORMAT_B8G8R8A8_SRGB) applies hardware gamma
    // automatically; manual Reinhard + pow(1/2.2) here would cause double gamma and produce
    // an over-bright, washed-out result.
    fragColor = vec4(color, albedo.a);
})";

VkShaderModule VulkanRenderer::CreateShaderModuleFromGLSL(const std::string& glsl, VkShaderStageFlagBits stage) const
{
    // Runtime GLSL → SPIR-V compilation via shaderc (Vulkan SDK)
    // If shaderc is not linked, this will fail to compile — install the Vulkan SDK.
#if HAS_SHADERC
    shaderc::Compiler       compiler;
    shaderc::CompileOptions opts;
    opts.SetOptimizationLevel(shaderc_optimization_level_size);

    shaderc_shader_kind kind = shaderc_glsl_vertex_shader;
    if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) kind = shaderc_glsl_fragment_shader;

    auto result = compiler.CompileGlslToSpv(glsl, kind, "shader", opts);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::string err = result.GetErrorMessage();
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[VulkanRenderer] Shader compile error: " + std::wstring(err.begin(), err.end()));
        return VK_NULL_HANDLE;
    }
    std::vector<uint32_t> spirv(result.cbegin(), result.cend());
    return CreateShaderModuleFromSPIRV(spirv);
#else
    // shaderc not available — return null handle; pipeline creation will be skipped
    (void)glsl; (void)stage;
    debug.logLevelMessage(LogLevel::LOG_WARNING,
        L"[VulkanRenderer] shaderc not available. Install Vulkan SDK to enable shader compilation.");
    return VK_NULL_HANDLE;
#endif
}

VkShaderModule VulkanRenderer::CreateShaderModuleFromSPIRV(const std::vector<uint32_t>& spirv) const
{
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode    = spirv.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(m_device, &ci, nullptr, &mod);
    return mod;
}

void VulkanRenderer::CreateGraphicsPipelines()
{
    // ---- 2D pipeline ----
    VkShaderModule vert2D = CreateShaderModuleFromGLSL(k_glsl2DVert, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderModule frag2D = CreateShaderModuleFromGLSL(k_glsl2DFrag, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vert2D == VK_NULL_HANDLE || frag2D == VK_NULL_HANDLE) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] 2D pipeline skipped — no shader modules.");
        if (vert2D) vkDestroyShaderModule(m_device, vert2D, nullptr);
        if (frag2D) vkDestroyShaderModule(m_device, frag2D, nullptr);
    } else {
        // Vertex input for VkVertex2D: pos(2), uv(2), color(4)
        VkVertexInputBindingDescription bind{};
        bind.binding   = 0;
        bind.stride    = sizeof(VkVertex2D);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(VkVertex2D, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(VkVertex2D, u) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkVertex2D, r) };

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &bind;
        vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vi.pVertexAttributeDescriptions    = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL;
        rast.cullMode    = VK_CULL_MODE_NONE;
        rast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rast.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.blendEnable         = VK_TRUE;
        blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
        blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
        blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                     | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blendAtt;

        std::array<VkDynamicState, 2> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
        dyn.pDynamicStates    = dynStates.data();

        // Push constants: screenSize, position, size, opacity
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset     = 0;
        pcRange.size       = sizeof(float) * 7;

        VkPipelineLayoutCreateInfo layoutCI{};
        layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCI.setLayoutCount         = 1;
        layoutCI.pSetLayouts            = &m_textureDescSetLayout;
        layoutCI.pushConstantRangeCount = 1;
        layoutCI.pPushConstantRanges    = &pcRange;
        vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &m_2dPipelineLayout);

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vert2D, "main" };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag2D, "main" };

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = static_cast<uint32_t>(stages.size());
        pci.pStages             = stages.data();
        pci.pVertexInputState   = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState      = &vp;
        pci.pRasterizationState = &rast;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &blend;
        pci.pDynamicState       = &dyn;
        pci.layout              = m_2dPipelineLayout;
        pci.renderPass          = m_renderPass;
        pci.subpass             = 0;
        vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_2dPipeline);

        // Multisampled variant for the main pass (the 1-sample pipeline above stays for any 1-sample pass)
        if (m_renderPassMS != VK_NULL_HANDLE && m_msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
            ms.rasterizationSamples = m_msaaSamples;
            pci.renderPass          = m_renderPassMS;
            if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_2dPipelineMS) != VK_SUCCESS)
                m_2dPipelineMS = VK_NULL_HANDLE;
        }

        vkDestroyShaderModule(m_device, vert2D, nullptr);
        vkDestroyShaderModule(m_device, frag2D, nullptr);
    }

    // ---- 3D pipeline ----
    VkShaderModule vert3D = CreateShaderModuleFromGLSL(k_glsl3DVert, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderModule frag3D = CreateShaderModuleFromGLSL(k_glsl3DFrag, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vert3D != VK_NULL_HANDLE && frag3D != VK_NULL_HANDLE) {
        // Vertex input for VkVertex3D: pos(3), normal(3), uv(2), tangent(3)
        VkVertexInputBindingDescription bind3D{};
        bind3D.binding   = 0;
        bind3D.stride    = sizeof(VkVertex3D);
        bind3D.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 4> attrs3D{};
        attrs3D[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex3D, x)  };
        attrs3D[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex3D, nx) };
        attrs3D[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(VkVertex3D, u)  };
        attrs3D[3] = { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex3D, tx) };

        VkPipelineVertexInputStateCreateInfo vi3D{};
        vi3D.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi3D.vertexBindingDescriptionCount   = 1;
        vi3D.pVertexBindingDescriptions      = &bind3D;
        vi3D.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs3D.size());
        vi3D.pVertexAttributeDescriptions    = attrs3D.data();

        VkPipelineInputAssemblyStateCreateInfo ia3D{};
        ia3D.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia3D.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp3D{};
        vp3D.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp3D.viewportCount = 1; vp3D.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rast3D{};
        rast3D.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast3D.polygonMode = bWireframeMode ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rast3D.cullMode    = VK_CULL_MODE_NONE;                    // All faces rendered; doubleSided support handled via material state
        rast3D.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rast3D.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms3D{};
        ms3D.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms3D.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds3D{};
        ds3D.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds3D.depthTestEnable  = VK_TRUE;
        ds3D.depthWriteEnable = VK_TRUE;
        ds3D.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAtt3D{};
        blendAtt3D.blendEnable    = VK_FALSE;
        blendAtt3D.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                  | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend3D{};
        blend3D.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend3D.attachmentCount = 1;
        blend3D.pAttachments    = &blendAtt3D;

        std::array<VkDynamicState, 2> dyn3DStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn3D{};
        dyn3D.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn3D.dynamicStateCount = static_cast<uint32_t>(dyn3DStates.size());
        dyn3D.pDynamicStates    = dyn3DStates.data();

        // set=0: transform+material UBOs, set=1: diffuse/normal/ORM/AO/gloss/emissive textures,
        // set=2: per-frame GlobalLightBuffer + ShadowBuffer + shadow maps (replaces the old
        // single-directional-light push constant).
        std::array<VkDescriptorSetLayout, 3> layouts3D = { m_3dUboSetLayout, m_3dTexSetLayout, m_3dFrameSetLayout };
        VkPipelineLayoutCreateInfo layoutCI3D{};
        layoutCI3D.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCI3D.setLayoutCount         = static_cast<uint32_t>(layouts3D.size());
        layoutCI3D.pSetLayouts            = layouts3D.data();
        layoutCI3D.pushConstantRangeCount = 0;
        layoutCI3D.pPushConstantRanges    = nullptr;
        vkCreatePipelineLayout(m_device, &layoutCI3D, nullptr, &m_3dPipelineLayout);

        std::array<VkPipelineShaderStageCreateInfo, 2> stages3D{};
        stages3D[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vert3D, "main" };
        stages3D[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag3D, "main" };

        VkGraphicsPipelineCreateInfo pci3D{};
        pci3D.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci3D.stageCount          = static_cast<uint32_t>(stages3D.size());
        pci3D.pStages             = stages3D.data();
        pci3D.pVertexInputState   = &vi3D;
        pci3D.pInputAssemblyState = &ia3D;
        pci3D.pViewportState      = &vp3D;
        pci3D.pRasterizationState = &rast3D;
        pci3D.pMultisampleState   = &ms3D;
        pci3D.pDepthStencilState  = &ds3D;
        pci3D.pColorBlendState    = &blend3D;
        pci3D.pDynamicState       = &dyn3D;
        pci3D.layout              = m_3dPipelineLayout;
        pci3D.renderPass          = m_renderPass;
        pci3D.subpass             = 0;
        vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci3D, nullptr, &m_3dPipeline);

        // Multisampled variant for the main pass.  Anti-Aliasing also turns on sample-rate shading (when the
        // device supports it) so specular / texture aliasing inside triangles is smoothed, not just their edges.
        if (m_renderPassMS != VK_NULL_HANDLE && m_msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
            ms3D.rasterizationSamples = m_msaaSamples;
            if (m_sampleShadingOk) {
                ms3D.sampleShadingEnable = VK_TRUE;
                ms3D.minSampleShading    = 0.25f;
            }
            pci3D.renderPass = m_renderPassMS;
            if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci3D, nullptr, &m_3dPipelineMS) != VK_SUCCESS)
                m_3dPipelineMS = VK_NULL_HANDLE;
        }

        vkDestroyShaderModule(m_device, vert3D, nullptr);
        vkDestroyShaderModule(m_device, frag3D, nullptr);
    } else {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] 3D pipeline skipped — no shader modules.");
        if (vert3D) vkDestroyShaderModule(m_device, vert3D, nullptr);
        if (frag3D) vkDestroyShaderModule(m_device, frag3D, nullptr);
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Graphics pipelines created.");
}

// ---------------------------------------------------------------------------------------------------------------
// Command pool, depth, framebuffers, sync, quad buffer
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateCommandPool()
{
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = m_graphicsQueueFamily;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create command pool.");

    VkCommandPoolCreateInfo lci{};
    lci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    lci.queueFamilyIndex = m_graphicsQueueFamily;
    lci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_device, &lci, nullptr, &m_loaderCommandPool) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create loader command pool.");

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = VK_MAX_FRAMES_IN_FLIGHT;
    std::array<VkCommandBuffer, VK_MAX_FRAMES_IN_FLIGHT> cmds;
    vkAllocateCommandBuffers(m_device, &ai, cmds.data());
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
        m_frames[i].commandBuffer = cmds[i];
}

void VulkanRenderer::CreateDepthResources()
{
    VkFormat depthFmt = FindDepthFormat();
    CreateImage(static_cast<uint32_t>(m_renderTargetWidth), static_cast<uint32_t>(m_renderTargetHeight),
                depthFmt, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_depthImage, m_depthImageMemory, m_renderPassMS != VK_NULL_HANDLE ? m_msaaSamples : VK_SAMPLE_COUNT_1_BIT);
    m_depthImageView = CreateImageView(m_depthImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT);

    CreateMsaaColorResources();
}

// Multisampled colour target of the main pass (resolved into the swapchain image at the end of the pass).
void VulkanRenderer::CreateMsaaColorResources()
{
    if (m_renderPassMS == VK_NULL_HANDLE || m_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        return;
    CreateImage(static_cast<uint32_t>(m_renderTargetWidth), static_cast<uint32_t>(m_renderTargetHeight),
                m_swapchainFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_msaaColorImage, m_msaaColorMemory, m_msaaSamples);
    m_msaaColorView = CreateImageView(m_msaaColorImage, m_swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanRenderer::CreateFramebuffers()
{
    const bool msaa = (m_renderPassMS != VK_NULL_HANDLE && m_msaaColorView != VK_NULL_HANDLE);
    m_framebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        // 1 sample: [swapchain, depth].  MSAA: [msaa colour, msaa depth, swapchain (resolve target)].
        std::array<VkImageView, 3> attachments = msaa
            ? std::array<VkImageView, 3>{ m_msaaColorView, m_depthImageView, m_swapchainImageViews[i] }
            : std::array<VkImageView, 3>{ m_swapchainImageViews[i], m_depthImageView, VK_NULL_HANDLE };
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = msaa ? m_renderPassMS : m_renderPass;
        ci.attachmentCount = msaa ? 3u : 2u;
        ci.pAttachments    = attachments.data();
        ci.width           = m_swapchainExtent.width;
        ci.height          = m_swapchainExtent.height;
        ci.layers          = 1;
        vkCreateFramebuffer(m_device, &ci, nullptr, &m_framebuffers[i]);
    }
}

void VulkanRenderer::CreateDescriptorPool()
{
    // Per-model: 6 samplers (diffuse/normal/ORM/AO/gloss/emissive) + 2 UBOs (transform + material).
    // Per-frame 2D: small transient allocations (freed each frame).
    // Pool sized for up to 512 loaded models + generous headroom for 2D.
    std::array<VkDescriptorPoolSize, 3> sizes{};
    sizes[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6144 }; // 512 models x 6 textures + 2D headroom
    sizes[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1024 + 6 * VK_MAX_FRAMES_IN_FLIGHT }; // 512 models x material UBO + per-frame light/shadow UBOs (set=2: normal + mirror + live copies)
    sizes[2] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024 }; // 512 models x transform UBO (dynamic offset: main + mirror slot)

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    ci.pPoolSizes    = sizes.data();
    ci.maxSets       = 4096;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descriptorPool);
}

void VulkanRenderer::CreateSyncObjects()
{
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // imageAvailable and inFlightFence: one per frame-in-flight (fence-protected reuse is safe)
    for (auto& fd : m_frames) {
        vkCreateSemaphore(m_device, &sci, nullptr, &fd.imageAvailable);
        vkCreateFence(m_device, &fci, nullptr, &fd.inFlightFence);
    }

    // renderFinished: one per SWAPCHAIN IMAGE so we never signal a semaphore that the
    // presentation engine is still waiting on (VUID-vkQueueSubmit-pSignalSemaphores-00067).
    m_renderFinishedSemaphores.resize(m_swapchainImages.size(), VK_NULL_HANDLE);
    for (auto& sem : m_renderFinishedSemaphores)
        vkCreateSemaphore(m_device, &sci, nullptr, &sem);
}

void VulkanRenderer::CreateQuadVertexBuffer()
{
    // Unit quad (0-1 UV, triangle strip: TL, TR, BL, BR)
    const VkVertex2D quadVerts[] = {
        { 0.0f, 0.0f, 0.0f, 0.0f, 1,1,1,1 },
        { 1.0f, 0.0f, 1.0f, 0.0f, 1,1,1,1 },
        { 0.0f, 1.0f, 0.0f, 1.0f, 1,1,1,1 },
        { 1.0f, 1.0f, 1.0f, 1.0f, 1,1,1,1 },
    };
    VkDeviceSize size = sizeof(quadVerts);
    VkBuffer     staging; VkDeviceMemory stagingMem;
    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);
    void* data; vkMapMemory(m_device, stagingMem, 0, size, 0, &data);
    std::memcpy(data, quadVerts, static_cast<size_t>(size));
    vkUnmapMemory(m_device, stagingMem);
    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_quadVertexBuffer, m_quadVertexMemory);
    CopyBuffer(staging, m_quadVertexBuffer, size);
    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);
}

// ---------------------------------------------------------------------------------------------------------------
// 2D overlay resources (Windows = D2D on WIC bitmap; Linux/Android = CPU RGBA buffer)
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateOverlayResources(uint32_t width, uint32_t height)
{
    m_overlayWidth  = width;
    m_overlayHeight = height;

    bool overlayOk = CreatePlatformOverlayTargets(width, height);

    // Overlay texture uses BGRA to match Direct2D's native WIC PBGRA output format.
    // Using R8G8B8A8 here would swap red and blue channels, producing wrong colours.
    m_overlayTexture = CreateTextureFromRGBA(nullptr, width, height, VK_FORMAT_B8G8R8A8_SRGB);

    VkDeviceSize bufSize = static_cast<VkDeviceSize>(width) * height << 2;
    CreateBuffer(bufSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_overlayStagingBuffer, m_overlayStagingMemory);

    CreatePlatformBackgroundTarget(width, height, overlayOk);

    CreatePlatformBackgroundOverlay(width, height, bufSize);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Overlay resources created.");
}

// ---------------------------------------------------------------------------------------------------------------
// Swap chain teardown and recreation
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CleanupSwapChain()
{
    for (auto fb : m_framebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();

    if (m_msaaColorView   != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_msaaColorView,   nullptr); m_msaaColorView   = VK_NULL_HANDLE; }
    if (m_msaaColorImage  != VK_NULL_HANDLE) { vkDestroyImage    (m_device, m_msaaColorImage,  nullptr); m_msaaColorImage  = VK_NULL_HANDLE; }
    if (m_msaaColorMemory != VK_NULL_HANDLE) { vkFreeMemory      (m_device, m_msaaColorMemory, nullptr); m_msaaColorMemory = VK_NULL_HANDLE; }

    if (m_depthImageView   != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_depthImageView,   nullptr); m_depthImageView   = VK_NULL_HANDLE; }
    if (m_depthImage       != VK_NULL_HANDLE) { vkDestroyImage    (m_device, m_depthImage,       nullptr); m_depthImage       = VK_NULL_HANDLE; }
    if (m_depthImageMemory != VK_NULL_HANDLE) { vkFreeMemory      (m_device, m_depthImageMemory, nullptr); m_depthImageMemory = VK_NULL_HANDLE; }

    for (auto iv : m_swapchainImageViews) vkDestroyImageView(m_device, iv, nullptr);
    m_swapchainImageViews.clear();

    // renderFinished semaphores are per-swapchain-image; destroy and repopulate on every recreation.
    for (auto& sem : m_renderFinishedSemaphores)
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(m_device, sem, nullptr);
    m_renderFinishedSemaphores.clear();

    if (m_renderPassMS != VK_NULL_HANDLE) { vkDestroyRenderPass(m_device, m_renderPassMS, nullptr); m_renderPassMS = VK_NULL_HANDLE; }
    if (m_renderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(m_device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
    if (m_swapchain  != VK_NULL_HANDLE) { vkDestroySwapchainKHR(m_device, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
}

// RecreateSurface — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

void VulkanRenderer::RecreateSwapChain(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    WaitForGPUToFinish();
    CleanupSwapChain();

    // If the surface is lost, rebuild it before retrying swapchain creation.
    try {
        CreateSwapChain(width, height);
    }
    catch (const std::runtime_error& e) {
        std::string msg(e.what());
        if (msg.find("Surface lost") != std::string::npos ||
            msg.find("Surface capabilities invalid") != std::string::npos)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] Surface lost during swapchain recreation — rebuilding surface.");
            RecreateSurface();
            CreateSwapChain(width, height);
        }
        else { throw; }
    }

    CreateImageViews();
    CreateRenderPass();
    CreateDepthResources();
    CreateFramebuffers();

    // Recreate per-image renderFinished semaphores to match the new image count.
    // CleanupSwapChain already destroyed the old ones.
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    m_renderFinishedSemaphores.resize(m_swapchainImages.size(), VK_NULL_HANDLE);
    for (auto& sem : m_renderFinishedSemaphores)
        if (sem == VK_NULL_HANDLE) vkCreateSemaphore(m_device, &sci, nullptr, &sem);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Swap chain recreated.");
}

// ---------------------------------------------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------------------------------------------
bool VulkanRenderer::Resize(uint32_t width, uint32_t height)
{
    if (m_bHasCleanedUp) return false;
    if (width == 0 || height == 0) return false;
    m_prevWindowedWidth  = static_cast<uint32_t>(m_renderTargetWidth);
    m_prevWindowedHeight = static_cast<uint32_t>(m_renderTargetHeight);

    wasResizing.store(true);
    threadManager.threadVars.bIsResizing.store(true);

    // Drain any in-progress render frame before tearing down D2D/Vulkan resources.
    // The old spin-wait on bIsRendering had a TOCTOU race: the render thread checks
    // bIsRendering=false and sets it to true AFTER the load(); Resize() could see the
    // false window and proceed, resetting m_d2dRenderTarget while the render thread
    // was between its null-check and DrawText() — producing the memcpy crash in DWrite.
    //
    // Acquiring the exclusive frame lock guarantees the render thread has fully released
    // it (i.e. its current frame is complete) before we touch any shared D2D state.
    // bIsResizing=true prevents a new frame from entering RenderFrame() while we work.
    {
        ThreadLockHelper frameLock(threadManager, m_renderFrameLockName, 2000);
        // Lock acquired (or timed out after 2 s) — render thread is not mid-frame.
    }

    WaitForGPUToFinish();

    m_renderTargetWidth  = static_cast<int>(width);
    m_renderTargetHeight = static_cast<int>(height);
    iOrigWidth  = static_cast<int>(width);
    iOrigHeight = static_cast<int>(height);

    RecreateSwapChain(width, height);

    // Recreate overlay surfaces at new size
    DestroyVulkanTexture(m_overlayTexture);
    DestroyVulkanTexture(m_bgOverlayTexture);
    ResetPlatformOverlayTargets();
    // Staging buffers are plain Vulkan resources (created on every platform
    // by CreateOverlayResources) — destroy them here too, or Resize leaks a
    // staging buffer pair per resize on Linux/Android.
    if (m_overlayStagingBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_overlayStagingBuffer, nullptr);
    if (m_overlayStagingMemory  != VK_NULL_HANDLE) vkFreeMemory(m_device, m_overlayStagingMemory, nullptr);
    m_overlayStagingBuffer = VK_NULL_HANDLE;
    m_overlayStagingMemory = VK_NULL_HANDLE;
    if (m_bgStagingBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_bgStagingBuffer, nullptr);
    if (m_bgStagingMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_bgStagingMemory, nullptr);
    m_bgStagingBuffer = VK_NULL_HANDLE;
    m_bgStagingMemory = VK_NULL_HANDLE;
    CreateOverlayResources(width, height);

    threadManager.threadVars.bIsResizing.store(false);
    wasResizing.store(false);

    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[VulkanRenderer] Resize complete: " + std::to_wstring(width) + L"x" + std::to_wstring(height));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Thread management
// ---------------------------------------------------------------------------------------------------------------
bool VulkanRenderer::StartRendererThreads()
{
    bool result = true;
    try
    {
        threadManager.SetThread(THREAD_LOADER, [this]() { LoaderTaskThread(); }, true);
        threadManager.StartThread(THREAD_LOADER);
#ifdef RENDERER_IS_THREAD
        threadManager.SetThread(THREAD_RENDERER, [this]() { RenderFrame(); }, true);
        threadManager.StartThread(THREAD_RENDERER);
#endif
    }
    catch (const std::exception&)
    {
        result = false;
    }
    return result;
}

void VulkanRenderer::ResumeLoader(bool isResizing)
{
    wasResizing.store(isResizing);
    threadManager.ResumeThread(THREAD_LOADER);
}

void VulkanRenderer::WaitForGPUToFinish()
{
    if (m_device != VK_NULL_HANDLE) {
        // Hold the queue mutex so no thread is mid-submit when we wait.
        // vkDeviceWaitIdle implicitly touches the same VkQueue object that
        // vkQueueSubmit / vkQueueWaitIdle use; accessing it from two threads
        // simultaneously triggers a Vulkan threading-error validation message.
        std::lock_guard<std::mutex> lock(m_queueMutex);
        vkDeviceWaitIdle(m_device);
    }
}

void VulkanRenderer::WaitToFinishThenPauseThread()
{
    WaitForGPUToFinish();
    threadManager.PauseThread(THREAD_RENDERER);
}

// ---------------------------------------------------------------------------------------------------------------
// Display mode transition helpers
// ---------------------------------------------------------------------------------------------------------------

// EnforceFullscreenWindowPlacement — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).
// ReleaseFullScreenExclusiveIfActive — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// AcquireFullScreenExclusiveIfAvailable — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

void VulkanRenderer::OnBeforeResizeOrModeChange()
{
    // Ensure GPU is idle, then release any held exclusive mode before swapchain is destroyed.
    WaitForGPUToFinish();
    ReleaseFullScreenExclusiveIfActive();
}

void VulkanRenderer::OnAfterResizeOrModeChange(int width, int height, DisplayMode mode)
{
    // Swapchain is rebuilt by Resize(); acquire exclusive ownership afterward if requested.
    Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    if (mode == DisplayMode::ExclusiveFullscreen)
        AcquireFullScreenExclusiveIfAvailable();
}

// ---------------------------------------------------------------------------------------------------------------
// Screen mode helpers
// ---------------------------------------------------------------------------------------------------------------
// SetFullScreen — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// SetFullExclusive — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// SetWindowedScreen — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

bool VulkanRenderer::SetDisplayMode()
{
    const DisplayMode mode = static_cast<DisplayMode>(std::clamp(config.myConfig.displayMode, 0, 2));
    return SetDisplayMode(
        mode,
        config.myConfig.resolutionWidth,
        config.myConfig.resolutionHeight,
        config.myConfig.refreshRate);
}

bool VulkanRenderer::SetDisplayMode(DisplayMode mode, int width, int height, int /*refreshHz*/)
{
    // Sequence per the CPGE guideline:
    //   1. WaitForGPUIdle  2. Release exclusive (if held)  3. Change Win32 window
    //   4. Rebuild swapchain (via Resize inside each helper)  5. Acquire exclusive (if requested)
    WaitForGPUToFinish();

    bool result = false;
    switch (mode)
    {
        case DisplayMode::Windowed:
            m_requestExclusiveMode = false;
            ReleaseFullScreenExclusiveIfActive();
            result = SetWindowedScreen();
            break;

        case DisplayMode::BorderlessFullscreen:
            m_requestExclusiveMode = false;
            ReleaseFullScreenExclusiveIfActive();
            result = SetFullScreen();
            break;

        case DisplayMode::ExclusiveFullscreen:
            if (width > 0 && height > 0) {
                m_requestExclusiveMode = true;  // CreateSwapChain picks this up
                ReleaseFullScreenExclusiveIfActive();
                result = SetFullExclusive(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
                // Acquire VK exclusive ownership after swapchain is rebuilt.
                if (result) AcquireFullScreenExclusiveIfAvailable();
            } else {
                debug.logLevelMessage(LogLevel::LOG_ERROR,
                    L"[VulkanRenderer] SetDisplayMode: invalid exclusive fullscreen dimensions");
                return false;
            }
            break;

        default:
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] SetDisplayMode: unknown mode — ignoring");
            return false;
    }

    // Reposition the GameMenu to the far right of the new resolution.
    if (result)
        guiManager.OnWindowResize(iOrigWidth, iOrigHeight);

    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// Device accessor overrides
// ---------------------------------------------------------------------------------------------------------------
void* VulkanRenderer::GetDevice()        { return static_cast<void*>(m_device); }
void* VulkanRenderer::GetDeviceContext() { return static_cast<void*>(m_graphicsQueue); }
void* VulkanRenderer::GetSwapChain()     { return static_cast<void*>(m_swapchain); }

VkCommandBuffer VulkanRenderer::GetCurrentCommandBuffer() const
{
    return m_frames[m_currentFrame].commandBuffer;
}

// ---------------------------------------------------------------------------------------------------------------
// Texture loading
// ---------------------------------------------------------------------------------------------------------------
bool VulkanRenderer::LoadTexture(int textureId, const std::wstring& filename, bool is2D)
{
    // Bounds guard — catch bad indices before any array access.
    const int maxIdx = is2D ? MAX_TEXTURE_BUFFERS : MAX_TEXTURE_BUFFERS_3D;
    if (textureId < 0 || textureId >= maxIdx) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[VulkanRenderer] LoadTexture: index " + std::to_wstring(textureId) + L" out of range");
        return false;
    }
    if (filename.empty()) return false;
    if (m_device == VK_NULL_HANDLE) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[VulkanRenderer] LoadTexture: Vulkan device not ready, cannot load: " + filename);
        return false;
    }

    std::filesystem::path fullPath = AssetsDir / filename;

    return LoadTextureFromFilePlatform(filename, is2D, textureId, fullPath);
}

bool VulkanRenderer::LoadAllKnownTextures()
{
    bool allOk = true;
    for (int i = 0; i < MAX_TEXTURE_BUFFERS; ++i) {
        if (texFilename[i].empty()) continue;
        if (!LoadTexture(i, texFilename[i], true)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] Could not load 2D texture: " + texFilename[i]);
            allOk = false;
        }
    }
    return allOk;
}

void VulkanRenderer::UnloadTexture(int textureId)
{
    if (textureId < 0 || textureId >= MAX_TEXTURE_BUFFERS) return;
    DestroyVulkanTexture(m_textures2D[textureId]);
    ReleasePlatformD2DTexture(textureId);
}

// ---------------------------------------------------------------------------------------------------------------
// VulkanTexture helpers
// ---------------------------------------------------------------------------------------------------------------
VulkanTexture VulkanRenderer::CreateTextureFromRGBA(const uint8_t* pixels, uint32_t width, uint32_t height,
                                                    VkFormat format)
{
    VulkanTexture tex{};
    tex.width  = width;
    tex.height = height;

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height << 2;

    // Staging buffer
    VkBuffer       staging; VkDeviceMemory stagingMem;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);

    if (pixels) {
        void* data; vkMapMemory(m_device, stagingMem, 0, imageSize, 0, &data);
        MemoryCopy(pixels, data, static_cast<size_t>(imageSize));
        vkUnmapMemory(m_device, stagingMem);
    }

    CreateImage(width, height, format, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory);

    TransitionImageLayout(tex.image, format,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (pixels) CopyBufferToImage(staging, tex.image, width, height);
    TransitionImageLayout(tex.image, format,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    tex.view = CreateImageView(tex.image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo si{};
    si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter     = VK_FILTER_LINEAR;
    si.minFilter     = VK_FILTER_LINEAR;
    si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy    = 16.0f;
    si.borderColor       = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    si.unnormalizedCoordinates = VK_FALSE;
    si.compareEnable   = VK_FALSE;
    si.mipmapMode      = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(m_device, &si, nullptr, &tex.sampler);

    tex.isValid = true;
    return tex;
}

// ============================================================================
// GetSamplerForWrap — UV settings support (GLTF sampler wrapS/wrapT, FBX
// WrapModeU/V).  Returns the shared default sampler for (REPEAT, REPEAT);
// other combinations are created lazily with the same filtering as the
// default sampler and cached for the renderer's lifetime (destroyed in
// Cleanup alongside the default textures).
// ============================================================================
VkSampler VulkanRenderer::GetSamplerForWrap(int wrapU, int wrapV)
{
    // Clamp indices into the supported range (0=REPEAT 1=CLAMP 2=MIRROR).
    if (wrapU < 0 || wrapU > 2) wrapU = 0;
    if (wrapV < 0 || wrapV > 2) wrapV = 0;

    // Default combination — reuse the shared default sampler.
    if (wrapU == 0 && wrapV == 0)
        return GetDefaultSampler();

    if (m_wrapSamplers[wrapU][wrapV] != VK_NULL_HANDLE)
        return m_wrapSamplers[wrapU][wrapV];

    auto vkWrap = [](int wrapMode) -> VkSamplerAddressMode {
        if (wrapMode == 1) return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (wrapMode == 2) return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };

    // Same filtering as the default sampler (CreateVulkanTexture).
    VkSamplerCreateInfo si{};
    si.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter        = VK_FILTER_LINEAR;
    si.minFilter        = VK_FILTER_LINEAR;
    si.addressModeU     = vkWrap(wrapU);
    si.addressModeV     = vkWrap(wrapV);
    si.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy    = 16.0f;
    si.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    si.unnormalizedCoordinates = VK_FALSE;
    si.compareEnable    = VK_FALSE;
    si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_device, &si, nullptr, &m_wrapSamplers[wrapU][wrapV]) != VK_SUCCESS)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"[VulkanRenderer] GetSamplerForWrap: sampler creation failed — using default sampler");
        m_wrapSamplers[wrapU][wrapV] = VK_NULL_HANDLE;
        return GetDefaultSampler();
    }
    return m_wrapSamplers[wrapU][wrapV];
}

void VulkanRenderer::DestroyVulkanTexture(VulkanTexture& tex)
{
    if (!tex.isValid || m_device == VK_NULL_HANDLE) return;
    // Wait for all in-flight GPU work to finish before destroying resources.
    // Without this, vkDestroySampler fires a validation error if a descriptor
    // set still references the sampler in a queued command buffer.
    // Hold the queue mutex: this runs on the loader thread and vkDeviceWaitIdle
    // touches the same VkQueue object the render thread submits/presents on
    // (see WaitForGPUToFinish above) -- without the lock, Vulkan reports a
    // threading error when both threads touch the queue concurrently.
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        vkDeviceWaitIdle(m_device);
    }
    if (tex.sampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, tex.sampler, nullptr);   tex.sampler = VK_NULL_HANDLE; }
    if (tex.view    != VK_NULL_HANDLE) { vkDestroyImageView(m_device, tex.view, nullptr);    tex.view    = VK_NULL_HANDLE; }
    if (tex.image   != VK_NULL_HANDLE) { vkDestroyImage(m_device, tex.image, nullptr);       tex.image   = VK_NULL_HANDLE; }
    if (tex.memory  != VK_NULL_HANDLE) { vkFreeMemory(m_device, tex.memory, nullptr);        tex.memory  = VK_NULL_HANDLE; }
    tex.isValid = false;
}

// ---------------------------------------------------------------------------------------------------------------
// Vulkan utility helpers
// ---------------------------------------------------------------------------------------------------------------
uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("[VulkanRenderer] Failed to find suitable memory type.");
}

void VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags props,
                                   VkBuffer& buffer, VkDeviceMemory& memory) const
{
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_device, &ci, nullptr, &buffer);

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, buffer, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    vkAllocateMemory(m_device, &ai, nullptr, &memory);
    vkBindBufferMemory(m_device, buffer, memory, 0);
}

void VulkanRenderer::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const
{
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkBufferCopy copy{ 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &copy);
    EndSingleTimeCommands(cmd);
}

VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands() const
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = m_loaderCommandPool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer cmd) const
{
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    {
        std::lock_guard<std::mutex> qlock(m_queueMutex);
        vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphicsQueue);
    }
    vkFreeCommandBuffers(m_device, m_loaderCommandPool, 1, &cmd);
}

void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, VkFormat format,
                                  VkImageTiling tiling, VkImageUsageFlags usage,
                                  VkMemoryPropertyFlags props,
                                  VkImage& image, VkDeviceMemory& memory,
                                  VkSampleCountFlagBits samples) const
{
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.extent        = { width, height, 1 };
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.format        = format;
    ci.tiling        = tiling;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage         = usage;
    ci.samples       = samples;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(m_device, &ci, nullptr, &image);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    vkAllocateMemory(m_device, &ai, nullptr, &memory);
    vkBindImageMemory(m_device, image, memory, 0);
}

VkImageView VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const
{
    VkImageViewCreateInfo ci{};
    ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image                           = image;
    ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ci.format                          = format;
    ci.subresourceRange.aspectMask     = aspect;
    ci.subresourceRange.baseMipLevel   = 0;
    ci.subresourceRange.levelCount     = 1;
    ci.subresourceRange.baseArrayLayer = 0;
    ci.subresourceRange.layerCount     = 1;
    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(m_device, &ci, nullptr, &view);
    return view;
}

void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format,
                                            VkImageLayout oldLayout, VkImageLayout newLayout) const
{
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
            (HasStencilComponent(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);

    VkPipelineStageFlags src = 0, dst = 0;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmd);
}

void VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
{
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { width, height, 1 };
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);
}

VkFormat VulkanRenderer::FindDepthFormat() const
{
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                               VkImageTiling tiling, VkFormatFeatureFlags features) const
{
    for (auto fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, fmt, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR  && (props.linearTilingFeatures  & features) == features) return fmt;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) return fmt;
    }
    throw std::runtime_error("[VulkanRenderer] Failed to find supported format.");
}

bool VulkanRenderer::HasStencilComponent(VkFormat fmt) const
{
    return fmt == VK_FORMAT_D32_SFLOAT_S8_UINT || fmt == VK_FORMAT_D24_UNORM_S8_UINT;
}

// ---------------------------------------------------------------------------------------------------------------
// Per-frame lighting + shadow mapping resources (3D pipeline set = 2).  See Lights.h for the shared
// planner and the ShadowBufferData layout.  Platform-neutral (Windows / Linux / Android).
//
// Part 1 (REQUIRED by the 3D pipeline - lighting lives here since the push-constant light was removed):
//   per-frame GlobalLightBuffer + ShadowBuffer UBOs, the two depth images (left in
//   SHADER_READ_ONLY_OPTIMAL), the comparison sampler and the set=2 descriptor sets.
// Part 2 (optional): depth-only render pass, framebuffers and pipeline.  If this fails the scene
//   still renders fully lit - m_shadowResourcesReady stays false and ShadowBuffer keeps shadows off.
// ---------------------------------------------------------------------------------------------------------------
bool VulkanRenderer::CreateShadowResourcesVK()
{
    ReleaseShadowResourcesVK();
    if (m_device == VK_NULL_HANDLE || m_descriptorPool == VK_NULL_HANDLE || m_3dFrameSetLayout == VK_NULL_HANDLE)
        return false;

    m_shadowDirSize   = ShadowDirMapSizeFromConfig();
    m_shadowLocalSize = ShadowLocalMapSizeFromConfig();

    try
    {
        // ---- Per-frame UBOs (host-visible, coherent, persistently mapped) ----
        for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
        {
            CreateBuffer(sizeof(GlobalLightBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_lightUBO[f], m_lightUBOMemory[f]);
            vkMapMemory(m_device, m_lightUBOMemory[f], 0, sizeof(GlobalLightBuffer), 0, &m_lightUBOMapped[f]);
            if (m_lightUBOMapped[f]) std::memset(m_lightUBOMapped[f], 0, sizeof(GlobalLightBuffer));

            CreateBuffer(sizeof(ShadowBufferData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_shadowUBO[f], m_shadowUBOMemory[f]);
            vkMapMemory(m_device, m_shadowUBOMemory[f], 0, sizeof(ShadowBufferData), 0, &m_shadowUBOMapped[f]);
            if (m_shadowUBOMapped[f]) std::memset(m_shadowUBOMapped[f], 0, sizeof(ShadowBufferData));

            // Copy of the ShadowBuffer with planar reflections off, for the mirror + capture passes: the per-model
            // material (planar strength) is read when the command buffer executes, so those passes must not mix
            // in the planar array (they bind a dummy texture there).
            CreateBuffer(sizeof(ShadowBufferData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_shadowUBOMirror[f], m_shadowUBOMirrorMemory[f]);
            vkMapMemory(m_device, m_shadowUBOMirrorMemory[f], 0, sizeof(ShadowBufferData), 0, &m_shadowUBOMirrorMapped[f]);
            if (m_shadowUBOMirrorMapped[f]) std::memset(m_shadowUBOMirrorMapped[f], 0, sizeof(ShadowBufferData));
        }

        // ---- Depth format: D32_SFLOAT preferred; D16_UNORM is guaranteed sampleable by the spec ----
        m_shadowDepthFormat = FindSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM }, VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
        VkFormatProperties fmtProps{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, m_shadowDepthFormat, &fmtProps);
        const bool linearOK = (fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;

        // ---- Depth images ----
        auto makeDepthImage = [this](uint32_t size, uint32_t layers, VkImage& image, VkDeviceMemory& memory)
        {
            VkImageCreateInfo ci{};
            ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType     = VK_IMAGE_TYPE_2D;
            ci.extent        = { size, size, 1 };
            ci.mipLevels     = 1;
            ci.arrayLayers   = layers;
            ci.format        = m_shadowDepthFormat;
            ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ci.samples       = VK_SAMPLE_COUNT_1_BIT;
            ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateImage(m_device, &ci, nullptr, &image) != VK_SUCCESS)
                throw std::runtime_error("[VulkanRenderer] Shadow image creation failed.");

            VkMemoryRequirements req;
            vkGetImageMemoryRequirements(m_device, image, &req);
            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(m_device, &ai, nullptr, &memory) != VK_SUCCESS)
                throw std::runtime_error("[VulkanRenderer] Shadow image memory allocation failed.");
            vkBindImageMemory(m_device, image, memory, 0);
        };
        auto makeView = [this](VkImage image, VkImageViewType type, uint32_t baseLayer, uint32_t layerCount) -> VkImageView
        {
            VkImageViewCreateInfo ci{};
            ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image                           = image;
            ci.viewType                        = type;
            ci.format                          = m_shadowDepthFormat;
            ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
            ci.subresourceRange.baseMipLevel   = 0;
            ci.subresourceRange.levelCount     = 1;
            ci.subresourceRange.baseArrayLayer = baseLayer;
            ci.subresourceRange.layerCount     = layerCount;
            VkImageView view = VK_NULL_HANDLE;
            if (vkCreateImageView(m_device, &ci, nullptr, &view) != VK_SUCCESS)
                throw std::runtime_error("[VulkanRenderer] Shadow image view creation failed.");
            return view;
        };

        makeDepthImage(static_cast<uint32_t>(m_shadowDirSize), 1, m_shadowDirImage, m_shadowDirMemory);
        makeDepthImage(static_cast<uint32_t>(m_shadowLocalSize), MAX_LOCAL_SHADOW_SLICES, m_shadowLocalImage, m_shadowLocalMemory);

        m_shadowDirView        = makeView(m_shadowDirImage,   VK_IMAGE_VIEW_TYPE_2D,       0, 1);
        m_shadowLocalArrayView = makeView(m_shadowLocalImage, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, MAX_LOCAL_SHADOW_SLICES);
        for (int s = 0; s < MAX_LOCAL_SHADOW_SLICES; ++s)
            m_shadowLocalLayerViews[s] = makeView(m_shadowLocalImage, VK_IMAGE_VIEW_TYPE_2D, static_cast<uint32_t>(s), 1);

        // ---- Initial layout: SHADER_READ_ONLY_OPTIMAL so set=2 is valid before the first shadow pass ----
        {
            VkCommandBuffer cmd = BeginSingleTimeCommands();
            VkImageMemoryBarrier barriers[2]{};
            for (int b = 0; b < 2; ++b)
            {
                barriers[b].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[b].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                barriers[b].newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[b].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[b].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[b].srcAccessMask       = 0;
                barriers[b].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            }
            barriers[0].image            = m_shadowDirImage;
            barriers[0].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            barriers[1].image            = m_shadowLocalImage;
            barriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, static_cast<uint32_t>(MAX_LOCAL_SHADOW_SLICES) };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, barriers);
            EndSingleTimeCommands(cmd);
        }

        // ---- Comparison sampler (PCF): outside the map = lit ----
        {
            VkSamplerCreateInfo si{};
            si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            si.magFilter     = linearOK ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            si.minFilter     = linearOK ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            si.compareEnable = VK_TRUE;
            si.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
            si.minLod        = 0.0f;
            si.maxLod        = 0.0f;
            if (vkCreateSampler(m_device, &si, nullptr, &m_shadowSampler) != VK_SUCCESS)
                throw std::runtime_error("[VulkanRenderer] Shadow sampler creation failed.");
        }

        // ---- Scene reflection probe image + sampler (set=2 binding 4 must be valid from the first draw) ----
        CreateReflectionResourcesVK();

        // ---- Planar reflection target + compatible render pass (set=2 binding 5) ----
        CreatePlanarResourcesVK();

        // ---- Live scene capture cube (set=2 "live" copy, binding 4); non-fatal ----
        CreateCaptureResourcesVK();

        // ---- set=2 descriptor sets (one per frame in flight) ----
        for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
        {
            VkDescriptorSetAllocateInfo dsai{};
            dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsai.descriptorPool     = m_descriptorPool;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts        = &m_3dFrameSetLayout;
            if (vkAllocateDescriptorSets(m_device, &dsai, &m_3dFrameSets[f]) != VK_SUCCESS)
                throw std::runtime_error("[VulkanRenderer] set=2 descriptor allocation failed.");

            VkDescriptorBufferInfo lightInfo { m_lightUBO[f],  0, sizeof(GlobalLightBuffer) };
            VkDescriptorBufferInfo shadowInfo{ m_shadowUBO[f], 0, sizeof(ShadowBufferData) };
            VkDescriptorImageInfo  dirInfo   { m_shadowSampler, m_shadowDirView,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo  localInfo { m_shadowSampler, m_shadowLocalArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo  probeInfo { m_reflSampler,   m_reflView,             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo  planarInfo{ m_planarSampler, m_planarView,           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            std::array<VkWriteDescriptorSet, 6> writes{};
            for (uint32_t b = 0; b < 6; ++b) {
                writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet          = m_3dFrameSets[f];
                writes[b].dstBinding      = b;
                writes[b].descriptorCount = 1;
                writes[b].descriptorType  = (b < 2) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                    : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
            writes[0].pBufferInfo = &lightInfo;
            writes[1].pBufferInfo = &shadowInfo;
            writes[2].pImageInfo  = &dirInfo;
            writes[3].pImageInfo  = &localInfo;
            writes[4].pImageInfo  = &probeInfo;
            writes[5].pImageInfo  = &planarInfo;
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            // Mirror-pass copy of the set: identical, except binding 5 is the default texture (the planar image
            // is a framebuffer attachment while the mirror pass is recorded, so it must not be in a bound set).
            if (vkAllocateDescriptorSets(m_device, &dsai, &m_3dFrameSetsMirror[f]) != VK_SUCCESS)
                throw std::runtime_error("[VulkanRenderer] set=2 mirror descriptor allocation failed.");
            VkDescriptorImageInfo  dummyInfo{ m_planarSampler, m_planarDummyView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorBufferInfo shadowMirrorInfo{ m_shadowUBOMirror[f], 0, sizeof(ShadowBufferData) };
            for (uint32_t b = 0; b < 6; ++b) writes[b].dstSet = m_3dFrameSetsMirror[f];
            writes[1].pBufferInfo = &shadowMirrorInfo;
            writes[5].pImageInfo  = &dummyInfo;
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            // Live copy: identical to the main set except binding 4 is the live capture cube (bound once a capture
            // cycle has completed; the sky cube set is used until then).
            if (!m_capFailed && m_capCubeView != VK_NULL_HANDLE)
            {
                if (vkAllocateDescriptorSets(m_device, &dsai, &m_3dFrameSetsLive[f]) != VK_SUCCESS)
                    throw std::runtime_error("[VulkanRenderer] set=2 live descriptor allocation failed.");
                VkDescriptorImageInfo capInfo{ m_reflSampler, m_capCubeView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                for (uint32_t b = 0; b < 6; ++b) writes[b].dstSet = m_3dFrameSetsLive[f];
                writes[1].pBufferInfo = &shadowInfo;
                writes[4].pImageInfo  = &capInfo;
                writes[5].pImageInfo  = &planarInfo;
                vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }
    }
    catch (const std::exception& e)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR,
            L"[VulkanRenderer] Lighting/shadow frame resources failed: %hs - 3D scene rendering disabled.", e.what());
        ReleaseShadowResourcesVK();
        return false;
    }

    // ---------------- Part 2: depth-only pass (optional) ----------------
    try
    {
        // Render pass: clear -> depth writes -> SHADER_READ_ONLY_OPTIMAL for the main pass.
        VkAttachmentDescription depth{};
        depth.format         = m_shadowDepthFormat;
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 0;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;                           // previous frame's fragment reads
        deps[0].dstSubpass      = 0;
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass      = 0;                                             // depth writes -> main pass sampling
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments    = &depth;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = static_cast<uint32_t>(deps.size());
        rpci.pDependencies   = deps.data();
        if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_shadowRenderPass) != VK_SUCCESS)
            throw std::runtime_error("[VulkanRenderer] Shadow render pass creation failed.");

        // Framebuffers: [dir] + one per local layer
        auto makeFB = [this](VkImageView view, uint32_t size) -> VkFramebuffer
        {
            VkFramebufferCreateInfo fci{};
            fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fci.renderPass      = m_shadowRenderPass;
            fci.attachmentCount = 1;
            fci.pAttachments    = &view;
            fci.width           = size;
            fci.height          = size;
            fci.layers          = 1;
            VkFramebuffer fb = VK_NULL_HANDLE;
            if (vkCreateFramebuffer(m_device, &fci, nullptr, &fb) != VK_SUCCESS)
                throw std::runtime_error("[VulkanRenderer] Shadow framebuffer creation failed.");
            return fb;
        };
        m_shadowDirFramebuffer = makeFB(m_shadowDirView, static_cast<uint32_t>(m_shadowDirSize));
        for (int s = 0; s < MAX_LOCAL_SHADOW_SLICES; ++s)
            m_shadowLocalFramebuffers[s] = makeFB(m_shadowLocalLayerViews[s], static_cast<uint32_t>(m_shadowLocalSize));

        // Depth-only pipeline: gl_Position = worldLightVP * vec4(pos * scale, 1).
        // worldLightVP is pushed raw (row-major); GLSL reads column-major = transpose = column-vector form.
        static const char* k_glslShadowVert = R"(
#version 450
layout(location = 0) in vec3 inPos;
layout(push_constant) uniform ShadowPC {
    mat4 worldLightVP;
    vec4 scale;
} pc;
void main() {
    gl_Position = pc.worldLightVP * vec4(inPos * pc.scale.xyz, 1.0);
})";
        VkShaderModule vertShadow = CreateShaderModuleFromGLSL(k_glslShadowVert, VK_SHADER_STAGE_VERTEX_BIT);
        if (vertShadow == VK_NULL_HANDLE)
            throw std::runtime_error("[VulkanRenderer] Shadow vertex shader compile failed.");

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset     = 0;
        pcRange.size       = sizeof(float) * 20;                                 // mat4 + vec4 = 80 bytes

        VkPipelineLayoutCreateInfo plci{};
        plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount         = 0;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pcRange;
        if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(m_device, vertShadow, nullptr);
            throw std::runtime_error("[VulkanRenderer] Shadow pipeline layout creation failed.");
        }

        // Same vertex buffer layout as the 3D pipeline (VkVertex3D); only position is read.
        VkVertexInputBindingDescription bind{};
        bind.binding   = 0;
        bind.stride    = sizeof(VkVertex3D);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attr{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex3D, x) };

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &bind;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions    = &attr;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode             = VK_POLYGON_MODE_FILL;
        rast.cullMode                = VK_CULL_MODE_NONE;                       // single-sided geometry still casts
        rast.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rast.lineWidth               = 1.0f;
        rast.depthBiasEnable         = VK_TRUE;
        rast.depthBiasConstantFactor = 1.25f;
        rast.depthBiasClamp          = 0.0f;
        rast.depthBiasSlopeFactor    = 1.75f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 0;

        std::array<VkDynamicState, 2> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
        dyn.pDynamicStates    = dynStates.data();

        VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                               VK_SHADER_STAGE_VERTEX_BIT, vertShadow, "main" };

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 1;                                             // depth only - no fragment stage
        pci.pStages             = &stage;
        pci.pVertexInputState   = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState      = &vp;
        pci.pRasterizationState = &rast;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &blend;
        pci.pDynamicState       = &dyn;
        pci.layout              = m_shadowPipelineLayout;
        pci.renderPass          = m_shadowRenderPass;
        pci.subpass             = 0;
        VkResult pr = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_shadowPipeline);
        vkDestroyShaderModule(m_device, vertShadow, nullptr);
        if (pr != VK_SUCCESS)
            throw std::runtime_error("[VulkanRenderer] Shadow pipeline creation failed.");

        m_shadowResourcesReady = true;
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Shadow resources created (dir %d, local %d x %d slices)",
            m_shadowDirSize, m_shadowLocalSize, MAX_LOCAL_SHADOW_SLICES);
    }
    catch (const std::exception& e)
    {
        // Lighting (set=2) stays valid; only the depth pass is unavailable.
        debug.logDebugMessage(LogLevel::LOG_WARNING,
            L"[VulkanRenderer] Shadow depth pass unavailable (%hs) - rendering without shadows.", e.what());
        m_shadowResourcesReady = false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Scene reflection probe resources (see "Scene Reflections" in Lights.h).  Throws on failure so the
// caller's set=2 setup unwinds the same way the shadow images do.
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateReflectionResourcesVK()
{
    m_reflSize = ReflectionProbeSizeFromConfig();
    m_reflMips = ReflectionMipCount(m_reflSize);
    m_reflProbe = ReflectionProbe{};
    m_reflProbe.requestedSize = m_reflSize;                                     // the image already exists at this size
    m_reflUploadedVersion = 0;

    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.extent        = { static_cast<uint32_t>(m_reflSize), static_cast<uint32_t>(m_reflSize), 1 };
    ci.mipLevels     = static_cast<uint32_t>(m_reflMips);
    ci.arrayLayers   = 6;
    ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &ci, nullptr, &m_reflImage) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Reflection probe image creation failed.");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, m_reflImage, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &ai, nullptr, &m_reflMemory) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Reflection probe image memory allocation failed.");
    vkBindImageMemory(m_device, m_reflImage, m_reflMemory, 0);

    VkImageViewCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                           = m_reflImage;
    vi.viewType                        = VK_IMAGE_VIEW_TYPE_CUBE;
    vi.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel   = 0;
    vi.subresourceRange.levelCount     = static_cast<uint32_t>(m_reflMips);
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount     = 6;
    if (vkCreateImageView(m_device, &vi, nullptr, &m_reflView) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Reflection probe image view creation failed.");

    VkSamplerCreateInfo si{};
    si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter     = VK_FILTER_LINEAR;
    si.minFilter     = VK_FILTER_LINEAR;
    si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod        = 0.0f;
    si.maxLod        = static_cast<float>(m_reflMips);
    if (vkCreateSampler(m_device, &si, nullptr, &m_reflSampler) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Reflection probe sampler creation failed.");

    // Per-frame staging buffers: every face x every mip, RGBA8, tightly packed.
    VkDeviceSize total = 0;
    for (int mip = 0; mip < m_reflMips; ++mip)
        total += static_cast<VkDeviceSize>(ReflectionMipSize(m_reflSize, mip)) * ReflectionMipSize(m_reflSize, mip) * 4u;
    total *= 6;
    for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
    {
        CreateBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_reflStaging[f], m_reflStagingMemory[f]);
        vkMapMemory(m_device, m_reflStagingMemory[f], 0, total, 0, &m_reflStagingMapped[f]);
    }

    // Initial layout: SHADER_READ_ONLY_OPTIMAL so the set=2 descriptor is valid before the first upload.
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = m_reflImage;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(m_reflMips), 0, 6 };
    b.srcAccessMask       = 0;
    b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    EndSingleTimeCommands(cmd);

    debug.logDebugMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Reflection probe created (%d px, %d mips)",
        m_reflSize, m_reflMips);
}

// ---------------------------------------------------------------------------------------------------------------
// Planar reflection target (see "Planar Reflections" in Lights.h).  Colour uses the swapchain format and
// depth the main depth format, 1 sample, so a render pass built from the same attachment formats is
// COMPATIBLE with m_renderPass and the main 3D pipeline can draw into it unchanged.  Throws on failure.
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreatePlanarResourcesVK()
{
    m_planarW = PlanarWidthFromConfig();
    m_planarH = PlanarHeightFromConfig();
    const VkFormat colorFmt = m_swapchainFormat;
    const VkFormat depthFmt = FindDepthFormat();

    auto makeImage = [this](uint32_t w, uint32_t h, uint32_t layers, VkFormat fmt, VkImageUsageFlags usage,
                            VkImage& img, VkDeviceMemory& mem)
    {
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.extent        = { w, h, 1 };
        ci.mipLevels     = 1;
        ci.arrayLayers   = layers;
        ci.format        = fmt;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage         = usage;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &ci, nullptr, &img) != VK_SUCCESS)
            throw std::runtime_error("[VulkanRenderer] Planar reflection image creation failed.");
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_device, img, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("[VulkanRenderer] Planar reflection image memory allocation failed.");
        vkBindImageMemory(m_device, img, mem, 0);
    };
    auto makeView = [this](VkImage img, VkFormat fmt, VkImageAspectFlags aspect, VkImageViewType type,
                           uint32_t baseLayer, uint32_t layers) -> VkImageView
    {
        VkImageViewCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                           = img;
        vi.viewType                        = type;
        vi.format                          = fmt;
        vi.subresourceRange.aspectMask     = aspect;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = baseLayer;
        vi.subresourceRange.layerCount     = layers;
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(m_device, &vi, nullptr, &view) != VK_SUCCESS)
            throw std::runtime_error("[VulkanRenderer] Planar reflection image view creation failed.");
        return view;
    };

    const uint32_t pw = static_cast<uint32_t>(m_planarW), ph = static_cast<uint32_t>(m_planarH);

    // Colour: one array layer per reflection plane.
    makeImage(pw, ph, MAX_PLANAR_PLANES, colorFmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              m_planarImage, m_planarMemory);
    m_planarView = makeView(m_planarImage, colorFmt, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, MAX_PLANAR_PLANES);
    for (int p = 0; p < MAX_PLANAR_PLANES; ++p)
        m_planarLayerViews[p] = makeView(m_planarImage, colorFmt, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, static_cast<uint32_t>(p), 1);

    // Depth: one image shared by all planes (cleared at the start of each plane's render pass).
    makeImage(pw, ph, 1, depthFmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_planarDepthImage, m_planarDepthMemory);
    m_planarDepthView = makeView(m_planarDepthImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, 0, 1);

    // Dummy 1x1 array image: the mirror-pass copy of set=2 binds this at binding 5, because the real planar
    // array has a layer that is a framebuffer attachment while the mirror passes are recorded.
    makeImage(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, m_planarDummyImage, m_planarDummyMemory);
    m_planarDummyView = makeView(m_planarDummyImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1);

    // Render pass: same attachment formats / samples as m_renderPass (=> compatible), but the colour layer
    // ends in SHADER_READ_ONLY_OPTIMAL and is made visible to fragment shaders.
    VkAttachmentDescription color{};
    color.format         = colorFmt;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format         = depthFmt;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;                               // earlier frames' fragment reads / earlier planes' writes
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;      // the shared depth image is reused by the next plane
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass    = 0;                                                  // mirror pass -> main pass fragment reads
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { color, depth };
    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments    = attachments.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies   = deps;
    if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_planarRenderPass) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Planar reflection render pass creation failed.");

    for (int p = 0; p < MAX_PLANAR_PLANES; ++p)
    {
        std::array<VkImageView, 2> fbViews = { m_planarLayerViews[p], m_planarDepthView };
        VkFramebufferCreateInfo fci{};
        fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass      = m_planarRenderPass;
        fci.attachmentCount = static_cast<uint32_t>(fbViews.size());
        fci.pAttachments    = fbViews.data();
        fci.width           = pw;
        fci.height          = ph;
        fci.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fci, nullptr, &m_planarFramebuffers[p]) != VK_SUCCESS)
            throw std::runtime_error("[VulkanRenderer] Planar reflection framebuffer creation failed.");
    }

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod       = 0.0f;
    si.maxLod       = 0.0f;
    if (vkCreateSampler(m_device, &si, nullptr, &m_planarSampler) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Planar reflection sampler creation failed.");

    // Initial layout: SHADER_READ_ONLY_OPTIMAL for every layer + the dummy, so the set=2 descriptors are valid
    // before the first mirror pass.  (Each plane's render pass then loads UNDEFINED and ends read-only again.)
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkImageMemoryBarrier b[2]{};
    for (int k = 0; k < 2; ++k)
    {
        b[k].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[k].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b[k].newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b[k].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[k].srcAccessMask       = 0;
        b[k].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    }
    b[0].image            = m_planarImage;
    b[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, MAX_PLANAR_PLANES };
    b[1].image            = m_planarDummyImage;
    b[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, b);
    EndSingleTimeCommands(cmd);

    debug.logDebugMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Planar reflection target created (%d x %d x %d planes)",
        m_planarW, m_planarH, MAX_PLANAR_PLANES);
}

// ---------------------------------------------------------------------------------------------------------------
// Live scene capture resources (see "Live scene capture" in Lights.h).  Non-throwing: on any failure
// m_capFailed stays true and the reflections keep using the sky cube.
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::CreateCaptureResourcesVK()
{
    m_capFailed = true;
    if (m_device == VK_NULL_HANDLE || m_reflSize <= 0 || m_reflMips <= 0) return;

    const VkFormat fmt = m_swapchainFormat;
    switch (fmt)
    {
        case VK_FORMAT_B8G8R8A8_UNORM: m_capBGR = true;  m_capSRGB = false; break;
        case VK_FORMAT_B8G8R8A8_SRGB:  m_capBGR = true;  m_capSRGB = true;  break;
        case VK_FORMAT_R8G8B8A8_UNORM: m_capBGR = false; m_capSRGB = false; break;
        case VK_FORMAT_R8G8B8A8_SRGB:  m_capBGR = false; m_capSRGB = true;  break;
        default:
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] Live reflection capture unavailable (swapchain format %d).", static_cast<int>(fmt));
            return;
    }
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, fmt, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                      VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((fp.optimalTilingFeatures & need) != need)
    {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] Live reflection capture unavailable (format lacks blit/attachment support).");
        return;
    }

    try
    {
        const uint32_t size = static_cast<uint32_t>(m_reflSize);
        const uint32_t mips = static_cast<uint32_t>(m_reflMips);
        const VkFormat depthFmt = FindDepthFormat();

        // --- colour cube ---
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.extent        = { size, size, 1 };
        ci.mipLevels     = mips;
        ci.arrayLayers   = 6;
        ci.format        = fmt;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &ci, nullptr, &m_capImage) != VK_SUCCESS)
            throw std::runtime_error("capture image");
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_device, m_capImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_capMemory) != VK_SUCCESS)
            throw std::runtime_error("capture memory");
        vkBindImageMemory(m_device, m_capImage, m_capMemory, 0);

        auto makeView = [this, fmt](VkImageViewType type, uint32_t baseLayer, uint32_t layers, uint32_t levels) -> VkImageView
        {
            VkImageViewCreateInfo vi{};
            vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image                           = m_capImage;
            vi.viewType                        = type;
            vi.format                          = fmt;
            vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.baseMipLevel   = 0;
            vi.subresourceRange.levelCount     = levels;
            vi.subresourceRange.baseArrayLayer = baseLayer;
            vi.subresourceRange.layerCount     = layers;
            VkImageView v = VK_NULL_HANDLE;
            if (vkCreateImageView(m_device, &vi, nullptr, &v) != VK_SUCCESS)
                throw std::runtime_error("capture view");
            return v;
        };
        m_capCubeView = makeView(VK_IMAGE_VIEW_TYPE_CUBE, 0, 6, mips);
        for (uint32_t f = 0; f < 6; ++f)
            m_capFaceViews[f] = makeView(VK_IMAGE_VIEW_TYPE_2D, f, 1, 1);

        // --- depth ---
        CreateImage(size, size, depthFmt, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_capDepthImage, m_capDepthMemory);
        m_capDepthView = CreateImageView(m_capDepthImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (m_capDepthView == VK_NULL_HANDLE)
            throw std::runtime_error("capture depth view");

        // --- render pass: LOAD the sky copy, compatible with m_renderPass (same formats / samples) ---
        VkAttachmentDescription color{};
        color.format         = fmt;
        color.samples        = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout  = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;           // left there by the sky copy
        color.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentDescription depth{};
        depth.format         = depthFmt;
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;                           // sky copy + earlier faces' depth use
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        std::array<VkAttachmentDescription, 2> attachments = { color, depth };
        VkRenderPassCreateInfo rpci{};
        rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
        rpci.pAttachments    = attachments.data();
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 2;
        rpci.pDependencies   = deps;
        if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_capRenderPass) != VK_SUCCESS)
            throw std::runtime_error("capture render pass");

        for (uint32_t f = 0; f < 6; ++f)
        {
            std::array<VkImageView, 2> fbViews = { m_capFaceViews[f], m_capDepthView };
            VkFramebufferCreateInfo fci{};
            fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fci.renderPass      = m_capRenderPass;
            fci.attachmentCount = static_cast<uint32_t>(fbViews.size());
            fci.pAttachments    = fbViews.data();
            fci.width           = size;
            fci.height          = size;
            fci.layers          = 1;
            if (vkCreateFramebuffer(m_device, &fci, nullptr, &m_capFramebuffers[f]) != VK_SUCCESS)
                throw std::runtime_error("capture framebuffer");
        }

        // --- staging (one face of converted sky pixels per frame in flight) ---
        const VkDeviceSize stagingBytes = static_cast<VkDeviceSize>(size) * size * 4u;
        for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
        {
            CreateBuffer(stagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_capStaging[f], m_capStagingMemory[f]);
            vkMapMemory(m_device, m_capStagingMemory[f], 0, stagingBytes, 0, &m_capStagingMapped[f]);
        }

        // Initial layout: SHADER_READ_ONLY_OPTIMAL for every mip / face.
        VkCommandBuffer cmd = BeginSingleTimeCommands();
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_capImage;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 6 };
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        EndSingleTimeCommands(cmd);

        m_capSkyConvVersion = 0;
        m_capFailed = false;
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Live reflection capture created (%d px)", m_reflSize);
    }
    catch (const std::exception& e)
    {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] Live reflection capture unavailable (%hs).", e.what());
        ReleaseCaptureResourcesVK();
        m_capFailed = true;
    }
}

// Caller guarantees the GPU is idle (Cleanup path).
void VulkanRenderer::ReleaseCaptureResourcesVK()
{
    g_reflectionCapture.ready = false;
    if (m_device == VK_NULL_HANDLE) return;

    for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
    {
        if (m_capStagingMapped[f])                  { vkUnmapMemory(m_device, m_capStagingMemory[f]); m_capStagingMapped[f] = nullptr; }
        if (m_capStaging[f]       != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_capStaging[f], nullptr);       m_capStaging[f]       = VK_NULL_HANDLE; }
        if (m_capStagingMemory[f] != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_capStagingMemory[f], nullptr);    m_capStagingMemory[f] = VK_NULL_HANDLE; }
    }
    for (auto& fb : m_capFramebuffers)
        if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(m_device, fb, nullptr); fb = VK_NULL_HANDLE; }
    if (m_capRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(m_device, m_capRenderPass, nullptr); m_capRenderPass = VK_NULL_HANDLE; }
    if (m_capDepthView  != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_capDepthView, nullptr);  m_capDepthView  = VK_NULL_HANDLE; }
    if (m_capDepthImage != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_capDepthImage, nullptr);     m_capDepthImage = VK_NULL_HANDLE; }
    if (m_capDepthMemory!= VK_NULL_HANDLE) { vkFreeMemory(m_device, m_capDepthMemory, nullptr);      m_capDepthMemory= VK_NULL_HANDLE; }
    for (auto& v : m_capFaceViews)
        if (v != VK_NULL_HANDLE) { vkDestroyImageView(m_device, v, nullptr); v = VK_NULL_HANDLE; }
    if (m_capCubeView   != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_capCubeView, nullptr);   m_capCubeView   = VK_NULL_HANDLE; }
    if (m_capImage      != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_capImage, nullptr);          m_capImage      = VK_NULL_HANDLE; }
    if (m_capMemory     != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_capMemory, nullptr);           m_capMemory     = VK_NULL_HANDLE; }
    m_capFailed = true;
}

// Caller guarantees the GPU is idle (Cleanup path).
void VulkanRenderer::ReleasePlanarResourcesVK()
{
    g_planarFrame.active   = false;
    g_planarFrame.hasImage = false;
    if (m_device == VK_NULL_HANDLE) return;

    for (auto& fb : m_planarFramebuffers)
        if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(m_device, fb, nullptr); fb = VK_NULL_HANDLE; }
    if (m_planarRenderPass  != VK_NULL_HANDLE) { vkDestroyRenderPass(m_device, m_planarRenderPass, nullptr);   m_planarRenderPass  = VK_NULL_HANDLE; }
    if (m_planarSampler     != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_planarSampler, nullptr);         m_planarSampler     = VK_NULL_HANDLE; }
    for (auto& v : m_planarLayerViews)
        if (v != VK_NULL_HANDLE) { vkDestroyImageView(m_device, v, nullptr); v = VK_NULL_HANDLE; }
    if (m_planarView        != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_planarView, nullptr);          m_planarView        = VK_NULL_HANDLE; }
    if (m_planarImage       != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_planarImage, nullptr);             m_planarImage       = VK_NULL_HANDLE; }
    if (m_planarMemory      != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_planarMemory, nullptr);              m_planarMemory      = VK_NULL_HANDLE; }
    if (m_planarDepthView   != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_planarDepthView, nullptr);     m_planarDepthView   = VK_NULL_HANDLE; }
    if (m_planarDepthImage  != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_planarDepthImage, nullptr);        m_planarDepthImage  = VK_NULL_HANDLE; }
    if (m_planarDepthMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_planarDepthMemory, nullptr);         m_planarDepthMemory = VK_NULL_HANDLE; }
    if (m_planarDummyView   != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_planarDummyView, nullptr);     m_planarDummyView   = VK_NULL_HANDLE; }
    if (m_planarDummyImage  != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_planarDummyImage, nullptr);        m_planarDummyImage  = VK_NULL_HANDLE; }
    if (m_planarDummyMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_planarDummyMemory, nullptr);         m_planarDummyMemory = VK_NULL_HANDLE; }
}

// Caller guarantees the GPU is idle (Cleanup path).
void VulkanRenderer::ReleaseReflectionResourcesVK()
{
    g_reflectionFrame.active = false;
    m_reflProbe = ReflectionProbe{};
    m_reflUploadedVersion = 0;
    if (m_device == VK_NULL_HANDLE) return;

    for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
    {
        if (m_reflStagingMapped[f])                  { vkUnmapMemory(m_device, m_reflStagingMemory[f]); m_reflStagingMapped[f] = nullptr; }
        if (m_reflStaging[f]       != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_reflStaging[f], nullptr);       m_reflStaging[f]       = VK_NULL_HANDLE; }
        if (m_reflStagingMemory[f] != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_reflStagingMemory[f], nullptr);    m_reflStagingMemory[f] = VK_NULL_HANDLE; }
    }
    if (m_reflSampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_reflSampler, nullptr);     m_reflSampler = VK_NULL_HANDLE; }
    if (m_reflView    != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_reflView, nullptr);      m_reflView    = VK_NULL_HANDLE; }
    if (m_reflImage   != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_reflImage, nullptr);         m_reflImage   = VK_NULL_HANDLE; }
    if (m_reflMemory  != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_reflMemory, nullptr);          m_reflMemory  = VK_NULL_HANDLE; }
}

// Caller guarantees the GPU is idle (Cleanup path).
void VulkanRenderer::ReleaseShadowResourcesVK()
{
    m_shadowResourcesReady = false;
    if (m_device == VK_NULL_HANDLE) return;

    ReleaseReflectionResourcesVK();

    if (m_shadowPipeline       != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);             m_shadowPipeline       = VK_NULL_HANDLE; }
    if (m_shadowPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, m_shadowPipelineLayout, nullptr); m_shadowPipelineLayout = VK_NULL_HANDLE; }
    if (m_shadowDirFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(m_device, m_shadowDirFramebuffer, nullptr);    m_shadowDirFramebuffer = VK_NULL_HANDLE; }
    for (auto& fb : m_shadowLocalFramebuffers)
        if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(m_device, fb, nullptr); fb = VK_NULL_HANDLE; }
    if (m_shadowRenderPass     != VK_NULL_HANDLE) { vkDestroyRenderPass(m_device, m_shadowRenderPass, nullptr);        m_shadowRenderPass     = VK_NULL_HANDLE; }

    for (auto& ds : m_3dFrameSets)
        if (ds != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE) { vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &ds); ds = VK_NULL_HANDLE; }
    for (auto& ds : m_3dFrameSetsLive)
        if (ds != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE) { vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &ds); ds = VK_NULL_HANDLE; }
    for (auto& ds : m_3dFrameSetsMirror)
        if (ds != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE) { vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &ds); ds = VK_NULL_HANDLE; }
    ReleaseCaptureResourcesVK();
    ReleasePlanarResourcesVK();

    if (m_shadowSampler        != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_shadowSampler, nullptr);               m_shadowSampler        = VK_NULL_HANDLE; }
    for (auto& v : m_shadowLocalLayerViews)
        if (v != VK_NULL_HANDLE) { vkDestroyImageView(m_device, v, nullptr); v = VK_NULL_HANDLE; }
    if (m_shadowLocalArrayView != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_shadowLocalArrayView, nullptr);     m_shadowLocalArrayView = VK_NULL_HANDLE; }
    if (m_shadowDirView        != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_shadowDirView, nullptr);            m_shadowDirView        = VK_NULL_HANDLE; }
    if (m_shadowLocalImage     != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_shadowLocalImage, nullptr);             m_shadowLocalImage     = VK_NULL_HANDLE; }
    if (m_shadowLocalMemory    != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_shadowLocalMemory, nullptr);              m_shadowLocalMemory    = VK_NULL_HANDLE; }
    if (m_shadowDirImage       != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_shadowDirImage, nullptr);               m_shadowDirImage       = VK_NULL_HANDLE; }
    if (m_shadowDirMemory      != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_shadowDirMemory, nullptr);                m_shadowDirMemory      = VK_NULL_HANDLE; }

    for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
    {
        if (m_lightUBOMapped[f])                 { vkUnmapMemory(m_device, m_lightUBOMemory[f]);  m_lightUBOMapped[f]  = nullptr; }
        if (m_lightUBO[f]       != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_lightUBO[f], nullptr);     m_lightUBO[f]       = VK_NULL_HANDLE; }
        if (m_lightUBOMemory[f] != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_lightUBOMemory[f], nullptr);  m_lightUBOMemory[f] = VK_NULL_HANDLE; }
        if (m_shadowUBOMirrorMapped[f])          { vkUnmapMemory(m_device, m_shadowUBOMirrorMemory[f]); m_shadowUBOMirrorMapped[f] = nullptr; }
        if (m_shadowUBOMirror[f]       != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_shadowUBOMirror[f], nullptr);    m_shadowUBOMirror[f]       = VK_NULL_HANDLE; }
        if (m_shadowUBOMirrorMemory[f] != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_shadowUBOMirrorMemory[f], nullptr); m_shadowUBOMirrorMemory[f] = VK_NULL_HANDLE; }
        if (m_shadowUBOMapped[f])                { vkUnmapMemory(m_device, m_shadowUBOMemory[f]); m_shadowUBOMapped[f] = nullptr; }
        if (m_shadowUBO[f]       != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_shadowUBO[f], nullptr);    m_shadowUBO[f]       = VK_NULL_HANDLE; }
        if (m_shadowUBOMemory[f] != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_shadowUBOMemory[f], nullptr); m_shadowUBOMemory[f] = VK_NULL_HANDLE; }
    }
}

bool VulkanRenderer::CheckValidationLayerSupport() const
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const char* name : k_validationLayers) {
        bool found = false;
        for (auto& l : layers) if (std::string(l.layerName) == name) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// 2D Drawing — Windows uses D2D to the WIC bitmap overlay, then uploads to Vulkan
// ---------------------------------------------------------------------------------------------------------------
// DrawRectangle — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// DrawCircle — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Native filled convex polygon through the Direct2D overlay (the same surface DrawRectangle uses).
// DrawFilledPolygon — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Batched fill (see DX11Renderer::DrawFilledPolygonBatch).
// DrawFilledPolygonBatch — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// DrawCurve — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// PushClipRect — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// PopClipRect — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// D2D helpers (DrawRectangleD2D/DrawTextD2D/GetOrCreateTextFormat/InvalidateTextFormatCache) — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

void VulkanRenderer::UploadOverlayToVulkan(VkCommandBuffer cmd)
{
    if (!m_overlayDirty || !m_overlayTexture.isValid) return;
    if (m_overlayTexture.image == VK_NULL_HANDLE) return;  // guard: image must be valid

    CopyPlatformOverlayToStaging();

    // Transition overlay to TRANSFER_DST
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image               = m_overlayTexture.image;
    toTransfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toTransfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { m_overlayWidth, m_overlayHeight, 1 };
    vkCmdCopyBufferToImage(cmd, m_overlayStagingBuffer, m_overlayTexture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition back to SHADER_READ
    VkImageMemoryBarrier toShader = toTransfer;
    toShader.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toShader);

    m_overlayDirty = false;
}

void VulkanRenderer::UploadBgOverlayToVulkan(VkCommandBuffer cmd)
{
    if (!m_bgOverlayDirty || !m_bgOverlayTexture.isValid) return;

    CopyPlatformBgOverlayToStaging();

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image               = m_bgOverlayTexture.image;
    toTransfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toTransfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { m_overlayWidth, m_overlayHeight, 1 };
    vkCmdCopyBufferToImage(cmd, m_bgStagingBuffer, m_bgOverlayTexture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader = toTransfer;
    toShader.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toShader);

    m_bgOverlayDirty = false;
}

// ---------------------------------------------------------------------------------------------------------------
// DrawMyText overloads
// ---------------------------------------------------------------------------------------------------------------
// DrawMyText-1 — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// DrawMyText-2 — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// DrawMyTextCentered — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// DrawMyTextWithFont — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// DrawMyTextStyled — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// ---------------------------------------------------------------------------------------------------------------
// GetCharacterWidth / CalculateText helpers
// ---------------------------------------------------------------------------------------------------------------
// Vulkan text measurement uses a proportional character-width table rather than
// DWrite text layout creation, which is a DX11-specific measurement path.
// This is crash-safe, platform-agnostic, and accurate enough for GUI layout.
// ---------------------------------------------------------------------------------------------------------------

// Returns the estimated pixel width of a single character at the given font size.
static float VKCharWidth(wchar_t c, float fs)
{
    // Narrow
    if (c == L' ')                                   return fs * 0.28f;
    if (c == L'\t')                                  return fs * 1.12f; // 4 spaces approx
    if (c == L'\n' || c == L'\r')                    return 0.0f;
    if (wcschr(L"il1jI|!.,;:'\"", c))              return fs * 0.30f;
    // Medium-narrow
    if (wcschr(L"frtJ()", c))                        return fs * 0.42f;
    // Wide
    if (wcschr(L"mwMW", c))                          return fs * 0.75f;
    // Uppercase broad
    if (c >= L'A' && c <= L'Z')                      return fs * 0.62f;
    // Digits
    if (c >= L'0' && c <= L'9')                      return fs * 0.58f;
    // Default proportional average
    return fs * 0.55f;
}

float VulkanRenderer::GetCharacterWidth(wchar_t character, float FontSize)
{
    return VKCharWidth(character, FontSize);
}

float VulkanRenderer::GetCharacterWidth(wchar_t character, float FontSize, const std::wstring& /*fontName*/)
{
    return VKCharWidth(character, FontSize);
}

float VulkanRenderer::GetCharacterWidth(wchar_t character, float FontSize, bool bold)
{
    float w = VKCharWidth(character, FontSize);
    return bold ? w * 1.1f : w;
}

float VulkanRenderer::CalculateTextWidth(const std::wstring& text, float FontSize, float /*containerWidth*/)
{
    if (text.empty()) return 0.0f;
    // Return the width of the longest line (handles multi-line strings).
    float maxWidth = 0.0f;
    float lineWidth = 0.0f;
    for (wchar_t c : text) {
        if (c == L'\n' || c == L'\r') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0.0f;
        } else {
            lineWidth += VKCharWidth(c, FontSize);
        }
    }
    return std::max(maxWidth, lineWidth);
}

float VulkanRenderer::CalculateTextHeight(const std::wstring& text, float FontSize, float /*containerHeight*/)
{
    if (text.empty()) return FontSize;
    // Count lines; each line is FontSize pixels tall with 20% line-gap.
    const float lineHeight = FontSize * 1.20f;
    int lines = 1;
    for (wchar_t c : text) {
        if (c == L'\n') ++lines;
    }
    return static_cast<float>(lines) * lineHeight;
}

// ---------------------------------------------------------------------------------------------------------------
// DrawTexture
// ---------------------------------------------------------------------------------------------------------------
// DrawTexture — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// ---------------------------------------------------------------------------------------------------------------
// 2D Blit operations (Windows D2D path)
// ---------------------------------------------------------------------------------------------------------------
// Blit2DObject — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Blit2DObjectToSize — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Blit2DObjectToSizeWithAlpha — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Blit2DObjectAtOffset — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

void VulkanRenderer::Blit2DWrappedObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY,
                                                   int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY)
{
    // Wrapping atlas blit: same as offset blit for single-tile use-cases in Vulkan.
    // Platform-independent: Blit2DObjectAtOffset is Direct2D on Windows and a
    // no-op on Linux/Android (native Vulkan 2D blits are Phase C).
    Blit2DObjectAtOffset(iIndex, iBlitX, iBlitY, iXOffset, iYOffset, iTileSizeX, iTileSizeY);
    (void)iIndex; (void)iBlitX; (void)iBlitY; (void)iXOffset; (void)iYOffset; (void)iTileSizeX; (void)iTileSizeY;
}

// Blits one tile out of a tileset atlas image, selected by tileIndex (0-based, row-major).
// Tiles-per-row is derived from the atlas bitmap width / iTileSizeX, then delegates to the
// existing Blit2DObjectAtOffset (which already blits an explicit source sub-rect).
// Blit2DAtlasTile — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Stretches the image to iWidth x iHeight while shifting its sampled content HORIZONTALLY by
// scrollFraction * bitmap-width pixels, wrapping around the bitmap width, so the image's own
// colour banding appears to travel sideways across the fixed destination rect. Drawn as two
// DrawBitmap calls. reverseDirection=false travels left->right, true travels right->left.
// Blit2DScrollingObjectToSize — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Blit2DCenteredZoom — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// Blit2DColoredPixel — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

bool VulkanRenderer::Place2DBlitObjectToQueue(BlitObj2DIndexType iIndex, BlitPhaseLevel BlitPhaseLvl,
                                               BlitObj2DType objType, BlitObj2DDetails objDetails,
                                               CanBlitType BlitType)
{
    int idx = static_cast<int>(iIndex);
    if (BlitType == CanBlitType::CAN_BLIT_SINGLE && m_blitActiveIDs.count(idx)) return false;
    for (int i = 0; i < MAX_2D_IMG_QUEUE_OBJS; ++i) {
        if (!My2DBlitQueue[i].bInUse) {
            My2DBlitQueue[i].bInUse        = true;
            My2DBlitQueue[i].BlitType      = BlitType;
            My2DBlitQueue[i].BlitPhase     = BlitPhaseLvl;
            My2DBlitQueue[i].BlitObjType   = objType;
            My2DBlitQueue[i].BlitObjDetails = objDetails;
            m_blitActiveIDs.insert(idx);
            return true;
        }
    }
    return false;
}

void VulkanRenderer::Clear2DBlitQueue()
{
    std::memset(My2DBlitQueue, 0, sizeof(My2DBlitQueue));
    m_blitActiveIDs.clear();
}

// ---------------------------------------------------------------------------------------------------------------
// RenderBackgroundImage — draws the scene background onto the D2D overlay.
// Called from within VULKAN_RenderFrame's BeginDraw/EndDraw block; does NOT call
// BeginDraw/EndDraw itself (unlike DX11 which has its own D2D session per call).
// ---------------------------------------------------------------------------------------------------------------
// RenderBackgroundImage — implemented per-platform in the windows/linux/android renderer files (platform split, 24 Nov 2026).

// ---------------------------------------------------------------------------------------------------------------
// Validation layer check
// ---------------------------------------------------------------------------------------------------------------

#endif // __USE_VULKAN__
