// -------------------------------------------------------------------------------------------------------------
// linux-vulkan-render.cpp — Linux Vulkan implementation
// -------------------------------------------------------------------------------------------------------------
// Platform-specific implementation of VulkanRenderer, split out of VULKAN_Renderer.cpp /
// VULKAN_RenderFrame.cpp (platform split, 24 Nov 2026) so that working on one platform
// means working in one file. This file is the platform: it contains NO platform guards
// and is compiled only on its platform (Linux/CMakeLists.txt, CrossPlatformGameEngine.vcxproj).
// The code below is moved verbatim from the shared files; behaviour is unchanged.
// -------------------------------------------------------------------------------------------------------------
#define NOMINMAX
#include "Includes.h"
#include "linux-vulkan-render.h"

#if defined(__USE_VULKAN__)

// External globals used by the moved code (mirrors VULKAN_Renderer.cpp / VULKAN_RenderFrame.cpp).
extern Debug debug;
extern ExceptionHandler exceptionHandler;
extern SystemUtils sysUtils;
extern ThreadManager threadManager;
extern SceneManager scene;
extern Configuration config;
extern GUIManager guiManager;
extern FXManager fxManager;
extern WindowMetrics winMetrics;
extern LightsManager lightsManager;
extern Model models[MAX_MODELS];
extern ScreenRecorder screenRecorder;
extern MoviePlayer moviePlayer;
extern Vector2 myMouseCoords;
extern std::atomic<bool> bResizeInProgress;
extern std::atomic<bool> bFullScreenTransition;

void VulkanRenderer::RenderBackgroundImage()
{
    // Background clear handled by the Vulkan render-pass clear colour
}

// -------------------------------------------------------------------------------------------------------------
// Phase C native Vulkan 2D blits (Linux, 25 Nov 2026)
//
// Blits are issued from RenderPlatformOverlay(), which runs BEFORE the main
// render pass begins, so they cannot draw immediately: each body below
// validates its arguments, converts its source rect to float UVs from
// m_textures2D[idx].width/height, and appends one quad (two for the wrapped
// scroll) to a per-frame queue via EnqueueLinuxBlit(). ReplayQueued2DBlits()
// draws the queue inside the pass. Behaviour mirrors the Windows D2D
// reference (windows-vulkan-render.cpp) and the OpenGL float-UV reference
// (OpenGLRenderer::Render2DQuadUV): zoom clamped 0-0.75 with a float centre
// crop (never rounded, so the zoom glides without 1-pixel stutter);
// scrolling is a two-part wrapped split (the 2D sampler in
// CreateTextureFromRGBA is REPEAT, but the split is the safe path either
// way and matches Windows); atlas tiles are row-major with the out-of-range
// guard. Positions stay unit-quad 0..1 in the vertex data, so the existing
// 2D vertex shader and 7-float push-constant layout work unchanged — the
// source sub-rect is baked into the per-vertex UVs.
//
// Draw cost note: the 2D pipeline is a triangle strip with per-quad push
// constants (dest rect), so each queued quad is its own vkCmdDraw(4) —
// descriptor sets are still shared per distinct texture per frame (one
// allocation per texture, freed via fd.pendingFreeSets like the
// background/overlay quads). A single-draw batched triangle-list pipeline
// is a follow-up if draw-call overhead ever matters; the tilemap worst
// case (~8,540 quads at 1920x1080/16px) fits the queue cap.
// -------------------------------------------------------------------------------------------------------------

// Lazily create the per-frame-in-flight dynamic vertex buffers for the
// queued blit quads (host-visible + coherent, persistently mapped). Called
// from the replay step, so no blit ever allocates per frame — the vectors
// are reserved once here and clear()ed per frame.
void VulkanRenderer::EnsureLinuxBlitBuffers()
{
    if (m_linuxBlitVBReady || m_device == VK_NULL_HANDLE) return;
    const VkDeviceSize vbSize = static_cast<VkDeviceSize>(kLinuxBlitMaxQuads) * 4u * sizeof(VkVertex2D);
    for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
    {
        CreateBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_linuxBlitVB[f], m_linuxBlitVBMem[f]);
        if (m_linuxBlitVB[f] == VK_NULL_HANDLE || m_linuxBlitVBMem[f] == VK_NULL_HANDLE)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] Linux blit vertex buffer creation failed — 2D blits disabled this run");
            DestroyLinuxBlitBuffers();
            return;
        }
        if (vkMapMemory(m_device, m_linuxBlitVBMem[f], 0, vbSize, 0, &m_linuxBlitVBMapped[f]) != VK_SUCCESS ||
            m_linuxBlitVBMapped[f] == nullptr)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] Linux blit vertex buffer mapping failed — 2D blits disabled this run");
            DestroyLinuxBlitBuffers();
            return;
        }
    }
    m_linuxBlitQueueBg.reserve(kLinuxBlitMaxQuads);
    m_linuxBlitQueueOverlay.reserve(kLinuxBlitMaxQuads);
    m_linuxBlitVBReady = true;
}

