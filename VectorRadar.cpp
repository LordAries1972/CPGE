// ================================================================================================
//  VectorRadar.cpp  -  implementation of the radar widgets declared in VectorRadar.h
//
//  All drawing goes through MyRadarCanvas, so this file contains no renderer code.  Everything is per-frame work done with a handful of
//  rectangles / polygons and no heap allocation (MyGridRadar::BuildRuns allocates only when the map changes, into the caller's vector).
// ================================================================================================
#include "Includes.h"                    // global platform / renderer definitions (must come first)
#include "VectorRadar.h"
#include "VectorShapes.h"              // MyShapes::Shade, used to dim out-of-range blips
#include "MathPrecalculation.h"          // FAST_SINCOS_DEG: table sine / cosine

#include <cmath>

// ================================================================================================
//  MyStripRadar
// ================================================================================================
MyStripRadar::MyStripRadar(MyRadarCanvas& canvas, float x, float y, float w, float h)
    : m_canvas(canvas), m_x(x), m_y(y), m_w(w), m_h(h)
{
}

void MyStripRadar::DrawFrame(const MyColor& border, const MyColor& background) const
{
    m_canvas.FillRect(m_x - 2.0f, m_y - 2.0f, m_w + 4.0f, m_h + 4.0f, border);                          // border: the strip grown by two pixels
    m_canvas.FillRect(m_x, m_y, m_w, m_h, background);
}

