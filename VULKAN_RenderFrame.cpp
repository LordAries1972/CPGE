// ---------------------------------------------------------------------------------------------------------------
// VULKAN_RenderFrame.cpp  —  Vulkan Render Loop & Scene Rendering
// ---------------------------------------------------------------------------------------------------------------
// Implements VulkanRenderer::RenderFrame() and the scene-specific rendering functions
// RenderGamePlay() and RenderIntroMovie().
//
// Pipeline order (mirrors DXRenderFrame.cpp):
//   1) Safety guards, acquire exclusive lock
//   2) Acquire swap chain image, begin command buffer
//   3) D2D overlay draw — main overlay (HUD, GUI, cursor) into main CPU bitmap;
//      SCENE_GAMETITLE starfield drawn inline into a separate bg CPU bitmap
//   4) Upload both D2D overlays to Vulkan textures (Windows)
//   5) Begin render pass: background image quad
//   6) bg overlay composite (starfield behind 3D models — matches DX11 draw order)
//   7) 3D scene rendering per scene type (RenderGamePlay)
//   8) Main overlay composite (GUI/HUD always in front of 3D models)
//   9) FX fullscreen effects (ColorFader/fades — always on top)
//  10) End render pass, submit, present
//
// Platform guards:
//   #if defined(PLATFORM_WINDOWS)  /  #elif defined(PLATFORM_LINUX)  /  #elif defined(PLATFORM_ANDROID)
// ---------------------------------------------------------------------------------------------------------------
#include "Includes.h"

#if defined(__USE_VULKAN__)

#include "VULKAN_Renderer.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "Configuration.h"
#include "FXManager.h"
#include "GUIManager.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "MoviePlayer.h"
#include "ThreadManager.h"

#if defined(PLATFORM_WINDOWS)
    #include "WinSystem.h"
    #include "ScreenRecorder.h"
    #include "ConsoleWindow.h"
#endif

// ---------------------------------------------------------------------------------------------------------------
// Externals (same external references as DXRenderFrame.cpp)
// ---------------------------------------------------------------------------------------------------------------
#if defined(PLATFORM_WINDOWS)
extern HWND                  hwnd;
extern HINSTANCE             hInst;
extern SystemUtils           sysUtils;
extern WindowMetrics         winMetrics;
extern ScreenRecorder        screenRecorder;
extern ConsoleWindow         consoleWindow;
#endif
extern GUIManager            guiManager;
extern Debug                 debug;
extern ExceptionHandler      exceptionHandler;
extern SceneManager          scene;
extern ThreadManager         threadManager;
extern FXManager           fxManager;
extern Vector2               myMouseCoords;
extern Model                 models[MAX_MODELS];
extern LightsManager         lightsManager;
extern MoviePlayer           moviePlayer;
extern Configuration         config;
extern std::atomic<bool>     bResizeInProgress;
extern std::atomic<bool>     bFullScreenTransition;
extern bool                  bResizing;

// ---------------------------------------------------------------------------------------------------------------
// UBO layout matching the 3D vertex shader (set=0, binding=0)
// Fields: model(mat4) + view(mat4) + proj(mat4) + camPos(vec4) + scale(vec4) = 224 bytes
// ---------------------------------------------------------------------------------------------------------------
struct VKCameraUBO
{
    float model[16];   // 64 bytes
    float view[16];    // 64 bytes
    float proj[16];    // 64 bytes
    float camPos[4];   // 16 bytes (xyz + pad)
    float scale[4];    // 16 bytes (xyz + pad)
};

// ---------------------------------------------------------------------------------------------------------------
// VkShadowWorldMatrix — the world matrix RenderGamePlay() uploads for model `m`, as a row-major
// (row-vector) float[16].  MUST stay in step with the transform-UBO world build below so shadow
// casters line up with what the main pass draws (scale is baked into the world matrix on Vulkan).
// ---------------------------------------------------------------------------------------------------------------
static void VkShadowWorldMatrix(Model& m, float out[16])
{
#if defined(PLATFORM_WINDOWS)
    XMMATRIX world;
    if (m.m_modelInfo.bHasBaseLocalTRS)
    {
        world = m.GetWorldMatrix();
    }
    else
    {
        world = XMMatrixScaling(m.m_modelInfo.scale.x, m.m_modelInfo.scale.y, m.m_modelInfo.scale.z)
              * XMMatrixRotationRollPitchYaw(m.m_modelInfo.rotation.x, m.m_modelInfo.rotation.y, m.m_modelInfo.rotation.z)
              * XMMatrixTranslation(m.m_modelInfo.position.x, m.m_modelInfo.position.y, m.m_modelInfo.position.z);
    }
    XMFLOAT4X4 wf;
    XMStoreFloat4x4(&wf, world);
    std::memcpy(out, &wf, sizeof(float) * 16);
#else
    // Non-Windows Vulkan: ModelInfo::worldMatrix is the portable row-major Matrix4x4.
    std::memcpy(out, &m.m_modelInfo.worldMatrix.m[0][0], sizeof(float) * 16);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// UpdateReflectionProbeVK — scene reflection probe (see "Scene Reflections" in Lights.h).
// Rebuilds the CPU sky cube when the lighting changed and records a staging-buffer -> cube image
// copy on `cmd`.  MUST be recorded outside a render pass.  The image rests in
// SHADER_READ_ONLY_OPTIMAL; the barriers also order the copy after earlier frames' fragment reads.
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::UpdateReflectionProbeVK(VkCommandBuffer cmd, float deltaTime)
{
    if (m_reflImage == VK_NULL_HANDLE || !m_reflStagingMapped[m_currentFrame])
    {
        ReflectionFrameSet(m_reflProbe, false);
        return;
    }

    const std::vector<LightStruct> lights = lightsManager.GetAllLights();
    ReflectionProbeUpdate(lights, deltaTime, m_reflProbe);

    if (m_reflProbe.size == m_reflSize && m_reflUploadedVersion != m_reflProbe.version)
    {
        // Pack every face / mip into this frame's staging buffer and describe the copy regions.
        uint8_t* dst = static_cast<uint8_t*>(m_reflStagingMapped[m_currentFrame]);
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(static_cast<size_t>(6 * m_reflMips));
        VkDeviceSize offset = 0;
        for (int face = 0; face < 6; ++face)
        {
            for (int mip = 0; mip < m_reflMips; ++mip)
            {
                const std::vector<uint8_t>& px = m_reflProbe.data[face][mip];
                const uint32_t ms = static_cast<uint32_t>(ReflectionMipSize(m_reflSize, mip));
                const VkDeviceSize bytes = static_cast<VkDeviceSize>(ms) * ms * 4u;
                if (px.size() < bytes) continue;
                std::memcpy(dst + offset, px.data(), static_cast<size_t>(bytes));

                VkBufferImageCopy r{};
                r.bufferOffset      = offset;
                r.bufferRowLength   = 0;                                        // tightly packed
                r.bufferImageHeight = 0;
                r.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(mip), static_cast<uint32_t>(face), 1 };
                r.imageOffset       = { 0, 0, 0 };
                r.imageExtent       = { ms, ms, 1 };
                regions.push_back(r);
                offset += bytes;
            }
        }

        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_reflImage;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(m_reflMips), 0, 6 };

        b.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);

        vkCmdCopyBufferToImage(cmd, m_reflStaging[m_currentFrame], m_reflImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());

        b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);

        m_reflUploadedVersion = m_reflProbe.version;
    }

    // The probe may only be sampled once its contents were uploaded at least once.
    ReflectionFrameSet(m_reflProbe, m_reflUploadedVersion != 0);
}

