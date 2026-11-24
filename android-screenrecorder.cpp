// android-screenrecorder.cpp — Android MediaCodec backend for
// ScreenRecorder (Phase D step 4, 24 Nov 2026).
//
// Replaces the Linux FFmpeg encode path (ScreenRecorder.cpp,
// HAS_FFMPEG) with the platform APIs: AMediaCodec encodes H.264 video
// (from the back-buffer BGRA, converted by android-mediacodec-convert.h)
// and AAC audio (from the tracker-music miniaudio tap, resampled
// 44100 -> 48000 Hz), and AMediaMuxer writes the MP4.
//
// This file is Android-only (compiled only when PLATFORM_ANDROID; see
// Linux/CMakeLists.txt). It implements the ScreenRecorder methods
// declared in ScreenRecorder.h under PLATFORM_ANDROID. The public
// ScreenRecorder methods (StartRecording / StopRecording / CaptureFrame)
// dispatch here when HAS_MEDIACODEC is true; Windows and Linux
// behaviour is untouched.
//
// The write path mirrors the FFmpeg WriteVideoFrameFFmpeg /
// WriteAudioFramesFFmpeg step for step — same video-clock timestamps
// (the frame index in the stream time base), same audio-locked-to-video
// rule (audio is never encoded ahead of the current video frame), same
// flush behaviour at StopRecording (the final partial AAC frame is
// padded with silence so the tail is not dropped). The resampler has
// under one frame of internal delay, so there is no separate resampler
// flush — the staged audio is the whole tail.
//
// Output file: the recorder is given a relative path
// ("Assets/recording_....mp4"). On Android the process working
// directory is not the game directory, so the open tries the path
// as given first and then under the internal data path (creating the
// parent directory when needed).
#include "ScreenRecorder.h"
#include "android-screenrecorder.h"
#include "AndroidApp.h"  // GetInternalDataPath
#include "Debug.h"  // debug instance + LogLevel (ScreenRecorder.cpp's style)
#include "MiniaudioStream.h"

#include <sys/stat.h>

#include <cstring>

// H.264 output bitrate, matching the Windows MF path and the Linux
// FFmpeg path (kRecorderVideoBitrate in ScreenRecorder.cpp).
static constexpr int kMediaCodecVideoBitrate = 10'000'000;
// AAC output sample rate (the native AAC encoder's preferred rate).
static constexpr int kMediaCodecAudioRate = 48000;
// AAC-LC frame size, samples per channel. (No API reports it; 1024 is
// the AAC-LC frame size on every Android encoder.)
static constexpr int kAacFrameSize = 1024;

