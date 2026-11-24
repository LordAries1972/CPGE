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

// On Linux/Android XMFLOAT4 aliases to Vector4 (Includes.h), so this is the
// overload the base declares there. No-op until the native Vulkan 2D blits
// land (Phase C) — same as the other Blit2D* stubs above.
void VulkanRenderer::Blit2DColoredPixel(int x, int y, float pixelSize, const Vector4& color)
{
    (void)x; (void)y; (void)pixelSize; (void)color;
}

void VulkanRenderer::Blit2DCenteredZoom(BlitObj2DIndexType iIndex, int iDestX, int iDestY, int iDestW, int iDestH, float zoomFactor)
{
    // No 2D blit backend on Linux/Android yet (Phase C).
    (void)iIndex; (void)iDestX; (void)iDestY; (void)iDestW; (void)iDestH; (void)zoomFactor;
}

void VulkanRenderer::Blit2DScrollingObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float scrollFraction, bool reverseDirection)
{
    // No 2D blit backend on Linux/Android yet (Phase C).
    (void)iIndex; (void)iX; (void)iY; (void)iWidth; (void)iHeight; (void)scrollFraction; (void)reverseDirection;
}

void VulkanRenderer::Blit2DAtlasTile(BlitObj2DIndexType iIndex, int iTileIndex, int iTileSizeX, int iTileSizeY, int iDestX, int iDestY)
{
    // No 2D blit backend on Linux/Android yet (Phase C): the tile routing
    // needs the Direct2D bitmap size, which does not exist here.
    (void)iIndex; (void)iTileIndex; (void)iTileSizeX; (void)iTileSizeY; (void)iDestX; (void)iDestY;
}

void VulkanRenderer::Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY,
                                           int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY)
{
    (void)iIndex; (void)iBlitX; (void)iBlitY; (void)iXOffset; (void)iYOffset; (void)iTileSizeX; (void)iTileSizeY;
}

void VulkanRenderer::Blit2DObjectToSizeWithAlpha(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float alpha)
{
    (void)iIndex; (void)iX; (void)iY; (void)iWidth; (void)iHeight; (void)alpha;
}

void VulkanRenderer::Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight)
{
    (void)iIndex; (void)iX; (void)iY; (void)iWidth; (void)iHeight;
}

void VulkanRenderer::Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY)
{
    (void)iIndex; (void)iX; (void)iY;
}

void VulkanRenderer::DrawTexture(int textureId, const Vector2& position, const Vector2& size,
                                  const MyColor& tintColor, bool is2D)
{
    (void)textureId; (void)position; (void)size; (void)tintColor; (void)is2D;
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
    (void)cmd; // Unused on Linux/Android.
}

void VulkanRenderer::RenderPlatformOverlay(VkCommandBuffer cmd)
{
    (void)cmd; // Unused on Linux/Android.
    // Linux/Android: scene 2D and GUI rendered via CPU rasterizer (stub)
    try { fxManager.Render2D(); } catch (...) {} // intentional silent swallow (per-frame path)
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
    if (moviePlayer.IsPlaying() || (gameplayPlaybackActive && moviePlayer.IsPaused())) {
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
}

#endif // __USE_VULKAN__
