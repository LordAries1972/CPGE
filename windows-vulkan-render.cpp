// -------------------------------------------------------------------------------------------------------------
// windows-vulkan-render.cpp — Windows Vulkan implementation
// -------------------------------------------------------------------------------------------------------------
// Platform-specific implementation of VulkanRenderer, split out of VULKAN_Renderer.cpp /
// VULKAN_RenderFrame.cpp (platform split, 24 Nov 2026) so that working on one platform
// means working in one file. This file is the platform: it contains NO platform guards
// and is compiled only on its platform (Linux/CMakeLists.txt, CrossPlatformGameEngine.vcxproj).
// The code below is moved verbatim from the shared files; behaviour is unchanged.
// -------------------------------------------------------------------------------------------------------------
#define NOMINMAX
#include "Includes.h"
#include "windows-vulkan-render.h"

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
    if (!m_d2dRenderTarget) return;
    if (threadManager.threadVars.bIsShuttingDown.load() ||
        bIsMinimized.load()                             ||
        threadManager.threadVars.bIsResizing.load()     ||
        !bIsInitialized.load()) return;

    switch (scene.stSceneType)
    {
        case SceneType::SCENE_GAMETITLE:
        {
            if (threadManager.threadVars.bLoaderTaskFinished.load())
            {
                // IMG_GAMEINTRO1 is rendered as the FIRST Vulkan quad inside the render
                // pass (before 3D geometry) so the ship appears in front of it.
                // Do NOT blit it here — blitting to D2D composites it AFTER 3D models,
                // which would hide them completely (DX11 renders 3D over D2D; Vulkan
                // composites D2D overlay over 3D, so backgrounds must go into the pass).

                // Company logo only — this overlay element sits ON TOP of the 3D scene.
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]) {
                    D2D1_SIZE_F logoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                    int halfW = static_cast<int>(logoSz.width  * 0.5f);
                    int halfH = static_cast<int>(logoSz.height * 0.5f);
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
                }
            }
            else
            {
                // Loading screen: no 3D models render while the loader is in progress,
                // so the fullscreen loading image can safely go through the D2D overlay.
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)]) {
                    // Consume (but no longer act on) the legacy black-fade-in trigger --
                    // the end-of-load pixel fader now owns the loading-screen reveal.
                    if (threadManager.threadVars.bInitiateFader.load())
                        threadManager.threadVars.bInitiateFader.store(false);
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                }
            }
            break;
        }

        case SceneType::SCENE_GAMEPLAY:
        {
            if (!threadManager.threadVars.bLoaderTaskFinished.load())
            {
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)]) {
                    // Consume (but no longer act on) the legacy black-fade-in trigger --
                    // the end-of-load pixel fader now owns the loading-screen reveal.
                    if (threadManager.threadVars.bInitiateFader.load())
                        threadManager.threadVars.bInitiateFader.store(false);
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                }
            }
            break;
        }

        #if defined(_DEBUG)
        case SceneType::SCENE_EXPERIMENT:
            break; // Black background — warp-dot tunnel renders over it
        #endif

        default:
            break;
    }
}

void VulkanRenderer::Blit2DColoredPixel(int x, int y, float pixelSize, XMFLOAT4 color)
{
    if (m_drawToBackground ? !m_bgD2dRenderTarget : !m_d2dRenderTarget) return;
    MyColor c(static_cast<uint8_t>(color.x * 255), static_cast<uint8_t>(color.y * 255),
               static_cast<uint8_t>(color.z * 255), static_cast<uint8_t>(color.w * 255));
    DrawRectangleD2D(Vector2(static_cast<float>(x), static_cast<float>(y)),
                     Vector2(pixelSize, pixelSize), c);
    if (m_drawToBackground)
        m_bgOverlayDirty = true;
    else
        m_overlayDirty = true;
}

void VulkanRenderer::Blit2DCenteredZoom(BlitObj2DIndexType iIndex, int iDestX, int iDestY, int iDestW, int iDestH, float zoomFactor)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS) return;
    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[idx];
    // Route to background overlay when m_drawToBackground is active (e.g. GAMEINTRO1 zoom)
    ComPtr<ID2D1RenderTarget> rt     = (m_drawToBackground && m_bgD2dRenderTarget)
                                           ? m_bgD2dRenderTarget
                                           : m_d2dRenderTarget;
    if (!bitmap || !rt) return;

    // Clamp zoom factor to valid range (0.0–0.75)
    float z    = std::clamp(zoomFactor, 0.0f, 0.75f);
    D2D1_SIZE_F sz = bitmap->GetSize();
    float srcW = sz.width  * (1.0f - z);                                       // Cropped source width
    float srcH = sz.height * (1.0f - z);                                       // Cropped source height
    float srcX = (sz.width  - srcW) * 0.5f;                                   // Centre-aligned source X
    float srcY = (sz.height - srcH) * 0.5f;                                   // Centre-aligned source Y

    D2D1_RECT_F srcRect  = D2D1::RectF(srcX, srcY, srcX + srcW, srcY + srcH);
    D2D1_RECT_F destRect = D2D1::RectF(
        static_cast<float>(iDestX),          static_cast<float>(iDestY),
        static_cast<float>(iDestX + iDestW), static_cast<float>(iDestY + iDestH)
    );
    rt->DrawBitmap(bitmap.Get(), destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRect);
    // Mark the appropriate overlay dirty so UploadBgOverlayToVulkan sees the change
    if (m_drawToBackground)
        m_bgOverlayDirty = true;
}

void VulkanRenderer::Blit2DScrollingObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float scrollFraction, bool reverseDirection)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS) return;
    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[idx];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
    if (!bitmap || !rt) return;

    D2D1_SIZE_F bmpSize = bitmap->GetSize();
    int bmpW = static_cast<int>(bmpSize.width);
    int bmpH = static_cast<int>(bmpSize.height);
    if (bmpW <= 0 || bmpH <= 0 || iWidth <= 0 || iHeight <= 0) return;

    float wrappedFrac = scrollFraction - std::floor(scrollFraction);
    float xOffFrac = reverseDirection ? wrappedFrac : (1.0f - wrappedFrac);
    xOffFrac -= std::floor(xOffFrac);
    int xOff = static_cast<int>(xOffFrac * static_cast<float>(bmpW));

    int srcW1 = bmpW - xOff;
    float scaleX = static_cast<float>(iWidth) / static_cast<float>(bmpW);
    int destW1 = static_cast<int>(srcW1 * scaleX);

    // Part 1: source columns [xOff, bmpW) drawn at the left of the destination rect
    D2D1_RECT_F src1  = D2D1::RectF(static_cast<float>(xOff), 0.0f, static_cast<float>(bmpW), static_cast<float>(bmpH));
    D2D1_RECT_F dest1 = D2D1::RectF(static_cast<float>(iX), static_cast<float>(iY),
                                     static_cast<float>(iX + destW1), static_cast<float>(iY + iHeight));
    rt->DrawBitmap(bitmap.Get(), dest1, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src1);

    // Part 2: wrapped source columns [0, xOff) drawn to the right of part 1
    if (destW1 < iWidth)
    {
        D2D1_RECT_F src2  = D2D1::RectF(0.0f, 0.0f, static_cast<float>(xOff), static_cast<float>(bmpH));
        D2D1_RECT_F dest2 = D2D1::RectF(static_cast<float>(iX + destW1), static_cast<float>(iY),
                                         static_cast<float>(iX + iWidth), static_cast<float>(iY + iHeight));
        rt->DrawBitmap(bitmap.Get(), dest2, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src2);
    }
}