void VulkanRenderer::DestroyLinuxBlitBuffers()
{
    if (m_device == VK_NULL_HANDLE) return;
    for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
    {
        if (m_linuxBlitVBMapped[f] != nullptr) { vkUnmapMemory(m_device, m_linuxBlitVBMem[f]); m_linuxBlitVBMapped[f] = nullptr; }
        if (m_linuxBlitVB[f]    != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_linuxBlitVB[f], nullptr);    m_linuxBlitVB[f]    = VK_NULL_HANDLE; }
        if (m_linuxBlitVBMem[f] != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_linuxBlitVBMem[f], nullptr);   m_linuxBlitVBMem[f] = VK_NULL_HANDLE; }
    }
    m_linuxBlitVBReady = false;
}

// Append one quad to the background queue (m_drawToBackground, e.g. the
// zoomed IMG_GAMEINTRO1) or the overlay queue (everything else). Overflow
// drops the extra quad with a throttled warning — never a crash, never a
// reallocation mid-frame (the vectors are reserved at first use).
void VulkanRenderer::EnqueueLinuxBlit(int texIndex, float dx, float dy, float dw, float dh,
                                      float u0, float v0, float u1, float v1,
                                      float r, float g, float b, float a)
{
    if (m_device == VK_NULL_HANDLE || dw <= 0.0f || dh <= 0.0f) return;
    std::vector<LinuxBlitQuad>& queue = m_drawToBackground ? m_linuxBlitQueueBg : m_linuxBlitQueueOverlay;
    if (queue.size() >= kLinuxBlitMaxQuads)
    {
        ++m_linuxBlitDropped;
        if (m_linuxBlitDropped == 1 || (m_linuxBlitDropped % 600) == 0)
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] Linux blit queue full — dropping 2D blits (" +
                std::to_wstring(m_linuxBlitDropped) + L" dropped so far)");
        return;
    }
    LinuxBlitQuad q{};
    q.texIndex = texIndex;
    q.dx = dx; q.dy = dy; q.dw = dw; q.dh = dh;
    q.u0 = u0; q.v0 = v0; q.u1 = u1; q.v1 = v1;
    q.r = r; q.g = g; q.b = b; q.a = a;
    queue.push_back(q);
}

