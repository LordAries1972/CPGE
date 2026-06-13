// VULKAN_FXManager.cpp  —  Visual Effects Manager implementation (Vulkan)
#include "Includes.h"
#include "VULKAN_FXManager.h"

#if defined(__USE_VULKAN__)

#include "VULKAN_Renderer.h"
#include "Debug.h"
#include "MathPrecalculation.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"

#if __has_include(<shaderc/shaderc.hpp>)
    #include <shaderc/shaderc.hpp>
    #define HAS_SHADERC 1
#else
    #define HAS_SHADERC 0
#endif

extern Debug         debug;
extern ThreadManager threadManager;
extern std::shared_ptr<Renderer> renderer;

#pragma warning(push)
#pragma warning(disable: 4101 4267)

// ---------------------------------------------------------------------------------------------------------------
// Fade GLSL shaders (fullscreen triangle, no vertex buffer)
// ---------------------------------------------------------------------------------------------------------------
static const char* k_fadeVertGLSL = R"(
#version 450
void main() {
    vec2 pos[3] = vec2[](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));
    gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
}
)";

static const char* k_fadeFragGLSL = R"(
#version 450
layout(push_constant) uniform PC { vec4 color; } pc;
layout(location = 0) out vec4 outColor;
void main() { outColor = pc.color; }
)";

// ---------------------------------------------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------------------------------------------
VKFXManager::VKFXManager()
    : bHasCleanedUp(false),
      m_fadePipeline(VK_NULL_HANDLE),
      m_fadePipelineLayout(VK_NULL_HANDLE),
      m_isRendering(false)
{
}

VKFXManager::~VKFXManager() {
    CleanUp();
}

// ---------------------------------------------------------------------------------------------------------------
// CleanUp
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::CleanUp() {
    if (bHasCleanedUp) return;

    m_isRendering.store(false);

    DestroyFadePipeline();

    effects.clear();
    m_pendingCallbacks.clear();

    bHasCleanedUp = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::Initialize() {
    if (bHasCleanedUp) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Cannot initialize - already cleaned up");
        return;
    }
    if (!renderer || !renderer->bIsInitialized.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VKFXManager] Renderer not ready - deferring initialization");
        return;
    }
    m_weakRenderer = renderer;
    try {
        if (!CreateFadePipeline()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VKFXManager] Fade pipeline unavailable (shaderc missing?)");
        }
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] Initialized");
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[VKFXManager] Exception during Initialize: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// AddEffect
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::AddEffect(const VKFXItem& fxItem) {
    // Guard with m_effectsMutex so push_back (which may reallocate and copy-construct
    // every existing element) cannot race with CreateStarfield, StopStarfield, or other
    // AddEffect calls on different threads.  Without this lock, a concurrent reallocation
    // can crash in VKFXItem's copy constructor when an element's vector members are in a
    // partially-moved state.
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    VKFXItem newEffect = fxItem;
    newEffect.startTime  = std::chrono::steady_clock::now();
    newEffect.lastUpdate = newEffect.startTime;
    effects.push_back(newEffect);
}