namespace
{

// Drain one encoder's output into the muxer (non-blocking). Stops at
// the first empty queue or end-of-stream buffer.
void DrainEncoderOutputMediaCodec(RecorderMediaCodec* r, AMediaCodec* enc, int trackIndex)
{
    if (!r || !enc || trackIndex < 0 || !r->muxer || !r->muxerStarted)
        return;
    while (true)
    {
        AMediaCodecBufferInfo info{};
        const ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(enc, &info, 0);
        if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
        {
            // The track format was fixed at addTrack time; a later change
            // is unexpected — ignore it and keep draining.
            continue;
        }
        if (outIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER || outIndex < 0)
            return;
        if (info.size > 0)
        {
            size_t bufSize = 0;
            uint8_t* buf = AMediaCodec_getOutputBuffer(enc, static_cast<size_t>(outIndex), &bufSize);
            if (buf && static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= bufSize)
            {
                // writeSampleData reads buf + info.offset, info.size
                // bytes; the info's presentationTimeUs is the sample
                // timestamp (set when the input was queued).
                AMediaMuxer_writeSampleData(r->muxer, static_cast<size_t>(trackIndex), buf, &info);
            }
        }
        const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
        AMediaCodec_releaseOutputBuffer(enc, static_cast<size_t>(outIndex), false);
        if (eos)
            return;
    }
}

// Queue one whole AAC frame (s16 stereo, frameSize samples) into the
// audio encoder. Returns false when no input buffer is free (the frame
// stays staged; the caller retries next call).
bool QueueAudioFrameMediaCodec(RecorderMediaCodec* r, const int16_t* frames, int frameSize)
{
    if (!r || !r->audioEncoder || !frames || frameSize <= 0)
        return false;
    const ssize_t inIndex = AMediaCodec_dequeueInputBuffer(r->audioEncoder, 0);
    if (inIndex < 0)
        return false;
    size_t capacity = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(r->audioEncoder, static_cast<size_t>(inIndex), &capacity);
    const size_t need = static_cast<size_t>(frameSize) * 2 * sizeof(int16_t);
    if (!buf || capacity < need)
    {
        AMediaCodec_queueInputBuffer(r->audioEncoder, static_cast<size_t>(inIndex), 0, 0, 0, 0);
        return false;
    }
    std::memcpy(buf, frames, need);
    const uint64_t ptsUs = static_cast<uint64_t>(r->audioPts) * 1000000ULL /
        static_cast<uint64_t>(kMediaCodecAudioRate);
    AMediaCodec_queueInputBuffer(r->audioEncoder, static_cast<size_t>(inIndex), 0, need, ptsUs, 0);
    return true;
}

// Signal end of stream to one encoder and drain its output until the
// EOS buffer arrives (or the deadline passes). Returns true when the
// encoder reported EOS.
bool SignalEosAndDrainMediaCodec(RecorderMediaCodec* r, AMediaCodec* enc, int trackIndex)
{
    if (!r || !enc)
        return true;
    // Queue the empty EOS input buffer (the encoder is idle now, so an
    // input buffer frees up quickly; 100 ms of retries, then give up).
    bool eosQueued = false;
    for (int attempt = 0; attempt < 10 && !eosQueued; ++attempt)
    {
        const ssize_t inIndex = AMediaCodec_dequeueInputBuffer(enc, 10000);
        if (inIndex >= 0)
        {
            AMediaCodec_queueInputBuffer(enc, static_cast<size_t>(inIndex), 0, 0, 0,
                AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            eosQueued = true;
        }
    }
    if (!eosQueued)
        return false;
    // Drain until the EOS output buffer (up to ~2 s of output).
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        AMediaCodecBufferInfo info{};
        const ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(enc, &info, 10000);
        if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
            continue;
        if (outIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER || outIndex < 0)
            continue;
        if (info.size > 0 && r->muxer && r->muxerStarted && trackIndex >= 0)
        {
            size_t bufSize = 0;
            uint8_t* buf = AMediaCodec_getOutputBuffer(enc, static_cast<size_t>(outIndex), &bufSize);
            if (buf && static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= bufSize)
                AMediaMuxer_writeSampleData(r->muxer, static_cast<size_t>(trackIndex), buf, &info);
        }
        const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
        AMediaCodec_releaseOutputBuffer(enc, static_cast<size_t>(outIndex), false);
        if (eos)
            return true;
    }
    return false;
}

// Create, configure and start a video encoder for one colour format.
// Returns nullptr when configure/start fails (the caller tries the next
// format). The output format is returned through outFormat for the
// muxer track (caller deletes it after addTrack).
AMediaCodec* CreateVideoEncoderMediaCodec(int width, int height, int fps, int bitrate,
                                          int colorFormat, AMediaFormat** outFormat)
{
    AMediaFormat* format = AMediaFormat_new();
    if (!format)
        return nullptr;
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, colorFormat);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);  // keyframe every second
    AMediaCodec* codec = AMediaCodec_createEncoderByType("video/avc");
    if (codec)
    {
        media_status_t status = AMediaCodec_configure(codec, format, nullptr, nullptr,
            AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        if (status == AMEDIA_OK)
            status = AMediaCodec_start(codec);
        if (status != AMEDIA_OK)
        {
            AMediaCodec_delete(codec);
            codec = nullptr;
        }
    }
    AMediaFormat_delete(format);
    if (codec && outFormat)
    {
        *outFormat = AMediaCodec_getOutputFormat(codec);
        if (!*outFormat)
        {
            // No output format yet: the muxer track cannot be added.
            AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
            codec = nullptr;
        }
    }
    return codec;
}