// Draw one queued queue inside the main render pass. Uploads the quads as
// 4-vert triangle-strip quads into the current frame-in-flight dynamic
// vertex buffer, binds the 2D pipeline, and draws each quad with its own
// push constants (screen size, dest rect, opacity 1 — alpha travels in the
// vertex colour). Descriptor sets are allocated once per distinct texture
// per replay and freed via fd.pendingFreeSets, exactly like the background
// and overlay quads. The background replay clears only the background
// queue; the overlay replay clears both, so no quad ever survives into the
// next frame.
void VulkanRenderer::ReplayQueued2DBlits(VkCommandBuffer cmd, bool background)
{
    std::vector<LinuxBlitQuad>& queue = background ? m_linuxBlitQueueBg : m_linuxBlitQueueOverlay;
    if (queue.empty())
    {
        if (!background) m_linuxBlitQueueBg.clear();
        return;
    }
    if (m_device == VK_NULL_HANDLE || m_2dPipeline == VK_NULL_HANDLE ||
        m_2dPipelineLayout == VK_NULL_HANDLE || m_descriptorPool == VK_NULL_HANDLE)
    {
        queue.clear();
        if (!background) m_linuxBlitQueueBg.clear();
        return;
    }
    EnsureLinuxBlitBuffers();
    if (!m_linuxBlitVBReady)
    {
        queue.clear();
        if (!background) m_linuxBlitQueueBg.clear();
        return;
    }

    const uint32_t frame = m_currentFrame;
    VkVertex2D* verts = static_cast<VkVertex2D*>(m_linuxBlitVBMapped[frame]);
    for (size_t i = 0; i < queue.size(); ++i)
    {
        const LinuxBlitQuad& q = queue[i];
        VkVertex2D* v = &verts[i * 4];
        // Unit-quad positions (0..1) with the source sub-rect baked into the
        // UVs — the vertex shader maps position through the push constants.
        v[0] = { 0.0f, 0.0f, q.u0, q.v0, q.r, q.g, q.b, q.a };
        v[1] = { 1.0f, 0.0f, q.u1, q.v0, q.r, q.g, q.b, q.a };
        v[2] = { 0.0f, 1.0f, q.u0, q.v1, q.r, q.g, q.b, q.a };
        v[3] = { 1.0f, 1.0f, q.u1, q.v1, q.r, q.g, q.b, q.a };
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Main2DPipeline());
    VkBuffer     vb[]  = { m_linuxBlitVB[frame] };
    VkDeviceSize off[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vb, off);

    // One descriptor set per distinct texture in this replay, cached locally.
    struct BoundTex { int texIndex; VkDescriptorSet set; };
    BoundTex bound[16];
    uint32_t boundCount = 0;
    VulkanFrameData& fd = m_frames[frame];

    for (size_t i = 0; i < queue.size(); ++i)
    {
        const LinuxBlitQuad& q = queue[i];
        VkDescriptorSet set = VK_NULL_HANDLE;
        for (uint32_t b = 0; b < boundCount; ++b)
            if (bound[b].texIndex == q.texIndex) { set = bound[b].set; break; }
        if (set == VK_NULL_HANDLE)
        {
            // New texture in this replay: allocate one set for it (unless
            // the local cache is full — then fall back to rebinding the
            // default texture's set, which is always valid).
            const VulkanTexture& tex = (q.texIndex >= 0) ? m_textures2D[q.texIndex] : m_defaultTexture;
            if (!tex.isValid) continue;
            VkDescriptorSetAllocateInfo dsai{};
            dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsai.descriptorPool     = m_descriptorPool;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts        = &m_textureDescSetLayout;
            VkDescriptorSet fresh = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(m_device, &dsai, &fresh) != VK_SUCCESS) continue;
            VkDescriptorImageInfo imgInfo{};
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgInfo.imageView   = tex.view;
            imgInfo.sampler     = tex.sampler;
            VkWriteDescriptorSet wr{};
            wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr.dstSet          = fresh;
            wr.dstBinding      = 0;
            wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr.descriptorCount = 1;
            wr.pImageInfo      = &imgInfo;
            vkUpdateDescriptorSets(m_device, 1, &wr, 0, nullptr);
            fd.pendingFreeSets.push_back(fresh);
            if (boundCount < 16) { bound[boundCount++] = { q.texIndex, fresh }; }
            set = fresh;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_2dPipelineLayout, 0, 1, &set, 0, nullptr);
        // Push constants are the existing 7 floats; opacity stays 1.0 and
        // the quad alpha travels in the vertex colour (see LinuxBlitQuad).
        float pc[7] = {
            static_cast<float>(m_renderTargetWidth),
            static_cast<float>(m_renderTargetHeight),
            q.dx, q.dy, q.dw, q.dh,
            1.0f
        };
        vkCmdPushConstants(cmd, m_2dPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), pc);
        vkCmdDraw(cmd, 4, 1, static_cast<uint32_t>(i * 4), 0);
    }

    queue.clear();
    if (!background) m_linuxBlitQueueBg.clear();
}

void VulkanRenderer::Blit2DColoredPixel(int x, int y, float pixelSize, const Vector4& color)
{
    if (pixelSize <= 0.0f) return;
    // Draw against the 1x1 white m_defaultTexture (texIndex -1): the 2D
    // fragment shader multiplies texture x vertex colour, so white x colour
    // = colour. Colour components are 0..1 floats (XMFLOAT4 on Linux).
    EnqueueLinuxBlit(-1,
        static_cast<float>(x), static_cast<float>(y), pixelSize, pixelSize,
        0.0f, 0.0f, 1.0f, 1.0f,
        std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f),
        std::clamp(color.z, 0.0f, 1.0f), std::clamp(color.w, 0.0f, 1.0f));
}

void VulkanRenderer::Blit2DCenteredZoom(BlitObj2DIndexType iIndex, int iDestX, int iDestY, int iDestW, int iDestH, float zoomFactor)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS || !m_textures2D[idx].isValid) return;
    if (iDestW <= 0 || iDestH <= 0) return;
    // Clamp zoom factor to valid range (0.0-0.75), mirroring Windows.
    float z = std::clamp(zoomFactor, 0.0f, 0.75f);
    // Float centre crop — never rounded (OpenGL reference), so the zoom
    // glides without 1-pixel stutter.
    float texW = static_cast<float>(m_textures2D[idx].width);
    float texH = static_cast<float>(m_textures2D[idx].height);
    float srcW = texW * (1.0f - z);
    float srcH = texH * (1.0f - z);
    float srcX = (texW - srcW) * 0.5f;
    float srcY = (texH - srcH) * 0.5f;
    EnqueueLinuxBlit(idx,
        static_cast<float>(iDestX), static_cast<float>(iDestY),
        static_cast<float>(iDestW), static_cast<float>(iDestH),
        srcX / texW, srcY / texH, (srcX + srcW) / texW, (srcY + srcH) / texH,
        1.0f, 1.0f, 1.0f, 1.0f);
}