// ---------------------------------------------------------------------------------------------------------------
// RenderShadowPassVK — shadow depth passes, recorded BEFORE the main render pass begins
// (Vulkan render passes cannot nest).  Plans against lightsManager.GetAllLights(): RenderGamePlay
// uploads the same map in the same order (AnimateLights changes values, not order), so
// lightShadowInfo[i] lines up with globalLights[i].  Always writes this frame's ShadowBuffer UBO.
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::RenderShadowPassVK(VkCommandBuffer cmd, float deltaTime)
{
    // Scene reflection probe first: it records a buffer->image copy (outside any render pass) and
    // publishes g_reflectionFrame, which ShadowPackGPU copies into this frame's ShadowBuffer below.
    UpdateReflectionProbeVK(cmd, deltaTime);

    m_shadowFrame.enabled = false;
    m_shadowFrame.views.clear();

    // Per-caster world matrices (same order as m_shadowCasters).
    std::vector<std::array<float, 16>> casterWorld;

    if (m_shadowResourcesReady && m_shadowPipeline != VK_NULL_HANDLE &&
        threadManager.threadVars.bLoaderTaskFinished.load())
    {
        const std::vector<LightStruct> lights = lightsManager.GetAllLights();

        m_shadowCasters.clear();
        m_shadowCasterModels.clear();
        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            Model& m = scene.scene_models[i];
            if (!m.m_isLoaded || m.bIsDestroyed) continue;
            ModelInfo& mi = m.m_modelInfo;
            if (mi.bIsTransformProxy || mi.bIsTransformOnly || !mi.castShadows) continue;
            if (mi.vertexBuffer == VK_NULL_HANDLE || mi.indexBuffer == VK_NULL_HANDLE || mi.indices.empty()) continue;
            if (!ModelComputeShadowBounds(mi)) continue;

            std::array<float, 16> w{};
            VkShadowWorldMatrix(m, w.data());
            const float one[3] = { 1.0f, 1.0f, 1.0f };                         // scale already in world
            ShadowCaster c{};
            ShadowMakeWorldSphere(mi.shadowBoundsCenter, mi.shadowBoundsRadius, one, w.data(), c);
            m_shadowCasters.push_back(c);
            m_shadowCasterModels.push_back(i);
            casterWorld.push_back(w);
        }

        const glm::vec3 cp = myCamera.GetPosition();
        const float camPos[3] = { cp.x, cp.y, cp.z };
        BuildShadowFrame(lights, m_shadowCasters, camPos, m_shadowFrame);
    }

    if (m_shadowFrame.enabled)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

        struct ShadowPC { float worldLightVP[16]; float scale[4]; } pc{};
        pc.scale[0] = pc.scale[1] = pc.scale[2] = pc.scale[3] = 1.0f;

        for (const ShadowView& view : m_shadowFrame.views)
        {
            const bool     isDir = (view.target == SHADOW_TARGET_DIRECTIONAL);
            VkFramebuffer  fb    = isDir ? m_shadowDirFramebuffer : m_shadowLocalFramebuffers[view.target];
            const uint32_t size  = static_cast<uint32_t>(isDir ? m_shadowDirSize : m_shadowLocalSize);
            if (fb == VK_NULL_HANDLE) continue;

            VkClearValue clear{};
            clear.depthStencil = { 1.0f, 0 };
            VkRenderPassBeginInfo rp{};
            rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp.renderPass        = m_shadowRenderPass;
            rp.framebuffer       = fb;
            rp.renderArea.offset = { 0, 0 };
            rp.renderArea.extent = { size, size };
            rp.clearValueCount   = 1;
            rp.pClearValues      = &clear;
            vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp{ 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
            VkRect2D   sc{ { 0, 0 }, { size, size } };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);

            for (size_t c = 0; c < m_shadowCasters.size(); ++c)
            {
                if (!ShadowViewAffectsCaster(view, m_shadowCasters[c])) continue;
                ModelInfo& mi = scene.scene_models[m_shadowCasterModels[c]].m_modelInfo;

                // Raw row-major push; GLSL reads column-major = transpose = column-vector form.
                ShadowMat4Mul(casterWorld[c].data(), view.viewProj, pc.worldLightVP);
                vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

                VkBuffer     vbufs[]   = { mi.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
                vkCmdBindIndexBuffer(cmd, mi.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, static_cast<uint32_t>(mi.indices.size()), 1, 0, 0, 0);
            }

            vkCmdEndRenderPass(cmd);                                            // -> SHADER_READ_ONLY_OPTIMAL
        }
    }

    // This frame's ShadowBuffer UBO (set = 2, binding 1) - raw row-major for GLSL.
    if (m_shadowUBOMapped[m_currentFrame])
    {
        ShadowBufferData sb;
        ShadowPackGPU(m_shadowFrame, false, sb);
        std::memcpy(m_shadowUBOMapped[m_currentFrame], &sb, sizeof(ShadowBufferData));

        // Copy with planar reflections off for the mirror + capture passes (see CreateShadowResourcesVK).
        if (m_shadowUBOMirrorMapped[m_currentFrame])
        {
            sb.planarParams[0] = 0.0f;
            std::memcpy(m_shadowUBOMirrorMapped[m_currentFrame], &sb, sizeof(ShadowBufferData));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// DrawModelsVK — the scene model draw loop, shared by the main pass and the planar mirror pass.
// The pipeline and descriptor set 2 must already be bound.  mirrorPass = true skips reflector surfaces and
// writes the transform UBO slot reserved for that pass: mirrorPlane = -1 main, 0..3 planar plane, MAX_PLANAR_PLANES + face
// for a live-capture face (reflectors are drawn there).
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::DrawModelsVK(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
                                  const glm::vec3& camPos, int mirrorPlane)
{
    const bool mirrorPass  = (mirrorPlane >= 0);                              // any non-main pass: own UBO slot, no material writes
    const bool skipReflect = (mirrorPlane >= 0 && mirrorPlane < MAX_PLANAR_PLANES);   // planar mirror passes skip reflectors
    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        Model& m = scene.scene_models[i];

        // Skip unloaded slots and transform-hierarchy nodes that carry no geometry
        if (!m.m_isLoaded) continue;
        if (m.m_modelInfo.bIsTransformProxy || m.m_modelInfo.bIsTransformOnly) continue;
        if (skipReflect && ModelIsPlanarReflector(m.m_modelInfo)) continue;   // a reflector never reflects itself

        m.m_modelInfo.fxActive = false; // mirrors DX11 RenderGamePlay

        // ---- Build per-model transform UBO (set=0, binding=0) ----
        // Layout: model(mat4) + view(mat4) + proj(mat4) + camPos(vec4) + scale(vec4) = 224 bytes
        VKCameraUBO ubo{};
#if defined(PLATFORM_WINDOWS)
        // DirectXMath stores matrices row-major. GLSL reads them column-major: the byte
        // layout is automatically the correct transpose, so do NOT call XMMatrixTranspose.
        XMMATRIX world;
        if (m.m_modelInfo.bHasBaseLocalTRS)
        {
            world = m.GetWorldMatrix();
        }
        else
        {
            world = XMMatrixScaling(m.m_modelInfo.scale.x, m.m_modelInfo.scale.y, m.m_modelInfo.scale.z)
                  * XMMatrixRotationRollPitchYaw(m.m_modelInfo.rotation.x, m.m_modelInfo.rotation.y, m.m_modelInfo.rotation.z)
                  * XMMatrixTranslation(m.m_modelInfo.position.x, m.m_modelInfo.position.y, m.m_modelInfo.position.z);
        }
        XMFLOAT4X4 worldF;
        XMStoreFloat4x4(&worldF, world);
        // asm MatrixCopy4x4F: 4 SSE MOVUPS loads+stores for 64 bytes — no loop overhead
        MatrixCopy4x4F(&worldF, ubo.model);
#else
        std::memcpy(ubo.model, glm::value_ptr(m.GetWorldMatrix()), sizeof(ubo.model));
#endif
        // Copy view + projection matrices into the UBO
        #if defined(PLATFORM_WINDOWS)
            // asm MatrixCopy4x4F: 4 SSE MOVUPS for 64 bytes — no loop overhead
            MatrixCopy4x4F(glm::value_ptr(view), ubo.view);
            MatrixCopy4x4F(glm::value_ptr(proj), ubo.proj);
        #else
            std::memcpy(ubo.view, glm::value_ptr(view), sizeof(ubo.view));
            std::memcpy(ubo.proj, glm::value_ptr(proj), sizeof(ubo.proj));
        #endif
        ubo.camPos[0] = camPos.x; ubo.camPos[1] = camPos.y;
        ubo.camPos[2] = camPos.z;
        ubo.camPos[3] = m.m_modelInfo.receiveShadows ? 1.0f : 0.0f;         // w = receiveShadows flag (fragment shader)
        // Scale is baked into the world matrix for GLTF nodes; supply (1,1,1) so the
        // vertex shader's "scaledPos = inPos * ubo.scale.xyz" has no extra effect.
        ubo.scale[0] = 1.0f; ubo.scale[1] = 1.0f; ubo.scale[2] = 1.0f; ubo.scale[3] = 0.0f;

        // The transform UBO has two 256-byte slots selected by a dynamic offset:
        // slot 0 = main pass, slot 1 + plane = that plane's mirror pass (all live in the same command buffer).
        const uint32_t dynOffset = mirrorPass ? 256u * static_cast<uint32_t>(1 + mirrorPlane) : 0u;
        if (m.m_modelInfo.uniformBufferMapped)
        {
            void* uboDst = static_cast<uint8_t*>(m.m_modelInfo.uniformBufferMapped) + dynOffset;
            #if defined(PLATFORM_WINDOWS)
                // asm MemoryCopy: REP MOVSQ — uploads full 224-byte VKCameraUBO to mapped GPU memory
                MemoryCopy(&ubo, uboDst, sizeof(ubo));
            #else
                std::memcpy(uboDst, &ubo, sizeof(ubo));
            #endif
        }

        // Planar reflection mix + plane layer for this model (material UBO floats 19 / 20 == planarStrength / planarIndex).
        // Written in the main pass only; the mirror pass never draws reflectors, so the value is unused there.
        if (!mirrorPass && m.m_modelInfo.materialUniformBufferMapped)
        {
            const bool planarOn = g_planarFrame.active && m.m_modelInfo.planarPlaneIndex >= 0 && ModelIsPlanarReflector(m.m_modelInfo);
            float* matF = static_cast<float*>(m.m_modelInfo.materialUniformBufferMapped);
            matF[19] = planarOn ? std::clamp(m.m_modelInfo.planarStrength, 0.0f, 1.0f) : 0.0f;                  // planarStrength
            matF[20] = planarOn ? static_cast<float>(m.m_modelInfo.planarPlaneIndex) : 0.0f;                    // planarIndex

            // Emission setting: material UBO float 11 == emissiveStrength.  SceneManager uploads the authored value
            // once at load, so rewrite authored * EmissionScale() every frame (live Video-settings changes).
            if (!m.m_materials.empty())
                matF[11] = m.m_materials.begin()->second.emissiveStrength * config.myConfig.EmissionScale();
        }

        // ---- Bind set=0 (transform UBO + material UBO) ----
        if (m.m_modelInfo.descriptorSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_3dPipelineLayout, 0, 1,
                                    &m.m_modelInfo.descriptorSet, 1, &dynOffset);   // binding 0 is UNIFORM_BUFFER_DYNAMIC

        // ---- Bind set=1 (diffuse/normal/ORM/AO/gloss/emissive textures) ----
        // Use the per-model texture descriptor set if available; else the global 6-slot fallback.
        // Both sets cover all 6 bindings required by m_3dTexSetLayout.
        VkDescriptorSet texSet = (m.m_modelInfo.textureDescriptorSet != VK_NULL_HANDLE)
                                 ? m.m_modelInfo.textureDescriptorSet
                                 : m_defaultTexSetDescSet;
        if (texSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_3dPipelineLayout, 1, 1, &texSet, 0, nullptr);

        // ---- Draw indexed geometry ----
        // Guard on descriptorSet so we never draw with an uninitialised UBO binding.
        if (m.m_modelInfo.descriptorSet  != VK_NULL_HANDLE &&
            m.m_modelInfo.vertexBuffer   != VK_NULL_HANDLE &&
            m.m_modelInfo.indexBuffer    != VK_NULL_HANDLE &&
            !m.m_modelInfo.indices.empty())
        {
            VkBuffer     vbufs[]   = { m.m_modelInfo.vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
            vkCmdBindIndexBuffer(cmd, m.m_modelInfo.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m.m_modelInfo.indices.size()),
                             1, 0, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// PlanarPlanVK - registers every reflector's plane for this frame and decides whether the planar array is
// active (see "Planar Reflections" in Lights.h).  Runs BEFORE RenderShadowPassVK so the ShadowBuffer UBO
// written there already carries the planes.
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::PlanarPlanVK()
{
    PlanarBeginPlan();

    if (config.myConfig.planarEnabled && m_planarImage != VK_NULL_HANDLE &&
        threadManager.threadVars.bLoaderTaskFinished.load())
    {
        const glm::vec3 cp = myCamera.GetPosition();
        const float camPos[3] = { cp.x, cp.y, cp.z };
        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            Model& m = scene.scene_models[i];
            m.m_modelInfo.planarPlaneIndex = -1;
            if (!m.m_isLoaded || m.bIsDestroyed || m.m_modelInfo.bIsTransformProxy || m.m_modelInfo.bIsTransformOnly) continue;
            if (!ModelIsPlanarReflector(m.m_modelInfo)) continue;

            std::array<float, 16> w{};
            VkShadowWorldMatrix(m, w.data());
            const float one[3] = { 1.0f, 1.0f, 1.0f };                          // scale already in world
            ModelPlanarRegister(m.m_modelInfo, w.data(), one, camPos);
        }
    }

    PlanarFrameBegin(m_planarImage != VK_NULL_HANDLE,
                     static_cast<float>(m_swapchainExtent.width), static_cast<float>(m_swapchainExtent.height));
}

// ---------------------------------------------------------------------------------------------------------------
// RenderPlanarPassVK - planar reflection mirror renders (see "Planar Reflections" in Lights.h).
// Recorded BEFORE the main render pass.  For each plane: draws every non-reflector model through that plane's
// mirrored camera into its array layer with the main 3D pipeline (the planar render pass is compatible with
// m_renderPass).  Set=2 is the mirror copy whose binding 5 is a dummy texture, because a layer of the real
// planar array is a framebuffer attachment here.  Runs after RenderShadowPassVK (ShadowBuffer UBO + shadow
// maps ready).
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::RenderPlanarPassVK(VkCommandBuffer cmd, float deltaTime)
{
    (void)deltaTime;
    if (!g_planarFrame.active || !g_planarFrame.renderThisFrame) return;
    if (m_planarImage == VK_NULL_HANDLE || m_3dPipeline == VK_NULL_HANDLE || m_3dPipelineLayout == VK_NULL_HANDLE ||
        m_3dFrameSetsMirror[m_currentFrame] == VK_NULL_HANDLE)
        return;

    const glm::vec3 camPos = myCamera.GetPosition();
    const glm::mat4 view   = myCamera.GetViewMatrix();
    const glm::mat4 proj   = myCamera.GetProjectionMatrix();
    const float camArr[3]  = { camPos.x, camPos.y, camPos.z };

    for (int plane = 0; plane < g_planarFrame.planeCount; ++plane)
    {
        // Mirrored camera (glm bytes == row-vector float[16]; Vulkan depth is [0,1]).
        float mv[16], mp[16], rcam[3];
        PlanarBuildCamera(glm::value_ptr(view), glm::value_ptr(proj), camArr,
                          g_planarFrame.planes[plane].n, g_planarFrame.planes[plane].d, true, mv, mp, rcam);
        glm::mat4 mirrorView, mirrorProj;
        std::memcpy(glm::value_ptr(mirrorView), mv, sizeof(mv));
        std::memcpy(glm::value_ptr(mirrorProj), mp, sizeof(mp));

        VkClearValue clears[2];
        clears[0].color        = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
        clears[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_planarRenderPass;
        rp.framebuffer       = m_planarFramebuffers[plane];
        rp.renderArea.offset = { 0, 0 };
        rp.renderArea.extent = { static_cast<uint32_t>(m_planarW), static_cast<uint32_t>(m_planarH) };
        rp.clearValueCount   = 2;
        rp.pClearValues      = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{ 0.0f, 0.0f, static_cast<float>(m_planarW), static_cast<float>(m_planarH), 0.0f, 1.0f };
        VkRect2D   sc{ { 0, 0 }, { static_cast<uint32_t>(m_planarW), static_cast<uint32_t>(m_planarH) } };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_3dPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_3dPipelineLayout,
                                2, 1, &m_3dFrameSetsMirror[m_currentFrame], 0, nullptr);
        DrawModelsVK(cmd, mirrorView, mirrorProj, glm::vec3(rcam[0], rcam[1], rcam[2]), plane);

        vkCmdEndRenderPass(cmd);                                                // layer -> SHADER_READ_ONLY_OPTIMAL
    }
    g_planarFrame.hasImage = true;
}

// ---------------------------------------------------------------------------------------------------------------
// RenderReflectionCaptureVK - live scene capture (see "Live scene capture" in Lights.h).
// Captures ONE face of the capture cube per frame: the converted sky face is copied in as the background
// (staging buffer -> image), every model is drawn over it with a 90 degree camera at the capture point using
// the mirror copy of set=2 (sky probe at binding 4, dummy planar, planar-off ShadowBuffer), and after the
// sixth face the mip chain is rebuilt with a blit chain.  Recorded outside any render pass, after
// RenderShadowPassVK + RenderPlanarPassVK.
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::RenderReflectionCaptureVK(VkCommandBuffer cmd, float deltaTime)
{
    if (!g_reflectionFrame.active || !config.myConfig.reflectionLive || m_capFailed || m_capImage == VK_NULL_HANDLE ||
        m_reflProbe.size != m_reflSize || m_reflUploadedVersion == 0 ||
        m_3dFrameSetsMirror[m_currentFrame] == VK_NULL_HANDLE || !m_capStagingMapped[m_currentFrame] ||
        m_3dPipeline == VK_NULL_HANDLE || m_3dPipelineLayout == VK_NULL_HANDLE ||
        !threadManager.threadVars.bLoaderTaskFinished.load())
    {
        g_reflectionCapture.ready = false;
        return;
    }

    const glm::vec3 cp = myCamera.GetPosition();
    const float camPos[3] = { cp.x, cp.y, cp.z };
    int  face = 0;
    bool lastFace = false;
    if (!ReflectionCaptureNext(deltaTime, camPos, face, lastFace))
        return;

    const uint32_t size = static_cast<uint32_t>(m_reflSize);
    const uint32_t mips = static_cast<uint32_t>(m_reflMips);

    // ---- Sky mip 0 converted to the swapchain format (channel order + sRGB encode), refreshed when the sky changed ----
    if (m_capSkyConvVersion != m_reflProbe.version)
    {
        static float s_srgbLut[256];
        static bool  s_srgbInit = false;
        if (!s_srgbInit)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float c = i / 255.0f;
                const float e = (c <= 0.0031308f) ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
                s_srgbLut[i] = std::clamp(e, 0.0f, 1.0f) * 255.0f;
            }
            s_srgbInit = true;
        }
        for (int f = 0; f < 6; ++f)
        {
            const std::vector<uint8_t>& src = m_reflProbe.data[f][0];
            std::vector<uint8_t>& dst = m_capSkyConv[f];
            dst.resize(src.size());
            for (size_t px = 0; px + 3 < src.size(); px += 4)
            {
                uint8_t c[3] = { src[px], src[px + 1], src[px + 2] };
                if (m_capSRGB) for (int k = 0; k < 3; ++k) c[k] = static_cast<uint8_t>(s_srgbLut[c[k]] + 0.5f);
                dst[px]     = m_capBGR ? c[2] : c[0];
                dst[px + 1] = c[1];
                dst[px + 2] = m_capBGR ? c[0] : c[2];
                dst[px + 3] = 255;
            }
        }
        m_capSkyConvVersion = m_reflProbe.version;
    }
    const std::vector<uint8_t>& skyFace = m_capSkyConv[face];
    if (skyFace.size() < static_cast<size_t>(size) * size * 4u)
        return;
    std::memcpy(m_capStagingMapped[m_currentFrame], skyFace.data(), static_cast<size_t>(size) * size * 4u);

    auto layoutBarrier = [&](uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount,
                             VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA, VkAccessFlags dstA,
                             VkPipelineStageFlags srcS, VkPipelineStageFlags dstS)
    {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = oldL;
        b.newLayout           = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_capImage;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, baseLayer, layerCount };
        b.srcAccessMask       = srcA;
        b.dstAccessMask       = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // ---- 1. sky face -> capture face (mip 0) ----
    layoutBarrier(0, 1, static_cast<uint32_t>(face), 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(face), 1 };
    region.imageExtent       = { size, size, 1 };
    vkCmdCopyBufferToImage(cmd, m_capStaging[m_currentFrame], m_capImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // ---- 2. models over it ----
    float v[16], p[16];
    const float nearZ = std::max(static_cast<float>(config.myConfig.nearPlane), 0.05f);
    const float farZ  = std::max(static_cast<float>(config.myConfig.farPlane), nearZ + 1.0f);
    ReflectionCaptureFaceCamera(face, g_reflectionCapture.origin, nearZ, farZ, true, true, v, p);
    glm::mat4 capView, capProj;
    std::memcpy(glm::value_ptr(capView), v, sizeof(v));
    std::memcpy(glm::value_ptr(capProj), p, sizeof(p));

    VkClearValue clears[2];
    clears[0].color        = {{ 0.0f, 0.0f, 0.0f, 1.0f }};                    // ignored (loadOp LOAD)
    clears[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = m_capRenderPass;
    rp.framebuffer       = m_capFramebuffers[face];
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = { size, size };
    rp.clearValueCount   = 2;
    rp.pClearValues      = clears;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{ 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
    VkRect2D   sc{ { 0, 0 }, { size, size } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_3dPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_3dPipelineLayout,
                            2, 1, &m_3dFrameSetsMirror[m_currentFrame], 0, nullptr);
    DrawModelsVK(cmd, capView, capProj,
                 glm::vec3(g_reflectionCapture.origin[0], g_reflectionCapture.origin[1], g_reflectionCapture.origin[2]),
                 MAX_PLANAR_PLANES + face);
    vkCmdEndRenderPass(cmd);                                                   // face mip 0 -> SHADER_READ_ONLY_OPTIMAL

    // ---- 3. last face: rebuild mips 1.. from mip 0 with a blit chain ----
    if (lastFace)
    {
        if (mips > 1)
        {
            layoutBarrier(0, 1, 0, 6, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            layoutBarrier(1, mips - 1, 0, 6, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            for (uint32_t m = 1; m < mips; ++m)
            {
                const int32_t srcSize = static_cast<int32_t>(ReflectionMipSize(m_reflSize, static_cast<int>(m - 1)));
                const int32_t dstSize = static_cast<int32_t>(ReflectionMipSize(m_reflSize, static_cast<int>(m)));
                VkImageBlit blit{};
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 6 };
                blit.srcOffsets[1]  = { srcSize, srcSize, 1 };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 6 };
                blit.dstOffsets[1]  = { dstSize, dstSize, 1 };
                vkCmdBlitImage(cmd, m_capImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_capImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                if (m + 1 < mips)                                              // this mip is the next blit's source
                    layoutBarrier(m, 1, 0, 6, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }

            // Everything back to SHADER_READ_ONLY: mips 0..N-2 are TRANSFER_SRC, the last mip is TRANSFER_DST.
            layoutBarrier(0, mips - 1, 0, 6, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            layoutBarrier(mips - 1, 1, 0, 6, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        g_reflectionCapture.ready = true;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 3D scene rendering helper (inlined, called from RenderFrame)
// Mirrors DXRenderFrame.cpp::RenderGamePlay exactly:
//   - iterates scene.scene_models[] (scene instances, not the global cache models[])
//   - skips transform-proxy / transform-only nodes (no GPU geometry)
//   - sets fxActive = false per DX11 parity
//   - binds per-model diffuse texture when available; falls back to 1×1 white
//   - light push constant computed once per frame, pushed before the draw loop
// ---------------------------------------------------------------------------------------------------------------
inline void VulkanRenderer::RenderGamePlay(float deltaTime)
{
    if (!bIsInitialized.load()) return;
    if (m_3dPipeline == VK_NULL_HANDLE || m_3dPipelineLayout == VK_NULL_HANDLE) return;

    VkCommandBuffer cmd = m_frames[m_currentFrame].commandBuffer;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Main3DPipeline());   // main pass: multisampled variant when MSAA is active

    // Camera matrices — VulkanCamera returns GLM types on all platforms.
    // View:       VulkanCamera uses glm::lookAtLH (LH Y-up world).
    // Projection: Use the camera's pre-computed Vulkan LH ZO matrix built by SetupDefaultCamera().
    //             That matrix already incorporates FOV, near/far, and aspect ratio from GameConfig.cfg,
    //             and applies the Y-flip (proj[1][1] *= -1) required for Vulkan Y-down NDC.
    //             Hardcoded nearZ/farZ have been removed — values come from config.myConfig.nearPlane/farPlane.
    glm::vec3 camPos = myCamera.GetPosition();
    glm::mat4 view   = myCamera.GetViewMatrix();
    glm::mat4 proj   = myCamera.GetProjectionMatrix();

    if (!threadManager.threadVars.bLoaderTaskFinished.load()) return;

    // Animate lights (pulse / flicker / strobe) each frame before uploading
    // the light buffer — mirrors the DX11 and OpenGL render paths.
    lightsManager.AnimateLights(deltaTime);

    // ---- Per-frame lighting + shadows (set = 2) ----
    // GlobalLightBuffer: all scene lights (directional / point / spot), same layout as DX b3 /
    // GL binding 3.  Replaces the old single-directional-light push constant.  The ShadowBuffer
    // UBO for this frame was written by RenderShadowPassVK() before the render pass began.
    if (m_3dFrameSets[m_currentFrame] == VK_NULL_HANDLE || !m_lightUBOMapped[m_currentFrame])
        return;                                                                 // set=2 is required by the 3D pipeline
    {
        const std::vector<LightStruct> lights = lightsManager.GetAllLights();
        GlobalLightBuffer glb{};
        glb.numLights = std::min(static_cast<int>(lights.size()), MAX_GLOBAL_LIGHTS);
        for (int li = 0; li < glb.numLights; ++li)
            glb.lights[li] = lights[li];
        std::memcpy(m_lightUBOMapped[m_currentFrame], &glb, sizeof(GlobalLightBuffer));
    }
    const bool liveReady = g_reflectionCapture.ready && config.myConfig.reflectionLive && m_3dFrameSetsLive[m_currentFrame] != VK_NULL_HANDLE;
    const VkDescriptorSet frameSet = liveReady ? m_3dFrameSetsLive[m_currentFrame] : m_3dFrameSets[m_currentFrame];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_3dPipelineLayout,
                            2, 1, &frameSet, 0, nullptr);

    // ---- Render each scene-instanced model (mirrors DX11: scene.scene_models[]) ----
    DrawModelsVK(cmd, view, proj, camPos, -1);
}

// ---------------------------------------------------------------------------------------------------------------
// Intro movie rendering helper
// ---------------------------------------------------------------------------------------------------------------
inline void VulkanRenderer::RenderIntroMovie()
{
#if defined(PLATFORM_WINDOWS)
    if (!moviePlayer.IsPlaying() || !m_d2dRenderTarget) return;

    // Advance the decoder and audio pipeline
    moviePlayer.UpdateFrame();

    // Retrieve the most recently decoded BGRA frame from the CPU buffer
    uint32_t fw = 0, fh = 0;
    const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
    if (!frameData || fw == 0 || fh == 0) return;

    // Create or reuse the D2D bitmap for the video frame.
    // Use BGRA + pre-multiplied alpha — matches the MF ARGB32/BGRA output format.
    const D2D1_PIXEL_FORMAT pixFmt =
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    const UINT stride = fw * 4;

    if (!m_videoBitmap || m_videoBitmapWidth != fw || m_videoBitmapHeight != fh)
    {
        // Dimensions changed or first frame — (re)create the bitmap
        m_videoBitmap.Reset();
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(pixFmt);
        HRESULT hr = m_d2dRenderTarget->CreateBitmap(
            D2D1::SizeU(fw, fh), frameData, stride, props, &m_videoBitmap);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] RenderIntroMovie: CreateBitmap failed");
            return;
        }
        m_videoBitmapWidth  = fw;
        m_videoBitmapHeight = fh;
    }
    else
    {
        // Update existing bitmap in-place (avoids re-allocation every frame)
        D2D1_RECT_U updateRect = D2D1::RectU(0, 0, fw, fh);
        if (FAILED(m_videoBitmap->CopyFromMemory(&updateRect, frameData, stride))) return;
    }

    // Draw the video frame fullscreen
    D2D1_RECT_F dest = D2D1::RectF(0.0f, 0.0f,
                                    static_cast<float>(iOrigWidth),
                                    static_cast<float>(iOrigHeight));
    m_d2dRenderTarget->DrawBitmap(m_videoBitmap.Get(), dest, 1.0f,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

    // Company logo overlay at half size, bottom-left corner (mirrors DX11 RenderIntroMovie)
    if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]) {
        D2D1_SIZE_F logoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
        int halfW = static_cast<int>(logoSz.width  * 0.5f);
        int halfH = static_cast<int>(logoSz.height * 0.5f);
        if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
            fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - halfH, halfW, halfH);
        else
            Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
    }

    // Spacebar skip -- identical across all four renderers: the movie keeps playing
    // through the fade (no jarring freeze-frame cut) and is only stopped once
    // FadeOutThenCallback's own completion callback fires, so the fade duration
    // (1.0s) is the single source of truth instead of a guessed frame count.
    // Only active in SCENE_INTRO_MOVIE -- not the splash SCENE_INTRO.
    if (scene.stSceneType == SceneType::SCENE_INTRO_MOVIE && (GetAsyncKeyState(' ') & 0x8000) && !scene.bSceneSwitching)
    {
        scene.bSceneSwitching = true;
        fxManager.FadeOutThenCallback(XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f, 0.06f, []() {
            moviePlayer.Stop();
        });
    }

#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    if (moviePlayer.IsPlaying()) {
        uint32_t fw = 0, fh = 0;
        const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
        if (frameData && fw == m_overlayWidth && fh == m_overlayHeight) {
            void* mapped = nullptr;
            VkDeviceSize sz = static_cast<VkDeviceSize>(fw) * fh * 4;
            vkMapMemory(m_device, m_overlayStagingMemory, 0, sz, 0, &mapped);
            std::memcpy(mapped, frameData, static_cast<size_t>(sz));
            vkUnmapMemory(m_device, m_overlayStagingMemory);
            m_overlayDirty = true;
        }
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// RenderFrame  —  main render loop (mirrors DXRenderFrame.cpp structure)
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::RenderFrame()
{
    // ---- Safety guards ----
    if (m_bHasCleanedUp || m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE)
        return;

    if (threadManager.threadVars.bIsShuttingDown.load() ||
        bIsMinimized.load()                             ||
        threadManager.threadVars.bIsResizing.load()     ||
        !bIsInitialized.load())
        return;

    ThreadLockHelper exclusiveLock(threadManager, m_renderFrameLockName, 50);
    if (!exclusiveLock.IsLocked()) return;

    if (threadManager.threadVars.bIsRendering.load()) return;

    try
    {
        exceptionHandler.RecordFunctionCall("VulkanRenderer::RenderFrame");
        threadManager.threadVars.bIsRendering.store(true);

#ifdef RENDERER_IS_THREAD
        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
        while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
               (!threadManager.threadVars.bIsShuttingDown.load()))
        {
            status = threadManager.GetThreadStatus(THREAD_RENDERER);
            if (status == ThreadStatus::Paused) {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (threadManager.threadVars.bIsResizing.load() || bIsMinimized.load()) {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            threadManager.threadVars.bIsRendering.store(true);
#endif

            // ---- Delta time ----
            auto   now            = std::chrono::steady_clock::now();
            float  rawDelta       = std::chrono::duration<float>(now - lastFrameTime).count();
            float  deltaTime      = m_deltaTimeSmoothing.ProcessDelta(rawDelta, 60.0f);
            deltaTime             = std::clamp(deltaTime, 0.001f, 0.1f);
            lastFrameTime         = now;

            myCamera.UpdateJumpAnimation();

            #if defined(_DEBUG)
                const bool bCollectTiming = (scene.stSceneType == SceneType::SCENE_GAMETITLE) && IsTimingCaptureActive();
                const auto timingFrameStart = std::chrono::steady_clock::now();
                auto timingPhaseStart = timingFrameStart;
                Renderer::RenderTimingSample timingSample = {};
                #if defined(PLATFORM_WINDOWS)
                    timingSample.d2dAvailable = (m_d2dRenderTarget != nullptr);
                #else
                    timingSample.d2dAvailable = true;
                #endif
                timingSample.screenRecorderActive = screenRecorder.IsRecording();
                auto timingMs = [](const std::chrono::steady_clock::time_point& start,
                                   const std::chrono::steady_clock::time_point& end) -> double {
                    return std::chrono::duration<double, std::milli>(end - start).count();
                };
            #endif

            // ---- Acquire swap chain image ----
            auto& fd = m_frames[m_currentFrame];
            {
                VkResult waitResult = vkWaitForFences(m_device, 1, &fd.inFlightFence, VK_TRUE, UINT64_MAX);
                if (waitResult == VK_ERROR_DEVICE_LOST) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR,
                        L"[VulkanRenderer] VK_ERROR_DEVICE_LOST in vkWaitForFences — stopping render loop");
                    threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
                    break;
#else
                    return;
#endif
                }
            }

            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingAfterWait = std::chrono::steady_clock::now();
                    timingSample.waitPreviousMs = timingMs(timingPhaseStart, timingAfterWait);
                    timingPhaseStart = timingAfterWait;
                }
            #endif

            // Free descriptor sets deferred from the previous use of this frame slot
            if (!fd.pendingFreeSets.empty()) {
                vkFreeDescriptorSets(m_device, m_descriptorPool,
                    static_cast<uint32_t>(fd.pendingFreeSets.size()),
                    fd.pendingFreeSets.data());
                fd.pendingFreeSets.clear();
            }

            uint32_t imageIndex = 0;
            VkResult acquireResult = vkAcquireNextImageKHR(
                m_device, m_swapchain, UINT64_MAX,
                fd.imageAvailable, VK_NULL_HANDLE, &imageIndex);

            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
                acquireResult == VK_ERROR_SURFACE_LOST_KHR) {
                if (!threadManager.threadVars.bIsResizing.load())
                    RecreateSwapChain(static_cast<uint32_t>(m_renderTargetWidth),
                                      static_cast<uint32_t>(m_renderTargetHeight));
                threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
                continue;
#else
                return;
#endif
            }
            if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
                threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
                continue;
#else
                return;
#endif
            }
            m_currentImageIndex = imageIndex;
            vkResetFences(m_device, 1, &fd.inFlightFence);

            // ---- Begin command buffer ----
            VkCommandBuffer cmd = fd.commandBuffer;
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            vkBeginCommandBuffer(cmd, &bi);

            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingAfterReset = std::chrono::steady_clock::now();
                    timingSample.resetMs = timingMs(timingPhaseStart, timingAfterReset);
                    timingPhaseStart = timingAfterReset;
                }
            #endif

            // ---- Upload 2D overlay (Windows: D2D, Linux/Android: CPU buffer) ----
            #if defined(PLATFORM_WINDOWS)
                // Begin D2D drawing for this frame
                if (m_d2dRenderTarget) {
                    m_d2dRenderTarget->BeginDraw();
                    m_d2dRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // transparent clear
                }
            #endif

            // Render background image to the D2D overlay (or CPU buffer on Linux/Android)
            RenderBackgroundImage();

            // Scene-specific D2D overlay content
            #if defined(PLATFORM_WINDOWS)
            if (m_d2dRenderTarget) {
                switch (scene.stSceneType) {
                    case SceneType::SCENE_INTRO:
                        if (moviePlayer.IsPlaying())
                            RenderIntroMovie(); // also draws the video frame to the D2D overlay    
    
                        /* if (m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)]) {
                            if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_SPLASH1)))
                                fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_SPLASH1), 0, 0, iOrigWidth, iOrigHeight);
                            else
                                Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                        } */
                        break;

                    case SceneType::SCENE_INTRO_MOVIE:
                        if (moviePlayer.IsPlaying())
                            RenderIntroMovie();
                        break;

                    case SceneType::SCENE_GAMETITLE:
                        // Loading image when assets not yet ready (mirrors DX11/DX12)
                        if (!threadManager.threadVars.bLoaderTaskFinished.load() &&
                            m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                        {
                            // Consume (but no longer act on) the legacy black-fade-in trigger --
                            // the end-of-load pixel fader now owns the loading-screen reveal.
                            if (threadManager.threadVars.bInitiateFader.load())
                                threadManager.threadVars.bInitiateFader.store(false);
                            if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                                fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                            else
                                Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                        }
                        
                        fxManager.RenderLoadingText();
                        break;

                    case SceneType::SCENE_GAMEPLAY:
                        // Loading image while assets are loading (mirrors DX11/DX12)
                        if (!threadManager.threadVars.bLoaderTaskFinished.load() &&
                            m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                        {
                            // Consume (but no longer act on) the legacy black-fade-in trigger --
                            // the end-of-load pixel fader now owns the loading-screen reveal.
                            if (threadManager.threadVars.bInitiateFader.load())
                                threadManager.threadVars.bInitiateFader.store(false);
                            if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                                fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                            else
                                Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                        }
                        break;
                    default: break;
                }

                // FPS / debug overlay
                if (USE_FPS_DISPLAY && config.myConfig.showDebugInfo) {
                    static auto lastFPSTime   = std::chrono::steady_clock::now();
                    static int  fpsCounter    = 0;
                    auto        currentTime   = std::chrono::steady_clock::now();
                    float       elapsedForFPS = std::chrono::duration<float>(currentTime - lastFPSTime).count();
                    fpsCounter++;
                    if (elapsedForFPS >= 1.0f) {
                        m_fps        = static_cast<float>(fpsCounter) / elapsedForFPS;
                        fpsCounter   = 0;
                        lastFPSTime  = currentTime;
                    }
                    glm::vec3 coords = myCamera.GetPosition();
                    std::wstring fpsText =
                        L"FPS: " + std::to_wstring(m_fps) +
                        L"\nMOUSE: x" + std::to_wstring(myMouseCoords.x) + L", y" + std::to_wstring(myMouseCoords.y) +
                        L"\nClient Width: " + std::to_wstring(iOrigWidth) + L", Client Height:" + std::to_wstring(iOrigHeight) +
                        L"\nCamera X: " + std::to_wstring(coords.x) + L", Y: " + std::to_wstring(coords.y) +
                        L", Z: " + std::to_wstring(coords.z) +
                        L", Yaw: " + std::to_wstring(myCamera.m_yaw) + L", Pitch: " + std::to_wstring(myCamera.m_pitch) +
                        L"\nGlobal Lights: " + std::to_wstring(lightsManager.GetLightCount());
                    DrawMyText(fpsText, Vector2(5.0f, 5.0f), MyColor(255, 255, 255, 255), 10.0f);
                }

                // Debug OSD toggle notification
                if (bDebugOSDActive) {
                    float osdElapsed = std::chrono::duration<float>(
                        std::chrono::steady_clock::now() - debugOSDStartTime).count();
                    if (osdElapsed < 5.0f) {
                        std::wstring osdMsg = config.myConfig.showDebugInfo
                            ? L"=> Debug Info: ENABLED" : L"=> Debug Info: DISABLED";
                        DrawMyText(osdMsg, Vector2(10.0f, 80.0f), MyColor(255, 220, 0, 255), 14.0f);
                    } else { bDebugOSDActive = false; }
                }

                #if defined(_DEBUG)
                    if (bTimingOSDActive) {
                        float timingOsdElapsed = std::chrono::duration<float>(
                            std::chrono::steady_clock::now() - timingOSDStartTime).count();
                        if (timingOsdElapsed < 5.0f)
                            DrawMyText(timingOSDMessage, Vector2(10.0f, 104.0f), MyColor(255, 220, 0, 255), 14.0f);
                        else
                            bTimingOSDActive = false;
                    }
                #endif

                // ── Renderer info overlay (bottom-right) ─────────────────────────────
                // Mirrors DXRenderFrame.cpp: "CPGE Windows Vulkan v0.0.XXXX"
                if (USE_RENDERER_INFO && !scene.bSceneSwitching && m_d2dRenderTarget && m_dwriteFactory)
                {
                    bool riShow = (scene.stSceneType == SceneType::SCENE_GAMETITLE  ||
                                   scene.stSceneType == SceneType::SCENE_GAMEPLAY   ||
                                   scene.stSceneType == SceneType::SCENE_INTRO      ||
                                   scene.stSceneType == SceneType::SCENE_INTRO_MOVIE||
                                   scene.stSceneType == SceneType::SCENE_GAMEOVER);
                    #if defined(_DEBUG)
                        riShow = riShow || (scene.stSceneType == SceneType::SCENE_EXPERIMENT);
                    #endif

                    if (riShow)
                    {
                        const float riFontSize = std::clamp(
                            static_cast<float>(iOrigHeight) / 72.0f, 10.0f, 16.0f);

                        // e.g. "Debug CPGE v0.0.1723 15-06-2026"
                        static const std::wstring buildDate = []() -> std::wstring {
                            const char* d = __DATE__; // "Mmm DD YYYY", e.g. "Jun 15 2026"
                            const char* m = "JanFebMarAprMayJunJulAugSepOctNovDec";
                            int mon = 1;
                            for (int i = 0; i < 12; i++) {
                                if (d[0]==m[i*3] && d[1]==m[i*3+1] && d[2]==m[i*3+2]) { mon=i+1; break; }
                            }
                            int day  = (d[4]==' ') ? (d[5]-'0') : ((d[4]-'0')*10+(d[5]-'0'));
                            int year = (d[7]-'0')*1000+(d[8]-'0')*100+(d[9]-'0')*10+(d[10]-'0');
                            wchar_t buf[12];
                            swprintf_s(buf, 12, L"%02d-%02d-%04d", day, mon, year);
                            return std::wstring(buf);
                        }();
                        const std::wstring riText =
                            std::wstring(BUILD_TYPE_W L" " RENDERER_NAME_W L" " GAME_NAME_W L" v") +
                            std::to_wstring(CURRENT_BUILD_VERSION)    + L"." +
                            std::to_wstring(CURRENT_BUILD_SUBVERSION) + L"." +
                            std::to_wstring(CURRENT_BUILD)            + L" " +
                            buildDate;

                        IDWriteTextFormat* riFmt = GetOrCreateTextFormat(FontName, riFontSize);
                        if (riFmt)
                        {
                            ComPtr<IDWriteTextLayout> riLayout;
                            HRESULT riHr = m_dwriteFactory->CreateTextLayout(
                                riText.c_str(), static_cast<UINT32>(riText.size()),
                                riFmt,
                                static_cast<float>(iOrigWidth),
                                riFontSize * 2.0f,
                                &riLayout);

                            if (SUCCEEDED(riHr) && riLayout)
                            {
                                DWRITE_TEXT_METRICS riMetrics = {};
                                riLayout->GetMetrics(&riMetrics);

                                const float riX = static_cast<float>(iOrigWidth)  - riMetrics.width;
                                const float riY = static_cast<float>(iOrigHeight) - riMetrics.height;

                                ComPtr<ID2D1SolidColorBrush> ribrush;
                                m_d2dRenderTarget->CreateSolidColorBrush(
                                    D2D1::ColorF(220.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f),
                                    &ribrush);
                                if (ribrush)
                                    m_d2dRenderTarget->DrawTextLayout(
                                        D2D1::Point2F(riX, riY),
                                        riLayout.Get(),
                                        ribrush.Get());
                            }
                        }
                    }
                }

                // Loading ring animation (only while loading)
                if (!threadManager.threadVars.bLoaderTaskFinished.load()) {
                    m_delay++;
                    if (m_delay > 3) { m_loadIndex = (m_loadIndex + 1) % 10; m_delay = 0; }
                    if (m_d2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)]) {
                        m_iPosX = m_loadIndex << 5;
                        Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE,
                                             iOrigWidth - 34, iOrigHeight - 45,
                                             m_iPosX, 0, 32, 32);
                    }
                }
                // Loading text: for SCENE_GAMETITLE it is rendered unconditionally above
                // (matches DX11/OpenGL); for all other scenes use the conditional guard.
                if (scene.stSceneType != SceneType::SCENE_GAMETITLE &&
                    (!threadManager.threadVars.bLoaderTaskFinished.load() ||
                     fxManager.HasActiveLoadingTextEffects()))
                {
                    fxManager.RenderLoadingText();
                }

                // FX 2D effects (scrollers, particles)
                try { fxManager.Render2D(); } catch (...) {} // intentional silent swallow (per-frame path)

                // 3D warp dot tunnel (projects to 2D overlay via Blit2DColoredPixel)
                if (fxManager.tunnelID > 0) {
                    try { fxManager.RenderFX(fxManager.tunnelID, cmd, myCamera.GetViewMatrix()); } catch (...) {} // intentional silent swallow (per-frame path)
                }

                // Background image zoom + 3D starfield — both composite BEFORE 3D models.
                // IMG_GAMEINTRO1 zoom goes into the bg overlay so the D2D crop blit lands under
                // the 3D ship; starfield renders on top of the background image.
                // Neither is shown during the loader phase.
                bool bGametitleLoaded = (scene.stSceneType == SceneType::SCENE_GAMETITLE &&
                                         threadManager.threadVars.bLoaderTaskFinished.load());
                bool bGameintroZoom   = (bGametitleLoaded &&
                                         fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)));
                if ((fxManager.starfieldID > 0 || bGameintroZoom) && bGametitleLoaded)
                {
                    if (m_bgD2dRenderTarget)
                    {
                        m_bgD2dRenderTarget->BeginDraw();
                        m_bgD2dRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                        m_drawToBackground = true;
                        // Zoomed background image first — starfield appears on top
                        if (bGameintroZoom)
                            fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                        // Starfield
                        if (fxManager.starfieldID > 0)
                            try { fxManager.RenderFX(fxManager.starfieldID, cmd, myCamera.GetViewMatrix()); } catch (...) {} // intentional silent swallow (per-frame path)

                        // Fireworks rendered into the bg overlay (m_drawToBackground still true)
                        // so they composite BEFORE 3D models and BEFORE the TSOO blit.
                        fxManager.RenderFireworks();
                        m_drawToBackground = false;

                        int startX = (iOrigWidth - 536) / 2; // Centered horizontally
                        int startY = (iOrigHeight - 466) / 2; // Centered vertically
                        if (fxManager.IsImageFadeStrobeActive(BlitObj2DIndexType::IMG_TSOO))
                            fxManager.RenderImageFadeStrobe(BlitObj2DIndexType::IMG_TSOO, startX, startY, 536, 466);
                        else
                            Blit2DObjectToSize(BlitObj2DIndexType::IMG_TSOO, startX, startY, 536, 466);

                        m_bgD2dRenderTarget->EndDraw();
                        m_bgOverlayDirty = true;
                    }
                    else
                    {
                        if (fxManager.starfieldID > 0)
                            try { fxManager.RenderFX(fxManager.starfieldID, cmd, myCamera.GetViewMatrix()); } catch (...) {} // intentional silent swallow (per-frame path)
                    }
                }

                // GUI windows
                try { guiManager.Render(); } catch (...) {} // intentional silent swallow (per-frame path)

                // Console window rendering is now handled by GUIManager::Render()
                // via the GUIWindow::onCustomRender callback set in ConsoleWindow::CreateInGUIManager().

                // Cursor
                if (m_d2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)])
                    Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR,
                                 static_cast<int>(myMouseCoords.x), static_cast<int>(myMouseCoords.y));

                // REC indicator
                if (screenRecorder.IsRecording()) {
                    static int recBlink = 0;
                    recBlink = (recBlink + 1) % 60;
                    if (recBlink < 30)
                        DrawMyText(L"* REC",
                                   Vector2(static_cast<float>(m_renderTargetWidth) - 75.0f, 12.0f),
                                   MyColor::Red(), 18.0f);
                }

                // End D2D drawing
                m_d2dRenderTarget->EndDraw();
                m_overlayDirty = true;
            } // m_d2dRenderTarget
            #elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
                // Linux/Android: scene 2D and GUI rendered via CPU rasterizer (stub)
                try { fxManager.Render2D(); } catch (...) {} // intentional silent swallow (per-frame path)
                try { guiManager.Render();  } catch (...) {} // intentional silent swallow (per-frame path)
            #endif

            // Upload overlay textures to GPU (if dirty)
            #if defined(PLATFORM_WINDOWS)
                UploadOverlayToVulkan(cmd);
                UploadBgOverlayToVulkan(cmd);
            #endif

            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingAfterOverlayUpload = std::chrono::steady_clock::now();
                    timingSample.d2dOverlayMs = timingMs(timingPhaseStart, timingAfterOverlayUpload);
                    timingPhaseStart = timingAfterOverlayUpload;
                }
            #endif

            // ---- 3D pre-pass: animation update + shadow depth passes ----
            // Must be recorded BEFORE the main render pass begins (render passes cannot nest).
            // UpdateAnimations moved here from the RenderGamePlay switch below (CPU-only, same
            // per-scene conditions) so the shadow casters use THIS frame's world matrices.
            {
                const bool bLoaded = threadManager.threadVars.bLoaderTaskFinished.load();
                if (scene.stSceneType == SceneType::SCENE_GAMETITLE)
                {
                    if (bLoaded)
                        scene.modelAnimator.UpdateAnimations(deltaTime);
                    PlanarPlanVK();
                    RenderShadowPassVK(cmd, deltaTime);
                    RenderPlanarPassVK(cmd, deltaTime);
                    RenderReflectionCaptureVK(cmd, deltaTime);
                }
                else if (scene.stSceneType == SceneType::SCENE_GAMEPLAY)
                {
                    scene.modelAnimator.UpdateAnimations(deltaTime);
                    PlanarPlanVK();
                    RenderShadowPassVK(cmd, deltaTime);
                    RenderPlanarPassVK(cmd, deltaTime);
                    RenderReflectionCaptureVK(cmd, deltaTime);
                }
            }

            // ---- Begin render pass ----
            VkClearValue clearValues[2];
            clearValues[0].color = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            // With MSAA the framebuffers were built for the multisampled pass (resolve into the swapchain image)
            rpBegin.renderPass        = (m_renderPassMS != VK_NULL_HANDLE && m_msaaColorView != VK_NULL_HANDLE) ? m_renderPassMS : m_renderPass;
            rpBegin.framebuffer       = m_framebuffers[imageIndex];
            rpBegin.renderArea.offset = { 0, 0 };
            rpBegin.renderArea.extent = m_swapchainExtent;
            rpBegin.clearValueCount   = 2;
            rpBegin.pClearValues      = clearValues;

            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            // Dynamic viewport and scissor
            VkViewport viewport{};
            viewport.x        = 0.0f;
            viewport.y        = 0.0f;
            viewport.width    = static_cast<float>(m_swapchainExtent.width);
            viewport.height   = static_cast<float>(m_swapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{ { 0, 0 }, m_swapchainExtent };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // ---- Scene background: rendered BEFORE 3D models so geometry appears in front ----
            // In DX11 the render order is: D2D overlay first, then 3D geometry draws OVER it.
            // In Vulkan the D2D overlay composites AFTER 3D, so a fullscreen background blit
            // via D2D would hide the models.  Fix: draw the background GPU texture as the first
            // quad in the render pass via the 2D pipeline, then let the 3D pipeline render on top.
            // D2D carries only UI elements (company logo, HUD, cursor) on a transparent canvas.
            if (m_2dPipeline != VK_NULL_HANDLE && m_2dPipelineLayout != VK_NULL_HANDLE)
            {
                int bgIdx = -1;
                if (scene.stSceneType == SceneType::SCENE_GAMETITLE &&
                    threadManager.threadVars.bLoaderTaskFinished.load())
                    bgIdx = int(BlitObj2DIndexType::IMG_GAMEINTRO1);

                // Skip GPU blit when zoom is active — the D2D bg-overlay path handles it instead
                bool bBgZoomActive = (bgIdx >= 0 && fxManager.IsImageZoomActive(bgIdx));
                if (bgIdx >= 0 && bgIdx < MAX_TEXTURE_BUFFERS && m_textures2D[bgIdx].isValid && !bBgZoomActive)
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Main2DPipeline());

                    VkDescriptorSetAllocateInfo bgDsai{};
                    bgDsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                    bgDsai.descriptorPool     = m_descriptorPool;
                    bgDsai.descriptorSetCount = 1;
                    bgDsai.pSetLayouts        = &m_textureDescSetLayout;
                    VkDescriptorSet bgSet = VK_NULL_HANDLE;
                    if (vkAllocateDescriptorSets(m_device, &bgDsai, &bgSet) == VK_SUCCESS)
                    {
                        VkDescriptorImageInfo bgImg{};
                        bgImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        bgImg.imageView   = m_textures2D[bgIdx].view;
                        bgImg.sampler     = m_textures2D[bgIdx].sampler;
                        VkWriteDescriptorSet bgWr{};
                        bgWr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        bgWr.dstSet          = bgSet;
                        bgWr.dstBinding      = 0;
                        bgWr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        bgWr.descriptorCount = 1;
                        bgWr.pImageInfo      = &bgImg;
                        vkUpdateDescriptorSets(m_device, 1, &bgWr, 0, nullptr);

                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_2dPipelineLayout, 0, 1, &bgSet, 0, nullptr);
                        float bgPc[7] = {
                            static_cast<float>(m_renderTargetWidth),
                            static_cast<float>(m_renderTargetHeight),
                            0.0f, 0.0f,
                            static_cast<float>(m_renderTargetWidth),
                            static_cast<float>(m_renderTargetHeight),
                            1.0f
                        };
                        vkCmdPushConstants(cmd, m_2dPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                           0, sizeof(bgPc), bgPc);
                        VkBuffer     bgVB[]  = { m_quadVertexBuffer };
                        VkDeviceSize bgOff[] = { 0 };
                        vkCmdBindVertexBuffers(cmd, 0, 1, bgVB, bgOff);
                        vkCmdDraw(cmd, 4, 1, 0, 0);
                        fd.pendingFreeSets.push_back(bgSet);
                    }
                }
            }

            // ---- Background overlay composite (starfield) — BEFORE 3D models ----
            // Composited here so starfield dots appear behind 3D scene geometry on SCENE_GAMETITLE,
            // matching the DX11 draw order where D2D writes to the backbuffer before 3D renders over it.