void VulkanRenderer::Blit2DAtlasTile(BlitObj2DIndexType iIndex, int iTileIndex, int iTileSizeX, int iTileSizeY, int iDestX, int iDestY)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS) return;
    if (iTileIndex < 0 || iTileSizeX <= 0 || iTileSizeY <= 0) return;
    ComPtr<ID2D1Bitmap> bitmap = m_d2dTextures[idx];
    if (!bitmap) return;

    D2D1_SIZE_F bmpSize = bitmap->GetSize();
    int bmpW = static_cast<int>(bmpSize.width);
    int bmpH = static_cast<int>(bmpSize.height);
    if (bmpW <= 0 || bmpH <= 0) return;

    int tilesPerRow = bmpW / iTileSizeX;
    int tilesPerCol = bmpH / iTileSizeY;
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
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS) return;
    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[idx];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
    if (!bitmap || !rt) return;
    D2D1_RECT_F dest   = D2D1::RectF(static_cast<float>(iBlitX),
                                      static_cast<float>(iBlitY),
                                      static_cast<float>(iBlitX + iTileSizeX),
                                      static_cast<float>(iBlitY + iTileSizeY));
    D2D1_RECT_F srcRct = D2D1::RectF(static_cast<float>(iXOffset),
                                      static_cast<float>(iYOffset),
                                      static_cast<float>(iXOffset + iTileSizeX),
                                      static_cast<float>(iYOffset + iTileSizeY));
    rt->DrawBitmap(bitmap.Get(), dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRct);
}

void VulkanRenderer::Blit2DObjectToSizeWithAlpha(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float alpha)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS) return;
    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[idx];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
    if (!bitmap || !rt) return;
    D2D1_SIZE_F sz   = bitmap->GetSize();
    D2D1_RECT_F dest = D2D1::RectF(static_cast<float>(iX),          static_cast<float>(iY),
                                    static_cast<float>(iX + iWidth), static_cast<float>(iY + iHeight));
    float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    rt->DrawBitmap(bitmap.Get(), dest, clampedAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                   D2D1::RectF(0.0f, 0.0f, sz.width, sz.height));
}

void VulkanRenderer::Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS) return;
    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[idx];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
    if (!bitmap || !rt) return;
    D2D1_SIZE_F sz = bitmap->GetSize();
    D2D1_RECT_F dest = D2D1::RectF(static_cast<float>(iX),          static_cast<float>(iY),
                                    static_cast<float>(iX + iWidth), static_cast<float>(iY + iHeight));
    rt->DrawBitmap(bitmap.Get(), dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                   D2D1::RectF(0.0f, 0.0f, sz.width, sz.height));
}

void VulkanRenderer::Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY)
{
    int idx = static_cast<int>(iIndex);
    if (idx < 0 || idx >= MAX_TEXTURE_BUFFERS) return;
    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[idx];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
    if (!bitmap || !rt) return;
    D2D1_SIZE_F sz = bitmap->GetSize();
    D2D1_RECT_F dest = D2D1::RectF(static_cast<float>(iX), static_cast<float>(iY),
                                    static_cast<float>(iX) + sz.width,
                                    static_cast<float>(iY) + sz.height);
    rt->DrawBitmap(bitmap.Get(), dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                   D2D1::RectF(0.0f, 0.0f, sz.width, sz.height));
}

void VulkanRenderer::DrawTexture(int textureId, const Vector2& position, const Vector2& size,
                                  const MyColor& tintColor, bool is2D)
{
    if (!m_d2dRenderTarget) return;
    if (is2D && textureId >= 0 && textureId < MAX_TEXTURE_BUFFERS && m_textures2D[textureId].isValid) {
        // Draw using the 2D Vulkan texture via D2D overlay, respecting tintColor alpha for fade support
        ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[textureId];
        ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
        if (!bitmap || !rt) return;
        D2D1_SIZE_F sz   = bitmap->GetSize();
        D2D1_RECT_F dest = D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y);
        rt->DrawBitmap(bitmap.Get(), dest, tintColor.a / 255.0f,
                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                       D2D1::RectF(0.0f, 0.0f, sz.width, sz.height));
    }
}

void VulkanRenderer::DrawMyTextStyled(const std::wstring& text, const Vector2& position,
                                       const MyColor& color, const TextRenderStyle& style)
{
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || style.fontSize <= 0.0f) return;

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        style.fontName.empty() ? L"Arial" : style.fontName.c_str(),
        nullptr,
        style.bold   ? DWRITE_FONT_WEIGHT_BOLD   : DWRITE_FONT_WEIGHT_NORMAL,
        style.italic ? DWRITE_FONT_STYLE_ITALIC   : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        style.fontSize,
        L"en-us",
        &fmt);
    if (FAILED(hr) || !fmt) return;

    // style.centered: centre text horizontally across the full render-target width
    // (matches DX11Renderer::DrawMyTextStyled — used for loading-screen messages)
    float    layoutWidth = 2000.0f;
    float    drawX       = position.x;
    if (style.centered) {
        float rtWidth = (m_renderTargetWidth > 0)
                        ? static_cast<float>(m_renderTargetWidth)
                        : 1920.0f;
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layoutWidth = rtWidth;
        drawX       = 0.0f;
    }

    UINT32 textLen = static_cast<UINT32>(text.size());
    ComPtr<IDWriteTextLayout> layout;
    hr = m_dwriteFactory->CreateTextLayout(text.c_str(), textLen, fmt.Get(), layoutWidth, 500.0f, &layout);
    if (FAILED(hr) || !layout) return;

    DWRITE_TEXT_RANGE all{ 0, textLen };
    if (style.underline)     layout->SetUnderline(TRUE,  all);
    if (style.strikethrough) layout->SetStrikethrough(TRUE, all);

    D2D1_COLOR_F dc = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dRenderTarget->CreateSolidColorBrush(dc, &brush);
    if (!brush) return;

    m_d2dRenderTarget->DrawTextLayout(D2D1::Point2F(drawX, position.y), layout.Get(), brush.Get());
    m_overlayDirty = true;
}

void VulkanRenderer::DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color,
                                         const float FontSize, const std::wstring& fontName)
{
    if (!m_d2dRenderTarget) return;
    DrawTextD2D(text, position, Vector2(0,0), color, FontSize, fontName, false);
    m_overlayDirty = true;
}