// Create, configure and start the AAC encoder. Returns nullptr on
// failure. The output format is returned through outFormat for the
// muxer track (caller deletes it after addTrack).
AMediaCodec* CreateAudioEncoderMediaCodec(int bitrate, AMediaFormat** outFormat)
{
    AMediaFormat* format = AMediaFormat_new();
    if (!format)
        return nullptr;
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, kMediaCodecAudioRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);  // AAC LC
    AMediaCodec* codec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (codec)
    {
        media_status_t status = AMediaCodec_configure(codec, format, nullptr, nullptr,
            AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        if (status == AMEDIA_OK)
            status = AMediaCodec_start(codec);
        if (status != AMEDIA_OK)
        {
            AMediaCodec_delete(codec);
            codec = nullptr;
        }
    }
    AMediaFormat_delete(format);
    if (codec && outFormat)
    {
        *outFormat = AMediaCodec_getOutputFormat(codec);
        if (!*outFormat)
        {
            AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
            codec = nullptr;
        }
    }
    return codec;
}

// Open the recording file for writing. Tries the path as given, then
// under the internal data path (creating the parent directory).
FILE* OpenRecordingFileMediaCodec(const std::string& narrow, std::string& openedPath)
{
    FILE* f = fopen(narrow.c_str(), "wb");
    if (f)
    {
        openedPath = narrow;
        return f;
    }
    const std::string internal = AndroidApp::GetInternalDataPath() + "/" + narrow;
    // Create the parent directory (everything up to the last '/').
    const size_t slash = internal.find_last_of('/');
    if (slash != std::string::npos)
    {
        const std::string dir = internal.substr(0, slash);
        // mkdir -p, one level at a time (the path is at most two deep).
        std::string cur;
        for (size_t i = 0; i < dir.size(); ++i)
        {
            cur += dir[i];
            if (dir[i] == '/' && cur.size() > 1)
                mkdir(cur.c_str(), 0755);
        }
        mkdir(dir.c_str(), 0755);  // EEXIST is fine
    }
    f = fopen(internal.c_str(), "wb");
    if (f)
        openedPath = internal;
    return f;
}

} // namespace

void FreeRecorderMediaCodec(RecorderMediaCodec* r)
{
    if (!r)
        return;
    if (r->videoEncoder)
    {
        AMediaCodec_stop(r->videoEncoder);
        AMediaCodec_delete(r->videoEncoder);
        r->videoEncoder = nullptr;
    }
    if (r->audioEncoder)
    {
        AMediaCodec_stop(r->audioEncoder);
        AMediaCodec_delete(r->audioEncoder);
        r->audioEncoder = nullptr;
    }
    if (r->muxer)
    {
        // The StopRecording path stops the muxer first; reaching here
        // with a live muxer means the start failed part-way — stop it
        // (it returns an error on an unstarted muxer, which is safe).
        AMediaMuxer_stop(r->muxer);
        AMediaMuxer_delete(r->muxer);
        r->muxer = nullptr;
    }
    if (r->outFile)
    {
        fclose(r->outFile);
        r->outFile = nullptr;
    }
}