// ---------------------------------------------------------------------------------------------------------------
// IsFadeActive
// ---------------------------------------------------------------------------------------------------------------
bool VKFXManager::IsFadeActive() const {
    for (const auto& fx : effects)
        if (fx.type == FXType::ColorFader && fx.progress < 1.0f)
            return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// StopAllFXForResize / RestartFXAfterResize
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::StopAllFXForResize() {
    ThreadLockHelper lock(threadManager, "vkfxmanager_stop_resize_lock", 5000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Failed to acquire lock for StopAllFXForResize");
        return;
    }
    try {
        m_savedFXState = VKActiveFXState{};

        if (starfieldID > 0) {
            m_savedFXState.starfieldActive = true;
            m_savedFXState.starfieldID     = starfieldID;
            StopStarfield();
        }

        if (tunnelID > 0) {
            m_savedFXState.tunnelActive = true;
            m_savedFXState.tunnelID     = tunnelID;
            StopWarpDotTunnel();
        }

        std::vector<int> activeTextIDs;
        for (const auto& fx : effects)
            if (fx.type == FXType::TextScroller)
                activeTextIDs.push_back(fx.fxID);

        for (int id : activeTextIDs) {
            StopTextScroller(id);
            m_savedFXState.textScrollerIDs.push_back(id);
        }
        if (!m_savedFXState.textScrollerIDs.empty())
            m_savedFXState.textScrollerActive = true;

        for (const auto& fx : effects)
            if (fx.type == FXType::ColorFader && fx.progress < 1.0f)
                { m_savedFXState.fadeEffectActive = true; break; }

        std::vector<VKFXItem> tmp; tmp.swap(effects);
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Exception in StopAllFXForResize: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
}

void VKFXManager::RestartFXAfterResize() {
    try {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Effects are restarted by the loader thread (see IOLoaderThread.cpp — Vulkan block)
#if defined(PLATFORM_WINDOWS)
        SecureZeroMemory(&m_savedFXState, sizeof(VKActiveFXState));
#else
        m_savedFXState = VKActiveFXState{};
#endif
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Exception in RestartFXAfterResize: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// CreateFadePipeline / DestroyFadePipeline
// ---------------------------------------------------------------------------------------------------------------
bool VKFXManager::CreateFadePipeline() {
#if !HAS_SHADERC
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VKFXManager] shaderc not available - fade pipeline skipped");
    return false;
#else
    auto* vkr = static_cast<VulkanRenderer*>(renderer.get());
    if (!vkr) return false;

    VkDevice device = vkr->GetVkDevice();
    if (device == VK_NULL_HANDLE) return false;

    auto compileGLSL = [&](const char* src, shaderc_shader_kind kind) -> std::vector<uint32_t> {
        shaderc::Compiler       compiler;
        shaderc::CompileOptions opts;
        opts.SetOptimizationLevel(shaderc_optimization_level_performance);
        auto result = compiler.CompileGlslToSpv(src, strlen(src), kind, "fade", "main", opts);
        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            std::string err = result.GetErrorMessage();
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Shader compile error: " +
                std::wstring(err.begin(), err.end()));
            return {};
        }
        return { result.cbegin(), result.cend() };
    };

    auto vertSpv = compileGLSL(k_fadeVertGLSL, shaderc_glsl_vertex_shader);
    auto fragSpv = compileGLSL(k_fadeFragGLSL, shaderc_glsl_fragment_shader);
    if (vertSpv.empty() || fragSpv.empty()) return false;

    auto makeModule = [&](const std::vector<uint32_t>& spv) -> VkShaderModule {
        VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = spv.size() * sizeof(uint32_t);
        ci.pCode    = spv.data();
        VkShaderModule mod = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &ci, nullptr, &mod);
        return mod;
    };

    VkShaderModule vertMod = makeModule(vertSpv);
    VkShaderModule fragMod = makeModule(fragSpv);
    if (!vertMod || !fragMod) {
        if (vertMod) vkDestroyShaderModule(device, vertMod, nullptr);
        if (fragMod) vkDestroyShaderModule(device, fragMod, nullptr);
        return false;
    }

    // Push constant: vec4 color (16 bytes) in fragment stage
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = 16;

    VkPipelineLayoutCreateInfo layoutCI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(device, &layoutCI, nullptr, &m_fadePipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertMod, nullptr);
        vkDestroyShaderModule(device, fragMod, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo   viCI{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo iaCI{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    iaCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo      vpCI{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vpCI.viewportCount = 1;
    vpCI.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rsCI{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rsCI.polygonMode = VK_POLYGON_MODE_FILL;
    rsCI.cullMode    = VK_CULL_MODE_NONE;
    rsCI.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsCI.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo   msCI{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    msCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo  dsCI{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    dsCI.depthTestEnable  = VK_FALSE;
    dsCI.depthWriteEnable = VK_FALSE;

    // Alpha blend for fade
    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.blendEnable         = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAttach.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbCI{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cbCI.attachmentCount = 1;
    cbCI.pAttachments    = &blendAttach;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynCI{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynCI.dynamicStateCount = 2;
    dynCI.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo pipeCI{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipeCI.stageCount          = 2;
    pipeCI.pStages             = stages;
    pipeCI.pVertexInputState   = &viCI;
    pipeCI.pInputAssemblyState = &iaCI;
    pipeCI.pViewportState      = &vpCI;
    pipeCI.pRasterizationState = &rsCI;
    pipeCI.pMultisampleState   = &msCI;
    pipeCI.pDepthStencilState  = &dsCI;
    pipeCI.pColorBlendState    = &cbCI;
    pipeCI.pDynamicState       = &dynCI;
    pipeCI.layout              = m_fadePipelineLayout;
    pipeCI.renderPass          = vkr->GetRenderPass();
    pipeCI.subpass             = 0;

    bool ok = (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &m_fadePipeline) == VK_SUCCESS);

    vkDestroyShaderModule(device, vertMod, nullptr);
    vkDestroyShaderModule(device, fragMod, nullptr);

    if (ok) debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] Fade pipeline created");
    return ok;
#endif
}

void VKFXManager::DestroyFadePipeline() {
    auto locked = m_weakRenderer.lock();
    auto* vkr   = locked ? static_cast<VulkanRenderer*>(locked.get()) : nullptr;
    VkDevice device = vkr ? vkr->GetVkDevice() : VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE) return;

    // The fade pipeline may be referenced by a command buffer that has been submitted
    // but not yet executed on the GPU.  Wait for all GPU work to complete before
    // destroying it — this is always called from a cleanup/transition path, never
    // from the hot render loop, so the stall is acceptable.
    vkDeviceWaitIdle(device);

    if (m_fadePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_fadePipeline, nullptr);
        m_fadePipeline = VK_NULL_HANDLE;
    }
    if (m_fadePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_fadePipelineLayout, nullptr);
        m_fadePipelineLayout = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// RenderFadeFullScreenQuad
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::RenderFadeFullScreenQuad(VkCommandBuffer cmd, const XMFLOAT4& color) {
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) return;
    if (cmd == VK_NULL_HANDLE || m_fadePipeline == VK_NULL_HANDLE) return;

    if (std::isnan(color.x) || std::isnan(color.y) || std::isnan(color.z) || std::isnan(color.w) ||
        std::isinf(color.x) || std::isinf(color.y) || std::isinf(color.z) || std::isinf(color.w)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Invalid color in RenderFadeFullScreenQuad");
        return;
    }

    float pc[4] = {
        std::max(0.0f, std::min(1.0f, color.x)),
        std::max(0.0f, std::min(1.0f, color.y)),
        std::max(0.0f, std::min(1.0f, color.z)),
        std::max(0.0f, std::min(1.0f, color.w))
    };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fadePipeline);
    vkCmdPushConstants(cmd, m_fadePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);

    // The pipeline uses VK_DYNAMIC_STATE_VIEWPORT / VK_DYNAMIC_STATE_SCISSOR, so
    // these MUST be set before the draw — there is no implicit state to inherit.
    VkViewport vp{};
    vp.width    = static_cast<float>(renderer->iOrigWidth);
    vp.height   = static_cast<float>(renderer->iOrigHeight);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.extent.width  = static_cast<uint32_t>(renderer->iOrigWidth);
    scissor.extent.height = static_cast<uint32_t>(renderer->iOrigHeight);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// ApplyColorFader
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::ApplyColorFader(VKFXItem& fxItem) {
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) return;
    if (fxItem.duration <= 0.0f) { fxItem.progress = 1.0f; return; }

    // Clamp color
    fxItem.targetColor.x = std::max(0.0f, std::min(1.0f, fxItem.targetColor.x));
    fxItem.targetColor.y = std::max(0.0f, std::min(1.0f, fxItem.targetColor.y));
    fxItem.targetColor.z = std::max(0.0f, std::min(1.0f, fxItem.targetColor.z));
    fxItem.targetColor.w = std::max(0.0f, std::min(1.0f, fxItem.targetColor.w));

    auto now = std::chrono::steady_clock::now();
    if (fxItem.lastUpdate.time_since_epoch().count() == 0) fxItem.lastUpdate = fxItem.startTime;

    float totalElapsed = std::chrono::duration<float>(now - fxItem.startTime).count();
    if (totalElapsed < 0.0f) { fxItem.startTime = now; fxItem.lastUpdate = now; fxItem.progress = 0.0f; return; }

    fxItem.lastUpdate = now;
    fxItem.progress   = (totalElapsed >= fxItem.duration)
                        ? 1.0f
                        : std::max(0.0f, std::min(1.0f, totalElapsed / fxItem.duration));

    float effectiveProgress = (fxItem.subtype == FXSubType::FadeToBackground)
                              ? 1.0f - fxItem.progress
                              : fxItem.progress;

    XMFLOAT4 fadeColor = fxItem.targetColor;
    fadeColor.w = std::max(0.0f, std::min(1.0f, effectiveProgress));

    if (!renderer) return;
    try {
        auto* vkr = static_cast<VulkanRenderer*>(renderer.get());
        VkCommandBuffer cmd = vkr->GetCurrentCommandBuffer();
        RenderFadeFullScreenQuad(cmd, fadeColor);
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Exception in ApplyColorFader: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
        fxItem.progress = 1.0f;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// RemoveCompletedEffects
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::RemoveCompletedEffects() {
    ThreadLockHelper lock(threadManager, "vkfxmanager_remove_effects_lock", 1000);
    if (!lock.IsLocked()) return;
    if (effects.empty()) return;

    auto now = std::chrono::steady_clock::now();
    std::vector<size_t> toRemove;
    toRemove.reserve(effects.size());

    for (size_t i = 0; i < effects.size(); ++i) {
        const VKFXItem& fx = effects[i];
        bool timedOut         = std::chrono::duration<float>(now - fx.startTime).count() >= fx.timeout;
        bool progressComplete = fx.progress >= 1.0f;

        if (fx.type == FXType::TextFadeInOut) {
            if (progressComplete) toRemove.push_back(i);
            continue;
        }

        if (fx.type == FXType::TextScroller && fx.subtype == FXSubType::TXT_SCROLL_CONSISTANT) {
            if (fx.duration != FLT_MAX && timedOut) toRemove.push_back(i);
        }
        else if (timedOut || progressComplete) {
            toRemove.push_back(i);
        }
    }

    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        if (*it < effects.size()) effects.erase(effects.begin() + *it);
}

// ---------------------------------------------------------------------------------------------------------------
// Render  (3D / fullscreen effects: ColorFader, Starfield)
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::Render() {
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) return;
    if (!renderer) return;
    if (effects.empty() && m_pendingCallbacks.empty()) return;
    if (m_isRendering.load()) return;

    m_isRendering.store(true);
    try {
        static auto lastRenderTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::min(std::chrono::duration<float>(now - lastRenderTime).count(), 0.1f);
        lastRenderTime  = now;

        // Update per-frame animated effects
        for (auto& fx : effects) {
            if (fx.type == FXType::Starfield)
                UpdateStarfield(deltaTime);
            else if (fx.type == FXType::WarpDotTunnel)
                UpdateWarpDotTunnel(fx, deltaTime);
        }

        for (auto& fx : effects) {
            if (threadManager.threadVars.bIsShuttingDown.load()) break;
            if (fx.type == FXType::ColorFader) ApplyColorFader(fx);
        }

        // Process pending callbacks
        if (!m_pendingCallbacks.empty()) {
            ThreadLockHelper cbLock(threadManager, "vkfxmanager_callback_lock", 500);
            if (cbLock.IsLocked()) {
                auto curTime = std::chrono::steady_clock::now();
                std::vector<size_t> toExecute, toRemove;
                toExecute.reserve(m_pendingCallbacks.size());
                toRemove.reserve(m_pendingCallbacks.size());

                for (size_t i = 0; i < m_pendingCallbacks.size(); ++i) {
                    VKCallbackEntry& entry = m_pendingCallbacks[i];
                    float age = std::chrono::duration<float>(curTime - entry.creationTime).count();
                    if (age > 30.0f || entry.isExecuted) { toRemove.push_back(i); continue; }

                    for (const auto& fx : effects)
                        if (fx.fxID == entry.fxID && fx.progress >= 1.0f) { toExecute.push_back(i); break; }
                }

                for (size_t idx : toExecute) {
                    if (idx >= m_pendingCallbacks.size()) continue;
                    VKCallbackEntry& entry = m_pendingCallbacks[idx];
                    if (!entry.isExecuted && entry.callback) {
                        try {
                            entry.callback();
                            entry.isExecuted = true;
                            toRemove.push_back(idx);
                        }
                        catch (...) {
                            entry.isExecuted = true;
                            toRemove.push_back(idx);
                        }
                    }
                }

                std::sort(toRemove.begin(), toRemove.end(), std::greater<size_t>());
                toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());
                for (size_t idx : toRemove)
                    if (idx < m_pendingCallbacks.size())
                        m_pendingCallbacks.erase(m_pendingCallbacks.begin() + idx);
            }
        }

        RemoveCompletedEffects();
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Exception in Render(): " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
    m_isRendering.store(false);
}

// ---------------------------------------------------------------------------------------------------------------
// Render2D  (2D overlay effects: Scroller, Particles, TextScroller)
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::Render2D() {
    if (bHasCleanedUp) return;

    static auto lastTweenTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastTweenTime).count();

    UpdateTweens(deltaTime);

    for (auto& fx : effects) {
        if (fx.type == FXType::Scroller)         ApplyScroller(fx);
        if (fx.type == FXType::ParticleExplosion) RenderParticles(fx);
        if (fx.type == FXType::TextScroller) {
            UpdateTextScroller(fx, deltaTime);
            RenderTextScroller(fx);
        }
    }

    lastTweenTime = now;
}

// ---------------------------------------------------------------------------------------------------------------
// RenderFX  (per-effect, called with explicit command buffer and world matrix)
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::RenderFX(int effectID, VkCommandBuffer cmd, const glm::mat4& viewMatrix) {
    if (!cmd || effectID < 0) return;

    for (VKFXItem& fx : effects) {
        if (fx.fxID != effectID) continue;

        auto fxNow = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(fxNow - fx.startTime).count();
        static auto lastTweenTime = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(fxNow - lastTweenTime).count();
        fx.progress  = fx.duration > 0.0f ? std::clamp(elapsed / fx.duration, 0.0f, 1.0f) : 1.0f;
        fx.lastUpdate = fxNow;

        switch (fx.type) {
        case FXType::ColorFader:   ApplyColorFader(fx);                              break;
        case FXType::Starfield:    UpdateStarfield(dt); RenderStarfield(fx, cmd, viewMatrix); break;
        case FXType::WarpDotTunnel: RenderWarpDotTunnel(fx, cmd);                   break;
        default: break;
        }

        if (fx.progress >= 1.0f) {
            if (fx.restartOnExpire) {
                fx.startTime  = std::chrono::steady_clock::now();
                fx.progress   = 0.0f;
                fx.lastUpdate = fx.startTime;
            }
            else if (fx.nextEffectID >= 0) {
                VKFXItem chain; chain.fxID = fx.nextEffectID;
                AddEffect(chain);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Fader helpers
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::FadeToColor(XMFLOAT4 color, float duration, float delay) {
    VKFXItem fx;
    fx.type        = FXType::ColorFader;
    fx.subtype     = FXSubType::FadeToTargetColor;
    fx.duration    = duration;
    fx.delay       = delay;
    fx.timeout     = duration + 1.0f;
    fx.progress    = 0.0f;
    fx.targetColor = color;
    AddEffect(fx);
}

void VKFXManager::FadeToBlack(float duration, float delay) {
    XMFLOAT4 c{ 0,0,0,1 }; FadeToColor(c, duration, delay);
}

void VKFXManager::FadeToWhite(float duration, float delay) {
    XMFLOAT4 c{ 1,1,1,1 }; FadeToColor(c, duration, delay);
}

void VKFXManager::FadeToImage(float duration, float delay) {
    VKFXItem fx;
    fx.type        = FXType::ColorFader;
    fx.subtype     = FXSubType::FadeToBackground;
    fx.duration    = duration;
    fx.delay       = delay;
    fx.timeout     = duration + 1.0f;
    fx.progress    = 0.0f;
    XMFLOAT4 black{ 0,0,0,1 };
    fx.targetColor = black;
    AddEffect(fx);
}

void VKFXManager::FadeOutThenCallback(XMFLOAT4 color, float duration, float delay, std::function<void()> callback) {
    if (!callback || duration <= 0.0f || delay < 0.0f) return;

    ThreadLockHelper lock(threadManager, "vkfxmanager_callback_lock", 1000);
    if (!lock.IsLocked()) return;

    try {
        VKFXItem fx;
        fx.type        = FXType::ColorFader;
        fx.subtype     = FXSubType::FadeToTargetColor;
        fx.fxID        = static_cast<int>(effects.size()) + 1000;
        fx.duration    = duration;
        fx.delay       = delay;
        fx.timeout     = duration + delay + 2.0f;
        fx.progress    = 0.0f;
        fx.targetColor = color;
        fx.startTime   = std::chrono::steady_clock::now();
        fx.lastUpdate  = fx.startTime;

        for (const auto& e : effects)
            if (e.fxID == fx.fxID) { fx.fxID = static_cast<int>(effects.size()) + static_cast<int>(m_pendingCallbacks.size()) + 2000; break; }

        AddEffect(fx);
        m_pendingCallbacks.emplace_back(fx.fxID, std::move(callback));
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VKFXManager] Exception in FadeOutThenCallback: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
}

void VKFXManager::FadeOutInSequence(XMFLOAT4 fadeOutColor, XMFLOAT4 fadeInColor, float duration, float delay,
                                    std::function<void()> midpointCallback) {
    FadeOutThenCallback(fadeOutColor, duration, delay, [=]() {
        if (midpointCallback) midpointCallback();
        FadeToColor(fadeInColor, duration, delay);
    });
}

// ---------------------------------------------------------------------------------------------------------------
// Scroller helpers
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::CancelEffect(int effectID) {
    effects.erase(std::remove_if(effects.begin(), effects.end(),
        [effectID](const VKFXItem& fx) { return fx.fxID == effectID; }), effects.end());
}

void VKFXManager::RestartEffect(int effectID) {
    for (auto& fx : effects)
        if (fx.fxID == effectID) { fx.startTime = std::chrono::steady_clock::now(); fx.progress = 0.0f; break; }
}

void VKFXManager::ChainEffect(int fromEffectID, int toEffectID) {
    for (auto& fx : effects)
        if (fx.fxID == fromEffectID) { fx.nextEffectID = toEffectID; break; }
}

void VKFXManager::StartScrollEffect(BlitObj2DIndexType textureIndex, FXSubType direction,
                                    int speed, int tileWidth, int tileHeight, float delay) {
    VKFXItem fx;
    fx.type         = FXType::Scroller;
    fx.subtype      = direction;
    fx.scrollSpeed  = speed;
    fx.textureIndex = textureIndex;
    fx.tileWidth    = tileWidth;
    fx.tileHeight   = tileHeight;
    fx.delay        = delay;
    fx.progress     = 0.0f;
    fx.timeout      = FLT_MAX;
    fx.startTime    = std::chrono::steady_clock::now();
    fx.lastUpdate   = fx.startTime;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] Scroll effect started");
    AddEffect(fx);
}

void VKFXManager::StopScrollEffect(BlitObj2DIndexType textureIndex) {
    effects.erase(std::remove_if(effects.begin(), effects.end(), [=](const VKFXItem& fx) {
        return fx.type == FXType::Scroller && fx.textureIndex == textureIndex;
    }), effects.end());
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] Scroll effect stopped");
}

void VKFXManager::UpdateScrollSpeed(BlitObj2DIndexType textureIndex, int newSpeed) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) fx.scrollSpeed = newSpeed;
}

void VKFXManager::PauseScroll(BlitObj2DIndexType textureIndex) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) fx.isPaused = true;
}

void VKFXManager::ResumeScroll(BlitObj2DIndexType textureIndex) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) {
            fx.isPaused  = false;
            fx.lastUpdate = std::chrono::steady_clock::now();
        }
}

void VKFXManager::SetScrollDirection(BlitObj2DIndexType textureIndex, FXSubType newDirection) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) fx.subtype = newDirection;
}