void VulkanRenderer::DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color,
                                         const float FontSize, float controlWidth, float controlHeight, bool /*bold*/)
{
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || FontSize <= 0.0f) return;

    // Bold weight for visual parity: Vulkan uses an off-screen WIC bitmap (96 DPI) which renders
    // glyphs thinner than DX11's DXGI backbuffer target.  Bold compensates so GUI text reads at
    // the same visual weight as the DX11 pipeline.
    ComPtr<IDWriteTextFormat> textFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        FontName, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        FontSize, L"en-us", &textFormat);
    if (FAILED(hr) || !textFormat) return;

    // Use leading/near alignment — centering is computed from exact glyph metrics below.
    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Create a temporary text layout to measure exact glyph extents for pixel-perfect centering.
    ComPtr<IDWriteTextLayout> textLayout;
    hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()),
        textFormat.Get(), 10000.f, 1000.f, &textLayout);
    if (FAILED(hr) || !textLayout) return;

    DWRITE_TEXT_METRICS metrics{};
    textLayout->GetMetrics(&metrics);

    // Pixel-perfect centering: offset by half control size minus half glyph size.
    float centredX = position.x + (controlWidth  * 0.5f) - (metrics.width  * 0.5f);
    float centredY = position.y + (controlHeight * 0.5f) - (metrics.height * 0.5f);

    float fr = color.r / 255.0f, fg = color.g / 255.0f, fb = color.b / 255.0f, fa = color.a / 255.0f;
    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(fr, fg, fb, fa), &brush);
    if (!brush) return;

    m_d2dRenderTarget->DrawText(
        text.c_str(), static_cast<UINT32>(text.size()), textFormat.Get(),
        D2D1::RectF(centredX, centredY, centredX + metrics.width + 4.0f, centredY + metrics.height),
        brush.Get());
    m_overlayDirty = true;
}

void VulkanRenderer::DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize)
{
    if (!m_d2dRenderTarget) return;
    DrawTextD2D(text, position, size, color, FontSize, FontName, false);
    m_overlayDirty = true;
}

void VulkanRenderer::DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize)
{
    if (!m_d2dRenderTarget) return;
    DrawTextD2D(text, position, Vector2(0,0), color, FontSize, FontName, false);
    m_overlayDirty = true;
}

void VulkanRenderer::CopyPlatformBgOverlayToStaging()
{
    if (!m_bgWicBitmap) return;

    WICRect lockRect = { 0, 0, static_cast<INT>(m_overlayWidth), static_cast<INT>(m_overlayHeight) };
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(m_bgWicBitmap->Lock(&lockRect, WICBitmapLockRead, &lock))) return;

    UINT stride = 0, bufSize = 0;
    WICInProcPointer dataPtr = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&bufSize, &dataPtr);

    void* mapped = nullptr;
    const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(m_overlayWidth) << 2;   // width * 4 == width << 2
    const VkDeviceSize mapSize  = rowBytes * m_overlayHeight;
    vkMapMemory(m_device, m_bgStagingMemory, 0, mapSize, 0, &mapped);
    uint8_t* dstBase = static_cast<uint8_t*>(mapped);
    for (uint32_t row = 0; row < m_overlayHeight; ++row)
        MemoryCopy(dataPtr + row * stride, dstBase + row * rowBytes, static_cast<size_t>(rowBytes));
    vkUnmapMemory(m_device, m_bgStagingMemory);
}

void VulkanRenderer::CopyPlatformOverlayToStaging()
{
    if (!m_wicBitmap) return;

    // Lock WIC bitmap and copy to staging buffer
    WICRect lockRect = { 0, 0, static_cast<INT>(m_overlayWidth), static_cast<INT>(m_overlayHeight) };
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(m_wicBitmap->Lock(&lockRect, WICBitmapLockRead, &lock))) return;

    UINT stride = 0, bufSize = 0;
    WICInProcPointer dataPtr = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&bufSize, &dataPtr);

    void* mapped = nullptr;
    const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(m_overlayWidth) << 2;   // width * 4 == width << 2
    const VkDeviceSize mapSize  = rowBytes * m_overlayHeight;
    vkMapMemory(m_device, m_overlayStagingMemory, 0, mapSize, 0, &mapped);
    // Copy row by row (stride may differ from rowBytes)
    uint8_t* dstBase = static_cast<uint8_t*>(mapped);
    for (uint32_t row = 0; row < m_overlayHeight; ++row)
        MemoryCopy(dataPtr + row * stride, dstBase + row * rowBytes, static_cast<size_t>(rowBytes));
    vkUnmapMemory(m_device, m_overlayStagingMemory);
}

void VulkanRenderer::DrawRectangleD2D(const Vector2& pos, const Vector2& size, const MyColor& color)
{
    ID2D1RenderTarget* target = (m_drawToBackground && m_bgD2dRenderTarget)
                                    ? m_bgD2dRenderTarget.Get()
                                    : m_d2dRenderTarget.Get();
    if (!target) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    D2D1_COLOR_F dc = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    target->CreateSolidColorBrush(dc, &brush);
    if (!brush) return;
    D2D1_RECT_F rect = D2D1::RectF(pos.x, pos.y, pos.x + size.x, pos.y + size.y);
    target->FillRectangle(rect, brush.Get());
}

void VulkanRenderer::DrawTextD2D(const std::wstring& text, const Vector2& pos, const Vector2& sz,
                                  const MyColor& color, float fontSize, const std::wstring& fontName,
                                  bool centered, float ctrlW, float ctrlH)
{
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    IDWriteTextFormat* fmt = GetOrCreateTextFormat(fontName.c_str(), fontSize);
    if (!fmt) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    D2D1_COLOR_F dc = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    m_d2dRenderTarget->CreateSolidColorBrush(dc, &brush);
    if (!brush) return;

    float w = (sz.x > 0.0f) ? sz.x : (ctrlW > 0.0f ? ctrlW : static_cast<float>(m_renderTargetWidth));
    float h = (sz.y > 0.0f) ? sz.y : (ctrlH > 0.0f ? ctrlH : static_cast<float>(m_renderTargetHeight));
    D2D1_RECT_F rect = D2D1::RectF(pos.x, pos.y, pos.x + w, pos.y + h);
    if (centered) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    m_d2dRenderTarget->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()),
                                 fmt, rect, brush.Get());
}

IDWriteTextFormat* VulkanRenderer::GetOrCreateTextFormat(const wchar_t* fontName, float fontSize)
{
    TextFormatKey key{ fontName, fontSize };
    auto it = m_textFormatCache.find(key);
    if (it != m_textFormatCache.end()) return it->second.Get();

    ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(m_dwriteFactory->CreateTextFormat(fontName, nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                fontSize, L"en-us", &fmt))) return nullptr;
    m_textFormatCache[key] = fmt;
    return fmt.Get();
}

void VulkanRenderer::InvalidateTextFormatCache() { m_textFormatCache.clear(); }

void VulkanRenderer::PopClipRect() {
    if (!m_d2dRenderTarget) return;
    m_d2dRenderTarget->PopAxisAlignedClip();
}

void VulkanRenderer::PushClipRect(float x, float y, float w, float h) {
    if (!m_d2dRenderTarget) return;
    m_d2dRenderTarget->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + h),
                                            D2D1_ANTIALIAS_MODE_ALIASED);
}

