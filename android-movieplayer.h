// android-movieplayer.h — Android MediaCodec state for MoviePlayer
// (Phase D step 4, 24 Nov 2026).
//
// On Linux, MoviePlayer decodes H.264 + AAC from mp4 with FFmpeg
// (libavformat / libavcodec) and converts with libswscale / libswresample.
// On Android there is no FFmpeg (Daniel's decision, 24 Nov 2026: native
// MediaCodec instead of vendoring it), so this struct holds the platform
// equivalent: AMediaExtractor demux + AMediaCodec decoders, with the
// pixel/audio conversion done by android-mediacodec-convert.h.
//
// The struct lives here (not in MoviePlayer.h) so the NDK media headers
// stay out of the shared header — MoviePlayer.h only holds an opaque
// void* m_mediacodec and the method declarations. The methods themselves
// are in android-movieplayer.cpp and are only compiled on Android.
//
// Two platform behaviours worth knowing (both verified against the NDK
// r27c headers; both need a real device to confirm at runtime):
//   - There is NO AMediaCodec_getOutputImage / AMediaImage API in the
//     NDK C headers, so video frames are read with
//     AMediaCodec_getOutputBuffer and interpreted from the output
//     format's stride / slice-height. Decoders report their colour
//     format as planar (19), semi-planar (21), or Flexible
//     (0x7F420888); Flexible is assumed to be NV12 (semi-planar) — the
//     layout every major SoC decoder (Qualcomm / Exynos / MediaTek)
//     produces for the buffer path.
//   - The "slice-height" and "pcm-encoding" format keys have no NDK
//     symbol before API 28 (minSdk is 26), so the code uses the literal
//     key strings with AMediaFormat_getInt32 instead of the symbols.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include "MiniaudioStream.h"
#include "android-mediacodec-convert.h"

// All Android MediaCodec state for one open movie. Owned through the
// opaque void* MoviePlayer::m_mediacodec. Mirrors MovieFFmpeg (the
// Linux struct in MoviePlayer.cpp) where the two backends overlap:
// resumePtsRel / eof / firstVideoPts / the held pending frame / the
// audio backlog drained into the miniaudio ring.
struct MovieMediaCodec
{
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* videoCodec = nullptr;
    AMediaCodec* audioCodec = nullptr;
    // Miniaudio ring the decoded movie audio is written into (same role
    // as MovieFFmpeg::audioStream on Linux).
    Miniaudio::Stream* audioStream = nullptr;

    int videoTrack = -1;
    int audioTrack = -1;

    // Video geometry. srcW/srcH are the visible size; outW/outH are the
    // renderer size the BGRA frame is converted to (same rule as the
    // FFmpeg path: renderer size, so the Vulkan overlay upload accepts
    // the frame as-is).
    int srcW = 0;
    int srcH = 0;
    int outW = 0;
    int outH = 0;
    // Decoder output layout (from the output format, updated on
    // INFO_OUTPUT_FORMAT_CHANGED). outColorFormat is 19 (planar I420),
    // 21 (semi-planar NV12), or 0x7F420888 (Flexible, assumed NV12).
    // outStrideUV is the chroma row stride in bytes; uvPixelStride is 1
    // for planar, 2 for interleaved UV.
    int outStrideY = 0;
    int outStrideUV = 0;
    int outSliceHeight = 0;
    int outColorFormat = 0;
    int uvPixelStride = 1;

    // Audio format (from the audio output format). The resampler is
    // (re)configured whenever these change.
    int audioSampleRate = 0;
    int audioChannels = 0;
    bool audioIsFloat = false;

    double firstVideoPts = -1.0;  // seconds, absolute (first frame's PTS)
    double durationSecs = 0.0;
    double resumePtsRel = 0.0;    // media-time anchor captured by Play()/SeekTo()

    bool inputEof = false;   // extractor exhausted and EOS queued to both decoders
    bool videoEof = false;   // video decoder reported end of stream
    bool audioEof = false;   // audio decoder reported end of stream
    bool videoEosSent = false;
    bool audioEosSent = false;

    // One video frame decoded ahead of its PTS, held exactly like
    // MovieFFmpeg::pending: the output buffer is NOT released until the
    // frame is due, and no further packets are read while it is held.
    ssize_t pendingBufIndex = -1;
    uint8_t* pendingBuf = nullptr;
    size_t pendingBufSize = 0;
    double pendingPtsRel = 0.0;

    // Resampled audio (44100 Hz stereo s16) waiting for room in the ring.
    std::vector<int16_t> audioBacklog;
    AndroidMediaConvert::AudioResampler resampler;
    bool resamplerReady = false;

    // Path of the asset copy in internal storage ("" when the movie was
    // opened directly from a regular file). The copy is cached, not
    // re-extracted on every open — see OpenMovieMediaCodecLocked.
    std::string cachePath;
};

// Free every handle in the struct (codecs stopped then deleted, extractor
// deleted, audio stream deleted). Safe on nullptr and on a partially
// initialised struct; does not free the struct itself.
void FreeMovieMediaCodec(MovieMediaCodec* mc);
