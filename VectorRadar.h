// ================================================================================================
//  VectorRadar.h  -  reusable radar / mini-map widgets
//
//  Three widgets cover every radar in the Vectors demo and are independent of the renderer and of any game:
//
//    MyStripRadar   a wide, flat strip showing a WRAPPED world (a ring or a looping side-scroller): dots wrap round the strip and a box
//                   marks the part of the world the camera currently shows.                          (Defender, Resogun)
//    MyScopeRadar   a round scope, north up, centred on the player, with range rings, a rotating sweep line, blips that are pinned to
//                   the rim (dimmed) when out of range and an arrow showing the player's aim.        (Morpheous)
//    MyGridRadar    a square mini-map of a tile map: the map is reduced to coloured cells, merged into horizontal runs so a few hundred
//                   rectangles draw a whole map, plus footprints, points and the camera box.         (Dominion RTS)
//
//  DRAWING: the widgets never call a renderer.  They draw through the tiny MyRadarCanvas interface (filled rectangle, filled polygon,
//  text).  A project implements it once - for the Vectors demo it is a 10-line adapter over Renderer::DrawRectangle /
//  DrawFilledPolygon / DrawMyText (RendererRadarCanvas in main.cpp).  Widgets are cheap value objects: construct one on the stack
//  inside the HUD function, call the Draw / Dot / Blip functions, let it go out of scope.  Nothing allocates per frame (MyGridRadar's
//  run list is owned by the caller and rebuilt only when the map changes).
//
//  Dependencies: Vectors.h (Vector2), Color.h (MyColor), VectorShapes.h (Shade) - nothing else.
//
//  Usage (strip radar, one frame):
//        RendererRadarCanvas canvas(r);
//        MyStripRadar radar(canvas, x, y, w, h);
//        radar.DrawFrame();
//        radar.DotWrapped(enemy.x, camX, worldSize, MyColor(255, 70, 70, 255), 4.0f);
//        radar.ViewBox(visibleWidth / worldSize, MyColor(255, 255, 255, 150));
// ================================================================================================
#pragma once

#include "Includes.h"                    // global platform / renderer definitions (must come first)
#include "Vectors.h"
#include "Color.h"

#include <functional>
#include <vector>

// ------------------------------------------------------------------------------------------------
//  MyRadarCanvas - the drawing surface the radar widgets use.  Coordinates are screen pixels, y down.
// ------------------------------------------------------------------------------------------------
class MyRadarCanvas
{
public:
    virtual ~MyRadarCanvas() = default;
    virtual void FillRect(float x, float y, float w, float h, const MyColor& colour) = 0;               // solid axis-aligned rectangle
    virtual void FillPoly(const Vector2* points, int count, const MyColor& colour) = 0;                  // solid convex polygon
    virtual void DrawLabel(const wchar_t* text, float x, float y, float size, const MyColor& colour) = 0;   // text with its top-left at (x, y); size in pixels
};

// ------------------------------------------------------------------------------------------------
//  MyStripRadar - radar of a wrapped (looping) world on a horizontal strip.
// ------------------------------------------------------------------------------------------------
class MyStripRadar
{
public:
    // The strip occupies the screen rectangle (x, y, w, h).
    MyStripRadar(MyRadarCanvas& canvas, float x, float y, float w, float h);

    // Draws the 2-pixel border and the dark background.  Call first.
    void DrawFrame(const MyColor& border = MyColor(90, 110, 150, 255), const MyColor& background = MyColor(4, 8, 16, 255)) const;

    // A square dot of `size` pixels at u (0 = left edge, 1 = right edge).  v is the height in the strip: 0 = bottom, 1 = top, 0.5 (the default) =
    // the vertical centre line.  The dot always stays fully inside the strip, so v is clamped to 0..1 and the dot's own size is allowed for.
    void Dot(float u, const MyColor& colour, float size, float v = 0.5f) const;

    // A dot for something at worldX in a world that wraps every worldSize units.  The strip is CAMERA-CENTRED: the camera sits in the middle
    // and the world wraps round, so objects just behind the camera appear on the right and just ahead on the left of the wrap seam.
    // v is the height as in Dot(): pass (worldY - lowestY) / (highestY - lowestY) to show where the object really is above the ground; the default
    // keeps the old behaviour (everything on the centre line).
    void DotWrapped(float worldX, float cameraX, float worldSize, const MyColor& colour, float size, float v = 0.5f) const;

    // A thin horizontal reference line across the whole strip at height v (0 = bottom, 1 = top): the ground level, say, so the heights of the dots
    // can be judged against it.  Draw it after DrawFrame() and before the dots.
    void Baseline(float v, const MyColor& colour) const;