void VulkanRenderer::DrawCurve(float startX, float startY, float ctrlX, float ctrlY, float endX, float endY,
                                const MyColor& color, float thickness, bool is2D) {
    if (!is2D || !m_d2dRenderTarget || !m_d2dFactory) return;

    ComPtr<ID2D1PathGeometry> pathGeometry;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&pathGeometry)) || !pathGeometry) return;

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pathGeometry->Open(&sink)) || !sink) return;

    // AddBezier only takes a cubic segment, so lift the quadratic control point to the
    // equivalent cubic control points (standard degree-elevation formula).
    const D2D1_POINT_2F p0 = D2D1::Point2F(startX, startY);
    const D2D1_POINT_2F p2 = D2D1::Point2F(endX, endY);
    const D2D1_POINT_2F c  = D2D1::Point2F(ctrlX, ctrlY);
    D2D1_BEZIER_SEGMENT seg;
    seg.point1 = D2D1::Point2F(p0.x + (2.0f / 3.0f) * (c.x - p0.x), p0.y + (2.0f / 3.0f) * (c.y - p0.y));
    seg.point2 = D2D1::Point2F(p2.x + (2.0f / 3.0f) * (c.x - p2.x), p2.y + (2.0f / 3.0f) * (c.y - p2.y));
    seg.point3 = p2;

    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(seg);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;

    float fr = color.r / 255.0f, fg = color.g / 255.0f, fb = color.b / 255.0f, fa = color.a / 255.0f;
    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(fr, fg, fb, fa), &brush);
    if (!brush) return;

    m_d2dRenderTarget->DrawGeometry(pathGeometry.Get(), brush.Get(), thickness);
    m_overlayDirty = true;
}

void VulkanRenderer::DrawFilledPolygonBatch(const Vector2* const* polys, const int* counts, int n, const MyColor& color) {
    if (!polys || !counts || n <= 0 || !m_d2dFactory) return;
    ID2D1RenderTarget* target = (m_drawToBackground && m_bgD2dRenderTarget) ? m_bgD2dRenderTarget.Get() : m_d2dRenderTarget.Get();
    if (!target) return;
    ComPtr<ID2D1PathGeometry> pathGeometry;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&pathGeometry)) || !pathGeometry) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pathGeometry->Open(&sink)) || !sink) return;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);                               // overlapping figures of equal winding are UNIONED
    D2D1_POINT_2F pts[32];
    int figures = 0;
    for (int i = 0; i < n; ++i)
    {
        int c = counts[i];
        const Vector2* p = polys[i];
        if (c < 3 || !p) continue;
        if (c > 32) c = 32;
        float area = 0.0f;                                                   // signed area: normalise every figure to one winding,
        for (int k = 0; k < c; ++k) { const Vector2& a = p[k]; const Vector2& b = p[(k + 1) % c]; area += a.x * b.y - b.x * a.y; }
        for (int k = 0; k < c; ++k)                                          // otherwise a figure and its mirror image would cancel
        {
            const Vector2& q = (area >= 0.0f) ? p[k] : p[c - 1 - k];
            pts[k] = D2D1::Point2F(q.x, q.y);
        }
        sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(pts + 1, static_cast<UINT32>(c - 1));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        ++figures;
    }
    if (FAILED(sink->Close()) || figures == 0) return;

    ComPtr<ID2D1SolidColorBrush> brush;
    D2D1_COLOR_F dc = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    target->CreateSolidColorBrush(dc, &brush);
    if (!brush) return;

    const D2D1_ANTIALIAS_MODE previousAA = target->GetAntialiasMode();
    target->SetAntialiasMode(VectorAAEnabled() ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE : D2D1_ANTIALIAS_MODE_ALIASED);       // the Video settings' Anti-Aliasing (see VectorAA.h)
    target->FillGeometry(pathGeometry.Get(), brush.Get());
    if (const float seam = VectorSeamStroke()) target->DrawGeometry(pathGeometry.Get(), brush.Get(), seam);              // MSAA level: a same-colour hairline covers the seams between faces
    target->SetAntialiasMode(previousAA);
    m_overlayDirty = true;
}

void VulkanRenderer::DrawFilledPolygon(const Vector2* points, int count, const MyColor& color) {
    if (!points || count < 3 || !m_d2dFactory) return;
    if (count > 32) count = 32;
    ID2D1RenderTarget* target = (m_drawToBackground && m_bgD2dRenderTarget)
                                    ? m_bgD2dRenderTarget.Get()
                                    : m_d2dRenderTarget.Get();
    if (!target) return;

    ComPtr<ID2D1PathGeometry> pathGeometry;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&pathGeometry)) || !pathGeometry) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pathGeometry->Open(&sink)) || !sink) return;

    D2D1_POINT_2F pts[32];
    for (int i = 0; i < count; ++i) pts[i] = D2D1::Point2F(points[i].x, points[i].y);
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(pts + 1, static_cast<UINT32>(count - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;

    ComPtr<ID2D1SolidColorBrush> brush;
    D2D1_COLOR_F dc = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    target->CreateSolidColorBrush(dc, &brush);
    if (!brush) return;

    const D2D1_ANTIALIAS_MODE previousAA = target->GetAntialiasMode();
    target->SetAntialiasMode(VectorAAEnabled() ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE : D2D1_ANTIALIAS_MODE_ALIASED);       // the Video settings' Anti-Aliasing (see VectorAA.h)
    target->FillGeometry(pathGeometry.Get(), brush.Get());
    if (const float seam = VectorSeamStroke()) target->DrawGeometry(pathGeometry.Get(), brush.Get(), seam);              // MSAA level: a same-colour hairline covers the seams between faces
    target->SetAntialiasMode(previousAA);
    m_overlayDirty = true;                                                    // overlay must be composited this frame
}

void VulkanRenderer::DrawCircle(const Vector2& center, float radius, const MyColor& color, bool filled) {
    if (!m_d2dRenderTarget) return;
    float fr = color.r / 255.0f, fg = color.g / 255.0f, fb = color.b / 255.0f, fa = color.a / 255.0f;
    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(fr, fg, fb, fa), &brush);
    if (!brush) return;
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(center.x, center.y), radius, radius);
    if (filled) m_d2dRenderTarget->FillEllipse(ellipse, brush.Get());
    else        m_d2dRenderTarget->DrawEllipse(ellipse, brush.Get());
    m_overlayDirty = true;
}

void VulkanRenderer::DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D)
{
    if (!is2D) return;
    if (!m_d2dRenderTarget) return;
    DrawRectangleD2D(position, size, color);
    m_overlayDirty = true;
}

void VulkanRenderer::ReleasePlatformD2DTexture(int textureId)
{
    m_d2dTextures[textureId].Reset();
}