bool ScreenRecorder::StartRecordingMediaCodecLocked(UINT32 fpsVal, const std::wstring& outputPath, bool captureAudio)
{
    RecorderMediaCodec* r = new RecorderMediaCodec();
    r->fps = static_cast<int>(fpsVal);
    r->hasAudio = captureAudio;
    r->audioFrameSize = kAacFrameSize;
    r->resampler.Reset(static_cast<double>(Miniaudio::kSampleRate),
        static_cast<double>(kMediaCodecAudioRate));

    // ---- Output file (MP4, guessed from the extension) ----
    const std::string narrow = NarrowPathForFstream(outputPath);
    std::string openedPath;
    r->outFile = OpenRecordingFileMediaCodec(narrow, openedPath);
    if (!r->outFile)
    {
        delete r;
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not open output file for writing.");
        return false;
    }
    r->outPath = openedPath;
    const int outFd = fileno(r->outFile);

    // ---- Video encoder: H.264, NV12 first, I420 as fallback ----
    AMediaFormat* videoOutFormat = nullptr;
    r->videoEncoder = CreateVideoEncoderMediaCodec(static_cast<int>(m_width), static_cast<int>(m_height),
        static_cast<int>(fpsVal), kMediaCodecVideoBitrate, 21, &videoOutFormat);
    if (r->videoEncoder)
    {
        r->videoColorFormat = 21;
        r->videoColorInterleaved = true;
    }
    else
    {
        r->videoEncoder = CreateVideoEncoderMediaCodec(static_cast<int>(m_width), static_cast<int>(m_height),
            static_cast<int>(fpsVal), kMediaCodecVideoBitrate, 19, &videoOutFormat);
        if (r->videoEncoder)
        {
            r->videoColorFormat = 19;
            r->videoColorInterleaved = false;
        }
    }
    if (!r->videoEncoder)
    {
        FreeRecorderMediaCodec(r);
        delete r;
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: No H.264 encoder available.");
        return false;
    }

    // ---- Audio encoder: AAC, 48 kHz stereo ----
    AMediaFormat* audioOutFormat = nullptr;
    if (r->hasAudio)
    {
        r->audioEncoder = CreateAudioEncoderMediaCodec(static_cast<int>(m_audioBitrate), &audioOutFormat);
        if (!r->audioEncoder)
        {
            r->hasAudio = false;
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
                L"ScreenRecorder: AAC encoder unavailable; recording without game audio.");
        }
    }

    // ---- Muxer: add the tracks, then start ----
    r->muxer = AMediaMuxer_new(outFd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!r->muxer)
    {
        if (videoOutFormat)
            AMediaFormat_delete(videoOutFormat);
        if (audioOutFormat)
            AMediaFormat_delete(audioOutFormat);
        FreeRecorderMediaCodec(r);
        delete r;
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not create the MP4 muxer.");
        return false;
    }
    const ssize_t vTrack = AMediaMuxer_addTrack(r->muxer, videoOutFormat);
    if (videoOutFormat)
        AMediaFormat_delete(videoOutFormat);
    if (vTrack < 0)
    {
        if (audioOutFormat)
            AMediaFormat_delete(audioOutFormat);
        FreeRecorderMediaCodec(r);
        delete r;
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not add the video track.");
        return false;
    }
    r->videoTrack = static_cast<int>(vTrack);
    if (r->audioEncoder && audioOutFormat)
    {
        const ssize_t aTrack = AMediaMuxer_addTrack(r->muxer, audioOutFormat);
        if (aTrack >= 0)
            r->audioTrack = static_cast<int>(aTrack);
        else
        {
            // The audio track could not be added: record video only.
            AMediaCodec_stop(r->audioEncoder);
            AMediaCodec_delete(r->audioEncoder);
            r->audioEncoder = nullptr;
            r->hasAudio = false;
        }
    }
    if (audioOutFormat)
        AMediaFormat_delete(audioOutFormat);
    if (AMediaMuxer_start(r->muxer) != AMEDIA_OK)
    {
        FreeRecorderMediaCodec(r);
        delete r;
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not start the MP4 muxer.");
        return false;
    }
    r->muxerStarted = true;
    m_mediacodec = r;

    // ---- Game audio tap (tracker music only; SFX have no output tap) ----
    if (r->hasAudio)
    {
        m_audioTapQueue = std::make_shared<AudioTapQueue>();
        std::shared_ptr<AudioTapQueue> queue = m_audioTapQueue;
        Miniaudio::SetTapOnAllStreams(
            [queue](const int16_t* frames, size_t frameCount)
            {
                // Audio thread: append mixed PCM to the queue. Short lock,
                // no allocation in the common case (the vector grows once).
                std::lock_guard<std::mutex> qlock(queue->mutex);
                queue->pcm.insert(queue->pcm.end(), frames, frames + frameCount * 2);
            });
    }

    m_recordingStart = std::chrono::steady_clock::now();
    m_lastVideoTime = m_recordingStart;
    m_isRecording.store(true);
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"ScreenRecorder: Started " + std::to_wstring(fpsVal) + L"fps [" +
        std::wstring(r->hasAudio ? L"game audio" : L"no audio") + L"] -> " + outputPath);
    return true;
}