#if defined(PLATFORM_WINDOWS)
            if (m_2dPipeline != VK_NULL_HANDLE && m_bgOverlayTexture.isValid &&
                scene.stSceneType == SceneType::SCENE_GAMETITLE &&
                threadManager.threadVars.bLoaderTaskFinished.load())
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Main2DPipeline());

                VkDescriptorSetAllocateInfo sfDsai{};
                sfDsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                sfDsai.descriptorPool     = m_descriptorPool;
                sfDsai.descriptorSetCount = 1;
                sfDsai.pSetLayouts        = &m_textureDescSetLayout;
                VkDescriptorSet sfSet = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(m_device, &sfDsai, &sfSet) == VK_SUCCESS)
                {
                    VkDescriptorImageInfo sfImg{};
                    sfImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    sfImg.imageView   = m_bgOverlayTexture.view;
                    sfImg.sampler     = m_bgOverlayTexture.sampler;
                    VkWriteDescriptorSet sfWr{};
                    sfWr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    sfWr.dstSet          = sfSet;
                    sfWr.dstBinding      = 0;
                    sfWr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    sfWr.descriptorCount = 1;
                    sfWr.pImageInfo      = &sfImg;
                    vkUpdateDescriptorSets(m_device, 1, &sfWr, 0, nullptr);

                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_2dPipelineLayout, 0, 1, &sfSet, 0, nullptr);
                    float sfPc[7] = {
                        static_cast<float>(m_renderTargetWidth),
                        static_cast<float>(m_renderTargetHeight),
                        0.0f, 0.0f,
                        static_cast<float>(m_renderTargetWidth),
                        static_cast<float>(m_renderTargetHeight),
                        1.0f
                    };
                    vkCmdPushConstants(cmd, m_2dPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(sfPc), sfPc);
                    VkBuffer     sfVB[]  = { m_quadVertexBuffer };
                    VkDeviceSize sfOff[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, sfVB, sfOff);
                    vkCmdDraw(cmd, 4, 1, 0, 0);
                    fd.pendingFreeSets.push_back(sfSet);
                }
            }