void VulkanRenderer::Blit2DScrollingObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float scrollFraction, bool reverseDirection)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS || !m_textures2D[idx].isValid) return;
    if (iWidth <= 0 || iHeight <= 0) return;
    float texW = static_cast<float>(m_textures2D[idx].width);
    float texH = static_cast<float>(m_textures2D[idx].height);
    if (texW <= 0.0f || texH <= 0.0f) return;
    // Wrapped scroll offset as a fraction, kept in float (no integer
    // truncation) so the scroll moves smoothly. Two-part split, mirroring
    // Windows: part 1 is source columns [xOff, 1) at the left of the dest
    // rect, part 2 wraps source columns [0, xOff) to the right of part 1.
    float wrappedFrac = scrollFraction - std::floor(scrollFraction);
    float xOffFrac = reverseDirection ? wrappedFrac : (1.0f - wrappedFrac);
    xOffFrac -= std::floor(xOffFrac);
    float destW1 = (1.0f - xOffFrac) * static_cast<float>(iWidth);
    EnqueueLinuxBlit(idx,
        static_cast<float>(iX), static_cast<float>(iY), destW1, static_cast<float>(iHeight),
        xOffFrac, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f);
    if (destW1 < static_cast<float>(iWidth))
    {
        EnqueueLinuxBlit(idx,
            static_cast<float>(iX) + destW1, static_cast<float>(iY),
            static_cast<float>(iWidth) - destW1, static_cast<float>(iHeight),
            0.0f, 0.0f, xOffFrac, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void VulkanRenderer::Blit2DAtlasTile(BlitObj2DIndexType iIndex, int iTileIndex, int iTileSizeX, int iTileSizeY, int iDestX, int iDestY)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS || !m_textures2D[idx].isValid) return;
    if (iTileIndex < 0 || iTileSizeX <= 0 || iTileSizeY <= 0) return;
    int texW = static_cast<int>(m_textures2D[idx].width);
    int texH = static_cast<int>(m_textures2D[idx].height);
    if (texW <= 0 || texH <= 0) return;
    int tilesPerRow = texW / iTileSizeX;
    int tilesPerCol = texH / iTileSizeY;
    if (tilesPerRow <= 0 || tilesPerCol <= 0) return;
    int tileCol = iTileIndex % tilesPerRow;
    int tileRow = iTileIndex / tilesPerRow;
    if (tileRow >= tilesPerCol) return;                                        // Out-of-range index — guard against corrupt map data
    Blit2DObjectAtOffset(iIndex, iDestX, iDestY, tileCol * iTileSizeX, tileRow * iTileSizeY, iTileSizeX, iTileSizeY);
}

void VulkanRenderer::Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY,
                                           int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS || !m_textures2D[idx].isValid) return;
    if (iTileSizeX <= 0 || iTileSizeY <= 0 || iXOffset < 0 || iYOffset < 0) return;
    float texW = static_cast<float>(m_textures2D[idx].width);
    float texH = static_cast<float>(m_textures2D[idx].height);
    // Clamp the source rect to the texture so a bad offset cannot sample
    // outside it (the sampler is REPEAT, so unclamped UVs would wrap).
    float srcX = std::min(static_cast<float>(iXOffset), texW);
    float srcY = std::min(static_cast<float>(iYOffset), texH);
    float srcW = std::min(static_cast<float>(iTileSizeX), texW - srcX);
    float srcH = std::min(static_cast<float>(iTileSizeY), texH - srcY);
    if (srcW <= 0.0f || srcH <= 0.0f) return;
    EnqueueLinuxBlit(idx,
        static_cast<float>(iBlitX), static_cast<float>(iBlitY), srcW, srcH,
        srcX / texW, srcY / texH, (srcX + srcW) / texW, (srcY + srcH) / texH,
        1.0f, 1.0f, 1.0f, 1.0f);
}

void VulkanRenderer::Blit2DObjectToSizeWithAlpha(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float alpha)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS || !m_textures2D[idx].isValid) return;
    if (iWidth <= 0 || iHeight <= 0) return;
    EnqueueLinuxBlit(idx,
        static_cast<float>(iX), static_cast<float>(iY),
        static_cast<float>(iWidth), static_cast<float>(iHeight),
        0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, std::clamp(alpha, 0.0f, 1.0f));
}

