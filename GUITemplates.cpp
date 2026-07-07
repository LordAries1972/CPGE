/* ---------------------------------------------------------------------------------------------------------
Description: GUITemplates.cpp

Implements GUIWindowTemplateType chrome. Each template installs an onCustomRender hook on the
target GUIWindow that draws its border/title-bar decoration on top of the window's background and
controls (onCustomRender fires last in GUIWindow::Render()), and sets the window's background
colour. Templates never call AddControl() — the caller supplies content controls separately.

Dependencies: GUITemplates.h, GUIManager.h, Renderer.h, Vectors.h, Color.h
--------------------------------------------------------------------------------------------------- */
#include "Includes.h"
#include "GUITemplates.h"
#include "GUIManager.h"
#include "Renderer.h"
#include "Vectors.h"
#include "Color.h"

namespace {

// --- Tech1 palette -------------------------------------------------------------------------
constexpr float   TECH1_BORDER_THICKNESS  = 2.0f;
constexpr float   TECH1_TITLEBAR_HEIGHT   = 26.0f;
constexpr float   TECH1_TITLEBAR_FONTSIZE = 14.0f;

const MyColor TECH1_BORDER_COLOR = MyColor(70, 220, 210, 255);   // cyan/teal frame
const MyColor TECH1_BODY_COLOR   = MyColor(4, 6, 10, 255);       // near-black body
const MyColor TECH1_TITLEBG      = MyColor(178, 20, 20, 255);    // alert red
const MyColor TECH1_TITLETXT     = MyColor(240, 240, 245, 255);  // near-white

void RenderTech1Chrome(Renderer* r, const Vector2& pos, const Vector2& size, const std::wstring& titleText)
{
    // Outer teal frame, drawn as four thin bars around the already-rendered body.
    r->DrawRectangle(pos, Vector2(size.x, TECH1_BORDER_THICKNESS), TECH1_BORDER_COLOR, true);
    r->DrawRectangle(Vector2(pos.x, pos.y + size.y - TECH1_BORDER_THICKNESS),
                      Vector2(size.x, TECH1_BORDER_THICKNESS), TECH1_BORDER_COLOR, true);
    r->DrawRectangle(pos, Vector2(TECH1_BORDER_THICKNESS, size.y), TECH1_BORDER_COLOR, true);
    r->DrawRectangle(Vector2(pos.x + size.x - TECH1_BORDER_THICKNESS, pos.y),
                      Vector2(TECH1_BORDER_THICKNESS, size.y), TECH1_BORDER_COLOR, true);

    // Red alert title bar, inset just inside the frame.
    const Vector2 barPos  = Vector2(pos.x + TECH1_BORDER_THICKNESS, pos.y + TECH1_BORDER_THICKNESS);
    const Vector2 barSize = Vector2(size.x - TECH1_BORDER_THICKNESS * 2.0f, TECH1_TITLEBAR_HEIGHT);
    r->DrawRectangle(barPos, barSize, TECH1_TITLEBG, true);

    if (titleText.empty()) return;

    float capW = 0.0f;
    for (wchar_t ch : titleText)
        capW += r->GetCharacterWidth(ch, TECH1_TITLEBAR_FONTSIZE, true);

    const float capX = barPos.x + (barSize.x - capW) * 0.5f;
    const float capY = barPos.y + (barSize.y - TECH1_TITLEBAR_FONTSIZE * 1.25f) * 0.5f;

    TextRenderStyle style;
    style.fontSize = TECH1_TITLEBAR_FONTSIZE;
    style.bold     = true;
    r->DrawMyTextStyled(titleText, Vector2(capX, capY), TECH1_TITLETXT, style);
}

// --- QuitWindow palette ----------------------------------------------------------------------
// Lifted from the original inline TitleBar control in GUIManager::CreateQuitConfirmDialog.
constexpr float   QUITWIN_TITLEBAR_FONTSIZE = 14.0f;
constexpr int     QUITWIN_GRADIENT_BANDS    = 15;

const MyColor QUITWIN_BODY_COLOR   = MyColor(0, 0, 0, 230);      // semi-transparent black
const MyColor QUITWIN_TITLE_TOP    = MyColor(220, 30, 200, 255); // gradient top (magenta)
const MyColor QUITWIN_TITLE_BOTTOM = MyColor(25, 0, 50, 255);    // gradient bottom (dark purple)
const MyColor QUITWIN_TITLE_TXT    = MyColor(255, 220, 0, 255);  // yellow caption

void RenderQuitWindowChrome(Renderer* r, const Vector2& pos, const Vector2& size, const std::wstring& titleText)
{
    // Magenta -> dark purple vertical gradient title bar, banded like the TitleBar control's
    // own useGradient rendering path (see GUIManager.cpp GUIControlType::TitleBar).
    for (int bi = 0; bi < QUITWIN_GRADIENT_BANDS; ++bi) {
        const float frac = static_cast<float>(bi) / static_cast<float>(QUITWIN_GRADIENT_BANDS - 1);
        MyColor band;
        band.r = static_cast<uint8_t>(QUITWIN_TITLE_TOP.r + (QUITWIN_TITLE_BOTTOM.r - QUITWIN_TITLE_TOP.r) * frac);
        band.g = static_cast<uint8_t>(QUITWIN_TITLE_TOP.g + (QUITWIN_TITLE_BOTTOM.g - QUITWIN_TITLE_TOP.g) * frac);
        band.b = static_cast<uint8_t>(QUITWIN_TITLE_TOP.b + (QUITWIN_TITLE_BOTTOM.b - QUITWIN_TITLE_TOP.b) * frac);
        band.a = 255;
        const float bandH = std::ceil(TITLEBAR_HEIGHT / QUITWIN_GRADIENT_BANDS) + 1.0f;
        const float bandY = pos.y + (TITLEBAR_HEIGHT / QUITWIN_GRADIENT_BANDS) * bi;
        r->DrawRectangle(Vector2(pos.x, bandY), Vector2(size.x, bandH), band, true);
    }

    if (titleText.empty()) return;

    // Left-aligned caption, vertically centred within the title bar — matches the
    // original dialog's lblCenterH = false.
    const float captionH = QUITWIN_TITLEBAR_FONTSIZE * 1.25f;
    const float captionY = pos.y + (TITLEBAR_HEIGHT - captionH) * 0.5f;
    r->DrawMyText(titleText, Vector2(pos.x + 6.0f, captionY), QUITWIN_TITLE_TXT, QUITWIN_TITLEBAR_FONTSIZE);
}

// --- Swarve1 palette -------------------------------------------------------------------------
// Windscribe-style. Tracks the reference screenshot's structural grey lines: rounded outer
// frame, a divider under the top icon row, a divider above the bottom bar, and a rounded-corner
// "location card" outline in between (callers place their own flag/server content inside it).
// Structure only — no text, icons, or controls drawn; callers overlay their own content.
constexpr float SWARVE1_CORNER_RADIUS     = 16.0f;
constexpr float SWARVE1_BORDER_THICKNESS  = 1.0f;
constexpr float SWARVE1_DIVIDER_THICKNESS = 1.0f;
constexpr float SWARVE1_CARD_RADIUS       = 10.0f;

// Divider Y positions and card bounds, as fractions of window size — matched by eye against
// the reference screenshot's proportions rather than exact pixel measurements.
constexpr float SWARVE1_DIVIDER1_Y_FRAC  = 0.205f; // under the hamburger/wordmark/PRO row
constexpr float SWARVE1_DIVIDER2_Y_FRAC  = 0.82f;  // above the firewall/locations bottom bar
constexpr float SWARVE1_CARD_TOP_FRAC    = 0.46f;
constexpr float SWARVE1_CARD_HEIGHT_FRAC = 0.33f;
constexpr float SWARVE1_MARGIN_X_FRAC    = 0.045f;

const MyColor SWARVE1_BODY_COLOR   = MyColor(8, 13, 24, 255);    // near-black navy body
const MyColor SWARVE1_BORDER_COLOR = MyColor(42, 54, 74, 255);   // subtle lighter navy edge — the "grey lines"

// --- MultiSegment1 palette -------------------------------------------------------------------
// Band heights as fractions of the window's total height, top to bottom.
constexpr float MULTISEG1_BAND1_FRAC = 0.25f; // amber top band
constexpr float MULTISEG1_BAND2_FRAC = 0.37f; // amber body band
constexpr float MULTISEG1_BAND3_FRAC = 0.19f; // near-black info band
constexpr float MULTISEG1_BAND4_FRAC = 0.19f; // orange status band

const MyColor MULTISEG1_AMBER_COLOR  = MyColor(172, 132, 40, 255);
const MyColor MULTISEG1_BLACK_COLOR  = MyColor(10, 10, 10, 255);
const MyColor MULTISEG1_ORANGE_COLOR = MyColor(196, 60, 20, 255);

void RenderMultiSegment1Chrome(Renderer* r, const Vector2& pos, const Vector2& size)
{
    float y = pos.y;

    const float h1 = size.y * MULTISEG1_BAND1_FRAC;
    r->DrawRectangle(Vector2(pos.x, y), Vector2(size.x, h1), MULTISEG1_AMBER_COLOR, true);
    y += h1;

    const float h2 = size.y * MULTISEG1_BAND2_FRAC;
    r->DrawRectangle(Vector2(pos.x, y), Vector2(size.x, h2), MULTISEG1_AMBER_COLOR, true);
    y += h2;

    const float h3 = size.y * MULTISEG1_BAND3_FRAC;
    r->DrawRectangle(Vector2(pos.x, y), Vector2(size.x, h3), MULTISEG1_BLACK_COLOR, true);
    y += h3;

    const float h4 = size.y - (y - pos.y);
    r->DrawRectangle(Vector2(pos.x, y), Vector2(size.x, h4), MULTISEG1_ORANGE_COLOR, true);
}

// --- BasicCurved1 palette ----------------------------------------------------------------------
constexpr float BASICCURVED1_CORNER_RADIUS    = 14.0f;
constexpr float BASICCURVED1_BORDER_THICKNESS = 2.0f;

const MyColor BASICCURVED1_BODY_COLOR   = MyColor(22, 24, 28, 255);   // near-black panel
const MyColor BASICCURVED1_BORDER_COLOR = MyColor(70, 74, 82, 255);   // subtle grey edge

enum class RoundedCorner { TopLeft, TopRight, BottomLeft, BottomRight };

// Fills one quarter-disc of a rounded corner via horizontal scanlines built from DrawRectangle —
// the same technique OpenGLRenderer::DrawCircle uses internally — so every renderer backend
// produces identical pixels regardless of whether it has a native ellipse/arc primitive.
void FillRoundedCorner(Renderer* r, float centerX, float centerY, float radius, const MyColor& color, RoundedCorner corner)
{
    const int steps = static_cast<int>(std::ceil(radius));
    for (int i = 0; i <= steps; ++i) {
        const float dy = static_cast<float>(i);
        const float dx = std::sqrt(std::max(0.0f, radius * radius - dy * dy));
        if (dx <= 0.0f) continue;

        float rowY = centerY, rowX = centerX;
        switch (corner) {
            case RoundedCorner::TopLeft:     rowY = centerY - dy; rowX = centerX - dx; break;
            case RoundedCorner::TopRight:    rowY = centerY - dy; rowX = centerX;      break;
            case RoundedCorner::BottomLeft:  rowY = centerY + dy; rowX = centerX - dx; break;
            case RoundedCorner::BottomRight: rowY = centerY + dy; rowX = centerX;      break;
        }
        r->DrawRectangle(Vector2(rowX, rowY), Vector2(dx, 1.0f), color, true);
    }
}

float ClampCornerRadius(const Vector2& size, float requestedRadius)
{
    return std::min(requestedRadius, std::min(size.x, size.y) * 0.5f);
}

// Body fill (opaque, full-window rounded rect) shared by any template that wants soft
// corners. Callers install this on onPreRender so it lands *behind* the window's controls
// (onPreRender -> background -> controls -> onCustomRender) — since the fill is opaque and
// covers the whole window, running it in onCustomRender instead (drawn last) would paint
// over any controls the caller added, such as a close button in the corner.
void FillRoundedRectBody(Renderer* r, const Vector2& pos, const Vector2& size, float radius, const MyColor& color)
{
    // A "+"-shaped cross covering everything except the four corner squares...
    r->DrawRectangle(Vector2(pos.x, pos.y + radius), Vector2(size.x, size.y - radius * 2.0f), color, true);
    r->DrawRectangle(Vector2(pos.x + radius, pos.y), Vector2(size.x - radius * 2.0f, radius), color, true);
    r->DrawRectangle(Vector2(pos.x + radius, pos.y + size.y - radius), Vector2(size.x - radius * 2.0f, radius), color, true);

    // ...then round each corner square down to a quarter-disc.
    FillRoundedCorner(r, pos.x + radius,          pos.y + radius,          radius, color, RoundedCorner::TopLeft);
    FillRoundedCorner(r, pos.x + size.x - radius, pos.y + radius,          radius, color, RoundedCorner::TopRight);
    FillRoundedCorner(r, pos.x + radius,          pos.y + size.y - radius, radius, color, RoundedCorner::BottomLeft);
    FillRoundedCorner(r, pos.x + size.x - radius, pos.y + size.y - radius, radius, color, RoundedCorner::BottomRight);
}

// Border (rounded-corner outline) shared by any template that wants soft corners. Callers
// install this on onCustomRender so it's always drawn on top, crisp against controls near
// the edge.
//
// DrawCurve strokes are centred on the path (half the stroke bleeds outside it, half
// inside) but the straight-edge rectangles below are drawn flush with the window's outer
// edge and extend inward only. Tracing the arcs directly on the true rounded-rect boundary
// would therefore make the border look thinner at the corners than along the straight
// edges (only the inner half would be visible against the fill). Insetting every arc point
// by half the stroke thickness cancels that out: the stroke's outer half then lands back on
// the true boundary and its inner half reaches exactly `thickness` inward, matching the
// straight edges exactly and giving a seamless join at each tangent point.
void DrawRoundedRectBorder(Renderer* r, const Vector2& pos, const Vector2& size, float radius,
                           const MyColor& color, float thickness)
{
    const float half = thickness * 0.5f;

    r->DrawCurve(pos.x + half, pos.y + radius, pos.x + half, pos.y + half, pos.x + radius, pos.y + half,
                 color, thickness, true);
    r->DrawCurve(pos.x + size.x - radius, pos.y + half, pos.x + size.x - half, pos.y + half, pos.x + size.x - half, pos.y + radius,
                 color, thickness, true);
    r->DrawCurve(pos.x + size.x - half, pos.y + size.y - radius, pos.x + size.x - half, pos.y + size.y - half, pos.x + size.x - radius, pos.y + size.y - half,
                 color, thickness, true);
    r->DrawCurve(pos.x + radius, pos.y + size.y - half, pos.x + half, pos.y + size.y - half, pos.x + half, pos.y + size.y - radius,
                 color, thickness, true);

    r->DrawRectangle(Vector2(pos.x + radius, pos.y), Vector2(size.x - radius * 2.0f, thickness), color, true);
    r->DrawRectangle(Vector2(pos.x + radius, pos.y + size.y - thickness), Vector2(size.x - radius * 2.0f, thickness), color, true);
    r->DrawRectangle(Vector2(pos.x, pos.y + radius), Vector2(thickness, size.y - radius * 2.0f), color, true);
    r->DrawRectangle(Vector2(pos.x + size.x - thickness, pos.y + radius), Vector2(thickness, size.y - radius * 2.0f), color, true);
}

void RenderBasicCurved1Body(Renderer* r, const Vector2& pos, const Vector2& size)
{
    FillRoundedRectBody(r, pos, size, ClampCornerRadius(size, BASICCURVED1_CORNER_RADIUS), BASICCURVED1_BODY_COLOR);
}

void RenderBasicCurved1Border(Renderer* r, const Vector2& pos, const Vector2& size)
{
    DrawRoundedRectBorder(r, pos, size, ClampCornerRadius(size, BASICCURVED1_CORNER_RADIUS),
                          BASICCURVED1_BORDER_COLOR, BASICCURVED1_BORDER_THICKNESS);
}

void RenderSwarve1Body(Renderer* r, const Vector2& pos, const Vector2& size)
{
    FillRoundedRectBody(r, pos, size, ClampCornerRadius(size, SWARVE1_CORNER_RADIUS), SWARVE1_BODY_COLOR);
}

void RenderSwarve1Border(Renderer* r, const Vector2& pos, const Vector2& size)
{
    // Outer rounded frame.
    DrawRoundedRectBorder(r, pos, size, ClampCornerRadius(size, SWARVE1_CORNER_RADIUS),
                          SWARVE1_BORDER_COLOR, SWARVE1_BORDER_THICKNESS);

    const float marginX = size.x * SWARVE1_MARGIN_X_FRAC;

    // Divider under the top icon row — a straight grey line, drawn with DrawCornerCut rather
    // than a thin DrawRectangle strip since it's exactly the "line between two points" case
    // that primitive exists for.
    const float divider1Y = pos.y + size.y * SWARVE1_DIVIDER1_Y_FRAC;
    r->DrawCornerCut(pos.x + marginX, divider1Y, pos.x + size.x - marginX, divider1Y,
                     SWARVE1_BORDER_COLOR, SWARVE1_DIVIDER_THICKNESS, true);

    // Divider above the bottom firewall/locations bar.
    const float divider2Y = pos.y + size.y * SWARVE1_DIVIDER2_Y_FRAC;
    r->DrawCornerCut(pos.x + marginX, divider2Y, pos.x + size.x - marginX, divider2Y,
                     SWARVE1_BORDER_COLOR, SWARVE1_DIVIDER_THICKNESS, true);

    // Inner rounded "location card" outline, between the two dividers — callers place their
    // own flag/server texture and text inside it; the template only draws its frame.
    const Vector2 cardPos  = Vector2(pos.x + marginX, pos.y + size.y * SWARVE1_CARD_TOP_FRAC);
    const Vector2 cardSize = Vector2(size.x - marginX * 2.0f, size.y * SWARVE1_CARD_HEIGHT_FRAC);
    DrawRoundedRectBorder(r, cardPos, cardSize, ClampCornerRadius(cardSize, SWARVE1_CARD_RADIUS),
                         SWARVE1_BORDER_COLOR, SWARVE1_BORDER_THICKNESS);
}

} // anonymous namespace

