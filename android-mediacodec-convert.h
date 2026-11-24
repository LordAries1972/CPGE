// android-mediacodec-convert.h — CPU pixel/audio conversion for the Android
// MediaCodec backend (Phase D step 4, 24 Nov 2026).
//
// Why this file exists: on Linux, MoviePlayer / ScreenRecorder use
// libswscale (YUV <-> BGRA) and libswresample (audio rate conversion).
// On Android there is no FFmpeg — the platform MediaCodec APIs hand us
// raw YUV420 frames and PCM audio, so the conversion has to be done here.
//
// The code is plain C++17 with no Android and no FFmpeg includes, so the
// same file is compiled into the Android library AND into the host-side
// validation test (scope/test-mediacodec-convert.cpp), where every
// function is checked against libswscale / libswresample. If you change
// the maths here, re-run that test.
//
// Colour: BT.601 limited range, the range MediaCodec decoders output for
// H.264 (and FFmpeg's default). YUV420 is planar (I420: separate U and V
// planes) or semi-planar (NV12: one interleaved UV plane), selected by
// the uvPixelStride argument (1 = planar, 2 = semi-planar).
//
// Performance note: this is scalar C++ — no NEON fast path yet. At
// 1920x1080 the 1:1 YUV->BGRA pass costs roughly 10 ms/frame on a phone
// core, which is fine for a movie; the bilinear scaling path is several
// times heavier and only runs when the renderer size differs from the
// video size. A NEON pass is the obvious follow-up once there is a
// device to measure on — the function signatures are already stride
// based so a NEON implementation can drop in underneath them.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace AndroidMediaConvert {

// ---------------------------------------------------------------------------
// BT.601 limited-range conversion (integer fixed point, 8-bit coefficients)
// ---------------------------------------------------------------------------

// YUV -> RGB. y in [16,235], u/v in [16,240]; result clamped to [0,255].
inline void YuvToRgb(int y, int u, int v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    const int c = y - 16;
    const int d = u - 128;
    const int e = v - 128;
    const int ri = (298 * c + 409 * e + 128) >> 8;
    const int gi = (298 * c - 100 * d - 208 * e + 128) >> 8;
    const int bi = (298 * c + 516 * d + 128) >> 8;
    r = static_cast<uint8_t>(std::clamp(ri, 0, 255));
    g = static_cast<uint8_t>(std::clamp(gi, 0, 255));
    b = static_cast<uint8_t>(std::clamp(bi, 0, 255));
}

// RGB -> YUV (BT.601 limited range).
inline void RgbToYuv(int r, int g, int b, uint8_t& y, uint8_t& u, uint8_t& v)
{
    const int yi = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    const int ui = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    const int vi = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
    y = static_cast<uint8_t>(std::clamp(yi, 0, 255));
    u = static_cast<uint8_t>(std::clamp(ui, 0, 255));
    v = static_cast<uint8_t>(std::clamp(vi, 0, 255));
}