void VulkanRenderer::Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS || !m_textures2D[idx].isValid) return;
    if (iWidth <= 0 || iHeight <= 0) return;
    EnqueueLinuxBlit(idx,
        static_cast<float>(iX), static_cast<float>(iY),
        static_cast<float>(iWidth), static_cast<float>(iHeight),
        0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f);
}

void VulkanRenderer::Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS || !m_textures2D[idx].isValid) return;
    EnqueueLinuxBlit(idx,
        static_cast<float>(iX), static_cast<float>(iY),
        static_cast<float>(m_textures2D[idx].width),
        static_cast<float>(m_textures2D[idx].height),
        0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f);
}

void VulkanRenderer::DrawTexture(int textureId, const Vector2& position, const Vector2& size,
                                  const MyColor& tintColor, bool is2D)
{
    // Mirrors Windows: only 2D textures draw here (3D textures have their
    // own path). Tint RGB travels in the vertex colour, tint alpha in the
    // vertex alpha — the same quad path as the blits, no special case.
    if (!is2D) return;
    if (textureId < 0 || textureId >= MAX_TEXTURE_BUFFERS || !m_textures2D[textureId].isValid) return;
    if (size.x <= 0.0f || size.y <= 0.0f) return;
    EnqueueLinuxBlit(textureId,
        position.x, position.y, size.x, size.y,
        0.0f, 0.0f, 1.0f, 1.0f,
        tintColor.r / 255.0f, tintColor.g / 255.0f, tintColor.b / 255.0f, tintColor.a / 255.0f);
}

void VulkanRenderer::DrawMyTextStyled(const std::wstring& text, const Vector2& position,
                                       const MyColor& color, const TextRenderStyle& style)
{
    (void)text; (void)position; (void)color; (void)style;
}

void VulkanRenderer::DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color,
                                         const float FontSize, const std::wstring& fontName)
{
    (void)text; (void)position; (void)color; (void)FontSize; (void)fontName;
}

void VulkanRenderer::DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color,
                                         const float FontSize, float controlWidth, float controlHeight, bool /*bold*/)
{
    (void)text; (void)position; (void)color; (void)FontSize; (void)controlWidth; (void)controlHeight;
}

void VulkanRenderer::DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize)
{
    (void)text; (void)position; (void)size; (void)color; (void)FontSize;
}

void VulkanRenderer::DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize)
{
    (void)text; (void)position; (void)color; (void)FontSize;
}

void VulkanRenderer::CopyPlatformBgOverlayToStaging()
{
    // Linux/Android: the background overlay is never drawn (no D2D starfield),
    // so m_bgOverlayDirty stays false and this returns above; the shared
    // upload tail below is kept for symmetry with UploadOverlayToVulkan.
}

void VulkanRenderer::CopyPlatformOverlayToStaging()
{
    // Linux/Android: no D2D/WIC — the staging buffer already holds CPU RGBA
    // (movie frames are copied in by VULKAN_RenderFrame.cpp); upload it as-is.
}

void VulkanRenderer::PopClipRect() {
}

void VulkanRenderer::PushClipRect(float x, float y, float w, float h) {
    (void)x; (void)y; (void)w; (void)h;
}

void VulkanRenderer::DrawCurve(float startX, float startY, float ctrlX, float ctrlY, float endX, float endY,
                                const MyColor& color, float thickness, bool is2D) {
    (void)startX; (void)startY; (void)ctrlX; (void)ctrlY; (void)endX; (void)endY;
    (void)color; (void)thickness; (void)is2D;
}

void VulkanRenderer::DrawFilledPolygonBatch(const Vector2* const* polys, const int* counts, int n, const MyColor& color) {
    for (int i = 0; i < n; ++i) DrawFilledPolygon(polys[i], counts[i], color);
}

void VulkanRenderer::DrawFilledPolygon(const Vector2* points, int count, const MyColor& color) {
    // Non-Windows Vulkan has no 2D overlay surface yet: defer to the portable scanline fallback.
    Renderer::DrawFilledPolygon(points, count, color);
}

void VulkanRenderer::DrawCircle(const Vector2& center, float radius, const MyColor& color, bool filled) {
    (void)center; (void)radius; (void)color; (void)filled;
}

void VulkanRenderer::DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D)
{
    if (!is2D) return;
    // Stub: software render to staging buffer — implement with CPU rasterizer or FreeType
    (void)position; (void)size; (void)color;
}

void VulkanRenderer::ReleasePlatformD2DTexture(int textureId)
{
}