bool VulkanRenderer::LoadTextureFromFilePlatform(const std::wstring& filename, bool is2D, int textureId, const std::filesystem::path& fullPath)
{
    // LoadTexture may be called from the loader worker thread which has NOT had
    // CoInitializeEx called on it (main.cpp only initialises COM on the main thread).
    // Initialise COM locally so WIC is always available regardless of caller thread.
    // CoInitializeEx returns S_OK (first call) or S_FALSE (already initialised on this
    // thread) — both mean we must call CoUninitialize to balance the ref-count.
    // RPC_E_CHANGED_MODE means another apartment type is already active; we skip uninit.
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[VulkanRenderer] LoadTexture: CoInitializeEx failed for: " + filename);
        return false;
    }
    const bool needCoUninit = SUCCEEDED(hrCom);

    bool loaded = false;
    do {
        // WIC factory — decodes image files to raw RGBA pixels on Windows.
        ComPtr<IWICImagingFactory2> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: WIC factory creation failed: " + filename);
            break;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(fullPath.wstring().c_str(), nullptr,
                    GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: file not found or unsupported format: " + fullPath.wstring());
            break;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: WIC GetFrame failed: " + filename);
            break;
        }

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter))) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: WIC CreateFormatConverter failed: " + filename);
            break;
        }
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                              WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: WIC pixel-format conversion failed: " + filename);
            break;
        }

        UINT w = 0, h = 0;
        if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: WIC GetSize returned zero dimensions: " + filename);
            break;
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: WIC CopyPixels failed: " + filename);
            break;
        }

        VulkanTexture tex = CreateTextureFromRGBA(pixels.data(), w, h);
        if (!tex.isValid) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[VulkanRenderer] LoadTexture: GPU upload (CreateTextureFromRGBA) failed: " + filename);
            break;
        }

        if (is2D) { DestroyVulkanTexture(m_textures2D[textureId]); m_textures2D[textureId] = tex; }
        else       { DestroyVulkanTexture(m_textures3D[textureId]); m_textures3D[textureId] = tex; }

        // For 2D textures: also create an ID2D1Bitmap so Blit2D functions can draw them onto
        // the D2D overlay.  Use PBGRA (pre-multiplied BGRA) — the native D2D format — via a
        // second WIC converter from the same decoded frame.
        if (is2D && m_d2dRenderTarget) {
            ComPtr<IWICFormatConverter> d2dConv;
            if (SUCCEEDED(factory->CreateFormatConverter(&d2dConv)) &&
                SUCCEEDED(d2dConv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapDitherTypeNone, nullptr, 0.0,
                                               WICBitmapPaletteTypeCustom)))
            {
                m_d2dTextures[textureId].Reset();
                if (FAILED(m_d2dRenderTarget->CreateBitmapFromWicBitmap(d2dConv.Get(),
                                                                          &m_d2dTextures[textureId])))
                    debug.logLevelMessage(LogLevel::LOG_WARNING,
                        L"[VulkanRenderer] LoadTexture: D2D bitmap creation failed: " + filename);
            }
        }

        loaded = true;
    } while (false);

    if (needCoUninit) CoUninitialize();
    return loaded;

}