void MyStripRadar::Dot(float u, const MyColor& colour, float size, float v) const
{
    // v = 0 is the bottom of the strip, 1 the top.  The usable travel is the strip height minus the dot, so a dot at either extreme still lies inside the frame;
    // v = 0.5 lands exactly on the centre line (the old behaviour).  The !(v > 0) form also sends a NaN to the bottom instead of poisoning the coordinates.
    const float vc = !(v > 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    const float travel = MyMath::Max(0.0f, m_h - size);
    const float cy = m_y + size * 0.5f + (1.0f - vc) * travel;                                          // screen y grows downward
    m_canvas.FillRect(m_x + u * m_w - size * 0.5f, cy - size * 0.5f, size, size, colour);
}

void MyStripRadar::DotWrapped(float worldX, float cameraX, float worldSize, const MyColor& colour, float size, float v) const
{
    // Distance from the camera in world units, expressed as a fraction of the world, shifted so the camera is at 0.5, then wrapped into 0..1.
    // Repeat() makes the shortest way round the ring the one that is shown, whatever the sign or size of worldX - cameraX.
    const float u = MyMath::Repeat((worldX - cameraX) / worldSize + 0.5f, 1.0f);
    Dot(u, colour, size, v);
}

void MyStripRadar::Baseline(float v, const MyColor& colour) const
{
    const float vc = !(v > 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);                                     // NaN-safe clamp, as in Dot()
    m_canvas.FillRect(m_x, m_y + (1.0f - vc) * m_h - 0.5f, m_w, 1.0f, colour);
}

void MyStripRadar::ViewBox(float fraction, const MyColor& colour) const
{
    const float bw = m_w * fraction, bx = m_x + (m_w - bw) * 0.5f;                                      // centred: the radar is camera-centred
    m_canvas.FillRect(bx, m_y - 1.0f, bw, 2.0f, colour);                                                // top edge
    m_canvas.FillRect(bx, m_y + m_h - 1.0f, bw, 2.0f, colour);                                          // bottom edge
    m_canvas.FillRect(bx - 1.0f, m_y, 2.0f, m_h, colour);                                               // left edge
    m_canvas.FillRect(bx + bw - 1.0f, m_y, 2.0f, m_h, colour);                                          // right edge
}

// ================================================================================================
//  MyScopeRadar
// ================================================================================================
MyScopeRadar::MyScopeRadar(MyRadarCanvas& canvas, float cx, float cy, float radius, float range)
    : m_canvas(canvas), m_cx(cx), m_cy(cy), m_r(radius), m_k(radius / range)
{
}

void MyScopeRadar::Disc(float radius, const MyColor& colour) const
{
    Vector2 p[32];
    for (int i = 0; i < 32; ++i)                                                                        // a 32-gon: round enough at HUD sizes, cheap to fill
    {
        float s, c;
        FAST_SINCOS_DEG(static_cast<float>(i) * 11.25f, s, c);
        p[i] = Vector2(m_cx + radius * c, m_cy + radius * s);
    }
    m_canvas.FillPoly(p, 32, colour);
}

void MyScopeRadar::Line(float x0, float y0, float x1, float y1, float halfThickness, const MyColor& colour) const
{
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    const float nx = -dy / len * halfThickness, ny = dx / len * halfThickness;                          // normal to the line, scaled to half the thickness
    const Vector2 q[4] = { Vector2(x0 + nx, y0 + ny), Vector2(x1 + nx, y1 + ny), Vector2(x1 - nx, y1 - ny), Vector2(x0 - nx, y0 - ny) };
    m_canvas.FillPoly(q, 4, colour);
}

// A thin circle outline: 48 short quads round the circumference.
void MyScopeRadar::Ring(float radius, float halfThickness, const MyColor& colour) const
{
    constexpr int kSeg = 48;
    float px = m_cx + radius, py = m_cy;
    for (int i = 1; i <= kSeg; ++i)
    {
        float s, c;
        FAST_SINCOS_DEG(360.0f * static_cast<float>(i) / static_cast<float>(kSeg), s, c);
        const float nx = m_cx + radius * c, ny = m_cy + radius * s;
        Line(px, py, nx, ny, halfThickness, colour);
        px = nx; py = ny;
    }
}

void MyScopeRadar::DrawFace(const MyColor& rim, const MyColor& scope, const MyColor& ring, const MyColor& cross) const
{
    if (scope.a < 255)                                                                                  // a TRANSLUCENT scope: solid discs underneath would show through it, so the rim and the
    {                                                                                                   // half-range ring are drawn as thin outlines and only the scope itself is a (see-through) disc
        Disc(m_r, scope);
        Ring(m_r + 1.0f, 1.4f, rim);
        Ring(m_r * 0.5f, 0.7f, ring);
        Line(m_cx - m_r, m_cy, m_cx + m_r, m_cy, 0.6f, cross);
        Line(m_cx, m_cy - m_r, m_cx, m_cy + m_r, 0.6f, cross);
        return;
    }
    Disc(m_r + 2.0f, rim);
    Disc(m_r, scope);
    Disc(m_r * 0.5f, ring);                                                                             // the half-range ring: a lighter disc ...
    Disc(m_r * 0.49f, scope);                                                                           // ... with a slightly smaller scope-coloured disc on top leaves a thin ring
    Line(m_cx - m_r, m_cy, m_cx + m_r, m_cy, 0.6f, cross);                                              // cross-hair
    Line(m_cx, m_cy - m_r, m_cx, m_cy + m_r, 0.6f, cross);
}

void MyScopeRadar::DrawSweep(float angleDeg, const MyColor& colour) const
{
    float s, c;
    FAST_SINCOS_DEG(MyMath::Repeat(angleDeg, 360.0f), s, c);
    Line(m_cx, m_cy, m_cx + m_r * c, m_cy + m_r * s, 0.9f, colour);
}

void MyScopeRadar::Blip(float dx, float dz, float size, const MyColor& colour) const
{
    float px = dx * m_k, py = -dz * m_k;                                                                // world +z is up the screen (screen y is down)
    const float d = std::sqrt(px * px + py * py);
    MyColor c = colour;
    if (d > m_r - 3.0f)                                                                                 // out of range: pinned just inside the rim, dimmer and smaller
    {
        const float s = (m_r - 3.0f) / d;
        px *= s; py *= s;
        c = MyShapes::Shade(colour, 0.45f);
        size *= 0.8f;
    }
    const Vector2 q[4] = { Vector2(m_cx + px, m_cy + py - size), Vector2(m_cx + px + size, m_cy + py), Vector2(m_cx + px, m_cy + py + size), Vector2(m_cx + px - size, m_cy + py) };
    m_canvas.FillPoly(q, 4, c);
}

void MyScopeRadar::DrawHeading(float aimX, float aimZ, const MyColor& colour) const
{
    const float ax = aimX, ay = -aimZ;                                                                  // screen direction of the aim
    const Vector2 tri[3] = { Vector2(m_cx + ax * 6.0f, m_cy + ay * 6.0f),
                             Vector2(m_cx - ax * 4.0f - ay * 3.5f, m_cy - ay * 4.0f + ax * 3.5f),
                             Vector2(m_cx - ax * 4.0f + ay * 3.5f, m_cy - ay * 4.0f - ax * 3.5f) };
    m_canvas.FillPoly(tri, 3, colour);
}

void MyScopeRadar::DrawLabel(const wchar_t* text, float screenH, const MyColor& colour) const
{
    m_canvas.DrawLabel(text, m_cx - 1.4f * screenH / 42.0f, m_cy - m_r - screenH * 0.032f, MyMath::Clamp(screenH / 46.0f, 10.0f, 18.0f), colour);
}

// ================================================================================================
//  MyGridRadar
// ================================================================================================
MyGridRadar::MyGridRadar(MyRadarCanvas& canvas, float x, float y, float w, float h, int mapTiles)
    : m_canvas(canvas), m_x(x), m_y(y), m_w(w), m_h(h), m_n(static_cast<float>(mapTiles))
{
}

void MyGridRadar::BuildRuns(std::vector<MyRadarRun>& runs, int cells, const std::function<MyColor(int, int)>& cellColour)
{
    runs.clear();                                                                                       // keeps the vector's capacity: no allocation once it has grown
    for (int cz = 0; cz < cells; ++cz)
    {
        int cx = 0;
        while (cx < cells)
        {
            const MyColor c = cellColour(cx, cz);
            if (c.a == 0) { ++cx; continue; }                                                           // alpha 0 = unexplored: left black
            MyRadarRun run; run.x = cx; run.y = cz; run.w = 1; run.c = c;
            while (cx + run.w < cells)                                                                  // extend while the next cell has the same colour
            {
                const MyColor c2 = cellColour(cx + run.w, cz);
                if (c2.a == 0 || c2.r != c.r || c2.g != c.g || c2.b != c.b) break;
                ++run.w;
            }
            runs.push_back(run);
            cx += run.w;
        }
    }
}

void MyGridRadar::DrawFrame(const MyColor& border, const MyColor& background) const
{
    m_canvas.FillRect(m_x - 2.0f, m_y - 2.0f, m_w + 4.0f, m_h + 4.0f, border);
    m_canvas.FillRect(m_x, m_y, m_w, m_h, background);
}

void MyGridRadar::DrawRuns(const std::vector<MyRadarRun>& runs, int cells) const
{
    const float cs = m_w / static_cast<float>(cells);                                                   // one cell in pixels
    for (const MyRadarRun& run : runs)                                                                  // +0.6 px overlap hides hairline gaps between neighbouring rectangles
        m_canvas.FillRect(m_x + static_cast<float>(run.x) * cs, m_y + static_cast<float>(cells - 1 - run.y) * cs, static_cast<float>(run.w) * cs + 0.6f, cs + 0.6f, run.c);
}

void MyGridRadar::MarkRect(float tx, float tz, float sizeTiles, const MyColor& colour) const
{
    const float k = m_w / m_n;                                                                          // one tile in pixels
    m_canvas.FillRect(m_x + tx * k, m_y + (m_n - tz - sizeTiles) * k, sizeTiles * k + 1.0f, sizeTiles * k + 1.0f, colour);
}

void MyGridRadar::MarkPoint(float x, float z, float sizePx, const MyColor& colour) const
{
    const float k = m_w / m_n;
    m_canvas.FillRect(m_x + x * k - 1.0f, m_y + (m_n - z) * k - 1.0f, sizePx, sizePx, colour);
}

void MyGridRadar::ViewBox(float x0, float x1, float z0, float z1, const MyColor& colour) const
{
    const float k = m_w / m_n;
    const float bx0 = m_x + MyMath::Clamp(x0, 0.0f, m_n) * k, bx1 = m_x + MyMath::Clamp(x1, 0.0f, m_n) * k;
    const float by0 = m_y + (m_n - MyMath::Clamp(z1, 0.0f, m_n)) * k, by1 = m_y + (m_n - MyMath::Clamp(z0, 0.0f, m_n)) * k;   // z is flipped: north is up
    m_canvas.FillRect(bx0, by0, bx1 - bx0, 1.5f, colour);                                               // top
    m_canvas.FillRect(bx0, by1, bx1 - bx0, 1.5f, colour);                                               // bottom
    m_canvas.FillRect(bx0, by0, 1.5f, by1 - by0, colour);                                               // left
    m_canvas.FillRect(bx1, by0, 1.5f, by1 - by0 + 1.5f, colour);                                        // right
}