void VKFXManager::FadeScrollSpeed(BlitObj2DIndexType textureIndex, int fromSpeed, int toSpeed, float duration) {
    UpdateScrollSpeed(textureIndex, fromSpeed);
    VKScrollTween t{ textureIndex, fromSpeed, toSpeed, duration };
    m_activeTweens.push_back(t);
}

void VKFXManager::UpdateTweens(float deltaTime) {
    for (auto& t : m_activeTweens) {
        if (!t.active) continue;
        t.elapsed += deltaTime;
        float pct      = std::min(t.elapsed / t.duration, 1.0f);
        int   newSpeed = static_cast<int>(t.from + (t.to - t.from) * pct);
        UpdateScrollSpeed(t.textureIndex, newSpeed);
        if (pct >= 1.0f) t.active = false;
    }
    m_activeTweens.erase(std::remove_if(m_activeTweens.begin(), m_activeTweens.end(),
        [](const VKScrollTween& t) { return !t.active; }), m_activeTweens.end());
}

void VKFXManager::StartParallaxLayer(BlitObj2DIndexType textureIndex, FXSubType direction,
                                     int baseSpeed, float depthMultiplier,
                                     int tileWidth, int tileHeight, float delay, bool cameraLinked) {
    VKFXItem fx;
    fx.type            = FXType::Scroller;
    fx.subtype         = direction;
    fx.scrollSpeed     = baseSpeed;
    fx.textureIndex    = textureIndex;
    fx.tileWidth       = tileWidth;
    fx.tileHeight      = tileHeight;
    fx.delay           = delay;
    fx.progress        = 0.0f;
    fx.timeout         = FLT_MAX;
    fx.depthMultiplier = depthMultiplier;
    fx.cameraLinked    = cameraLinked;
    fx.startTime       = std::chrono::steady_clock::now();
    fx.lastUpdate      = fx.startTime;
    AddEffect(fx);
}