bool VulkanRenderer::SetWindowedScreen()
{
    // Restore the specific monitor we changed — pass nullptr DEVMODE and 0 flags to let
    // Windows restore that adapter to its registry settings.
    if (m_isExclusiveFullscreen) {
        if (!m_exclusiveMonitorDeviceName.empty())
            ChangeDisplaySettingsExW(m_exclusiveMonitorDeviceName.c_str(), nullptr, nullptr, 0, nullptr);
        else
            ChangeDisplaySettingsW(nullptr, 0);  // fallback for legacy path
        m_isExclusiveFullscreen = false;
    }

    int rw, rh;
    if (m_savedWindowState.valid) {
        SetWindowLong(m_hwnd, GWL_STYLE,   m_savedWindowState.style);
        SetWindowLong(m_hwnd, GWL_EXSTYLE, m_savedWindowState.exStyle);
        RECT r = m_savedWindowState.rect;
        rw = r.right  - r.left;
        rh = r.bottom - r.top;
        SetWindowPos(m_hwnd, HWND_NOTOPMOST,
            r.left, r.top, rw, rh,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_savedWindowState.valid = false;
    } else {
        // Fallback: centre on screen using config dimensions.
        rw = config.myConfig.resolutionWidth;
        rh = config.myConfig.resolutionHeight;
        SetWindowLong(m_hwnd, GWL_STYLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
        SetWindowPos(m_hwnd, HWND_NOTOPMOST,
            (GetSystemMetrics(SM_CXSCREEN) - rw) / 2,
            (GetSystemMetrics(SM_CYSCREEN) - rh) / 2,
            rw, rh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    IsWindowMode.store(true);
    bool result = Resize(static_cast<uint32_t>(rw), static_cast<uint32_t>(rh));

    if (result) {
        float newAR = LookupAspectRatio(iOrigWidth, iOrigHeight);
        config.myConfig.aspectRatio = newAR;
        myCamera.UpdateResolution(static_cast<uint32_t>(iOrigWidth),
                                   static_cast<uint32_t>(iOrigHeight), newAR);
    }

    return result;
}

bool VulkanRenderer::SetFullExclusive(uint32_t width, uint32_t height)
{
    if (bFullScreenTransition.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"[VulkanRenderer] Exclusive fullscreen transition already in progress.");
        return false;
    }

    bFullScreenTransition.store(true);
    threadManager.threadVars.bSettingFullScreen.store(true);

    // Save window state for restoration (do this before any window/display change).
    if (!m_savedWindowState.valid) {
        m_savedWindowState.style   = GetWindowLong(m_hwnd, GWL_STYLE);
        m_savedWindowState.exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);
        GetWindowRect(m_hwnd, &m_savedWindowState.rect);
        m_savedWindowState.valid   = true;
    }

    // Vulkan fullscreen must keep Win32 placement, swapchain extent, D2D overlay,
    // GUI, and camera in the same logical coordinate space. The working renderers
    // expose fullscreen as app-space (0,0,width,height), so target the primary
    // monitor origin here instead of carrying virtual-desktop offsets into metrics.
    HMONITOR      hmon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi  = {};
    mi.cbSize          = sizeof(mi);
    GetMonitorInfoW(hmon, reinterpret_cast<LPMONITORINFO>(&mi));
    int monLeft = mi.rcMonitor.left;
    int monTop  = mi.rcMonitor.top;

    // Capture the current mode of THIS monitor so Cleanup has a reference if needed.
    if (!m_isExclusiveFullscreen) {
        m_originalDesktopMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettingsExW(mi.szDevice, ENUM_CURRENT_SETTINGS,
            reinterpret_cast<DEVMODEW*>(&m_originalDesktopMode), 0);
    }

    // Tear down the swapchain and surface BEFORE switching the display mode.
    // ChangeDisplaySettingsExW invalidates the existing VkSurfaceKHR; if we leave it
    // alive, Resize() later hits a double surface-lost fault:
    //   first inside RecreateSwapChain (caught, surface rebuilt)
    //   then immediately in the next RenderFrame (vkGetPhysicalDeviceSurfaceCapabilitiesKHR fails)
    // Destroying proactively eliminates both faults.
    WaitForGPUToFinish();
    CleanupSwapChain();
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    DEVMODEW dm = {};
    dm.dmSize       = sizeof(dm);
    dm.dmPelsWidth  = width;
    dm.dmPelsHeight = height;
    dm.dmBitsPerPel = 32;
    dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    if (config.myConfig.refreshRate > 0) {
        dm.dmDisplayFrequency = config.myConfig.refreshRate;
        dm.dmFields |= DM_DISPLAYFREQUENCY;
    }

    // Use the adapter name from MONITORINFOEXW so we change the correct display.
    // Save it so the restore paths (SetWindowedScreen, Cleanup) target the same adapter.
    std::wstring devName(mi.szDevice);
    m_exclusiveMonitorDeviceName = devName;
    if (ChangeDisplaySettingsExW(devName.c_str(), &dm, nullptr, CDS_FULLSCREEN, nullptr) != DISP_CHANGE_SUCCESSFUL) {
        // Retry without the frequency constraint — some monitors report 59 or 60.000 internally
        // and reject an exact match of 60Hz even though they run at 60Hz.
        if (dm.dmFields & DM_DISPLAYFREQUENCY) {
            dm.dmFields &= ~DM_DISPLAYFREQUENCY;
            if (ChangeDisplaySettingsExW(devName.c_str(), &dm, nullptr, CDS_FULLSCREEN, nullptr) != DISP_CHANGE_SUCCESSFUL) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] SetFullExclusive: ChangeDisplaySettingsExW failed (with and without frequency)");
                m_requestExclusiveMode = false;
                CreateSurface(m_hwnd);  // restore surface so the renderer can continue windowed
                threadManager.threadVars.bSettingFullScreen.store(false);
                bFullScreenTransition.store(false);
                return false;
            }
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] SetFullExclusive: succeeded only without frequency constraint");
        } else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] SetFullExclusive: ChangeDisplaySettingsExW failed");
            m_requestExclusiveMode = false;
            CreateSurface(m_hwnd);  // restore surface so the renderer can continue windowed
            threadManager.threadVars.bSettingFullScreen.store(false);
            bFullScreenTransition.store(false);
            return false;
        }
    }

    // Let the display mode change settle before touching Vulkan surface/swapchain.
    // Without this pause, vkGetPhysicalDeviceSurfaceCapabilitiesKHR may fail on the
    // freshly created surface because the OS compositor hasn't finished the mode switch.
    Sleep(150);

    m_isExclusiveFullscreen = true;
    IsWindowMode.store(false);

    // Position the window at fullscreen app origin, then size it to the requested
    // exclusive resolution.
    // m_requestExclusiveMode was set by SetDisplayMode so CreateSwapChain will chain
    // VkSurfaceFullScreenExclusiveInfoEXT with the correct HMONITOR.
    EnforceFullscreenWindowPlacement(
        monLeft,
        monTop,
        static_cast<int>(width),
        static_cast<int>(height),
        L"exclusive-before-surface");

    // Commit winMetrics NOW — before Resize — so main.cpp's post-SetDisplayMode window
    // geometry block never sees isFullScreen==false and re-applies windowed SetWindowPos.
    // Fullscreen render coordinates are app-space (0,0,width,height), independent
    // of any previous virtual-desktop offset.
    int fsW = static_cast<int>(width);
    int fsH = static_cast<int>(height);
    winMetrics.x               = monLeft;
    winMetrics.y               = monTop;
    winMetrics.width           = fsW;
    winMetrics.height          = fsH;
    winMetrics.clientWidth     = fsW;
    winMetrics.clientHeight    = fsH;
    winMetrics.borderWidth     = 0;
    winMetrics.titleBarHeight  = 0;
    winMetrics.isFullScreen    = true;
    winMetrics.isBorderless    = false;
    winMetrics.monitorFullArea = { monLeft, monTop, monLeft + fsW, monTop + fsH };
    winMetrics.monitorWorkArea = { monLeft, monTop, monLeft + fsW, monTop + fsH };

    // Recreate the surface against the now-stable display/window before calling Resize.
    CreateSurface(m_hwnd);

    bool result = Resize(width, height);

    if (result)
    {
        EnforceFullscreenWindowPlacement(
            monLeft,
            monTop,
            static_cast<int>(width),
            static_cast<int>(height),
            L"exclusive-after-resize");
// Update the camera projection to match the exclusive fullscreen resolution.
        // Without this the projection matrix retains the windowed aspect ratio, producing
        // Y-axis stretch when the display resolution differs from the windowed resolution.
        {
            float newAR = LookupAspectRatio(iOrigWidth, iOrigHeight);
            config.myConfig.aspectRatio = newAR;
            myCamera.UpdateResolution(static_cast<uint32_t>(iOrigWidth),
                                       static_cast<uint32_t>(iOrigHeight), newAR);
        }

        #if defined(_DEBUG_VULKANRENDERER_)
        {
            VkExtent2D ext = GetSwapchainExtent();
            UINT   dpi        = GetDpiForWindow(m_hwnd);
            RECT   windowRect = {}, clientRect = {};
            GetWindowRect(m_hwnd, &windowRect);
            GetClientRect(m_hwnd,  &clientRect);
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[VulkanRenderer] Exclusive fullscreen diag: DPI=%u  Window=%dx%d  Client=%dx%d  "
                L"Config=%dx%d  Internal=%dx%d  SwapchainExtent=%dx%d  VkExclusiveActive=%d",
                dpi,
                windowRect.right  - windowRect.left,
                windowRect.bottom - windowRect.top,
                clientRect.right  - clientRect.left,
                clientRect.bottom - clientRect.top,
                config.myConfig.resolutionWidth, config.myConfig.resolutionHeight,
                iOrigWidth, iOrigHeight,
                ext.width, ext.height,
                m_isVkExclusiveActive ? 1 : 0);
        }
        #endif
    }

    threadManager.threadVars.bSettingFullScreen.store(false);
    bFullScreenTransition.store(false);
    return result;
}