#endif

            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingAfterBackgroundPass = std::chrono::steady_clock::now();
                    timingSample.backgroundPrePassMs = timingMs(timingPhaseStart, timingAfterBackgroundPass);
                    timingSample.backgroundPrePass = true;
                    timingPhaseStart = timingAfterBackgroundPass;
                }
            #endif

            // ---- 3D scene rendering (RenderGamePlay) ----
            // Rendered after the background GPU-texture blit and starfield overlay but BEFORE the
            // main D2D overlay composite, so the UI overlay (GUI, console, cursor, HUD) always
            // appears in front of 3D scene geometry — matching the visual intent for SCENE_GAMETITLE
            // and SCENE_GAMEPLAY where the console (F8) must not be occluded by 3D models.
            // UpdateAnimations runs in the 3D pre-pass above (before the shadow depth passes) so
            // GLTF hierarchy world matrices are always valid here (static base-pose and animated clips alike).
            switch (scene.stSceneType) {
                case SceneType::SCENE_GAMETITLE:
                    RenderGamePlay(deltaTime);
                    break;
                case SceneType::SCENE_GAMEPLAY:
                    RenderGamePlay(deltaTime);
                    break;
                default: break;
            }

            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingAfter3D = std::chrono::steady_clock::now();
                    timingSample.commandRecordMs = timingMs(timingPhaseStart, timingAfter3D);
                    timingPhaseStart = timingAfter3D;
                }
            #endif

            // ---- 2D overlay composite — drawn AFTER 3D models so UI is always in front ----
            // The D2D overlay (transparent canvas) carries: starfield dots, tunnel dots, FPS HUD,
            // loading ring, loading text, GUI windows, F8 console window, custom cursor, REC badge.
            // Compositing AFTER RenderGamePlay guarantees all UI elements appear on top of 3D
            // geometry.  The background image (IMG_GAMEINTRO1) is handled separately above via a
            // dedicated GPU-texture blit that executes before 3D.
            if (m_2dPipeline != VK_NULL_HANDLE && m_overlayTexture.isValid) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Main2DPipeline());

                VkDescriptorSetAllocateInfo dsai{};
                dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                dsai.descriptorPool     = m_descriptorPool;
                dsai.descriptorSetCount = 1;
                dsai.pSetLayouts        = &m_textureDescSetLayout;
                VkDescriptorSet ds = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(m_device, &dsai, &ds) == VK_SUCCESS) {
                    VkDescriptorImageInfo imgInfo{};
                    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfo.imageView   = m_overlayTexture.view;
                    imgInfo.sampler     = m_overlayTexture.sampler;
                    VkWriteDescriptorSet write{};
                    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet          = ds;
                    write.dstBinding      = 0;
                    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.descriptorCount = 1;
                    write.pImageInfo      = &imgInfo;
                    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_2dPipelineLayout, 0, 1, &ds, 0, nullptr);

                    float pc[7] = {
                        static_cast<float>(m_renderTargetWidth),
                        static_cast<float>(m_renderTargetHeight),
                        0.0f, 0.0f,
                        static_cast<float>(m_renderTargetWidth),
                        static_cast<float>(m_renderTargetHeight),
                        1.0f
                    };
                    vkCmdPushConstants(cmd, m_2dPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(pc), pc);

                    VkBuffer vbufs[] = { m_quadVertexBuffer };
                    VkDeviceSize offsets[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
                    vkCmdDraw(cmd, 4, 1, 0, 0);

                    fd.pendingFreeSets.push_back(ds);
                }
            }

            // ---- FX fullscreen effects (fades, etc.) — drawn last so they overlay everything ----
            try { fxManager.Render(); } catch (...) {} // intentional silent swallow (per-frame path)

            vkCmdEndRenderPass(cmd);
            vkEndCommandBuffer(cmd);

            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingAfterCommandRecord = std::chrono::steady_clock::now();
                    timingSample.d2dOverlayMs += timingMs(timingPhaseStart, timingAfterCommandRecord);
                    timingPhaseStart = timingAfterCommandRecord;
                }
            #endif

            // ---- Submit ----
            VkSemaphore          waitSems[]   = { fd.imageAvailable };
            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            // renderFinished is indexed by swapchain imageIndex (not by currentFrame) to avoid
            // VUID-vkQueueSubmit-pSignalSemaphores-00067 when the presentation engine is still
            // waiting on the semaphore from a previous frame that used the same image.
            VkSemaphore          signalSems[] = { m_renderFinishedSemaphores[imageIndex] };

            VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.waitSemaphoreCount   = 1;
            submit.pWaitSemaphores      = waitSems;
            submit.pWaitDstStageMask    = waitStages;
            submit.commandBufferCount   = 1;
            submit.pCommandBuffers      = &cmd;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores    = signalSems;
            // Submit and present must share the queue mutex: on most GPUs the present
            // queue is the same VkQueue handle as the graphics queue, so vkQueuePresentKHR
            // races with loader-thread vkQueueSubmit calls if not serialised together.
            // Submit render work (separated from present to allow screen capture between them)
            {
                std::lock_guard<std::mutex> qlock(m_queueMutex);
                vkQueueSubmit(m_graphicsQueue, 1, &submit, fd.inFlightFence);
            }

            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingAfterSubmit = std::chrono::steady_clock::now();
                    timingSample.execute3DMs = timingMs(timingPhaseStart, timingAfterSubmit);
                    timingPhaseStart = timingAfterSubmit;
                }
            #endif

