// android-screenrecorder.h — Android MediaCodec state for ScreenRecorder
// (Phase D step 4, 24 Nov 2026).
//
// On Linux, ScreenRecorder encodes H.264 video + AAC audio to MP4 with
// FFmpeg (libx264 / native AAC, muxed by libavformat). On Android there
// is no FFmpeg (Daniel's decision, 24 Nov 2026: native MediaCodec
// instead of vendoring it), so this struct holds the platform
// equivalent: AMediaCodec video/audio encoders + AMediaMuxer, with the
// pixel/audio conversion done by android-mediacodec-convert.h.
//
// The struct lives here (not in ScreenRecorder.h) so the NDK media
// headers stay out of the shared header — ScreenRecorder.h only holds
// an opaque void* m_mediacodec and the method declarations. The methods
// themselves are in android-screenrecorder.cpp and are only compiled on
// Android.
//
// Scope matches the Linux recorder exactly: video frames + tracker
// music (the miniaudio tap); SFX are not captured (ma_engine has no
// output tap), and there is no microphone on Android either — a
// non-Off MicMode logs the same one-shot warning and records without
// a mic.
//
// Two platform behaviours worth knowing (verified against the NDK r27c
// headers; both need a real device to confirm at runtime):
//   - Encoder input buffers are tightly packed (stride == width) for
//     YUV420 — there is no stride in the encoder input path, so the
//     BGRA->YUV420 conversion writes directly into the input buffer.
//     The capacity check in WriteVideoFrameMediaCodec proves the
//     layout fits before every write.
//   - The encoder's output format (with csd-0 / SPS-PPS) is read right
//     after start, before the muxer is started; that is the documented
//     flow, and the muxer track is added from it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include "android-mediacodec-convert.h"

// All Android MediaCodec state for one recording. Owned through the
// opaque void* ScreenRecorder::m_mediacodec. Mirrors RecorderFFmpeg
// (the Linux struct in ScreenRecorder.cpp) where the two backends
// overlap: the video/audio encoder pair, the muxer, the audio PTS
// clock, and the resampled-audio staging area.
struct RecorderMediaCodec
{
    AMediaCodec* videoEncoder = nullptr;
    AMediaCodec* audioEncoder = nullptr;
    AMediaMuxer* muxer = nullptr;
    // The MP4 file, opened with fopen (the muxer writes through its fd).
    FILE* outFile = nullptr;
    std::string outPath;

    int videoTrack = -1;  // muxer track index, -1 until added
    int audioTrack = -1;
    // Negotiated video colour format: 21 (NV12, tried first) or 19
    // (I420, fallback). videoColorInterleaved is true for NV12.
    int videoColorFormat = 0;
    bool videoColorInterleaved = true;

    int audioFrameSize = 1024;  // AAC-LC frame size, samples per channel
    int64_t audioPts = 0;       // audio samples encoded (48 kHz clock)
    int fps = 60;
    bool hasAudio = false;
    bool muxerStarted = false;
    bool videoEosSent = false;
    bool audioEosSent = false;

    // Resampled game audio (48 kHz stereo s16) staged for AAC frames.
    std::vector<int16_t> audioStage;
    AndroidMediaConvert::AudioResampler resampler;
};

// Free every handle in the struct (encoders stopped then deleted, muxer
// deleted, file closed). Safe on nullptr and on a partially initialised
// struct; does not free the struct itself. Callers that already stopped
// the muxer and closed the file (the StopRecording path) clear those
// members first so this does not double-stop / double-close.
void FreeRecorderMediaCodec(RecorderMediaCodec* r);