bool VulkanRenderer::SetFullScreen()
{
    if (bFullScreenTransition.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"[VulkanRenderer] Borderless fullscreen transition already in progress.");
        return false;
    }

    bFullScreenTransition.store(true);
    threadManager.threadVars.bSettingFullScreen.store(true);

    // Save window state so SetWindowedScreen can restore the exact prior position and style.
    if (!m_savedWindowState.valid) {
        m_savedWindowState.style   = GetWindowLong(m_hwnd, GWL_STYLE);
        m_savedWindowState.exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);
        GetWindowRect(m_hwnd, &m_savedWindowState.rect);
        m_savedWindowState.valid   = true;
    }

    // Vulkan is sensitive to a mismatch between Win32 placement, swapchain extent,
    // and D2D overlay size. Startup later applies borderless geometry at primary
    // desktop origin (0,0), so use the same primary-monitor coordinate space here.
    HMONITOR hmon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hmon, &mi);
    uint32_t w = static_cast<uint32_t>(mi.rcMonitor.right  - mi.rcMonitor.left);
    uint32_t h = static_cast<uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top);

    SetWindowLong(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    // Do NOT clear GWL_EXSTYLE — flags like WS_EX_NOREDIRECTIONBITMAP affect how DWM
    // handles Vulkan flip-model presentation and must be preserved to avoid DPI/offset issues.
    SetWindowPos(m_hwnd, HWND_TOPMOST,
        mi.rcMonitor.left, mi.rcMonitor.top, static_cast<int>(w), static_cast<int>(h),
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    SetForegroundWindow(m_hwnd);
    IsWindowMode.store(false);

    bool result = Resize(w, h);

    if (result) {
        winMetrics.x               = mi.rcMonitor.left;
        winMetrics.y               = mi.rcMonitor.top;
        winMetrics.width           = static_cast<int>(w);
        winMetrics.height          = static_cast<int>(h);
        winMetrics.clientWidth     = static_cast<int>(w);
        winMetrics.clientHeight    = static_cast<int>(h);
        winMetrics.borderWidth     = 0;
        winMetrics.titleBarHeight  = 0;
        winMetrics.isFullScreen    = false;
        winMetrics.isBorderless    = true;
        winMetrics.monitorFullArea = mi.rcMonitor;
        winMetrics.monitorWorkArea = winMetrics.monitorFullArea;

        // Update the camera projection to match the new display resolution so the aspect ratio
        // is correct — prevents Y-axis stretching when the windowed and fullscreen aspect ratios differ.
        float newAR = LookupAspectRatio(static_cast<int>(iOrigWidth), static_cast<int>(iOrigHeight));
        config.myConfig.aspectRatio = newAR;
        myCamera.UpdateResolution(static_cast<uint32_t>(iOrigWidth),
                                   static_cast<uint32_t>(iOrigHeight), newAR);
    }

    threadManager.threadVars.bSettingFullScreen.store(false);
    bFullScreenTransition.store(false);
    return result;
}

void VulkanRenderer::AcquireFullScreenExclusiveIfAvailable()
{
    if (!m_supportsVkExclusiveFS || m_swapchain == VK_NULL_HANDLE) return;
    auto fn = reinterpret_cast<PFN_vkAcquireFullScreenExclusiveModeEXT>(
        vkGetDeviceProcAddr(m_device, "vkAcquireFullScreenExclusiveModeEXT"));
    if (fn) {
        VkResult result = fn(m_device, m_swapchain);
        m_isVkExclusiveActive = (result == VK_SUCCESS);
        if (result == VK_SUCCESS) {
            EnforceFullscreenWindowPlacement(
                winMetrics.x,
                winMetrics.y,
                winMetrics.width,
                winMetrics.height,
                L"post-vkAcquire");
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Acquired VK exclusive fullscreen mode.");
        }
        else
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] vkAcquireFullScreenExclusiveModeEXT returned non-success — continuing in borderless mode.");
    }
}

void VulkanRenderer::ReleaseFullScreenExclusiveIfActive()
{
    if (!m_isVkExclusiveActive || m_swapchain == VK_NULL_HANDLE) return;
    auto fn = reinterpret_cast<PFN_vkReleaseFullScreenExclusiveModeEXT>(
        vkGetDeviceProcAddr(m_device, "vkReleaseFullScreenExclusiveModeEXT"));
    if (fn) fn(m_device, m_swapchain);
    m_isVkExclusiveActive = false;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Released VK exclusive fullscreen mode.");
}

void VulkanRenderer::EnforceFullscreenWindowPlacement(int left, int top, int width, int height, const wchar_t* phase)
{
    if (!m_hwnd || width <= 0 || height <= 0) return;

    SetWindowLong(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    ShowWindow(m_hwnd, SW_SHOWNORMAL);

    SetWindowPos(m_hwnd, HWND_TOPMOST,
        left, top, width, height,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

    HWND foreground = GetForegroundWindow();
    DWORD currentThread = GetCurrentThreadId();
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    bool attachedForeground = false;

    if (foregroundThread != 0 && foregroundThread != currentThread)
        attachedForeground = AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;

    BringWindowToTop(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);
    SetForegroundWindow(m_hwnd);
    SetWindowPos(m_hwnd, HWND_TOPMOST,
        left, top, width, height,
        SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

    if (attachedForeground)
        AttachThreadInput(currentThread, foregroundThread, FALSE);

    RECT wr = {}, cr = {};
    GetWindowRect(m_hwnd, &wr);
    GetClientRect(m_hwnd, &cr);
    HWND logForeground = GetForegroundWindow();
    wchar_t foregroundClass[128] = {};
    if (logForeground)
        GetClassNameW(logForeground, foregroundClass, static_cast<int>(std::size(foregroundClass)));
    LONG exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[VulkanRenderer] Fullscreen HWND enforce (%s): Window=%ld,%ld %ldx%ld Client=%ldx%ld Topmost=%d Foreground=%d ForegroundClass=%s ExStyle=0x%08lx",
        phase ? phase : L"unknown",
        wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
        cr.right - cr.left, cr.bottom - cr.top,
        (exStyle & WS_EX_TOPMOST) ? 1 : 0,
        logForeground == m_hwnd ? 1 : 0,
        foregroundClass,
        static_cast<unsigned long>(exStyle));
}

void VulkanRenderer::ResetPlatformOverlayTargets()
{
    m_d2dRenderTarget.Reset();
    m_bgD2dRenderTarget.Reset();
    m_wicBitmap.Reset();
    m_bgWicBitmap.Reset();
    InvalidateTextFormatCache();
}

void VulkanRenderer::RecreateSurface()
{
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    CreateSurface(m_hwnd);
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"[VulkanRenderer] Surface recreated after loss.");
}

void VulkanRenderer::CreatePlatformBackgroundOverlay(uint32_t width, uint32_t height, VkDeviceSize bufSize)
{
    // Background overlay texture/staging exist only when the WIC bitmap was
    // created above (Windows-only); on Linux/Android the bg overlay stays
    // uncreated and m_bgOverlayDirty is never set.
    if (m_bgWicBitmap) {
        m_bgOverlayTexture = CreateTextureFromRGBA(nullptr, width, height, VK_FORMAT_B8G8R8A8_SRGB);
        CreateBuffer(bufSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_bgStagingBuffer, m_bgStagingMemory);
    }
}

void VulkanRenderer::CreatePlatformBackgroundTarget(uint32_t width, uint32_t height, bool overlayOk)
{
    HRESULT hr = S_OK; // Declared here: hr lived in (a) above; this helper owns its copy.
    // Background overlay — same dimensions, used for starfield drawn BEFORE 3D models.
    if (overlayOk) {
        hr = m_wicFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapCacheOnLoad, &m_bgWicBitmap);
        if (SUCCEEDED(hr)) {
            D2D1_RENDER_TARGET_PROPERTIES bgRtp = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (FAILED(m_d2dFactory->CreateWicBitmapRenderTarget(m_bgWicBitmap.Get(), bgRtp, &m_bgD2dRenderTarget)))
                m_bgD2dRenderTarget.Reset();
        }
    }
}