    // The camera window: a hollow box in the middle of the strip whose width is `fraction` of the strip (visible world width / worldSize).
    void ViewBox(float fraction, const MyColor& colour) const;

private:
    MyRadarCanvas& m_canvas;
    float m_x, m_y, m_w, m_h;
};

// ------------------------------------------------------------------------------------------------
//  MyScopeRadar - round, north-up scope centred on the player.
// ------------------------------------------------------------------------------------------------
class MyScopeRadar
{
public:
    // Centre (cx, cy) in pixels, radius in pixels, and `range`: the world distance that reaches the rim.
    MyScopeRadar(MyRadarCanvas& canvas, float cx, float cy, float radius, float range);

    // Rim, scope disc, a range ring half way out and the cross-hair lines.  Defaults are the green phosphor look of Morpheous.  Give `scope` an alpha below 255 for a see-through
    // scope (things drawn under the radar show through): the rim and ring then become thin outlines.
    void DrawFace(const MyColor& rim = MyColor(80, 200, 140, 255), const MyColor& scope = MyColor(4, 18, 14, 255),
                  const MyColor& ring = MyColor(8, 30, 24, 255), const MyColor& cross = MyColor(20, 70, 52, 255)) const;

    // The sweep line, turned to angleDeg (pass an angle that grows with time for the classic rotating beam).
    void DrawSweep(float angleDeg, const MyColor& colour = MyColor(40, 140, 100, 255)) const;

    // A diamond blip for something at world offset (dx, dz) from the player (+z is up the screen).  Beyond the range it is pinned to the
    // rim, dimmed and slightly smaller, so you always see which way it is.
    void Blip(float dx, float dz, float size, const MyColor& colour) const;

    // The player: a triangle pointing along the aim direction (aimX, aimZ in world axes).
    void DrawHeading(float aimX, float aimZ, const MyColor& colour = MyColor(255, 255, 255, 255)) const;

    // A caption above the scope; screenH scales the text with the window height.
    void DrawLabel(const wchar_t* text, float screenH, const MyColor& colour = MyColor(120, 230, 180, 255)) const;

private:
    void Disc(float radius, const MyColor& colour) const;                                                // filled 32-gon
    void Ring(float radius, float halfThickness, const MyColor& colour) const;                           // a thin circle outline (used when the scope is translucent)
    void Line(float x0, float y0, float x1, float y1, float halfThickness, const MyColor& colour) const;  // thin quad

    MyRadarCanvas& m_canvas;
    float m_cx, m_cy, m_r, m_k;                                                                          // m_k = pixels per world unit
};

// ------------------------------------------------------------------------------------------------
//  MyGridRadar - mini-map of a square tile map.
// ------------------------------------------------------------------------------------------------

// One horizontal run of identical radar cells: `w` cells starting at cell (x, y).  The caller keeps a std::vector of these and rebuilds
// it (BuildRuns) only when the map changes.
struct MyRadarRun { int x = 0, y = 0, w = 0; MyColor c = MyColor(0, 0, 0, 255); };

class MyGridRadar
{
public:
    // The map is mapTiles x mapTiles tiles; the radar occupies the screen rectangle (x, y, w, h).  World +z is UP the screen.
    MyGridRadar(MyRadarCanvas& canvas, float x, float y, float w, float h, int mapTiles);

    // Reduces a map to merged runs.  `cells` x `cells` cells; cellColour(cx, cz) gives a cell's colour, or any colour with alpha 0 for
    // "unexplored" (left black).  Neighbouring cells of the same colour merge, so the result is a few hundred runs whatever the map size.
    static void BuildRuns(std::vector<MyRadarRun>& runs, int cells, const std::function<MyColor(int, int)>& cellColour);

    // Border + dark background.
    void DrawFrame(const MyColor& border = MyColor(90, 110, 150, 255), const MyColor& background = MyColor(2, 3, 8, 255)) const;

    // Draws the runs built by BuildRuns for a `cells` x `cells` reduction of the map.
    void DrawRuns(const std::vector<MyRadarRun>& runs, int cells) const;

    // A building footprint: its south-west corner tile (tx, tz) and its size in tiles.
    void MarkRect(float tx, float tz, float sizeTiles, const MyColor& colour) const;

    // A unit: a small square (sizePx pixels) centred near map position (x, z).
    void MarkPoint(float x, float z, float sizePx, const MyColor& colour) const;

    // The camera box: the map rectangle [x0, x1] x [z0, z1], clamped to the map, drawn as a thin outline.
    void ViewBox(float x0, float x1, float z0, float z1, const MyColor& colour) const;

private:
    MyRadarCanvas& m_canvas;
    float m_x, m_y, m_w, m_h;
    float m_n;                                                                                           // map size in tiles
};