namespace detail {

// Bilinear sample of one 8-bit plane at fractional coordinates (x, y).
// The plane is w x h, row stride `stride` bytes, samples `pixelStride`
// bytes apart (1 for a luma/chroma plane, 2 for an interleaved UV plane
// sampled at `channelOffset` 0 = U, 1 = V). Coordinates are clamped to
// the plane edges, so the caller never reads out of bounds.
inline int BilinearSample(const uint8_t* plane, int stride, int pixelStride,
                          int w, int h, float x, float y, int channelOffset)
{
    const float cx = std::clamp(x, 0.0f, static_cast<float>(w - 1));
    const float cy = std::clamp(y, 0.0f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(cx);
    const int y0 = static_cast<int>(cy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);
    const uint8_t* p00 = plane + y0 * stride + x0 * pixelStride + channelOffset;
    const uint8_t* p10 = plane + y0 * stride + x1 * pixelStride + channelOffset;
    const uint8_t* p01 = plane + y1 * stride + x0 * pixelStride + channelOffset;
    const uint8_t* p11 = plane + y1 * stride + x1 * pixelStride + channelOffset;
    const float top = static_cast<float>(*p00) + (static_cast<float>(*p10) - static_cast<float>(*p00)) * fx;
    const float bot = static_cast<float>(*p01) + (static_cast<float>(*p11) - static_cast<float>(*p01)) * fx;
    return static_cast<int>(top + (bot - top) * fy + 0.5f);
}

// Bilinear sample of a BGRA image, returning one channel (0 = B, 1 = G,
// 2 = R). Same clamping as BilinearSample.
inline int BilinearSampleBgra(const uint8_t* bgra, int stride, int w, int h,
                              float x, float y, int channel)
{
    const float cx = std::clamp(x, 0.0f, static_cast<float>(w - 1));
    const float cy = std::clamp(y, 0.0f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(cx);
    const int y0 = static_cast<int>(cy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);
    const uint8_t* p00 = bgra + y0 * stride + x0 * 4 + channel;
    const uint8_t* p10 = bgra + y0 * stride + x1 * 4 + channel;
    const uint8_t* p01 = bgra + y1 * stride + x0 * 4 + channel;
    const uint8_t* p11 = bgra + y1 * stride + x1 * 4 + channel;
    const float top = static_cast<float>(*p00) + (static_cast<float>(*p10) - static_cast<float>(*p00)) * fx;
    const float bot = static_cast<float>(*p01) + (static_cast<float>(*p11) - static_cast<float>(*p01)) * fx;
    return static_cast<int>(top + (bot - top) * fy + 0.5f);
}

} // namespace detail

// ---------------------------------------------------------------------------
// YUV420 -> BGRA
// ---------------------------------------------------------------------------
// yPlane: luma, srcW x srcH, row stride srcStrideY.
// uPlane / vPlane: chroma. Planar (uvPixelStride == 1): uPlane and vPlane
// are separate planes, each srcW/2 x srcH/2 with row stride srcStrideUV.
// Semi-planar (uvPixelStride == 2): uPlane points at the interleaved UV
// plane (U at even bytes, V at odd bytes) and vPlane is ignored.
// dst: BGRA, dstW x dstH, row stride dstStride. The caller allocates it.
// When the sizes match, a direct 1:1 pass runs (no scaling); otherwise
// the frame is bilinear-scaled to the destination size.
inline void Yuv420ToBgra(const uint8_t* yPlane,
                         const uint8_t* uPlane, const uint8_t* vPlane,
                         int uvPixelStride,
                         int srcW, int srcH, int srcStrideY, int srcStrideUV,
                         uint8_t* dst, int dstW, int dstH, int dstStride)
{
    if (!yPlane || !uPlane || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    const int chromaW = (srcW + 1) / 2;
    const int chromaH = (srcH + 1) / 2;
    // The semi-planar UV plane is one plane; the planar V plane is separate.
    const uint8_t* vSrc = (uvPixelStride == 2) ? uPlane : vPlane;
    const int vOffset = (uvPixelStride == 2) ? 1 : 0;

    if (srcW == dstW && srcH == dstH)
    {
        // Direct pass: one luma sample and one chroma sample per pixel.
        for (int y = 0; y < dstH; ++y)
        {
            const uint8_t* yRow = yPlane + y * srcStrideY;
            const uint8_t* uRow = uPlane + (y / 2) * srcStrideUV;
            const uint8_t* vRow = vSrc + (y / 2) * srcStrideUV;
            uint8_t* dRow = dst + y * dstStride;
            for (int x = 0; x < dstW; ++x)
            {
                const int cx = (x / 2) * uvPixelStride;
                uint8_t r, g, b;
                YuvToRgb(yRow[x], uRow[cx], vRow[cx + vOffset], r, g, b);
                dRow[x * 4 + 0] = b;
                dRow[x * 4 + 1] = g;
                dRow[x * 4 + 2] = r;
                dRow[x * 4 + 3] = 255;
            }
        }
        return;
    }

    // Scaling pass: bilinear on luma at full resolution and on chroma at
    // half resolution (chroma coordinates are half the luma coordinates).
    const float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);
    for (int dy = 0; dy < dstH; ++dy)
    {
        uint8_t* dRow = dst + dy * dstStride;
        const float sy = (static_cast<float>(dy) + 0.5f) * scaleY - 0.5f;
        for (int dx = 0; dx < dstW; ++dx)
        {
            const float sx = (static_cast<float>(dx) + 0.5f) * scaleX - 0.5f;
            const int yy = detail::BilinearSample(yPlane, srcStrideY, 1, srcW, srcH, sx, sy, 0);
            const int uu = detail::BilinearSample(uPlane, srcStrideUV, uvPixelStride,
                                                  chromaW, chromaH, sx * 0.5f, sy * 0.5f, 0);
            const int vv = detail::BilinearSample(vSrc, srcStrideUV, uvPixelStride,
                                                  chromaW, chromaH, sx * 0.5f, sy * 0.5f, vOffset);
            uint8_t r, g, b;
            YuvToRgb(yy, uu, vv, r, g, b);
            dRow[dx * 4 + 0] = b;
            dRow[dx * 4 + 1] = g;
            dRow[dx * 4 + 2] = r;
            dRow[dx * 4 + 3] = 255;
        }
    }
}

// ---------------------------------------------------------------------------
// BGRA -> YUV420
// ---------------------------------------------------------------------------
// bgra: source, srcW x srcH, row stride srcStride (bytes).
// yPlane: luma output, dstW x dstH, row stride dstStrideY.
// uvPlane: chroma output. Interleaved (uvInterleaved == true, NV12): one
// plane, dstW/2 x dstH/2, row stride dstStrideUV, U at even bytes and V
// at odd bytes. Planar (false, I420): uvPlane is the U plane and vPlane
// is the V plane, each dstW/2 x dstH/2 with row stride dstStrideUV.
// The caller allocates all planes. Same 1:1 fast path / bilinear scaling
// split as Yuv420ToBgra.
inline void BgraToYuv420(const uint8_t* bgra, int srcW, int srcH, int srcStride,
                         uint8_t* yPlane, uint8_t* uvPlane, uint8_t* vPlane,
                         int dstW, int dstH, int dstStrideY, int dstStrideUV,
                         bool uvInterleaved)
{
    if (!bgra || !yPlane || !uvPlane || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    if (!uvInterleaved && !vPlane)
        return;
    const int chromaW = (dstW + 1) / 2;
    const int chromaH = (dstH + 1) / 2;

    if (srcW == dstW && srcH == dstH)
    {
        // Direct pass: luma per pixel; chroma is the average of each 2x2
        // source block (proper 4:2:0 subsampling, not a point sample).
        for (int y = 0; y < dstH; ++y)
        {
            const uint8_t* sRow = bgra + y * srcStride;
            uint8_t* yRow = yPlane + y * dstStrideY;
            for (int x = 0; x < dstW; ++x)
            {
                uint8_t yy, uu, vv;
                RgbToYuv(sRow[x * 4 + 2], sRow[x * 4 + 1], sRow[x * 4 + 0], yy, uu, vv);
                yRow[x] = yy;
            }
            if ((y & 1) == 0)
            {
                const uint8_t* sRow2 = bgra + std::min(y + 1, srcH - 1) * srcStride;
                uint8_t* uRow = uvPlane + (y / 2) * dstStrideUV;
                uint8_t* vRow = uvInterleaved ? nullptr : vPlane + (y / 2) * dstStrideUV;
                for (int cx = 0; cx < chromaW; ++cx)
                {
                    // Average the 2x2 block's U and V (clamped at the edges).
                    int uSum = 0, vSum = 0;
                    for (int oy = 0; oy < 2; ++oy)
                    {
                        const uint8_t* r = (oy == 0) ? sRow : sRow2;
                        for (int ox = 0; ox < 2; ++ox)
                        {
                            const int sx = std::min(cx * 2 + ox, srcW - 1);
                            uint8_t yy, uu, vv;
                            RgbToYuv(r[sx * 4 + 2], r[sx * 4 + 1], r[sx * 4 + 0], yy, uu, vv);
                            uSum += uu;
                            vSum += vv;
                        }
                    }
                    const uint8_t uAvg = static_cast<uint8_t>((uSum + 2) / 4);
                    const uint8_t vAvg = static_cast<uint8_t>((vSum + 2) / 4);
                    if (uvInterleaved)
                    {
                        uRow[cx * 2 + 0] = uAvg;
                        uRow[cx * 2 + 1] = vAvg;
                    }
                    else
                    {
                        uRow[cx] = uAvg;
                        vRow[cx] = vAvg;
                    }
                }
            }
        }
        return;
    }

    // Scaling pass: bilinear-sample the source for luma; bilinear-sample
    // at the chroma grid for U/V.
    const float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);
    for (int dy = 0; dy < dstH; ++dy)
    {
        uint8_t* yRow = yPlane + dy * dstStrideY;
        const float sy = (static_cast<float>(dy) + 0.5f) * scaleY - 0.5f;
        for (int dx = 0; dx < dstW; ++dx)
        {
            const float sx = (static_cast<float>(dx) + 0.5f) * scaleX - 0.5f;
            const int r = detail::BilinearSampleBgra(bgra, srcStride, srcW, srcH, sx, sy, 2);
            const int g = detail::BilinearSampleBgra(bgra, srcStride, srcW, srcH, sx, sy, 1);
            const int b = detail::BilinearSampleBgra(bgra, srcStride, srcW, srcH, sx, sy, 0);
            uint8_t yy, uu, vv;
            RgbToYuv(r, g, b, yy, uu, vv);
            yRow[dx] = yy;
        }
    }
    for (int cy = 0; cy < chromaH; ++cy)
    {
        uint8_t* uRow = uvPlane + cy * dstStrideUV;
        uint8_t* vRow = uvInterleaved ? nullptr : vPlane + cy * dstStrideUV;
        // Chroma sample (cx, cy) covers the source block centred at the
        // matching luma position: ((cx + 0.5) * 2 * scaleX, ...).
        const float sy = (static_cast<float>(cy * 2) + 1.0f) * scaleY - 0.5f;
        for (int cx = 0; cx < chromaW; ++cx)
        {
            const float sx = (static_cast<float>(cx * 2) + 1.0f) * scaleX - 0.5f;
            const int r = detail::BilinearSampleBgra(bgra, srcStride, srcW, srcH, sx, sy, 2);
            const int g = detail::BilinearSampleBgra(bgra, srcStride, srcW, srcH, sx, sy, 1);
            const int b = detail::BilinearSampleBgra(bgra, srcStride, srcW, srcH, sx, sy, 0);
            uint8_t yy, uu, vv;
            RgbToYuv(r, g, b, yy, uu, vv);
            if (uvInterleaved)
            {
                uRow[cx * 2 + 0] = uu;
                uRow[cx * 2 + 1] = vv;
            }
            else
            {
                uRow[cx] = uu;
                vRow[cx] = vv;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Streaming audio resampler (linear interpolation)
// ---------------------------------------------------------------------------
// Converts interleaved PCM to interleaved stereo s16 at a different sample
// rate. Stateful: the last source frame carries across calls, so a stream
// can be fed in arbitrary chunks (one AAC frame at a time) without clicks
// at the chunk boundaries. The caller owns the output buffer and the loop:
// keep calling Process* until it returns 0, then feed the next chunk.
// There is no Flush: the delay is under one output frame, and callers
// pad the tail with silence (the recorder's final partial AAC frame) or
// drop it (movie audio, where the ring target absorbs it).
//
// Determinism note: the source position is computed from the integer
// output-frame index (outIndex * ratio), never accumulated, so feeding a
// stream in 1024-frame chunks produces bit-identical output to feeding
// it in one chunk. The host test (scope/test-mediacodec-convert.cpp)
// checks exactly that.
struct AudioResampler
{
    double ratio = 1.0;       // source frames per output frame (srcRate/dstRate)
    int64_t outIndex = 0;     // total output frames produced so far
    int64_t chunkStart = 0;   // stream source-frame index of the current chunk's first frame
    int16_t prevL = 0;        // last source frame before the current chunk
    int16_t prevR = 0;
    bool havePrev = false;

    void Reset(double srcRate, double dstRate)
    {
        ratio = (dstRate > 0.0) ? srcRate / dstRate : 1.0;
        outIndex = 0;
        chunkStart = 0;
        prevL = 0;
        prevR = 0;
        havePrev = false;
    }

    // Maximum output frames one chunk can produce. Callers MUST pass an
    // output buffer at least this large: Process consumes a whole chunk
    // in one call, and if the buffer fills first the tail of that chunk
    // is dropped (audible glitch, not a crash). Size from the input:
    // outFrames = ceil(inFrames * dstRate / srcRate) + 2.
    size_t MaxOutputFrames(size_t inFrames) const
    {
        if (ratio <= 0.0)
            return inFrames + 2;
        return static_cast<size_t>(std::ceil(static_cast<double>(inFrames) / ratio)) + 2;
    }

    // Resample interleaved s16 (srcChannels 1 or 2; >2 keeps the first two).
    // Returns the output frames written (<= outCapacityFrames).
    size_t ProcessS16(const int16_t* in, size_t inFrames, int srcChannels,
                      int16_t* out, size_t outCapacityFrames)
    {
        return Process(in, inFrames, srcChannels, out, outCapacityFrames, false);
    }

    // Resample interleaved float32 (range -1..1, same channel rules).
    size_t ProcessFloat(const float* in, size_t inFrames, int srcChannels,
                        int16_t* out, size_t outCapacityFrames)
    {
        return Process(in, inFrames, srcChannels, out, outCapacityFrames, true);
    }

private:
    // Fetch source frame `index` (-1 = the carried previous frame).
    template <typename T>
    void FetchFrame(const T* in, size_t inFrames, int srcChannels, long index,
                    float& l, float& r, bool isFloat) const
    {
        int16_t sl = 0, sr = 0;
        if (index < 0)
        {
            sl = prevL;
            sr = prevR;
        }
        else if (static_cast<size_t>(index) < inFrames)
        {
            const size_t base = static_cast<size_t>(index) * static_cast<size_t>(srcChannels);
            sl = ToS16(in[base], isFloat);
            sr = (srcChannels >= 2) ? ToS16(in[base + 1], isFloat) : sl;
        }
        l = static_cast<float>(sl);
        r = static_cast<float>(sr);
    }

    static int16_t ToS16(int16_t v, bool /*isFloat*/) { return v; }
    static int16_t ToS16(float v, bool /*isFloat*/)
    {
        const float c = std::clamp(v, -1.0f, 1.0f);
        return static_cast<int16_t>(c * 32767.0f);
    }

    template <typename T>
    size_t Process(const T* in, size_t inFrames, int srcChannels,
                   int16_t* out, size_t outCapacityFrames, bool isFloat)
    {
        if (!in || !out || inFrames == 0 || outCapacityFrames == 0 || srcChannels <= 0)
            return 0;
        size_t written = 0;
        while (written < outCapacityFrames)
        {
            // Source position of the next output frame, in stream
            // coordinates, rebased to this chunk. Subtracting the integer
            // chunkStart leaves the fractional part bit-identical to the
            // one-shot case (see the determinism note above).
            const double local = static_cast<double>(outIndex) * ratio - static_cast<double>(chunkStart);
            const long i0 = static_cast<long>(std::floor(local));
            // The next output frame needs source frames i0 and i0+1. If
            // i0+1 is past the end of this chunk, stop: the leftover is
            // carried into the next call (the last frame becomes `prev`,
            // chunkStart advances).
            if (i0 + 1 >= static_cast<long>(inFrames))
                break;
            const double frac = local - static_cast<double>(i0);
            float l0, r0, l1, r1;
            FetchFrame(in, inFrames, srcChannels, i0, l0, r0, isFloat);
            FetchFrame(in, inFrames, srcChannels, i0 + 1, l1, r1, isFloat);
            const float l = l0 + (l1 - l0) * static_cast<float>(frac);
            const float r = r0 + (r1 - r0) * static_cast<float>(frac);
            out[written * 2 + 0] = static_cast<int16_t>(std::clamp(l, -32768.0f, 32767.0f));
            out[written * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768.0f, 32767.0f));
            ++written;
            ++outIndex;
        }
        // Carry the tail into the next call: the last frame of this chunk
        // becomes `prev` (local index -1 there) and chunkStart advances.
        // If the output buffer filled first (caller sized it below
        // MaxOutputFrames), the undelivered tail of this chunk is dropped
        // — a caller sizing bug, audible as a glitch, never a crash.
        if (inFrames > 0)
        {
            const size_t base = (inFrames - 1) * static_cast<size_t>(srcChannels);
            prevL = ToS16(in[base], isFloat);
            prevR = (srcChannels >= 2) ? ToS16(in[base + 1], isFloat) : prevL;
            havePrev = true;
            chunkStart += static_cast<int64_t>(inFrames);
        }
        return written;
    }
};

} // namespace AndroidMediaConvert