void VKFXManager::ApplyScroller(VKFXItem& fxItem) {
    auto now     = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - fxItem.lastUpdate).count();

    renderer->Blit2DWrappedObjectAtOffset(
        fxItem.textureIndex, 0, 0,
        fxItem.currentXOffset, fxItem.currentYOffset,
        fxItem.tileWidth, fxItem.tileHeight);

    if (fxItem.isPaused) return;

    if (elapsed >= fxItem.delay) {
        fxItem.lastUpdate = now;
        int speed = static_cast<int>(fxItem.scrollSpeed * fxItem.depthMultiplier);

        switch (fxItem.subtype) {
        case FXSubType::ScrollRight:         fxItem.currentXOffset += speed;  break;
        case FXSubType::ScrollLeft:          fxItem.currentXOffset -= speed;  break;
        case FXSubType::ScrollUp:            fxItem.currentYOffset -= speed;  break;
        case FXSubType::ScrollDown:          fxItem.currentYOffset += speed;  break;
        case FXSubType::ScrollUpAndLeft:     fxItem.currentXOffset -= speed; fxItem.currentYOffset -= speed; break;
        case FXSubType::ScrollUpAndRight:    fxItem.currentXOffset += speed; fxItem.currentYOffset -= speed; break;
        case FXSubType::ScrollDownAndLeft:   fxItem.currentXOffset -= speed; fxItem.currentYOffset += speed; break;
        case FXSubType::ScrollDownAndRight:  fxItem.currentXOffset += speed; fxItem.currentYOffset += speed; break;
        default: break;
        }

        fxItem.currentXOffset = ((fxItem.currentXOffset % fxItem.tileWidth)  + fxItem.tileWidth)  % fxItem.tileWidth;
        fxItem.currentYOffset = ((fxItem.currentYOffset % fxItem.tileHeight) + fxItem.tileHeight) % fxItem.tileHeight;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Particle explosion
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::CreateParticleExplosion(int startX, int startY, int maxParticles, int maxRadius) {
    std::lock_guard<std::mutex> lock(m_effectsMutex);

    VKFXItem newFX;
    newFX.type     = FXType::ParticleExplosion;
    newFX.fxID     = static_cast<int>(effects.size()) + 1;
    newFX.originX  = startX;
    newFX.originY  = startY;
    newFX.duration = 3.0f;
    newFX.timeout  = 5.0f;

    const float PI        = 3.14159265f;
    float       angleStep = 2.0f * PI / static_cast<float>(maxParticles);

    static const float colors[15][3] = {
        {1.0f,0.0f,0.0f},{1.0f,0.5f,0.0f},{1.0f,1.0f,0.0f},
        {0.0f,1.0f,0.0f},{0.0f,1.0f,1.0f},{0.0f,0.0f,1.0f},
        {0.5f,0.0f,1.0f},{1.0f,0.0f,1.0f},{1.0f,0.0f,0.5f},
        {0.7f,0.7f,0.7f},{1.0f,0.8f,0.2f},{0.3f,1.0f,0.3f},
        {0.9f,0.2f,0.9f},{0.6f,0.6f,1.0f},{0.8f,0.4f,0.2f}
    };

    for (int i = 0; i < maxParticles; ++i) {
        VKParticle p;
        p.angle     = angleStep * i + (static_cast<float>(rand()) / RAND_MAX * 0.2f - 0.1f);
        p.delayCount = rand() % 3;
        p.delayBase  = (rand() % 3) + 2;
        p.speed      = 2.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f;
        p.radius     = 0.0f;
        p.maxRadius  = static_cast<float>(maxRadius);
        int ci = rand() % 15;
        p.r = colors[ci][0]; p.g = colors[ci][1]; p.b = colors[ci][2]; p.a = 1.0f;
        p.x = static_cast<float>(startX);
        p.y = static_cast<float>(startY);
        p.completed          = false;
        p.hasLoggedCompletion = false;
        newFX.particles.push_back(p);
    }

    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void VKFXManager::RenderParticles(VKFXItem& fxItem) {
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    if (fxItem.type != FXType::ParticleExplosion) return;

    bool  allCompleted = true;
    auto  now          = std::chrono::steady_clock::now();
    float elapsed      = std::chrono::duration<float>(now - fxItem.startTime).count();
    float lifeFactor   = 1.0f;

    if (fxItem.duration > 0.0f && elapsed > fxItem.duration * 0.7f) {
        lifeFactor = 1.0f - ((elapsed - fxItem.duration * 0.7f) / (fxItem.duration * 0.3f));
        lifeFactor = std::max(0.0f, std::min(1.0f, lifeFactor));
    }

    for (auto& p : fxItem.particles) {
        if (!p.completed) {
            p.delayCount += 1;
            if (p.delayCount >= p.delayBase) {
                p.delayCount = 0;
                p.radius += p.speed;
                if (p.radius >= p.maxRadius) {
                    p.radius    = p.maxRadius;
                    p.completed = true;
                    continue;
                }
            }
            allCompleted = false;
        }

        float sinVal, cosVal;
        FAST_MATH.FastSinCos(p.angle, sinVal, cosVal);
        p.x = fxItem.originX + cosVal * p.radius;
        p.y = fxItem.originY + sinVal * p.radius;

        float distRatio = p.radius / p.maxRadius;
        float fade      = (1.0f - distRatio * distRatio) * lifeFactor;
        float alpha     = std::max(0.0f, std::min(1.0f, p.a * fade));

        XMFLOAT4 col(p.r, p.g, p.b, alpha);
        renderer->Blit2DColoredPixel(static_cast<int>(p.x), static_cast<int>(p.y), 2.0f, col);
    }

    if (allCompleted && !fxItem.restartOnExpire) {
        fxItem.progress = 1.0f;
        fxItem.timeout  = 0.0f;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Starfield
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::CreateStarfield(int numStars, float circularRadius, float resetDepthPos,
                                   XMFLOAT3 startPos, bool reverse) {
    std::lock_guard<std::mutex> lock(m_effectsMutex);

    VKFXItem newFX;
    newFX.type             = FXType::Starfield;
    newFX.fxID             = static_cast<int>(effects.size()) + 1;
    starfieldID            = newFX.fxID;
    newFX.duration         = FLT_MAX;
    newFX.timeout          = FLT_MAX;
    newFX.progress         = 0.0f;
    newFX.depthMultiplier  = resetDepthPos;
    newFX.starfieldOrigin  = startPos;
    newFX.starfieldReverse = reverse;

#if defined(PLATFORM_WINDOWS)
    const float TWO_PI = XM_2PI;
#else
    const float TWO_PI = 2.0f * 3.14159265f;
#endif

    for (int i = 0; i < numStars; ++i) {
        VKParticle p;
        float angle = static_cast<float>(rand()) / RAND_MAX * TWO_PI;
        float dist  = (0.1f + static_cast<float>(rand()) / RAND_MAX * 0.9f) * circularRadius;
        float spreadX = cosf(angle) * dist;
        float spreadY = sinf(angle) * dist;

        if (!reverse) {
            p.x     = startPos.x + spreadX;
            p.y     = startPos.y + spreadY;
            p.angle = startPos.z + resetDepthPos * (0.1f + 0.9f * static_cast<float>(rand()) / RAND_MAX);
            p.vx = p.vy = 0.0f;
        }
        else {
            float startZ   = 5.0f + static_cast<float>(rand()) / RAND_MAX * (resetDepthPos * 0.1f);
            float fraction = 1.0f - (startZ / resetDepthPos);
            p.vx    = spreadX; p.vy = spreadY;
            p.angle = startZ;
            p.x     = startPos.x + p.vx * fraction;
            p.y     = startPos.y + p.vy * fraction;
        }

        p.speed    = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
        p.radius   = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 2.0f;
        p.maxRadius = resetDepthPos;

        float brightness = 0.7f + static_cast<float>(rand()) / RAND_MAX * 0.3f;
        p.r = brightness;
        p.g = brightness * (0.85f + static_cast<float>(rand()) / RAND_MAX * 0.15f);
        p.b = brightness * (0.90f + static_cast<float>(rand()) / RAND_MAX * 0.10f);
        p.a = 1.0f;
        p.completed          = false;
        p.hasLoggedCompletion = false;
        p.delayCount         = 0;
        p.delayBase          = static_cast<int>(p.angle);
        newFX.particles.push_back(p);
    }

    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void VKFXManager::StopStarfield() {
    if (starfieldID <= 0) return;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [this](const VKFXItem& fx) {
        return fx.type == FXType::Starfield && fx.fxID == starfieldID;
    }), effects.end());
    starfieldID = 0;
}

void VKFXManager::UpdateStarfield(float deltaTime) {
#if defined(PLATFORM_WINDOWS)
    const float TWO_PI = XM_2PI;
#else
    const float TWO_PI = 2.0f * 3.14159265f;
#endif

    for (auto& fx : effects) {
        if (fx.type != FXType::Starfield) continue;

        float    resetDepth = fx.depthMultiplier;
        bool     reverse    = fx.starfieldReverse;
        XMFLOAT3 origin     = fx.starfieldOrigin;

        for (auto& p : fx.particles) {
            if (p.completed) continue;
            float clampedDelta = std::min(deltaTime, 0.1f);
            float zPos = p.angle;

            if (!reverse) {
                zPos -= p.speed * clampedDelta;
                float distRatio = zPos / resetDepth;
                p.a = std::max(0.0f, std::min(1.0f, distRatio * 1.2f));

                if (zPos <= 5.0f) {
                    float a   = static_cast<float>(rand()) / RAND_MAX * TWO_PI;
                    float d   = (0.1f + static_cast<float>(rand()) / RAND_MAX * 0.9f) * (resetDepth * 0.1f);
                    float outCos, outSin;
                    FAST_MATH.FastSinCos(a, outSin, outCos);
                    p.x      = origin.x + outCos * d;
                    p.y      = origin.y + outSin * d;
                    p.angle  = origin.z + resetDepth * (0.9f + 0.1f * static_cast<float>(rand()) / RAND_MAX);
                    p.speed  = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
                    p.radius = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 1.2f;
                    p.a      = 1.0f;
                }
                else {
                    p.angle = zPos;
                }
            }
            else {
                zPos += p.speed * clampedDelta;
                if (zPos >= resetDepth) {
                    float a = static_cast<float>(rand()) / RAND_MAX * TWO_PI;
                    float d = (0.1f + static_cast<float>(rand()) / RAND_MAX * 0.9f) * p.maxRadius;
                    p.vx    = cosf(a) * d; p.vy = sinf(a) * d;
                    p.angle = 5.0f + static_cast<float>(rand()) / RAND_MAX * (resetDepth * 0.1f);
                    float fraction = 1.0f - (p.angle / resetDepth);
                    p.x     = origin.x + p.vx * fraction;
                    p.y     = origin.y + p.vy * fraction;
                    p.speed  = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
                    p.radius = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 1.2f;
                    p.a      = 1.0f;
                }
                else {
                    float fraction = 1.0f - (zPos / resetDepth);
                    p.x     = origin.x + p.vx * fraction;
                    p.y     = origin.y + p.vy * fraction;
                    p.angle = zPos;
                    p.a     = std::max(0.0f, std::min(1.0f, fraction * 1.2f));
                }
            }
        }
    }
}

void VKFXManager::RenderStarfield(VKFXItem& fxItem, VkCommandBuffer cmd, const glm::mat4& viewMatrix) {
    if (fxItem.type != FXType::Starfield || cmd == VK_NULL_HANDLE || !renderer) return;

    // VulkanCamera returns GLM types on all platforms
    glm::mat4 viewProj = renderer->myCamera.GetProjectionMatrix() * renderer->myCamera.GetViewMatrix();

    for (auto& p : fxItem.particles) {
        if (p.completed) continue;

        // viewProj is glm::mat4 on all platforms (VulkanCamera always returns GLM)
        glm::vec4 worldPos(p.x, p.y, p.angle, 1.0f);
        glm::vec4 projPos = viewProj * worldPos;
        if (projPos.w <= 0.0f) continue;
        float ndcX = projPos.x / projPos.w;
        float ndcY = projPos.y / projPos.w;
        float ndcZ = projPos.z / projPos.w;
        if (ndcZ < 0.0f || ndcZ > 1.0f || ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) continue;
        // VulkanCamera now uses glm::lookAtLH: camera-right = world +X (correct LH convention).
        // Standard NDC-to-screen mapping: x in [-1,+1] maps right→right, y already Y-down from proj.
        float screenX = (1.0f + ndcX) * 0.5f * renderer->iOrigWidth;
        float screenY = (ndcY + 1.0f) * 0.5f * renderer->iOrigHeight;

        float sizeScale  = 1.0f + (fxItem.depthMultiplier - p.angle) / fxItem.depthMultiplier * 3.0f;
        float displaySize = p.radius * sizeScale;
        XMFLOAT4 col(p.r, p.g, p.b, p.a);
        renderer->Blit2DColoredPixel(static_cast<int>(screenX), static_cast<int>(screenY), displaySize, col);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Text scroller – Create
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::CreateTextScrollerLTOR(const std::wstring& text, const std::wstring& fontName,
    float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float centerHoldTime, float duration,
    float characterSpacing, float wordSpacing) {

    ThreadLockHelper lock(threadManager, "vkfxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;

    VKFXItem newFX;
    newFX.type    = FXType::TextScroller;
    newFX.subtype = FXSubType::TXT_SCROLL_LTOR;
    newFX.fxID    = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration;
    newFX.timeout  = duration + 1.0f;
    newFX.progress = 0.0f;

    auto& d = newFX.textScrollData;
    d.text            = text;         d.fontName        = fontName;
    d.fontSize        = fontSize;     d.textColor       = textColor;
    d.scrollSpeed     = scrollSpeed;  d.centerHoldTime  = centerHoldTime;
    d.centerHoldTimer = 0.0f;         d.regionX         = regionX;
    d.regionY         = regionY;      d.regionWidth     = regionWidth;
    d.regionHeight    = regionHeight;
    d.currentXPosition = regionX - 100.0f;
    d.currentYPosition = regionY + regionHeight / 2.0f;
    d.isInCenterPhase  = false;       d.hasReachedCenter = false;
    d.characterSpacing = characterSpacing; d.wordSpacing = wordSpacing;

    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void VKFXManager::CreateTextScrollerRTOL(const std::wstring& text, const std::wstring& fontName,
    float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float centerHoldTime, float duration,
    float characterSpacing, float wordSpacing) {

    ThreadLockHelper lock(threadManager, "vkfxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;

    VKFXItem newFX;
    newFX.type    = FXType::TextScroller;
    newFX.subtype = FXSubType::TXT_SCROLL_RTOL;
    newFX.fxID    = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration;
    newFX.timeout  = duration + 1.0f;
    newFX.progress = 0.0f;

    auto& d = newFX.textScrollData;
    d.text            = text;         d.fontName        = fontName;
    d.fontSize        = fontSize;     d.textColor       = textColor;
    d.scrollSpeed     = scrollSpeed;  d.centerHoldTime  = centerHoldTime;
    d.centerHoldTimer = 0.0f;         d.regionX         = regionX;
    d.regionY         = regionY;      d.regionWidth     = regionWidth;
    d.regionHeight    = regionHeight;
    d.currentXPosition = regionX + regionWidth + 100.0f;
    d.currentYPosition = regionY + regionHeight / 2.0f;
    d.isInCenterPhase  = false;       d.hasReachedCenter = false;
    d.characterSpacing = characterSpacing; d.wordSpacing = wordSpacing;

    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void VKFXManager::CreateTextScrollerConsistent(const std::wstring& text, const std::wstring& fontName,
    float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float duration,
    float characterSpacing, float wordSpacing) {

    ThreadLockHelper lock(threadManager, "vkfxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;

    VKFXItem newFX;
    newFX.type    = FXType::TextScroller;
    newFX.subtype = FXSubType::TXT_SCROLL_CONSISTANT;
    newFX.fxID    = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration;
    newFX.timeout  = (duration == FLT_MAX) ? FLT_MAX : duration + 1.0f;
    newFX.progress = 0.0f;

    auto& d = newFX.textScrollData;
    d.text             = text;        d.fontName         = fontName;
    d.fontSize         = fontSize;    d.textColor        = textColor;
    d.scrollSpeed      = scrollSpeed; d.characterSpacing = characterSpacing;
    d.wordSpacing      = wordSpacing; d.regionX          = regionX;
    d.regionY          = regionY;     d.regionWidth      = regionWidth;
    d.regionHeight     = regionHeight;
    d.currentXPosition = regionX + regionWidth;
    d.currentYPosition = regionY + regionHeight / 2.0f;

    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void VKFXManager::CreateTextScrollerMovie(const std::vector<std::wstring>& textLines,
    const std::wstring& fontName,
    float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float lineSpacing, float duration,
    float characterSpacing, float wordSpacing) {

    ThreadLockHelper lock(threadManager, "vkfxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;

    VKFXItem newFX;
    newFX.type    = FXType::TextScroller;
    newFX.subtype = FXSubType::TXT_SCROLL_MOVIE;
    newFX.fxID    = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration;
    newFX.timeout  = duration + 1.0f;
    newFX.progress = 0.0f;

    auto& d = newFX.textScrollData;
    d.textLines        = textLines;   d.fontSize         = fontSize;
    d.textColor        = textColor;   d.scrollSpeed      = scrollSpeed;
    d.lineSpacing      = lineSpacing; d.regionX          = regionX;
    d.regionY          = regionY;     d.regionWidth      = regionWidth;
    d.regionHeight     = regionHeight;
    d.currentYPosition = regionY + regionHeight;
    d.currentLineIndex = 0;
    d.characterSpacing = characterSpacing; d.wordSpacing = wordSpacing;

    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

// ---------------------------------------------------------------------------------------------------------------
// Text scroller – Stop / Pause / Resume
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::StopTextScroller(int effectID) {
    ThreadLockHelper lock(threadManager, "vkfxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [effectID](const VKFXItem& fx) {
        return fx.type == FXType::TextScroller && fx.fxID == effectID;
    }), effects.end());
}

void VKFXManager::PauseTextScroller(int effectID) {
    ThreadLockHelper lock(threadManager, "vkfxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    for (auto& fx : effects)
        if (fx.type == FXType::TextScroller && fx.fxID == effectID) { fx.isPaused = true; break; }
}

void VKFXManager::ResumeTextScroller(int effectID) {
    ThreadLockHelper lock(threadManager, "vkfxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    for (auto& fx : effects)
        if (fx.type == FXType::TextScroller && fx.fxID == effectID) {
            fx.isPaused  = false;
            fx.lastUpdate = std::chrono::steady_clock::now();
            break;
        }
}

// ---------------------------------------------------------------------------------------------------------------
// UpdateTextScroller
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::UpdateTextScroller(VKFXItem& fxItem, float deltaTime) {
    if (fxItem.isPaused || fxItem.type != FXType::TextScroller) return;
    auto& d = fxItem.textScrollData;

    switch (fxItem.subtype) {
    case FXSubType::TXT_SCROLL_LTOR: {
        if (!d.widthCached) {
            d.cachedTextWidth = renderer->CalculateTextWidth(d.text, d.fontSize, d.regionWidth);
            d.widthCached = true;
        }
        float centerX    = d.regionX + d.regionWidth / 2.0f;
        float textCenterX = centerX - d.cachedTextWidth / 2.0f;

        if (!d.hasReachedCenter) {
            float dist = fabsf(d.currentXPosition - textCenterX);
            float maxD = d.regionWidth / 2.0f;
            float mul  = 1.0f + (1.0f - dist / maxD) * 2.0f;
            d.currentXPosition += d.scrollSpeed * mul * deltaTime;
            if (d.currentXPosition >= textCenterX) {
                d.currentXPosition = textCenterX;
                d.hasReachedCenter = true;
                d.isInCenterPhase  = true;
                d.centerHoldTimer  = 0.0f;
            }
        }
        else if (d.isInCenterPhase) {
            d.centerHoldTimer += deltaTime;
            if (d.centerHoldTimer >= d.centerHoldTime) d.isInCenterPhase = false;
        }
        else {
            float dist = fabsf(d.currentXPosition - textCenterX);
            float maxD = d.regionWidth / 2.0f;
            float mul  = 1.0f + (dist / maxD) * 2.0f;
            d.currentXPosition += d.scrollSpeed * mul * deltaTime;
            if (d.currentXPosition > d.regionX + d.regionWidth + 100.0f) fxItem.progress = 1.0f;
        }
        break;
    }
    case FXSubType::TXT_SCROLL_RTOL: {
        if (!d.widthCached) {
            d.cachedTextWidth = renderer->CalculateTextWidth(d.text, d.fontSize, d.regionWidth);
            d.widthCached = true;
        }
        float centerX     = d.regionX + d.regionWidth / 2.0f;
        float textCenterX  = centerX - d.cachedTextWidth / 2.0f;

        if (!d.hasReachedCenter) {
            float dist = fabsf(d.currentXPosition - textCenterX);
            float maxD = d.regionWidth / 2.0f;
            float mul  = 1.0f + (1.0f - dist / maxD) * 2.0f;
            d.currentXPosition -= d.scrollSpeed * mul * deltaTime;
            if (d.currentXPosition <= textCenterX) {
                d.currentXPosition = textCenterX;
                d.hasReachedCenter = true;
                d.isInCenterPhase  = true;
                d.centerHoldTimer  = 0.0f;
            }
        }
        else if (d.isInCenterPhase) {
            d.centerHoldTimer += deltaTime;
            if (d.centerHoldTimer >= d.centerHoldTime) d.isInCenterPhase = false;
        }
        else {
            float dist = fabsf(d.currentXPosition - textCenterX);
            float maxD = d.regionWidth / 2.0f;
            float mul  = 1.0f + (dist / maxD) * 2.0f;
            d.currentXPosition -= d.scrollSpeed * mul * deltaTime;
            if (d.currentXPosition < d.regionX - 100.0f) fxItem.progress = 1.0f;
        }
        break;
    }
    case FXSubType::TXT_SCROLL_CONSISTANT: {
        if (!d.widthCached) {
            d.cachedCharWidths.resize(d.text.length());
            d.cachedCharOffsets.resize(d.text.length());
            float offset = 0.0f;
            for (size_t i = 0; i < d.text.length(); ++i) {
                float w = renderer->GetCharacterWidth(d.text[i], d.fontSize, d.fontName) + d.characterSpacing;
                if (d.text[i] == L' ') w += d.wordSpacing;
                d.cachedCharOffsets[i] = offset;
                d.cachedCharWidths[i]  = w;
                offset += w;
            }
            d.cachedTotalTextWidth = offset;
            d.widthCached = true;
        }
        d.currentXPosition -= d.scrollSpeed * deltaTime;
        if (d.currentXPosition + d.cachedTotalTextWidth < d.regionX)
            d.currentXPosition = d.regionX + d.regionWidth;
        if (fxItem.duration != FLT_MAX) {
            auto n = std::chrono::steady_clock::now();
            if (std::chrono::duration<float>(n - fxItem.startTime).count() >= fxItem.duration)
                fxItem.progress = 1.0f;
        }
        break;
    }
    case FXSubType::TXT_SCROLL_MOVIE: {
        d.currentYPosition -= d.scrollSpeed * deltaTime;
        float totalH = d.textLines.size() * d.lineSpacing;
        if (d.currentYPosition + totalH < d.regionY) fxItem.progress = 1.0f;
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// RenderTextScroller
// ---------------------------------------------------------------------------------------------------------------
void VKFXManager::RenderTextScroller(VKFXItem& fxItem) {
    if (fxItem.type != FXType::TextScroller) return;
    auto& d = fxItem.textScrollData;

    switch (fxItem.subtype) {
    case FXSubType::TXT_SCROLL_LTOR:
    case FXSubType::TXT_SCROLL_RTOL: {
        float transparency = 1.0f;
        if (!d.isInCenterPhase) {
            float centerX  = d.regionX + d.regionWidth / 2.0f;
            float dist     = fabsf(d.currentXPosition - centerX);
            float fadeDist = d.regionWidth / 4.0f;
            if (dist > fadeDist)
                transparency = std::max(0.0f, 1.0f - (dist - fadeDist) / fadeDist);
        }
        XMFLOAT4 col = d.textColor;
        col.w *= transparency;
        MyColor c(static_cast<uint8_t>(col.x * 255), static_cast<uint8_t>(col.y * 255),
                  static_cast<uint8_t>(col.z * 255), static_cast<uint8_t>(col.w * 255));
        Vector2 pos(d.currentXPosition, d.currentYPosition);
        renderer->DrawMyText(d.text, pos, c, d.fontSize);
        break;
    }
    case FXSubType::TXT_SCROLL_CONSISTANT: {
        const float baseX       = d.currentXPosition;
        const float baseY       = d.currentYPosition;
        const float regionLeft  = d.regionX;
        const float regionRight = regionLeft + d.regionWidth;
        const float fadeDist    = 100.0f;
        const float cullLeft    = regionLeft  - fadeDist - 50.0f;
        const float cullRight   = regionRight + fadeDist + 50.0f;

        for (size_t i = 0; i < d.text.length(); ++i) {
            float charX = baseX + d.cachedCharOffsets[i];
            if (charX < cullLeft || charX > cullRight) continue;
            float charW      = d.cachedCharWidths[i];
            float charCenterX = charX + charW * 0.5f;
            float t          = CalculateCharacterTransparency(charCenterX, regionLeft, regionRight, fadeDist);
            if (t <= 0.01f) continue;

            XMFLOAT4 col = d.textColor; col.w *= t;
            MyColor c(static_cast<uint8_t>(col.x*255), static_cast<uint8_t>(col.y*255),
                      static_cast<uint8_t>(col.z*255), static_cast<uint8_t>(col.w*255));
            Vector2 pos(charX, baseY);
            renderer->DrawMyTextWithFont(std::wstring(1, d.text[i]), pos, c, d.fontSize, d.fontName);
        }
        break;
    }
    case FXSubType::TXT_SCROLL_MOVIE: {
        if (!d.widthCached) {
            d.cachedLineWidths.resize(d.textLines.size());
            for (size_t i = 0; i < d.textLines.size(); ++i)
                d.cachedLineWidths[i] = renderer->CalculateTextWidth(d.textLines[i], d.fontSize, d.regionWidth);
            d.widthCached = true;
        }
        const float regionTop = d.regionY;
        const float regionBot = regionTop + d.regionHeight;

        for (size_t i = 0; i < d.textLines.size(); ++i) {
            float lineY = d.currentYPosition + i * d.lineSpacing;
            float t     = CalculateTextTransparency(lineY, regionTop, regionBot, 50.0f);
            if (t <= 0.0f) continue;
            XMFLOAT4 col = d.textColor; col.w *= t;
            MyColor c(static_cast<uint8_t>(col.x*255), static_cast<uint8_t>(col.y*255),
                      static_cast<uint8_t>(col.z*255), static_cast<uint8_t>(col.w*255));
            float cx = d.regionX + (d.regionWidth - d.cachedLineWidths[i]) * 0.5f;
            Vector2 pos(cx, lineY);
            renderer->DrawMyText(d.textLines[i], pos, c, d.fontSize);
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// CalculateTextWidthWithSpacing / CalculateTextTransparency / CalculateCharacterTransparency / SplitTextIntoLines
// ---------------------------------------------------------------------------------------------------------------
float VKFXManager::CalculateTextWidthWithSpacing(const std::wstring& text, const std::wstring& fontName,
                                                  float fontSize, float characterSpacing, float wordSpacing) {
    float total = 0.0f;
    for (wchar_t ch : text) {
        float w = renderer->GetCharacterWidth(ch, fontSize, fontName) + characterSpacing;
        if (ch == L' ') w += wordSpacing;
        total += w;
    }
    return total;
}

float VKFXManager::CalculateTextTransparency(float position, float regionStart, float regionEnd, float fadeDistance) {
    if (position < regionStart - fadeDistance || position > regionEnd + fadeDistance) return 0.0f;
    if (position < regionStart) return 1.0f - (regionStart - position) / fadeDistance;
    if (position > regionEnd)   return 1.0f - (position - regionEnd)   / fadeDistance;
    return 1.0f;
}

float VKFXManager::CalculateCharacterTransparency(float charPos, float regionStart, float regionEnd, float fadeDistance) {
    if (charPos < regionStart - fadeDistance || charPos > regionEnd + fadeDistance) return 0.0f;
    if (charPos > regionEnd)   return std::max(0.0f, std::min(1.0f, 1.0f - (charPos - regionEnd)   / fadeDistance));
    if (charPos < regionStart) return std::max(0.0f, std::min(1.0f, 1.0f - (regionStart - charPos) / fadeDistance));

    float regionWidth      = regionEnd - regionStart;
    float posInRegion      = (charPos - regionStart) / regionWidth;
    const float edgeFade   = 0.25f;
    if (posInRegion < edgeFade)         return std::max(0.0f, std::min(1.0f, posInRegion / edgeFade));
    if (posInRegion > 1.0f - edgeFade)  return std::max(0.0f, std::min(1.0f, (1.0f - posInRegion) / edgeFade));
    return 1.0f;
}

void VKFXManager::SplitTextIntoLines(const std::wstring& text, std::vector<std::wstring>& lines,
                                      float maxWidth, float fontSize) {
    lines.clear();
    std::wstringstream ss(text);
    std::wstring word, currentLine;
    while (std::getline(ss, word, L' ')) {
        std::wstring testLine = currentLine.empty() ? word : currentLine + L" " + word;
        float lineWidth = renderer->CalculateTextWidth(testLine, fontSize, 1000.0f);
        if (lineWidth > maxWidth && !currentLine.empty()) {
            lines.push_back(currentLine);
            currentLine = word;
        }
        else { currentLine = testLine; }
    }
    if (!currentLine.empty()) lines.push_back(currentLine);
}

// ============================================================================================================
// WarpDotTunnel Implementation (Vulkan)
// ============================================================================================================

void VKFXManager::StopAllFX()
{
    if (starfieldID > 0) StopStarfield();
    if (tunnelID    > 0) StopWarpDotTunnel();
    SafelyClearAllEffects();
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] StopAllFX: all effects cleared.");
}

void VKFXManager::DiscardSavedFXState()
{
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    m_sceneSavedEffects.clear();
    m_sceneSavedStarfieldID = 0;
    m_sceneSavedTunnelID    = 0;
    m_savedFXState          = VKActiveFXState{};
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] DiscardSavedFXState: saved snapshot cleared.");
}

void VKFXManager::SaveAndSuspendFXForScene()
{
    if (!m_sceneSavedEffects.empty() || m_sceneSavedStarfieldID > 0)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VKFXManager] SaveAndSuspendFXForScene: previous save not yet restored — overwriting.");
        m_sceneSavedEffects.clear();
    }

    m_sceneSavedEffects     = effects;
    m_sceneSavedStarfieldID = starfieldID;
    m_sceneSavedTunnelID    = tunnelID;

    SafelyClearAllEffects();
    starfieldID = 0;
    tunnelID    = 0;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] Scene FX state saved ("
        + std::to_wstring(m_sceneSavedEffects.size()) + L" effects). Scene suspended.");
}

void VKFXManager::RestoreFXAfterScene()
{
    if (m_sceneSavedEffects.empty() && m_sceneSavedStarfieldID == 0)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VKFXManager] RestoreFXAfterScene: nothing saved to restore.");
        return;
    }

    if (tunnelID > 0) StopWarpDotTunnel();
    SafelyClearAllEffects();

    effects     = std::move(m_sceneSavedEffects);
    starfieldID = m_sceneSavedStarfieldID;
    tunnelID    = m_sceneSavedTunnelID;

    m_sceneSavedEffects.clear();
    m_sceneSavedStarfieldID = 0;
    m_sceneSavedTunnelID    = 0;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] Scene FX state restored ("
        + std::to_wstring(effects.size()) + L" effects).");
}

void VKFXManager::Init3DWarpDOTTunnel(float x, float y, float z,
                                      float minRadius, float maxRadius,
                                      TunnelSpinCycle spinCycle,
                                      int travelSpeed, bool reverseTravel,
                                      int dotsPerCircle, int density)
{
    std::lock_guard<std::mutex> lock(m_effectsMutex);

    if (tunnelID > 0)
        StopWarpDotTunnel();

    VKFXItem newFX;
    newFX.type       = FXType::WarpDotTunnel;
    newFX.fxID       = static_cast<int>(effects.size()) + 1;
    newFX.duration   = FLT_MAX;
    newFX.timeout    = FLT_MAX;
    newFX.progress   = 0.0f;
    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    tunnelID         = newFX.fxID;

    VKWarpTunnelData& data = newFX.warpTunnelData;
    data.startX        = x;
    data.startY        = y;
    data.startZ        = z;
    data.minRadius     = minRadius;
    data.maxRadius     = maxRadius;
    data.spinCycle     = spinCycle;
    data.travelSpeed   = std::max(1, travelSpeed);
    data.reverseTravel = reverseTravel;
    data.dotsPerCircle = std::max(3, dotsPerCircle);
    data.density       = std::clamp(density, 1, 100);
    data.totalDistance  = 800.0f;
    data.nearZ          = z;
    data.farZ           = z + data.totalDistance;
    data.spinSpeed      = static_cast<float>(travelSpeed) * 0.05f;
    data.smoothLookTarget = XMFLOAT3(x, y, data.farZ);

    int ringCount = data.density;
    data.rings.reserve(ringCount);
    for (int i = 0; i < ringCount; ++i)
    {
        VKTunnelRing ring{};
        float fraction = static_cast<float>(i) / static_cast<float>(ringCount);

        ring.zPos = reverseTravel
            ? (data.nearZ + fraction * data.totalDistance)
            : (data.farZ  - fraction * data.totalDistance);

        ring.bornCx    = x + VKWarpTunnelData::kSideWaveRadius * sinf(fraction * XM_2PI);
        ring.bornCy    = y + VKWarpTunnelData::kSideWaveRadius * cosf(fraction * XM_2PI);
        ring.cx        = ring.bornCx;
        ring.cy        = ring.bornCy;
        ring.spinAngle = 0.0f;
        ring.alive     = true;
        ring.colorStep = i % VKWarpTunnelData::kGraySteps;

        data.rings.push_back(ring);
    }

    if (renderer)
    {
        renderer->myCamera.SetPosition(x, y, data.nearZ);
        renderer->myCamera.SetTarget(glm::vec3(x, y, data.farZ));
        renderer->myCamera.SetYawPitch(0.0f, 0.0f);
    }

    effects.push_back(newFX);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] WarpDotTunnel created: Density=" +
        std::to_wstring(density) + L", DotsPerCircle=" + std::to_wstring(dotsPerCircle) +
        L", FXID=" + std::to_wstring(newFX.fxID));
}

void VKFXManager::StopWarpDotTunnel()
{
    if (tunnelID <= 0) return;

    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [this](const VKFXItem& fx) {
            return fx.type == FXType::WarpDotTunnel && fx.fxID == tunnelID;
        }),
        effects.end()
    );

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VKFXManager] WarpDotTunnel stopped.");
    tunnelID = 0;
}

void VKFXManager::UpdateWarpDotTunnel(VKFXItem& fx, float deltaTime)
{
    VKWarpTunnelData& data = fx.warpTunnelData;
    if (data.rings.empty()) return;

    const float dt        = std::min(deltaTime, 0.05f);
    const float baseSpeed = static_cast<float>(data.travelSpeed);

    data.sideWaveTime += dt;

    for (auto& ring : data.rings)
    {
        if (!ring.alive) continue;

        float pathT = std::clamp((data.farZ - ring.zPos) / data.totalDistance, 0.0f, 1.0f);

        float t2          = pathT * pathT;
        float speedFactor = data.reverseTravel
            ? (1.0f + t2 * t2 * 6.0f)
            : (1.0f + t2 * t2 * 10.0f);

        float frameSpeed = baseSpeed * speedFactor * dt;

        if (!data.reverseTravel)
            ring.zPos -= frameSpeed;
        else
            ring.zPos += frameSpeed;

        if (!data.reverseTravel && ring.zPos < data.nearZ)
        {
            ring.zPos   = data.farZ;
            float phase = data.sideWaveTime * VKWarpTunnelData::kSideWaveSpeed;
            ring.bornCx = data.startX + VKWarpTunnelData::kSideWaveRadius * sinf(phase);
            ring.bornCy = data.startY + VKWarpTunnelData::kSideWaveRadius * cosf(phase);
        }
        else if (data.reverseTravel && ring.zPos > data.farZ)
        {
            ring.zPos   = data.nearZ;
            float phase = data.sideWaveTime * VKWarpTunnelData::kSideWaveSpeed;
            ring.bornCx = data.startX + VKWarpTunnelData::kSideWaveRadius * sinf(phase);
            ring.bornCy = data.startY + VKWarpTunnelData::kSideWaveRadius * cosf(phase);
        }

        ring.cx = ring.bornCx;
        ring.cy = ring.bornCy;

        // Reverse travel inverts the perceived rotation direction; negate to keep
        // Clockwise/AntiClockwise consistent regardless of travel direction.
        const float spinDelta = (data.reverseTravel ? -data.spinSpeed : data.spinSpeed) * dt;
        switch (data.spinCycle)
        {
        case TunnelSpinCycle::Clockwise:     ring.spinAngle += spinDelta; break;
        case TunnelSpinCycle::AntiClockwise: ring.spinAngle -= spinDelta; break;
        default: break;
        }
        ring.spinAngle = fmodf(ring.spinAngle, XM_2PI);
        if (ring.spinAngle < 0.0f) ring.spinAngle += XM_2PI;
    }

    if (renderer)
    {
        const int ringCount = static_cast<int>(data.rings.size());
        const int lookIdx   = std::min(19, ringCount - 1);

        std::vector<int> order;
        order.reserve(ringCount);
        for (int ri = 0; ri < ringCount; ++ri) order.push_back(ri);
        std::sort(order.begin(), order.end(), [&data](int a, int b) {
            float ptA = (data.farZ - data.rings[a].zPos) / data.totalDistance;
            float ptB = (data.farZ - data.rings[b].zPos) / data.totalDistance;
            return ptA > ptB;
        });

        const VKTunnelRing& lookRing = data.rings[order[lookIdx]];

        const float alpha = 1.0f - expf(-VKWarpTunnelData::kCameraSmooth * dt);
        data.smoothLookTarget.x += (lookRing.cx   - data.smoothLookTarget.x) * alpha;
        data.smoothLookTarget.y += (lookRing.cy   - data.smoothLookTarget.y) * alpha;
        data.smoothLookTarget.z += (lookRing.zPos - data.smoothLookTarget.z) * alpha;

        renderer->myCamera.SetTarget(glm::vec3(
            data.smoothLookTarget.x, data.smoothLookTarget.y, data.smoothLookTarget.z));
    }
}

void VKFXManager::RenderWarpDotTunnel(VKFXItem& fx, VkCommandBuffer cmd)
{
    const VKWarpTunnelData& data = fx.warpTunnelData;
    if (data.rings.empty()) return;

    // VulkanCamera returns GLM types on all platforms
    glm::mat4 viewProj = renderer->myCamera.GetProjectionMatrix() * renderer->myCamera.GetViewMatrix();

    const float angleStep = XM_2PI / static_cast<float>(data.dotsPerCircle);
    const float halfW     = static_cast<float>(renderer->iOrigWidth)  * 0.5f;
    const float halfH     = static_cast<float>(renderer->iOrigHeight) * 0.5f;
    const float edgeFade  = 0.08f;

    // Sequential gray ramp: very dark → white, kGraySteps entries
    static constexpr float kGrayRamp[VKWarpTunnelData::kGraySteps] = {
        0.08f, 0.19f, 0.30f, 0.44f, 0.58f, 0.72f, 0.86f, 1.0f
    };

    for (const auto& ring : data.rings)
    {
        if (!ring.alive) continue;

        float pathT = std::clamp((data.farZ - ring.zPos) / data.totalDistance, 0.0f, 1.0f);

        float ringRadius = data.minRadius + (data.maxRadius - data.minRadius) * pathT;

        float alpha = 1.0f;
        if (pathT < edgeFade)
            alpha = pathT / edgeFade;
        else if (pathT > 1.0f - edgeFade)
            alpha = (1.0f - pathT) / edgeFade;

        // Sequential gray from the ramp — each ring cycles independently from dark to white
        float gray = kGrayRamp[ring.colorStep % VKWarpTunnelData::kGraySteps];
        float r = gray;
        float g = gray;
        float b = gray;
        float dotSize = 1.0f + pathT * 3.0f;

        XMFLOAT4 dotColor(r, g, b, alpha);

        for (int i = 0; i < data.dotsPerCircle; ++i)
        {
            float dotAngle = angleStep * static_cast<float>(i) + ring.spinAngle;
            float sinA, cosA;
            FAST_MATH.FastSinCos(dotAngle, sinA, cosA);

            glm::vec4 worldPos(
                ring.cx + cosA * ringRadius,
                ring.cy + sinA * ringRadius,
                ring.zPos,
                1.0f
            );
            glm::vec4 clip = viewProj * worldPos;
            if (clip.w <= 0.0f) continue;
            float ndcX = clip.x / clip.w;
            float ndcY = clip.y / clip.w;
            float ndcZ = clip.z / clip.w;

            if (ndcZ <= 0.0f || ndcZ > 1.0f) continue;
            if (ndcX < -1.0f || ndcX > 1.0f) continue;
            if (ndcY < -1.0f || ndcY > 1.0f) continue;

            // VulkanCamera uses glm::lookAtLH: right = +X (correct LH). Standard NDC-to-screen:
            float screenX = (1.0f + ndcX) * halfW;
            float screenY = (ndcY + 1.0f) * halfH;

            renderer->Blit2DColoredPixel(
                static_cast<int>(screenX),
                static_cast<int>(screenY),
                dotSize,
                dotColor
            );
        }
    }
}

// =============================================================================
// TextFadeInOut — loading-screen text with frame-accurate fade in / out (VK)
// =============================================================================

int VKFXManager::ShowLoadingText(const std::wstring& text,
                                  XMFLOAT4 endColor,
                                  float fadeInDuration, float fadeOutDuration,
                                  XMFLOAT4 startColor,
                                  float posX, float posY,
                                  const TextRenderStyle* fontStyle)
{
    if (bHasCleanedUp) return -1;

    float maxRemainingFadeOut = 0.0f;
    for (auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        if (fx.textFadeData.immediateStop) continue;
        auto& d = fx.textFadeData;

        if (d.pendingDelay > 0.0f) {
            d.immediateStop = true; fx.progress = 1.0f;
            continue;
        }
        if (d.phase == TextFadePhase::FadeIn) {
            float t = (d.fadeInDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeInDuration, 0.0f, 1.0f) : 1.0f;
            d.fadeOutStartColor = {
                d.startColor.x + (d.endColor.x - d.startColor.x) * t,
                d.startColor.y + (d.endColor.y - d.startColor.y) * t,
                d.startColor.z + (d.endColor.z - d.startColor.z) * t,
                d.startColor.w + (d.endColor.w   - d.startColor.w) * t
            };
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
            if (d.fadeOutDuration > maxRemainingFadeOut) maxRemainingFadeOut = d.fadeOutDuration;
        } else if (d.phase == TextFadePhase::Holding) {
            d.fadeOutStartColor = d.endColor;
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
            if (d.fadeOutDuration > maxRemainingFadeOut) maxRemainingFadeOut = d.fadeOutDuration;
        } else if (d.phase == TextFadePhase::FadeOut) {
            float rem = d.fadeOutDuration - d.phaseTimer;
            if (rem > maxRemainingFadeOut) maxRemainingFadeOut = rem;
        }
    }

    static int nextID = 6000;
    int newID = nextID++;

    VKFXItem fx{};
    fx.fxID = newID; fx.nextEffectID = -1;
    fx.type = FXType::TextFadeInOut; fx.subtype = FXSubType::TXT_FADE_IN;
    fx.duration = 0.0f; fx.progress = 0.0f; fx.delay = 0.0f; fx.timeout = 0.0f;
    fx.startTime = std::chrono::steady_clock::now(); fx.lastUpdate = fx.startTime;

    VKTextFadeData& d = fx.textFadeData;
    d.text            = text;
    d.startColor      = startColor;
    d.endColor        = endColor;
    d.fadeOutColor    = XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f };
    d.fadeInDuration  = std::max(fadeInDuration,  0.05f);
    d.fadeOutDuration = std::max(fadeOutDuration, 0.05f);
    d.displayDuration = -1.0f;
    d.pendingDelay    = maxRemainingFadeOut;
    d.phase           = TextFadePhase::FadeIn;
    d.phaseTimer      = 0.0f;
    d.immediateStop   = false;
    d.posX = (posX < 0.0f) ? 20.0f : posX;
    d.posY = (posY < 0.0f) ? (renderer ? static_cast<float>(renderer->iOrigHeight) * LOADER_TEXT_Y_RATIO
                                        : fDEFAULT_WINDOW_HEIGHT * LOADER_TEXT_Y_RATIO) : posY;
    if (fontStyle) d.fontStyle = *fontStyle;

    effects.push_back(std::move(fx));
    return newID;
}

void VKFXManager::StopLoadingText()
{
    for (auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        if (fx.textFadeData.immediateStop) continue;
        auto& d = fx.textFadeData;
        if (d.pendingDelay > 0.0f || d.phase == TextFadePhase::Stopped) {
            d.immediateStop = true; fx.progress = 1.0f;
            continue;
        }
        if (d.phase == TextFadePhase::FadeIn) {
            float t = (d.fadeInDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeInDuration, 0.0f, 1.0f) : 1.0f;
            d.fadeOutStartColor = {
                d.startColor.x + (d.endColor.x - d.startColor.x) * t,
                d.startColor.y + (d.endColor.y - d.startColor.y) * t,
                d.startColor.z + (d.endColor.z - d.startColor.z) * t,
                d.startColor.w + (d.endColor.w   - d.startColor.w) * t
            };
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
        } else if (d.phase == TextFadePhase::Holding) {
            d.fadeOutStartColor = d.endColor;
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
        }
    }
}

void VKFXManager::RenderLoadingText()
{
    if (bHasCleanedUp || !renderer) return;
    static auto lastRLT = std::chrono::steady_clock::now();
    auto  now      = std::chrono::steady_clock::now();
    float deltaTime = std::min(std::chrono::duration<float>(now - lastRLT).count(), 0.1f);
    lastRLT = now;

    for (auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        UpdateTextFadeInOut(fx, deltaTime);
        if (fx.textFadeData.phase != TextFadePhase::Stopped && !fx.textFadeData.immediateStop)
            RenderTextFadeInOut(fx);
    }
}

void VKFXManager::UpdateTextFadeInOut(VKFXItem& fx, float deltaTime)
{
    VKTextFadeData& d = fx.textFadeData;
    if (d.immediateStop) { fx.progress = 1.0f; return; }
    if (d.pendingDelay > 0.0f) { d.pendingDelay -= deltaTime; return; }
    d.phaseTimer += deltaTime;
    switch (d.phase) {
    case TextFadePhase::FadeIn:
        if (d.phaseTimer >= d.fadeInDuration) { d.phase = TextFadePhase::Holding; d.phaseTimer = 0.0f; }
        break;
    case TextFadePhase::Holding:
        if (d.displayDuration >= 0.0f && d.phaseTimer >= d.displayDuration) {
            d.fadeOutStartColor = d.endColor;
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
        }
        break;
    case TextFadePhase::FadeOut:
        if (d.phaseTimer >= d.fadeOutDuration) { d.phase = TextFadePhase::Stopped; fx.progress = 1.0f; }
        break;
    case TextFadePhase::Stopped:
        fx.progress = 1.0f;
        break;
    }
}

void VKFXManager::RenderTextFadeInOut(VKFXItem& fx)
{
    if (!renderer) return;
    VKTextFadeData& d = fx.textFadeData;
    if (d.text.empty() || d.pendingDelay > 0.0f) return;

    XMFLOAT4 renderColor = d.endColor;
    float     alpha      = renderColor.w;
    switch (d.phase) {
    case TextFadePhase::FadeIn: {
        float t = (d.fadeInDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeInDuration, 0.0f, 1.0f) : 1.0f;
        renderColor.x = d.startColor.x + (d.endColor.x - d.startColor.x) * t;
        renderColor.y = d.startColor.y + (d.endColor.y - d.startColor.y) * t;
        renderColor.z = d.startColor.z + (d.endColor.z - d.startColor.z) * t;
        alpha         = d.startColor.w + (d.endColor.w   - d.startColor.w)   * t;
        break;
    }
    case TextFadePhase::Holding:
        renderColor = d.endColor; alpha = d.endColor.w; break;
    case TextFadePhase::FadeOut: {
        float t = (d.fadeOutDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeOutDuration, 0.0f, 1.0f) : 1.0f;
        renderColor.x = d.fadeOutStartColor.x + (d.fadeOutColor.x - d.fadeOutStartColor.x) * t;
        renderColor.y = d.fadeOutStartColor.y + (d.fadeOutColor.y - d.fadeOutStartColor.y) * t;
        renderColor.z = d.fadeOutStartColor.z + (d.fadeOutColor.z - d.fadeOutStartColor.z) * t;
        alpha         = d.fadeOutStartColor.w + (d.fadeOutColor.w  - d.fadeOutStartColor.w)  * t;
        break;
    }
    default: return;
    }

    renderColor.w = std::clamp(alpha, 0.0f, 1.0f);
    if (renderColor.w < 0.005f) return;

    uint8_t r = static_cast<uint8_t>(std::clamp(renderColor.x, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(std::clamp(renderColor.y, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(std::clamp(renderColor.z, 0.0f, 1.0f) * 255.0f);
    uint8_t a = static_cast<uint8_t>(renderColor.w * 255.0f);

    Vector2 pos(d.posX, d.posY);
    MyColor color(r, g, b, a);
    renderer->DrawMyTextStyled(d.text, pos, color, d.fontStyle);
}

bool VKFXManager::HasActiveLoadingTextEffects() const
{
    for (const auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        if (fx.textFadeData.immediateStop) continue;
        if (fx.textFadeData.phase == TextFadePhase::Stopped) continue;
        return true;
    }
    return false;
}

#pragma warning(pop)

#endif // __USE_VULKAN__