void ScreenRecorder::StopRecordingMediaCodecLocked()
{
    // Remove the audio tap first so no new audio arrives while flushing.
    Miniaudio::SetTapOnAllStreams(nullptr);

    RecorderMediaCodec* r = static_cast<RecorderMediaCodec*>(m_mediacodec);
    if (!r)
        return;

    // Drain any audio still queued, then flush (pad the final partial
    // AAC frame with silence) so the tail of the audio is not dropped.
    WriteAudioFramesMediaCodec(false);
    WriteAudioFramesMediaCodec(true);

    // Signal end of stream to both encoders and drain their output.
    if (r->videoEncoder && !r->videoEosSent)
    {
        if (SignalEosAndDrainMediaCodec(r, r->videoEncoder, r->videoTrack))
            r->videoEosSent = true;
        else
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Video encoder did not finish draining; the recording may be truncated.");
    }
    if (r->audioEncoder && r->hasAudio && !r->audioEosSent)
    {
        if (SignalEosAndDrainMediaCodec(r, r->audioEncoder, r->audioTrack))
            r->audioEosSent = true;
        else
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Audio encoder did not finish draining; the recording may be truncated.");
    }

    if (r->muxer)
    {
        AMediaMuxer_stop(r->muxer);
        AMediaMuxer_delete(r->muxer);
        r->muxer = nullptr;
        r->muxerStarted = false;
    }
    if (r->outFile)
    {
        fclose(r->outFile);
        r->outFile = nullptr;
    }
    const LONGLONG frames = m_videoFrameIndex.load();
    FreeRecorderMediaCodec(r);
    m_mediacodec = nullptr;
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"ScreenRecorder: Stopped (" + std::to_wstring(frames) + L" video frames).");
}

// Encode one BGRA video frame. The input may be any size (the back-buffer
// size); it is converted to the recording size (m_width x m_height).
// ts100ns is the frame timestamp in MF 100-ns units relative to the
// recording start (kept for API symmetry with the Windows path — the
// video pts itself is the frame index, so timestamps stay evenly spaced).
// Caller must hold m_mutex.
void ScreenRecorder::WriteVideoFrameMediaCodec(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t rowPitch, LONGLONG ts100ns)
{
    (void)ts100ns;
    RecorderMediaCodec* r = static_cast<RecorderMediaCodec*>(m_mediacodec);
    if (!r || !r->videoEncoder || !bgra || width == 0 || height == 0)
        return;

    const ssize_t inIndex = AMediaCodec_dequeueInputBuffer(r->videoEncoder, 0);
    if (inIndex < 0)
        return;  // encoder busy: drop this frame (keeps the recording real-time)
    size_t capacity = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(r->videoEncoder, static_cast<size_t>(inIndex), &capacity);
    const size_t need = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 3 / 2;
    if (!buf || capacity < need)
    {
        // Return the buffer unfilled (the encoder skips an empty input).
        AMediaCodec_queueInputBuffer(r->videoEncoder, static_cast<size_t>(inIndex), 0, 0, 0, 0);
        return;
    }

    // Convert BGRA -> YUV420 directly into the encoder's input buffer.
    // Encoder input buffers are tightly packed (stride == width); the
    // capacity check above proves the layout fits. NV12: one interleaved
    // UV plane (stride m_width). I420: separate U and V planes (stride
    // m_width / 2 each).
    uint8_t* yPlane = buf;
    uint8_t* uvPlane = buf + static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    uint8_t* vPlane = nullptr;
    const int dstStrideUV = r->videoColorInterleaved
        ? static_cast<int>(m_width) : static_cast<int>(m_width) / 2;
    if (!r->videoColorInterleaved)
        vPlane = uvPlane + static_cast<size_t>(m_width) * static_cast<size_t>(m_height) / 4;
    AndroidMediaConvert::BgraToYuv420(bgra, static_cast<int>(width), static_cast<int>(height),
        static_cast<int>(rowPitch),
        yPlane, uvPlane, vPlane,
        static_cast<int>(m_width), static_cast<int>(m_height),
        static_cast<int>(m_width), dstStrideUV, r->videoColorInterleaved);

    const int64_t ptsUs = m_videoFrameIndex.load() * 1000000LL /
        static_cast<int64_t>(static_cast<uint32_t>(m_targetFPS));
    AMediaCodec_queueInputBuffer(r->videoEncoder, static_cast<size_t>(inIndex), 0, need,
        static_cast<uint64_t>(ptsUs), 0);
    m_videoFrameIndex.fetch_add(1);
    DrainEncoderOutputMediaCodec(r, r->videoEncoder, r->videoTrack);

    // Keep the audio timestamp-locked to the video just written.
    WriteAudioFramesMediaCodec(false);
}