bool VulkanRenderer::CreatePlatformOverlayTargets(uint32_t width, uint32_t height)
{
    bool overlayOk = true;
    HRESULT hr = S_OK;

    // WIC factory
    hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&m_wicFactory));
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] CoCreateInstance(WICImagingFactory2) failed — 2D text/image overlay disabled.");
        overlayOk = false;
    }

    if (overlayOk) {
        hr = m_wicFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapCacheOnLoad, &m_wicBitmap);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] WIC CreateBitmap failed — 2D overlay disabled.");
            overlayOk = false;
        }
    }

    if (overlayOk) {
        // MULTI_THREADED: the factory is created on the main thread (Initialize) but all D2D
        // methods are called from THREAD_RENDERER (RenderFrame).  SINGLE_THREADED provides no
        // internal locking, causing access violations inside DWrite when the render thread uses
        // D2D resources derived from a factory that was created on a different thread.
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, m_d2dFactory.GetAddressOf());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] D2D1CreateFactory failed — 2D overlay disabled.");
            overlayOk = false;
        }
    }

    if (overlayOk) {
        D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        hr = m_d2dFactory->CreateWicBitmapRenderTarget(m_wicBitmap.Get(), rtp, &m_d2dRenderTarget);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] CreateWicBitmapRenderTarget failed — 2D overlay disabled.");
            overlayOk = false;
        }
    }

    if (overlayOk) {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                 __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[VulkanRenderer] DWriteCreateFactory failed — text rendering disabled.");
        }
    }
    return overlayOk;
}

bool VulkanRenderer::GetPlatformVSyncEnabled() const
{
    return config.myConfig.enableVSync;
}

void VulkanRenderer::AppendPlatformDeviceExtensions(std::vector<const char*>& enabledExtensions)
{
    // Add VK_EXT_full_screen_exclusive when the required instance extension is available.
    if (m_hasGetSurface2Ext) {
        uint32_t dextCount = 0;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &dextCount, nullptr);
        std::vector<VkExtensionProperties> davail(dextCount);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &dextCount, davail.data());
        for (auto& e : davail) {
            if (strcmp(e.extensionName, VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME) == 0) {
                enabledExtensions.push_back(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME);
                m_supportsVkExclusiveFS = true;
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    L"[VulkanRenderer] VK_EXT_full_screen_exclusive enabled.");
                break;
            }
        }
    }
}

void VulkanRenderer::CreateSurface(NativeWindowHandle hwnd)
{
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hwnd      = hwnd;
    ci.hinstance = m_hInst;
    if (vkCreateWin32SurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("[VulkanRenderer] Failed to create Win32 Vulkan surface.");


    debug.logLevelMessage(LogLevel::LOG_INFO, L"[VulkanRenderer] Surface created.");
}

void VulkanRenderer::ProbePlatformInstanceExtensions(std::vector<const char*>& extensions)
{
    // Optionally enable VK_KHR_get_surface_capabilities2 — required by VK_EXT_full_screen_exclusive.
    {
        uint32_t iextCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &iextCount, nullptr);
        std::vector<VkExtensionProperties> iavail(iextCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &iextCount, iavail.data());
        for (auto& e : iavail) {
            if (strcmp(e.extensionName, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) == 0) {
                extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
                m_hasGetSurface2Ext = true;
                break;
            }
        }
    }
}

void VulkanRenderer::AppendPlatformSurfaceExtension(std::vector<const char*>& extensions)
{
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
}

void VulkanRenderer::ReleasePlatformD2DTextures()
{
    for (auto& bmp : m_d2dTextures) bmp.Reset();
    m_videoBitmap.Reset();
}

void VulkanRenderer::ReleasePlatformOverlayResources()
{
    if (m_overlayStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_overlayStagingBuffer, nullptr);
        m_overlayStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_overlayStagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_overlayStagingMemory, nullptr);
        m_overlayStagingMemory = VK_NULL_HANDLE;
    }
    if (m_bgStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_bgStagingBuffer, nullptr);
        m_bgStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_bgStagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_bgStagingMemory, nullptr);
        m_bgStagingMemory = VK_NULL_HANDLE;
    }
    m_bgD2dRenderTarget.Reset();
    m_bgWicBitmap.Reset();
    m_d2dRenderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    m_wicBitmap.Reset();
    m_wicFactory.Reset();
    InvalidateTextFormatCache();
}

void VulkanRenderer::RestorePlatformDisplayMode()
{
    if (m_isExclusiveFullscreen) {
        if (!m_exclusiveMonitorDeviceName.empty())
            ChangeDisplaySettingsExW(m_exclusiveMonitorDeviceName.c_str(), nullptr, nullptr, 0, nullptr);
        else
            ChangeDisplaySettingsW(nullptr, 0);
        m_isExclusiveFullscreen = false;
    }
}

void VulkanRenderer::HidePlatformMouseCursor()
{
    sysUtils.DisableMouseCursor();
}

void VulkanRenderer::InitPlatformWindow(NativeWindowHandle hwnd, NativeInstanceHandle hInstance)
{
    m_hwnd  = hwnd;
    m_hInst = hInstance;
    m_renderTargetWidth  = config.myConfig.resolutionWidth;
    m_renderTargetHeight = config.myConfig.resolutionHeight;
}

void VulkanRenderer::InitPlatformDetails()
{
    SecureZeroMemory(&m_d2dTextures, sizeof(m_d2dTextures));
    osDetails.Platform.isWindows = true;
}

void VulkanRenderer::CompositePlatformBackgroundOverlay(VkCommandBuffer cmd, VulkanFrameData& fd)
{
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
}

void VulkanRenderer::UploadPlatformOverlays(VkCommandBuffer cmd)
{
    UploadOverlayToVulkan(cmd);
    UploadBgOverlayToVulkan(cmd);
}

void VulkanRenderer::RenderPlatformOverlay(VkCommandBuffer cmd)
{
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

        // Optional app-supplied 2D overlay (e.g. a game drawing its vector graphics with MyVectorRender, see VectorRender.h); no-op when unset.
        if (gameplayPlaybackActive) RenderIntroMovie();
        if (g_Overlay2DCallback) g_Overlay2DCallback(*this, iOrigWidth, iOrigHeight);

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
                VectorApplyD2DAA(m_bgD2dRenderTarget.Get());
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
}

void VulkanRenderer::BeginPlatformOverlayDraw()
{
    // Begin D2D drawing for this frame
    if (m_d2dRenderTarget) {
        m_d2dRenderTarget->BeginDraw();
        VectorApplyD2DAA(m_d2dRenderTarget.Get());                          // Video settings: Anti-Aliasing for every 2D shape and the text (VectorAA.h)
        m_d2dRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // transparent clear
    }
}

bool VulkanRenderer::HasPlatformOverlayTarget() const
{
    return m_d2dRenderTarget != nullptr;
}

inline void VulkanRenderer::RenderIntroMovie()
{
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
    if (!gameplayPlaybackActive && m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]) {
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
    if (scene.stSceneType == SceneType::SCENE_INTRO_MOVIE && KeyboardHandler::GetInstance().IsPlatformKeyPressed(' ') && !scene.bSceneSwitching)
    {
        scene.bSceneSwitching = true;
        fxManager.FadeOutThenCallback(XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f, 0.06f, []() {
            moviePlayer.Stop();
        });
    }

}

#endif // __USE_VULKAN__