void ApplyGUIWindowTemplate(const std::shared_ptr<GUIWindow>& window,
                             GUIWindowTemplateType templateType,
                             const std::wstring& titleText)
{
    if (!window) return;

    switch (templateType) {
        case GUIWindowTemplateType::Tech1: {
            window->backgroundColor     = TECH1_BODY_COLOR;
            window->backgroundTextureId = -1;

            // Chain any onCustomRender already installed (e.g. by a caller adding its own
            // content-drawing hook) so it still fires before the template chrome is drawn
            // on top — templates never replace a caller's rendering, only frame it.
            std::function<void(Renderer*)> previousRender = window->onCustomRender;
            std::weak_ptr<GUIWindow> weakWin = window;
            window->onCustomRender = [weakWin, titleText, previousRender](Renderer* r) {
                if (previousRender) previousRender(r);
                auto win = weakWin.lock();
                if (!win || !r) return;
                RenderTech1Chrome(r, win->position, win->size, titleText);
            };
            break;
        }

        case GUIWindowTemplateType::QuitWindow: {
            window->backgroundColor     = QUITWIN_BODY_COLOR;
            window->backgroundTextureId = -1;

            std::function<void(Renderer*)> previousRender = window->onCustomRender;
            std::weak_ptr<GUIWindow> weakWin = window;
            window->onCustomRender = [weakWin, titleText, previousRender](Renderer* r) {
                if (previousRender) previousRender(r);
                auto win = weakWin.lock();
                if (!win || !r) return;
                RenderQuitWindowChrome(r, win->position, win->size, titleText);
            };
            break;
        }

        case GUIWindowTemplateType::Swarve1: {
            // Fully transparent default background — corners are rounded now, so the body
            // fill below paints the entire shape itself; a square opaque backgroundColor
            // would show through at the cut corners (same reasoning as BasicCurved1).
            window->backgroundColor     = MyColor(0, 0, 0, 0);
            window->backgroundTextureId = -1;

            // Body fill goes on onPreRender (behind background/controls) so it never paints
            // over controls the caller adds (e.g. a close button) — see RenderSwarve1Body.
            std::function<void(Renderer*)> previousPreRender = window->onPreRender;
            std::weak_ptr<GUIWindow> weakWinBody = window;
            window->onPreRender = [weakWinBody, previousPreRender](Renderer* r) {
                if (previousPreRender) previousPreRender(r);
                auto win = weakWinBody.lock();
                if (!win || !r) return;
                RenderSwarve1Body(r, win->position, win->size);
            };

            // Border goes on onCustomRender (drawn last) so it stays crisp on top of controls.
            std::function<void(Renderer*)> previousRender = window->onCustomRender;
            std::weak_ptr<GUIWindow> weakWin = window;
            window->onCustomRender = [weakWin, previousRender](Renderer* r) {
                if (previousRender) previousRender(r);
                auto win = weakWin.lock();
                if (!win || !r) return;
                RenderSwarve1Border(r, win->position, win->size);
            };
            break;
        }

        case GUIWindowTemplateType::MultiSegment1: {
            window->backgroundColor     = MULTISEG1_AMBER_COLOR;
            window->backgroundTextureId = -1;

            std::function<void(Renderer*)> previousRender = window->onCustomRender;
            std::weak_ptr<GUIWindow> weakWin = window;
            window->onCustomRender = [weakWin, previousRender](Renderer* r) {
                if (previousRender) previousRender(r);
                auto win = weakWin.lock();
                if (!win || !r) return;
                RenderMultiSegment1Chrome(r, win->position, win->size);
            };
            break;
        }

        case GUIWindowTemplateType::BasicCurved1: {
            // Fully transparent default background — the shape isn't a plain rect, so the
            // body fill below paints the entire rounded body itself; a square opaque
            // backgroundColor would show through at the cut corners.
            window->backgroundColor     = MyColor(0, 0, 0, 0);
            window->backgroundTextureId = -1;

            // Body fill goes on onPreRender (behind background/controls) so it never paints
            // over controls the caller adds (e.g. a close button) — see RenderBasicCurved1Body.
            std::function<void(Renderer*)> previousPreRender = window->onPreRender;
            std::weak_ptr<GUIWindow> weakWinBody = window;
            window->onPreRender = [weakWinBody, previousPreRender](Renderer* r) {
                if (previousPreRender) previousPreRender(r);
                auto win = weakWinBody.lock();
                if (!win || !r) return;
                RenderBasicCurved1Body(r, win->position, win->size);
            };

            // Border goes on onCustomRender (drawn last) so it stays crisp on top of controls.
            std::function<void(Renderer*)> previousRender = window->onCustomRender;
            std::weak_ptr<GUIWindow> weakWin = window;
            window->onCustomRender = [weakWin, previousRender](Renderer* r) {
                if (previousRender) previousRender(r);
                auto win = weakWin.lock();
                if (!win || !r) return;
                RenderBasicCurved1Border(r, win->position, win->size);
            };
            break;
        }

        case GUIWindowTemplateType::None:
        default:
            break;
    }
}