// Encode every complete AAC frame available in the staging area, but
// never ahead of the current video time (audioTime < videoTime), so the
// audio stays timestamp-locked to the video clock. When flush is true
// (StopRecording) the video-time bound is ignored and the final partial
// frame is padded with silence and encoded, so the tail of the audio is
// not dropped. Caller must hold m_mutex.
void ScreenRecorder::WriteAudioFramesMediaCodec(bool flush)
{
    RecorderMediaCodec* r = static_cast<RecorderMediaCodec*>(m_mediacodec);
    if (!r || !r->hasAudio || !r->audioEncoder)
        return;

    // 1) Drain the tap queue into the resampler (44100 Hz stereo s16 ->
    //    48000 Hz stereo s16).
    std::vector<int16_t> queued;
    if (m_audioTapQueue)
    {
        std::lock_guard<std::mutex> qlock(m_audioTapQueue->mutex);
        queued.swap(m_audioTapQueue->pcm);
    }
    if (!queued.empty())
    {
        const size_t inFrames = queued.size() / 2;
        const size_t cap = r->resampler.MaxOutputFrames(inFrames);
        const size_t base = r->audioStage.size();
        r->audioStage.resize(base + cap * 2);
        const size_t written = r->resampler.ProcessS16(queued.data(), inFrames, 2,
            r->audioStage.data() + base, cap);
        r->audioStage.resize(base + written * 2);
    }

    // 2) Emit whole AAC frames, locked to the video clock.
    const int frameSize = r->audioFrameSize > 0 ? r->audioFrameSize : kAacFrameSize;
    const double videoTimeSecs = static_cast<double>(m_videoFrameIndex.load()) /
        static_cast<double>(static_cast<uint32_t>(m_targetFPS));
    while (static_cast<int>(r->audioStage.size() / 2) >= frameSize)
    {
        const double audioTimeSecs = static_cast<double>(r->audioPts) /
            static_cast<double>(kMediaCodecAudioRate);
        if (!flush && audioTimeSecs >= videoTimeSecs)
            break;  // do not encode audio ahead of the video
        if (!QueueAudioFrameMediaCodec(r, r->audioStage.data(), frameSize))
            break;  // encoder busy: the frame stays staged
        r->audioStage.erase(r->audioStage.begin(), r->audioStage.begin() + frameSize * 2);
        r->audioPts += frameSize;
    }

    // 3) Flush: pad the final partial frame with silence and encode it,
    // so the tail of the audio is not dropped at StopRecording.
    if (flush && !r->audioStage.empty())
    {
        std::vector<int16_t> padded(static_cast<size_t>(frameSize) * 2, 0);
        const size_t have = std::min(r->audioStage.size(), padded.size());
        std::memcpy(padded.data(), r->audioStage.data(), have * sizeof(int16_t));
        if (QueueAudioFrameMediaCodec(r, padded.data(), frameSize))
            r->audioPts += frameSize;
        r->audioStage.clear();
    }

    DrainEncoderOutputMediaCodec(r, r->audioEncoder, r->audioTrack);
}