bool VulkanRenderer::LoadTextureFromFilePlatform(const std::wstring& filename, bool is2D, int textureId, const std::filesystem::path& fullPath)
{
    // Use stb_image if available (included at file scope above; the
    // implementation lives in VulkanModels.cpp).
    #if __has_include("stb_image.h")
        int w, h, ch;
        std::string path = fullPath.string();
        uint8_t* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
        if (!pixels) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] stb_image failed to load: " + filename);
            return false;
        }
        VulkanTexture tex = CreateTextureFromRGBA(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        stbi_image_free(pixels);
        if (!tex.isValid) return false;
        if (is2D) { DestroyVulkanTexture(m_textures2D[textureId]); m_textures2D[textureId] = tex; }
        else       { DestroyVulkanTexture(m_textures3D[textureId]); m_textures3D[textureId] = tex; }
        return true;
    #else
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"[VulkanRenderer] stb_image not found. Cannot load texture on Linux/Android: " + filename);
        return false;
    #endif
}

bool VulkanRenderer::SetWindowedScreen()
{
    return true;
}

bool VulkanRenderer::SetFullExclusive(uint32_t width, uint32_t height)
{
    return Resize(width, height);
}

bool VulkanRenderer::SetFullScreen()
{
    return true; // handled by the platform windowing system
}

void VulkanRenderer::AcquireFullScreenExclusiveIfAvailable()
{
}

void VulkanRenderer::ReleaseFullScreenExclusiveIfActive()
{
}

void VulkanRenderer::ResetPlatformOverlayTargets()
{
}

void VulkanRenderer::RecreateSurface()
{
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    CreateSurface(reinterpret_cast<HWND>(static_cast<uintptr_t>(m_xcbWindow)));
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] Surface recreated after loss.");
}

void VulkanRenderer::CreatePlatformBackgroundOverlay(uint32_t width, uint32_t height, VkDeviceSize bufSize)
{
}

void VulkanRenderer::CreatePlatformBackgroundTarget(uint32_t width, uint32_t height, bool overlayOk)
{
}

bool VulkanRenderer::CreatePlatformOverlayTargets(uint32_t width, uint32_t height)
{
    // No WIC/D2D/DWrite on Linux: overlay setup is vacuously OK.
    return true;
}

bool VulkanRenderer::GetPlatformVSyncEnabled() const
{
    return true;
}

void VulkanRenderer::AppendPlatformDeviceExtensions(std::vector<const char*>& enabledExtensions)
{
}

void VulkanRenderer::CreateSurface(NativeWindowHandle hwnd)
{
    VkXcbSurfaceCreateInfoKHR ci{};
    ci.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    ci.connection = m_xcbConnection;
    ci.window     = m_xcbWindow;
    if (vkCreateXcbSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create XCB Vulkan surface.");


    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Surface created.");
}

void VulkanRenderer::ProbePlatformInstanceExtensions(std::vector<const char*>& extensions)
{
}

void VulkanRenderer::AppendPlatformSurfaceExtension(std::vector<const char*>& extensions)
{
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
}

void VulkanRenderer::ReleasePlatformD2DTextures()
{
}

void VulkanRenderer::ReleasePlatformOverlayResources()
{
    // Destroy the Phase C blit vertex buffers (created lazily by
    // EnsureLinuxBlitBuffers). Called from Cleanup() while the device is
    // still alive; DestroyLinuxBlitBuffers() guards on m_device.
    DestroyLinuxBlitBuffers();
}

void VulkanRenderer::RestorePlatformDisplayMode()
{
}

void VulkanRenderer::HidePlatformMouseCursor()
{
}

void VulkanRenderer::InitPlatformWindow(NativeWindowHandle hwnd, NativeInstanceHandle hInstance)
{
    // hwnd carries xcb_window_t, hInstance carries xcb_connection_t*
    m_xcbWindow     = static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(hwnd));
    m_xcbConnection = reinterpret_cast<xcb_connection_t*>(hInstance);
}

void VulkanRenderer::InitPlatformDetails()
{
    osDetails.Platform.isLinux = true;
}

void VulkanRenderer::CompositePlatformBackgroundOverlay(VkCommandBuffer cmd, VulkanFrameData& fd)
{
    (void)cmd; (void)fd; // Unused on Linux/Android.
}

void VulkanRenderer::UploadPlatformOverlays(VkCommandBuffer cmd)
{
    // Linux: the overlay staging buffer is filled on the CPU (movie frames
    // in RenderIntroMovie; the D2D/WIC path does not exist here), so upload
    // it through the shared UploadOverlayToVulkan, mirroring Windows. Both
    // uploads early-return when their texture is invalid or not dirty, so
    // this is a no-op on frames nothing wrote. (Before Phase C this was a
    // no-op, which meant a staged movie frame could never reach the GPU.)
    UploadOverlayToVulkan(cmd);
    UploadBgOverlayToVulkan(cmd);
}

