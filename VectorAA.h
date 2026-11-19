#pragma once
// ================================================================================================
//  ANTI-ALIASING FOR EVERYTHING THE 2D / VECTOR LAYER DRAWS (Video settings: Anti-Aliasing, MSAA, MSAA Samples) - all four renderers.
//  The 3D pipelines take the settings through their rasteriser / multisample targets; the vector scenes and every HUD are drawn by the 2D layer, which is Direct2D on DX11, DX12 and Vulkan
//  (Direct2D draws on a single-sample surface, so MSAA cannot reach it) and plain triangles in the multisampled framebuffer on OpenGL.  The settings therefore act on that layer directly:
//    Anti-Aliasing OFF -> EVERYTHING is aliased: polygons, circles, ellipses, rectangles, lines / curves AND text (Direct2D antialias mode and text antialias mode, once per frame:
//                         VectorApplyD2DAA); OpenGL turns off line smoothing and the polygon fringe, and its multisample format is not requested.
//    Anti-Aliasing ON  -> EVERYTHING is smooth: per-primitive anti-aliased shapes and grayscale-anti-aliased text.
//    MSAA ON (level)   -> a same-colour hairline stroke follows every polygon fill (width 0.5 / 0.8 / 1.1 px for 2x / 4x / 8x samples) so the seams anti-aliasing opens between
//                         neighbouring faces of a model are covered; on OpenGL the real hardware MSAA at the chosen sample count does the whole job.
// ================================================================================================
#include "Configuration.h"
#if defined(_WIN32)
    #include <d2d1.h>
#endif

inline bool VectorAAEnabled() { return config.myConfig.antiAliasingEnabled; }
inline bool VectorMSAAEnabled() { return config.myConfig.antiAliasingEnabled && config.myConfig.msaaEnabled; }
inline float VectorSeamStroke()                                      // 0 = none (AA off, or AA on without MSAA)
{
    if (!config.myConfig.antiAliasingEnabled || !config.myConfig.msaaEnabled) return 0.0f;
    const int s = config.myConfig.msaaSamples;
    return (s >= 8) ? 1.1f : ((s >= 4) ? 0.8f : 0.5f);
}
#if defined(_WIN32)
// Applies the Anti-Aliasing switch to a Direct2D render target (ID2D1RenderTarget or ID2D1DeviceContext): shapes AND text.  Call it right after BeginDraw, every frame, so a change in
// the options window takes effect at once.
template <class RT> inline void VectorApplyD2DAA(RT* t)
{
    if (!t) return;
    const bool aa = VectorAAEnabled();
    t->SetAntialiasMode(aa ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE : D2D1_ANTIALIAS_MODE_ALIASED);
    t->SetTextAntialiasMode(aa ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
}
#endif