#if defined(PLATFORM_WINDOWS)
            // Screen recording: capture the swap chain image before handing it to the WSI.
            // We wait for the render fence so the GPU has finished writing the image.
            // The fence is intentionally NOT reset here — it stays signalled until the
            // start of the next iteration for this slot where vkResetFences() resets it.
            //
            // m_commandPool is used (NOT m_loaderCommandPool) because:
            //   • m_commandPool belongs exclusively to the render thread — no race with the loader.
            //   • m_loaderCommandPool is used concurrently by the loader thread for texture uploads;
            //     Vulkan command pools are not thread-safe, so sharing them across threads without
            //     external synchronisation produces undefined behaviour and validation errors.
            if (screenRecorder.IsRecording())
            {
                vkWaitForFences(m_device, 1, &fd.inFlightFence, VK_TRUE, UINT64_MAX);
                screenRecorder.CaptureFrame(
                    m_device, m_physicalDevice,
                    m_commandPool, m_graphicsQueue,
                    m_swapchainImages[imageIndex],
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    m_swapchainFormat,
                    m_swapchainExtent.width, m_swapchainExtent.height,
                    &m_queueMutex);
            }
#endif

            VkResult presentResult = VK_SUCCESS;
            #if defined(_DEBUG)
                if (bCollectTiming)
                    timingPhaseStart = std::chrono::steady_clock::now();
            #endif
            {
                std::lock_guard<std::mutex> qlock(m_queueMutex);
                VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
                present.waitSemaphoreCount = 1;
                present.pWaitSemaphores    = signalSems;
                present.swapchainCount     = 1;
                present.pSwapchains        = &m_swapchain;
                present.pImageIndices      = &imageIndex;
                presentResult = vkQueuePresentKHR(m_presentQueue, &present);
            }

            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
                presentResult == VK_SUBOPTIMAL_KHR          ||
                presentResult == VK_ERROR_SURFACE_LOST_KHR) {
                if (!threadManager.threadVars.bIsResizing.load())
                    RecreateSwapChain(static_cast<uint32_t>(m_renderTargetWidth),
                                      static_cast<uint32_t>(m_renderTargetHeight));
            }

            #if defined(_DEBUG)
                const auto timingAfterPresent = std::chrono::steady_clock::now();
                if (bCollectTiming)
                {
                    timingSample.presentMs = timingMs(timingPhaseStart, timingAfterPresent);
                    timingPhaseStart = timingAfterPresent;
                }
            #endif

            // Advance frame index
            m_currentFrame = (m_currentFrame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    const auto timingFrameEnd = std::chrono::steady_clock::now();
                    timingSample.moveNextMs = timingMs(timingPhaseStart, timingFrameEnd);
                    timingSample.totalMs = timingMs(timingFrameStart, timingFrameEnd);
                    RecordTimingSample(timingSample);
                }
            #endif
            threadManager.threadVars.bIsRendering.store(false);

#ifdef RENDERER_IS_THREAD
        } // while loop
#endif
    }
    catch (const std::exception& e)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[VulkanRenderer::RenderFrame] Exception: " +
            std::wstring(e.what(), e.what() + std::strlen(e.what())));
        threadManager.threadVars.bIsRendering.store(false);
    }

    threadManager.threadVars.bIsRendering.store(false);
}

#endif // __USE_VULKAN__