void VulkanRenderer::RenderPlatformOverlay(VkCommandBuffer cmd)
{
    (void)cmd; // Unused on Linux/Android.
    // Intro movie — mirrors the Windows RenderPlatformOverlay cases: the
    // movie frame is copied into the overlay staging buffer here (pre-pass)
    // and uploaded by UploadPlatformOverlays() right after this call.
    // RenderIntroMovie() itself guards on IsPlaying / paused playback.
    if (scene.stSceneType == SceneType::SCENE_INTRO ||
        scene.stSceneType == SceneType::SCENE_INTRO_MOVIE)
    {
        if (moviePlayer.IsPlaying())
            RenderIntroMovie();
    }
    if (gameplayPlaybackActive)
        RenderIntroMovie();

    // Stale movie frame clear (Phase C, 25 Nov 2026): when a movie stops,
    // the overlay texture still holds its last decoded frame, and the
    // shared frame draws that texture fullscreen every frame. On Windows
    // the D2D overlay is re-cleared to transparent each frame, but on
    // Linux nothing rewrites the staging buffer — the stale frame (opaque,
    // alpha 255) would otherwise cover the GameTitle background and the
    // 3D scene for the rest of the run (proven: the GameTitle background
    // matched the movie's last frame at 56% bright-pixel overlap, static
    // across captures). Clear the staging buffer to transparent black
    // once, on the first frame after the movie stops; UploadPlatformOverlays
    // (called right after this) uploads it. Paused gameplay playback keeps
    // its frame. This lives here — not in RenderIntroMovie — because that
    // function is only called during movie scenes, so a clear placed there
    // never runs once the scene has changed to GameTitle.
    {
        static bool s_movieWasPlaying = false;
        bool moviePlaying = moviePlayer.IsPlaying() ||
            (gameplayPlaybackActive && moviePlayer.IsPaused());
        if (moviePlaying)
        {
            s_movieWasPlaying = true;
        }
        else if (s_movieWasPlaying)
        {
            s_movieWasPlaying = false;
            if (m_overlayWidth > 0 && m_overlayHeight > 0 &&
                m_overlayStagingMemory != VK_NULL_HANDLE)
            {
                VkDeviceSize dstSz = static_cast<VkDeviceSize>(m_overlayWidth) * m_overlayHeight * 4;
                void* mapped = nullptr;
                if (vkMapMemory(m_device, m_overlayStagingMemory, 0, dstSz, 0, &mapped) == VK_SUCCESS &&
                    mapped != nullptr)
                {
                    std::memset(mapped, 0, static_cast<size_t>(dstSz));
                    vkUnmapMemory(m_device, m_overlayStagingMemory);
                    m_overlayDirty = true;
                }
            }
        }
    }

    // GameTitle background zoom + TSOO fade strobe — mirrors the Windows
    // RenderPlatformOverlay background block. The zoomed IMG_GAMEINTRO1 is
    // routed to the background blit queue (m_drawToBackground) so it replays
    // before the 3D models; without this the shared frame skips the plain
    // background blit while zoom is active (bBgZoomActive) and nothing would
    // draw the background at all. The TSOO strobe stays on the overlay
    // queue (in front of 3D), as on Windows.
    if (scene.stSceneType == SceneType::SCENE_GAMETITLE &&
        threadManager.threadVars.bLoaderTaskFinished.load())
    {
        if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)))
        {
            m_drawToBackground = true;
            fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
            m_drawToBackground = false;
        }
        int startX = (iOrigWidth - 536) / 2; // Centered horizontally
        int startY = (iOrigHeight - 466) / 2; // Centered vertically
        if (fxManager.IsImageFadeStrobeActive(BlitObj2DIndexType::IMG_TSOO))
            fxManager.RenderImageFadeStrobe(BlitObj2DIndexType::IMG_TSOO, startX, startY, 536, 466);
        else
            Blit2DObjectToSize(BlitObj2DIndexType::IMG_TSOO, startX, startY, 536, 466);
    }

    // Scene 2D and GUI rendered via the native Vulkan blit queue (Phase C).
    // On the loaded GameTitle screen the Render2D blits (starfield dots,
    // fireworks particles) are routed to the background queue so they sit
    // behind the 3D models, as the Windows background overlay does; in every
    // other scene they stay on the overlay queue (in front of 3D).
    bool bg2D = (scene.stSceneType == SceneType::SCENE_GAMETITLE &&
                 threadManager.threadVars.bLoaderTaskFinished.load());
    m_drawToBackground = bg2D;
    try { fxManager.Render2D(); } catch (...) {} // intentional silent swallow (per-frame path)
    m_drawToBackground = false;
    try { guiManager.Render();  } catch (...) {} // intentional silent swallow (per-frame path)
}

void VulkanRenderer::BeginPlatformOverlayDraw()
{
}

bool VulkanRenderer::HasPlatformOverlayTarget() const
{
    return true;
}

inline void VulkanRenderer::RenderIntroMovie()
{
    if (!moviePlayer.IsPlaying() && !(gameplayPlaybackActive && moviePlayer.IsPaused())) return;

    // Advance the decoder and audio pipeline (mirrors Windows Vulkan /
    // OpenGL RenderIntroMovie). Without this the FFmpeg decoder never
    // advances, never reaches EOF, IsPlaying() stays true forever, and the
    // main-loop transition out of SCENE_INTRO_MOVIE (main.cpp, requires
    // !moviePlayer.IsPlaying()) never fires — the engine sat in the movie
    // scene for the whole 300 s Phase C run 6. One-line port omission.
    moviePlayer.UpdateFrame();

    uint32_t fw = 0, fh = 0;
    const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
    if (!frameData || fw == 0 || fh == 0) return;
    if (m_overlayWidth == 0 || m_overlayHeight == 0 || m_overlayStagingMemory == VK_NULL_HANDLE) return;

    // One-time diagnostic: the frame size the decoder actually delivers vs
    // the overlay size. The FFmpeg scaler targets iOrigWidth/iOrigHeight,
    // but a null renderer or a config change can still deliver the native
    // video size (test1.mp4 is 1280x720 vs the 800x600 overlay).
    static bool s_movieSizeLogged = false;
    if (!s_movieSizeLogged)
    {
        s_movieSizeLogged = true;
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[VulkanRenderer] RenderIntroMovie: frame " + std::to_wstring(fw) + L"x" + std::to_wstring(fh) +
            L", overlay " + std::to_wstring(m_overlayWidth) + L"x" + std::to_wstring(m_overlayHeight));
    }

    void* mapped = nullptr;
    VkDeviceSize dstSz = static_cast<VkDeviceSize>(m_overlayWidth) * m_overlayHeight * 4;
    if (vkMapMemory(m_device, m_overlayStagingMemory, 0, dstSz, 0, &mapped) != VK_SUCCESS || mapped == nullptr) return;
    if (fw == m_overlayWidth && fh == m_overlayHeight)
    {
        // Exact-match fast path — copy the BGRA frame as-is (the overlay
        // texture is B8G8R8A8, matching the decoder's BGRA output).
        std::memcpy(mapped, frameData, static_cast<size_t>(dstSz));
    }
    else
    {
        // Dimension gate fix (Phase C, 25 Nov 2026): the old code uploaded
        // ONLY on an exact size match, so a 1280x720 movie on an 800x600
        // overlay dropped EVERY frame and the screen stayed black. Scale
        // the decoded BGRA frame into the staging buffer instead
        // (nearest-neighbour: cheap, no extra dependency, fine for a
        // full-screen movie frame at these sizes).
        for (uint32_t y = 0; y < m_overlayHeight; ++y)
        {
            uint32_t sy = y * fh / m_overlayHeight;
            const uint8_t* srcRow = frameData + static_cast<size_t>(sy) * fw * 4;
            uint8_t* dstRow = static_cast<uint8_t*>(mapped) + static_cast<size_t>(y) * m_overlayWidth * 4;
            for (uint32_t x = 0; x < m_overlayWidth; ++x)
            {
                uint32_t sx = x * fw / m_overlayWidth;
                std::memcpy(dstRow + static_cast<size_t>(x) * 4, srcRow + static_cast<size_t>(sx) * 4, 4);
            }
        }
    }
    vkUnmapMemory(m_device, m_overlayStagingMemory);
    m_overlayDirty = true;

    // Company logo overlay at half size, bottom-left corner (mirrors
    // Windows Vulkan RenderIntroMovie) — now a queued blit, replayed
    // inside the pass with the overlay queue.
    if (!gameplayPlaybackActive)
    {
        int logoIdx = int(BlitObj2DIndexType::IMG_COMPANYLOGO);
        if (logoIdx >= 0 && logoIdx < MAX_TEXTURE_BUFFERS && m_textures2D[logoIdx].isValid)
        {
            int halfW = static_cast<int>(m_textures2D[logoIdx].width) / 2;
            int halfH = static_cast<int>(m_textures2D[logoIdx].height) / 2;
            Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
        }
    }
}

#endif // __USE_VULKAN__
