// android-movieplayer.cpp — Android MediaCodec backend for MoviePlayer
// (Phase D step 4, 24 Nov 2026).
//
// Replaces the Linux FFmpeg decode path (MoviePlayer.cpp, HAS_FFMPEG)
// with the platform APIs: AMediaExtractor demuxes the mp4, AMediaCodec
// decodes H.264 video and AAC audio, android-mediacodec-convert.h turns
// the YUV420 frames into the BGRA the overlay path expects and resamples
// the audio to the 44100 Hz stereo s16 the miniaudio ring takes.
//
// This file is Android-only (compiled only when PLATFORM_ANDROID; see
// Linux/CMakeLists.txt). It implements the MoviePlayer methods declared
// in MoviePlayer.h under PLATFORM_ANDROID. The public MoviePlayer methods
// (OpenMovie / Play / Pause / Stop / Reset / SeekTo / UpdateFrame /
// Cleanup) dispatch here when HAS_MEDIACODEC is true; Windows and Linux
// behaviour is untouched.
//
// The decode loop (DecodeUntilMediaCodecLocked) mirrors the FFmpeg
// DecodeUntilLocked step for step — drain the audio backlog, present a
// held pending frame when due, stop reading while one is held, then a
// bounded feed/decode loop — so the two backends stay in lockstep and a
// fix in one is easy to carry to the other. Two deliberate differences:
//   - The FFmpeg path stops decoding entirely once its `eof` is set
//     (frames still inside the decoders are dropped); the MediaCodec
//     path keeps draining decoder output until both decoders report
//     end of stream, so the tail of the movie is not cut.
//   - When the audio ring is full, the FFmpeg path drops the audio
//     packet (advancing past it); the MediaCodec path does the same
//     (advancing the extractor past the audio sample) so a backed-up
//     audio decoder never blocks the video samples behind it in the
//     shared extractor.
//
// Movie files on Android: the path OpenMovie receives
// (<internal>/Assets/cpge.mp4) is NOT a regular file — the assets live
// inside the APK, deflated (package-apk.sh stores them compressed).
// The open therefore tries, in order: (1) the path as a regular file
// (works for a copy in internal storage), (2) streaming the asset into
// a cache file under <internal>/cpge-movie-cache/ and opening that
// (validated by size, so the 23 MB cpge.mp4 is copied once, not on
// every open). There is no AAssetManager_openFd in the NDK C API
// (openFd is Java-only), so a cached copy is the only asset route.
#include "MoviePlayer.h"
#include "android-movieplayer.h"
#include "AndroidApp.h"  // GetAssetManager / GetInternalDataPath

#include <android/asset_manager.h>

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

// OMX colour-format values. The NDK headers do not define them; these
// are the stable values every Android decoder uses.
namespace
{
    constexpr int kColorFormatYUV420Planar = 19;      // I420
    constexpr int kColorFormatYUV420SemiPlanar = 21;  // NV12
    constexpr int kColorFormatYUV420Flexible = 0x7F420888;

    // Decode bounds, mirroring the FFmpeg path (MoviePlayer.cpp):
    // kMovieMaxPacketsPerCall (64) and kMovieAudioRingTarget (8192).
    constexpr int kMediaCodecMaxStepsPerCall = 64;
    constexpr size_t kMediaCodecAudioRingTarget = 8192;

    // "slice-height" and "pcm-encoding" have no NDK symbol before API 28
    // (minSdk is 26), so the literal key strings are used (see
    // android-movieplayer.h). These are the stable key names.
    constexpr const char* kKeySliceHeight = "slice-height";
    constexpr const char* kKeyPcmEncoding = "pcm-encoding";
    constexpr int kPcmEncodingS16 = 2;
    constexpr int kPcmEncodingFloat = 3;
}

void FreeMovieMediaCodec(MovieMediaCodec* mc)
{
    if (!mc)
        return;
    // Release a held pending frame before stopping the video codec.
    if (mc->videoCodec && mc->pendingBufIndex >= 0)
    {
        AMediaCodec_releaseOutputBuffer(mc->videoCodec,
            static_cast<size_t>(mc->pendingBufIndex), false);
        mc->pendingBufIndex = -1;
        mc->pendingBuf = nullptr;
        mc->pendingBufSize = 0;
    }
    if (mc->videoCodec)
    {
        AMediaCodec_stop(mc->videoCodec);
        AMediaCodec_delete(mc->videoCodec);
        mc->videoCodec = nullptr;
    }
    if (mc->audioCodec)
    {
        AMediaCodec_stop(mc->audioCodec);
        AMediaCodec_delete(mc->audioCodec);
        mc->audioCodec = nullptr;
    }
    if (mc->audioStream)
    {
        mc->audioStream->Stop();
        delete mc->audioStream;
        mc->audioStream = nullptr;
    }
    if (mc->extractor)
    {
        AMediaExtractor_delete(mc->extractor);
        mc->extractor = nullptr;
    }
}

// Close the currently open movie (if any) and clear all playback state.
// Caller must hold m_mutex. Mirrors CloseFFmpegLocked.
void MoviePlayer::CloseMediaCodecLocked()
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(m_mediacodec);
    if (mc)
    {
        FreeMovieMediaCodec(mc);
        delete mc;
        m_mediacodec = nullptr;
    }
    m_isPlaying.store(false);
    m_isPaused.store(false);
    m_hasVideo.store(false);
    m_hasAudio.store(false);
    m_hasNewFrame.store(false);
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_videoDuration = 0;
    m_llCurrentPosition.store(0);
    m_playStartMediaTime = -1;
    m_cpuFrameBuffer.clear();
    m_cpuFrameWidth = 0;
    m_cpuFrameHeight = 0;
}

namespace
{

// Asset name for a movie path: everything after "Assets/" when present,
// otherwise the bare filename. The engine passes
// <internal>/Assets/cpge.mp4; the APK asset is "cpge.mp4".
std::string AssetNameFromPath(const std::string& path)
{
    const std::string marker = "Assets/";
    const size_t at = path.find(marker);
    if (at != std::string::npos)
        return path.substr(at + marker.size());
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// Copy one asset to the internal-storage cache, streaming (the movies
// are up to 23 MB — never hold one whole in memory). Returns the cache
// path, or "" on failure. A cache file whose size matches the asset is
// reused, so the copy happens once per movie.
std::string CopyAssetToCache(const std::string& assetName)
{
    AAssetManager* mgr = AndroidApp::GetAssetManager();
    if (!mgr)
        return std::string();
    AAsset* asset = AAssetManager_open(mgr, assetName.c_str(), AASSET_MODE_STREAMING);
    if (!asset)
        return std::string();
    const off_t size = AAsset_getLength(asset);
    if (size <= 0)
    {
        AAsset_close(asset);
        return std::string();
    }

    const std::string dir = AndroidApp::GetInternalDataPath() + "/cpge-movie-cache";
    mkdir(dir.c_str(), 0755);  // EEXIST is fine
    const std::string cachePath = dir + "/" + assetName;

    // Reuse a complete earlier copy.
    struct stat st {};
    if (stat(cachePath.c_str(), &st) == 0 && st.st_size == size)
    {
        AAsset_close(asset);
        return cachePath;
    }

    FILE* out = fopen(cachePath.c_str(), "wb");
    if (!out)
    {
        AAsset_close(asset);
        return std::string();
    }
    std::vector<uint8_t> chunk(1024 * 1024);
    off_t remaining = size;
    bool ok = true;
    while (remaining > 0)
    {
        const size_t want = static_cast<size_t>(std::min<off_t>(remaining, static_cast<off_t>(chunk.size())));
        const int got = AAsset_read(asset, chunk.data(), want);
        if (got <= 0)
        {
            ok = false;
            break;
        }
        if (fwrite(chunk.data(), 1, static_cast<size_t>(got), out) != static_cast<size_t>(got))
        {
            ok = false;
            break;
        }
        remaining -= got;
    }
    AAsset_close(asset);
    fclose(out);
    if (!ok || remaining != 0)
    {
        remove(cachePath.c_str());
        return std::string();
    }
    return cachePath;
}

// Point an extractor at a movie file, trying the regular-file path
// first, then the APK asset (a cached copy in internal storage). On
// success, fills mc->cachePath when the copy route was used. Returns
// false when the movie cannot be opened by any route.
bool OpenExtractorSource(AMediaExtractor* ex, const std::string& narrow, MovieMediaCodec* mc)
{
    if (AMediaExtractor_setDataSource(ex, narrow.c_str()) == AMEDIA_OK)
        return true;

    const std::string assetName = AssetNameFromPath(narrow);
    // Route 2: stream the (deflated) asset into the internal cache.
    // (There is no AAssetManager_openFd in the NDK C API — openFd is
    // Java-only — so a cached copy is the only asset route, even if a
    // future packaging stored the mp4 uncompressed.)
    const std::string cachePath = CopyAssetToCache(assetName);
    if (!cachePath.empty() &&
        AMediaExtractor_setDataSource(ex, cachePath.c_str()) == AMEDIA_OK)
    {
        mc->cachePath = cachePath;
        return true;
    }
    return false;
}

// Queue an empty end-of-stream buffer into a decoder. Returns false
// when no input buffer is available yet (the caller retries next call).
bool QueueCodecEos(AMediaCodec* codec)
{
    if (!codec)
        return true;  // no such decoder: nothing to end
    const ssize_t inIndex = AMediaCodec_dequeueInputBuffer(codec, 0);
    if (inIndex < 0)
        return false;
    AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(inIndex), 0, 0, 0,
        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    return true;
}

// Read the video decoder's output format (called on
// INFO_OUTPUT_FORMAT_CHANGED and once after start as a fallback).
// Sets the output strides / colour format the frame presentation uses.
void UpdateVideoOutputFormat(MovieMediaCodec* mc)
{
    if (!mc || !mc->videoCodec)
        return;
    AMediaFormat* fmt = AMediaCodec_getOutputFormat(mc->videoCodec);
    if (!fmt)
        return;
    int32_t w = 0, h = 0, stride = 0, color = 0, sliceH = 0;
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_STRIDE, &stride);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color);
    AMediaFormat_getInt32(fmt, kKeySliceHeight, &sliceH);
    if (w > 0)
        mc->srcW = w;
    if (h > 0)
        mc->srcH = h;
    mc->outStrideY = (stride > 0) ? stride : mc->srcW;
    // No slice-height reported: H.264 coded height is a multiple of 16.
    mc->outSliceHeight = (sliceH > 0) ? sliceH : ((mc->srcH + 15) & ~15);
    mc->outColorFormat = color;
    if (color == kColorFormatYUV420Planar)
    {
        mc->uvPixelStride = 1;
        mc->outStrideUV = mc->outStrideY / 2;
    }
    else
    {
        // Semi-planar (21) and Flexible (assumed NV12): one interleaved
        // UV plane, whose rows are as wide in bytes as the luma rows.
        mc->uvPixelStride = 2;
        mc->outStrideUV = mc->outStrideY;
    }
    AMediaFormat_delete(fmt);
}

// Read the audio decoder's output format. (Re)configures the resampler
// whenever the rate / channels / sample type change.
void UpdateAudioOutputFormat(MovieMediaCodec* mc)
{
    if (!mc || !mc->audioCodec)
        return;
    AMediaFormat* fmt = AMediaCodec_getOutputFormat(mc->audioCodec);
    if (!fmt)
        return;
    int32_t rate = 0, channels = 0, encoding = 0;
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
    // Absent on most devices (s16 is the default); float is 3.
    if (!AMediaFormat_getInt32(fmt, kKeyPcmEncoding, &encoding))
        encoding = kPcmEncodingS16;
    const bool isFloat = (encoding == kPcmEncodingFloat);
    if (rate != mc->audioSampleRate || channels != mc->audioChannels ||
        isFloat != mc->audioIsFloat)
    {
        mc->audioSampleRate = (rate > 0) ? rate : mc->audioSampleRate;
        mc->audioChannels = (channels > 0) ? channels : mc->audioChannels;
        mc->audioIsFloat = isFloat;
        mc->resampler.Reset(static_cast<double>(mc->audioSampleRate),
            static_cast<double>(Miniaudio::kSampleRate));
        mc->resamplerReady = true;
    }
    AMediaFormat_delete(fmt);
}

// Create and start one decoder for a track. Returns nullptr when the
// mime has no decoder or configure/start fails (caller disables the
// track). The track format is borrowed from the extractor and must stay
// alive only for the configure call.
AMediaCodec* CreateTrackDecoder(AMediaExtractor* ex, size_t trackIndex, const char* mime)
{
    AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, trackIndex);
    if (!fmt)
        return nullptr;
    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
    if (codec)
    {
        media_status_t status = AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0);
        if (status == AMEDIA_OK)
            status = AMediaCodec_start(codec);
        if (status != AMEDIA_OK)
        {
            AMediaCodec_delete(codec);
            codec = nullptr;
        }
    }
    AMediaFormat_delete(fmt);
    return codec;
}

} // namespace

bool MoviePlayer::OpenMovieMediaCodecLocked(const std::wstring& filePath)
{
    // Close any movie already open.
    CloseMediaCodecLocked();

    const std::string narrow = NarrowPathForFstream(filePath);
    MovieMediaCodec* mc = new MovieMediaCodec();
    AMediaExtractor* ex = AMediaExtractor_new();
    if (!ex)
    {
        delete mc;
        Debug::logDiagLevelMessage(LogLevel::LOG_WARNING, L"[MoviePlayer] Could not create the media extractor.");
        return false;
    }
    mc->extractor = ex;
    if (!OpenExtractorSource(ex, narrow, mc))
    {
        FreeMovieMediaCodec(mc);
        delete mc;
        Debug::logDiagLevelMessage(LogLevel::LOG_WARNING, L"[MoviePlayer] Could not open movie file.");
        return false;
    }

    // ---- Find the video and audio tracks ----
    const size_t trackCount = AMediaExtractor_getTrackCount(ex);
    for (size_t i = 0; i < trackCount; ++i)
    {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, i);
        if (!fmt)
            continue;
        const char* mime = nullptr;
        if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && mime)
        {
            if (mc->videoTrack < 0 && strncmp(mime, "video/", 6) == 0)
            {
                mc->videoTrack = static_cast<int>(i);
                int32_t w = 0, h = 0;
                int64_t durationUs = 0;
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
                AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &durationUs);
                mc->srcW = w;
                mc->srcH = h;
                mc->durationSecs = (durationUs > 0)
                    ? static_cast<double>(durationUs) / 1000000.0 : 0.0;
            }
            else if (mc->audioTrack < 0 && strncmp(mime, "audio/", 6) == 0)
            {
                mc->audioTrack = static_cast<int>(i);
            }
        }
        AMediaFormat_delete(fmt);
    }
    if (mc->videoTrack < 0 && mc->audioTrack < 0)
    {
        FreeMovieMediaCodec(mc);
        delete mc;
        Debug::logDiagLevelMessage(LogLevel::LOG_WARNING, L"[MoviePlayer] Movie has no video or audio stream.");
        return false;
    }
    if (mc->videoTrack >= 0)
        AMediaExtractor_selectTrack(ex, static_cast<size_t>(mc->videoTrack));
    if (mc->audioTrack >= 0)
        AMediaExtractor_selectTrack(ex, static_cast<size_t>(mc->audioTrack));

    // ---- Video decoder ----
    if (mc->videoTrack >= 0)
    {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, static_cast<size_t>(mc->videoTrack));
        const char* mime = nullptr;
        if (fmt)
            AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
        // The mime string is owned by the format; copy it before the
        // format is deleted (createDecoderByType reads it immediately,
        // but the copy keeps the lifetime obvious).
        const std::string mimeCopy = mime ? mime : "video/avc";
        if (fmt)
            AMediaFormat_delete(fmt);
        mc->videoCodec = CreateTrackDecoder(ex, static_cast<size_t>(mc->videoTrack), mimeCopy.c_str());
        if (!mc->videoCodec)
            mc->videoTrack = -1;
        else
            UpdateVideoOutputFormat(mc);  // picks up the initial strides
    }

    // ---- Audio decoder (only when the engine wants movie audio) ----
    if (mc->audioTrack >= 0 && m_enableAudio)
    {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, static_cast<size_t>(mc->audioTrack));
        const char* mime = nullptr;
        if (fmt)
            AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
        const std::string mimeCopy = mime ? mime : "audio/mp4a-latm";
        if (fmt)
            AMediaFormat_delete(fmt);
        mc->audioCodec = CreateTrackDecoder(ex, static_cast<size_t>(mc->audioTrack), mimeCopy.c_str());
        if (mc->audioCodec)
        {
            mc->audioStream = new Miniaudio::Stream();
            if (!mc->audioStream->IsReady())
            {
                delete mc->audioStream;
                mc->audioStream = nullptr;
                AMediaCodec_stop(mc->audioCodec);
                AMediaCodec_delete(mc->audioCodec);
                mc->audioCodec = nullptr;
                mc->audioTrack = -1;
            }
            else
            {
                UpdateAudioOutputFormat(mc);
            }
        }
        else
        {
            mc->audioTrack = -1;
        }
    }
    else
    {
        mc->audioTrack = -1;
    }

    if (mc->videoTrack < 0 && mc->audioTrack < 0)
    {
        FreeMovieMediaCodec(mc);
        delete mc;
        Debug::logDiagLevelMessage(LogLevel::LOG_WARNING, L"[MoviePlayer] No usable video or audio decoder for this movie.");
        return false;
    }

    // ---- Output size: the renderer size, so the Vulkan overlay upload
    //      (which requires an exact match) accepts the frame as-is.
    //      Falls back to the native video size when there is no renderer.
    mc->outW = (m_renderer && m_renderer->iOrigWidth > 0) ? m_renderer->iOrigWidth : mc->srcW;
    mc->outH = (m_renderer && m_renderer->iOrigHeight > 0) ? m_renderer->iOrigHeight : mc->srcH;
    mc->durationSecs = (mc->durationSecs > 0.0) ? mc->durationSecs : 0.0;
    m_videoDuration = static_cast<LONGLONG>(mc->durationSecs * 10000000.0);
    m_videoWidth = static_cast<uint32_t>(mc->srcW);
    m_videoHeight = static_cast<uint32_t>(mc->srcH);
    m_hasVideo.store(mc->videoCodec != nullptr);
    m_hasAudio.store(mc->audioCodec != nullptr && mc->audioStream != nullptr);
    m_mediacodec = mc;

    // Decode the opening frame so the first GetCurrentFrameRGBA() is not
    // empty even before Play() is called. This also anchors
    // m_playStartMediaTime to the first frame's PTS.
    if (mc->videoCodec)
    {
        DecodeUntilMediaCodecLocked(-1.0);
        if (mc->firstVideoPts >= 0.0)
            m_playStartMediaTime = static_cast<LONGLONG>(mc->firstVideoPts * 10000000.0);
    }
    return true;
}

bool MoviePlayer::PlayMediaCodecLocked()
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(m_mediacodec);
    if (!mc)
    {
        Debug::logDiagLevelMessage(LogLevel::LOG_WARNING, L"[MoviePlayer] Play called with no movie open.");
        return false;
    }
    // Capture the resume anchor ONCE: the relative PTS of the frame being
    // shown when Play() is called (0 for a fresh play, the paused PTS on
    // resume). UpdateFrameMediaCodecLocked() adds the wall-clock elapsed
    // to this — it must NOT recompute the anchor from m_llCurrentPosition
    // on every call, because that member advances as frames are presented
    // and would double-count the elapsed time (video running at ~2x).
    mc->resumePtsRel = (m_playStartMediaTime >= 0)
        ? static_cast<double>(m_llCurrentPosition.load() - m_playStartMediaTime) / 10000000.0
        : 0.0;
    m_playStartWallTime = std::chrono::steady_clock::now();
    m_isPaused.store(false);
    m_isPlaying.store(true);
    if (mc->audioStream)
        mc->audioStream->Start();
    return true;
}

bool MoviePlayer::PauseMediaCodecLocked()
{
    if (!m_isPlaying.load() || m_isPaused.load())
        return false;
    // m_llCurrentPosition already holds the PTS of the last presented
    // frame, so Play() resumes the clock from exactly here.
    m_isPaused.store(true);
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(m_mediacodec);
    if (mc && mc->audioStream)
        mc->audioStream->Stop();
    return true;
}

bool MoviePlayer::StopMediaCodecLocked()
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(m_mediacodec);
    if (!mc)
        return false;
    if (mc->audioStream)
        mc->audioStream->Stop();
    m_isPlaying.store(false);
    m_isPaused.store(false);
    // Seek back to the start so a later Play() replays from the beginning.
    if (AMediaExtractor_seekTo(mc->extractor, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) == AMEDIA_OK)
    {
        if (mc->pendingBufIndex >= 0 && mc->videoCodec)
        {
            AMediaCodec_releaseOutputBuffer(mc->videoCodec,
                static_cast<size_t>(mc->pendingBufIndex), false);
            mc->pendingBufIndex = -1;
            mc->pendingBuf = nullptr;
            mc->pendingBufSize = 0;
        }
        if (mc->videoCodec)
            AMediaCodec_flush(mc->videoCodec);
        if (mc->audioCodec)
            AMediaCodec_flush(mc->audioCodec);
        mc->inputEof = false;
        mc->videoEof = false;
        mc->audioEof = false;
        mc->videoEosSent = false;
        mc->audioEosSent = false;
        mc->firstVideoPts = -1.0;
        mc->pendingPtsRel = 0.0;
        mc->audioBacklog.clear();
        if (mc->resamplerReady)
            mc->resampler.Reset(static_cast<double>(mc->audioSampleRate),
                static_cast<double>(Miniaudio::kSampleRate));
    }
    m_llCurrentPosition.store(m_playStartMediaTime >= 0 ? m_playStartMediaTime : 0);
    mc->resumePtsRel = 0.0;
    return true;
}

bool MoviePlayer::ResetMediaCodecLocked()
{
    CloseMediaCodecLocked();
    return true;
}

bool MoviePlayer::SeekToMediaCodecLocked(double timeInSeconds)
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(m_mediacodec);
    if (!mc || !mc->extractor)
        return false;
    const int64_t ts = static_cast<int64_t>(
        (mc->firstVideoPts >= 0.0 ? mc->firstVideoPts : 0.0) * 1000000.0 +
        timeInSeconds * 1000000.0);
    if (AMediaExtractor_seekTo(mc->extractor, ts, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK)
        return false;
    if (mc->pendingBufIndex >= 0 && mc->videoCodec)
    {
        AMediaCodec_releaseOutputBuffer(mc->videoCodec,
            static_cast<size_t>(mc->pendingBufIndex), false);
        mc->pendingBufIndex = -1;
        mc->pendingBuf = nullptr;
        mc->pendingBufSize = 0;
    }
    if (mc->videoCodec)
        AMediaCodec_flush(mc->videoCodec);
    if (mc->audioCodec)
        AMediaCodec_flush(mc->audioCodec);
    mc->inputEof = false;
    mc->videoEof = false;
    mc->audioEof = false;
    mc->videoEosSent = false;
    mc->audioEosSent = false;
    mc->firstVideoPts = -1.0;
    mc->pendingPtsRel = 0.0;
    mc->audioBacklog.clear();
    if (mc->resamplerReady)
        mc->resampler.Reset(static_cast<double>(mc->audioSampleRate),
            static_cast<double>(Miniaudio::kSampleRate));
    // Re-anchor the clock at the seek target (relative to the first frame).
    m_llCurrentPosition.store(m_playStartMediaTime +
        static_cast<LONGLONG>(timeInSeconds * 10000000.0));
    mc->resumePtsRel = timeInSeconds;
    m_playStartWallTime = std::chrono::steady_clock::now();
    return true;
}

bool MoviePlayer::UpdateFrameMediaCodecLocked()
{
    if (!m_isPlaying.load() || m_isPaused.load())
        return false;
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(m_mediacodec);
    if (!mc)
        return false;

    std::unique_lock<std::recursive_mutex> updateLock(m_mutex, std::try_to_lock);
    if (!updateLock.owns_lock())
        return false;

    // Media time, in seconds relative to the first video frame: the
    // resume anchor captured in Play()/SeekTo() plus the wall-clock
    // elapsed since then. (The anchor is fixed for the whole play run;
    // recomputing it from m_llCurrentPosition would double-count.)
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_playStartWallTime).count();
    const double mediaTimeRel = mc->resumePtsRel + elapsed;

    const bool presented = DecodeUntilMediaCodecLocked(mediaTimeRel);

    // End of movie: both decoders drained (unlike the FFmpeg path, which
    // stops at file exhaustion, the MediaCodec path drains the decoders
    // fully, so the tail is not cut). Video ends on the duration; an
    // audio-only movie ends when the ring has drained.
    if (mc->inputEof && mc->videoEof)
    {
        if (m_hasVideo.load())
        {
            if (mc->durationSecs > 0.0 && mediaTimeRel >= mc->durationSecs)
            {
                m_isPlaying.store(false);
                if (mc->audioStream)
                    mc->audioStream->Stop();
            }
        }
        else if (mc->audioStream && mc->audioStream->Readable() == 0 && mc->audioBacklog.empty())
        {
            m_isPlaying.store(false);
        }
    }
    return presented;
}

// Convert one decoded video frame into the CPU BGRA buffer at the
// renderer size and publish it. ptsAbsSeconds is the frame PTS in
// seconds (absolute); it is stored in m_llCurrentPosition in MF 100-ns
// units, like the Windows path. buf is one decoder output buffer; its
// layout comes from the output format (see UpdateVideoOutputFormat).
// Caller must hold m_mutex.
void MoviePlayer::PresentVideoFrameMediaCodecLocked(void* mcVoid, const uint8_t* buf, size_t bufSize, double ptsAbsSeconds)
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(mcVoid);
    if (!mc || !buf || mc->outW <= 0 || mc->outH <= 0 || mc->srcW <= 0 || mc->srcH <= 0)
        return;
    const int strideY = (mc->outStrideY > 0) ? mc->outStrideY : mc->srcW;
    const int sliceHeight = (mc->outSliceHeight > 0) ? mc->outSliceHeight : ((mc->srcH + 15) & ~15);
    const int strideUV = (mc->outStrideUV > 0) ? mc->outStrideUV : strideY;
    const size_t ySize = static_cast<size_t>(strideY) * static_cast<size_t>(sliceHeight);
    const uint8_t* uPlane = nullptr;
    const uint8_t* vPlane = nullptr;
    if (mc->uvPixelStride == 1)
    {
        // Planar I420: U then V, each strideUV x sliceHeight/2.
        uPlane = buf + ySize;
        vPlane = uPlane + static_cast<size_t>(strideUV) * static_cast<size_t>(sliceHeight / 2);
    }
    else
    {
        // Semi-planar NV12 (and Flexible, assumed NV12): one interleaved
        // UV plane after Y.
        uPlane = buf + ySize;
    }
    // Defensive: never read past the buffer the decoder handed us.
    const size_t chromaBytes = static_cast<size_t>(strideUV) * static_cast<size_t>(sliceHeight / 2) *
        (mc->uvPixelStride == 1 ? 2 : 1);
    if (bufSize < ySize + chromaBytes)
        return;  // truncated buffer: drop the frame
    const size_t bytes = static_cast<size_t>(mc->outW) * static_cast<size_t>(mc->outH) * 4;
    if (m_cpuFrameBuffer.size() != bytes)
        m_cpuFrameBuffer.resize(bytes);
    AndroidMediaConvert::Yuv420ToBgra(buf, uPlane, vPlane, mc->uvPixelStride,
        mc->srcW, mc->srcH, strideY, strideUV,
        m_cpuFrameBuffer.data(), mc->outW, mc->outH, mc->outW * 4);
    m_cpuFrameWidth = static_cast<uint32_t>(mc->outW);
    m_cpuFrameHeight = static_cast<uint32_t>(mc->outH);
    m_llCurrentPosition.store(static_cast<LONGLONG>(ptsAbsSeconds * 10000000.0));
    m_hasNewFrame.store(true);
    m_lastFrameTime = std::chrono::steady_clock::now();
}

// Drain resampled audio from the backlog into the miniaudio ring.
// Caller must hold m_mutex.
void MoviePlayer::DrainAudioBacklogMediaCodecLocked(void* mcVoid)
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(mcVoid);
    if (!mc || !mc->audioStream || mc->audioBacklog.empty())
        return;
    const size_t frames = mc->audioBacklog.size() / 2;
    const size_t written = mc->audioStream->Write(mc->audioBacklog.data(), frames);
    if (written >= frames)
    {
        mc->audioBacklog.clear();
    }
    else if (written > 0)
    {
        mc->audioBacklog.erase(mc->audioBacklog.begin(),
            mc->audioBacklog.begin() + static_cast<std::ptrdiff_t>(written * 2));
    }
}

// Resample one decoded audio buffer to 44100 Hz stereo s16 and append
// it to the backlog (drained into the ring by
// DrainAudioBacklogMediaCodecLocked, so nothing is lost when the ring is
// momentarily full). pcm is one decoder output buffer; its format is the
// audio output format (UpdateAudioOutputFormat). Caller must hold m_mutex.
void MoviePlayer::ResampleAndQueueAudioMediaCodecLocked(void* mcVoid, const uint8_t* pcm, size_t byteCount)
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(mcVoid);
    if (!mc || !pcm || byteCount == 0 || !mc->audioStream ||
        mc->audioChannels <= 0 || mc->audioSampleRate <= 0)
        return;
    if (!mc->resamplerReady)
    {
        mc->resampler.Reset(static_cast<double>(mc->audioSampleRate),
            static_cast<double>(Miniaudio::kSampleRate));
        mc->resamplerReady = true;
    }
    const int bytesPerSample = mc->audioIsFloat ? 4 : 2;
    const size_t inFrames = byteCount /
        (static_cast<size_t>(mc->audioChannels) * static_cast<size_t>(bytesPerSample));
    if (inFrames == 0)
        return;
    const size_t cap = mc->resampler.MaxOutputFrames(inFrames);
    const size_t base = mc->audioBacklog.size();
    mc->audioBacklog.resize(base + cap * 2);
    size_t written = 0;
    if (mc->audioIsFloat)
    {
        written = mc->resampler.ProcessFloat(reinterpret_cast<const float*>(pcm),
            inFrames, mc->audioChannels, mc->audioBacklog.data() + base, cap);
    }
    else
    {
        written = mc->resampler.ProcessS16(reinterpret_cast<const int16_t*>(pcm),
            inFrames, mc->audioChannels, mc->audioBacklog.data() + base, cap);
    }
    mc->audioBacklog.resize(base + written * 2);
}

// Decode until the video frame for mediaTimeRel is presented and the
// audio ring is topped up. mediaTimeRel is seconds relative to the
// first video frame; mediaTimeRel < 0 means "present the first video
// frame, whatever its PTS" (opening frame, called from
// OpenMovieMediaCodecLocked). Returns true if a new video frame was
// presented. Caller must hold m_mutex.
bool MoviePlayer::DecodeUntilMediaCodecLocked(double mediaTimeRel)
{
    MovieMediaCodec* mc = static_cast<MovieMediaCodec*>(m_mediacodec);
    if (!mc)
        return false;
    bool presented = false;

    // 1) Drain any backlog into the ring first.
    DrainAudioBacklogMediaCodecLocked(mc);

    // 2) Present a held pending frame if it is due now.
    if (mc->pendingBufIndex >= 0 && mediaTimeRel >= 0 && mc->pendingPtsRel <= mediaTimeRel)
    {
        PresentVideoFrameMediaCodecLocked(mc, mc->pendingBuf, mc->pendingBufSize,
            mc->firstVideoPts + mc->pendingPtsRel);
        AMediaCodec_releaseOutputBuffer(mc->videoCodec,
            static_cast<size_t>(mc->pendingBufIndex), false);
        mc->pendingBufIndex = -1;
        mc->pendingBuf = nullptr;
        mc->pendingBufSize = 0;
        mc->pendingPtsRel = 0.0;
        presented = true;
    }
    // 3) A pending frame held for the future bounds decoding: stop here
    //    (the audio ring holds ~186 ms, so this never starves audio).
    if (mc->pendingBufIndex >= 0)
        return presented;

    // 4) Feed and drain, bounded per call.
    for (int n = 0; n < kMediaCodecMaxStepsPerCall; ++n)
    {
        // 4a) Feed one sample into the decoder it belongs to (or EOS
        //     when the extractor is exhausted).
        if (!mc->inputEof)
        {
            const ssize_t track = AMediaExtractor_getSampleTrackIndex(mc->extractor);
            if (track < 0)
            {
                // End of file: queue EOS into both decoders (retrying
                // next call when no input buffer is free yet).
                const bool vEos = QueueCodecEos(mc->videoCodec);
                const bool aEos = QueueCodecEos(mc->audioCodec);
                if (vEos && aEos)
                {
                    mc->videoEosSent = true;
                    mc->audioEosSent = true;
                    mc->inputEof = true;
                }
            }
            else if (track == mc->videoTrack && mc->videoCodec)
            {
                const ssize_t inIndex = AMediaCodec_dequeueInputBuffer(mc->videoCodec, 0);
                if (inIndex >= 0)
                {
                    size_t capacity = 0;
                    uint8_t* buf = AMediaCodec_getInputBuffer(mc->videoCodec, static_cast<size_t>(inIndex), &capacity);
                    const ssize_t sampleSize = (buf && capacity > 0)
                        ? AMediaExtractor_readSampleData(mc->extractor, buf, capacity)
                        : -1;
                    if (sampleSize > 0)
                    {
                        const int64_t ptsUs = AMediaExtractor_getSampleTime(mc->extractor);
                        // Only the sync bit is meaningful to the decoder;
                        // mask off anything else (e.g. the encrypted bit,
                        // which would alias to CODEC_CONFIG).
                        const uint32_t flags = static_cast<uint32_t>(
                            AMediaExtractor_getSampleFlags(mc->extractor)) & 1u;
                        AMediaCodec_queueInputBuffer(mc->videoCodec, static_cast<size_t>(inIndex),
                            0, static_cast<size_t>(sampleSize),
                            static_cast<uint64_t>(ptsUs > 0 ? ptsUs : 0), flags);
                    }
                    // Advance past the sample whether it was queued or
                    // not, so a bad sample cannot stall the extractor.
                    AMediaExtractor_advance(mc->extractor);
                }
                // No video input buffer free right now: the sample stays
                // queued in the extractor; retry next call.
            }
            else if (track == mc->audioTrack && mc->audioCodec)
            {
                // Decode audio only while the ring (plus backlog) is
                // below the target, so a long UpdateFrame gap cannot
                // queue seconds of audio ahead of the video. When the
                // ring is full, the sample is DROPPED (advanced past)
                // so the video samples behind it in the shared
                // extractor are not blocked — the same tradeoff the
                // FFmpeg path makes.
                const size_t ringLevel = mc->audioStream
                    ? mc->audioStream->Readable() + mc->audioBacklog.size() / 2 : 0;
                if (ringLevel < kMediaCodecAudioRingTarget)
                {
                    const ssize_t inIndex = AMediaCodec_dequeueInputBuffer(mc->audioCodec, 0);
                    if (inIndex >= 0)
                    {
                        size_t capacity = 0;
                        uint8_t* buf = AMediaCodec_getInputBuffer(mc->audioCodec, static_cast<size_t>(inIndex), &capacity);
                        const ssize_t sampleSize = (buf && capacity > 0)
                            ? AMediaExtractor_readSampleData(mc->extractor, buf, capacity)
                            : -1;
                        if (sampleSize > 0)
                        {
                            const int64_t ptsUs = AMediaExtractor_getSampleTime(mc->extractor);
                            const uint32_t flags = static_cast<uint32_t>(
                                AMediaExtractor_getSampleFlags(mc->extractor)) & 1u;
                            AMediaCodec_queueInputBuffer(mc->audioCodec, static_cast<size_t>(inIndex),
                                0, static_cast<size_t>(sampleSize),
                                static_cast<uint64_t>(ptsUs > 0 ? ptsUs : 0), flags);
                        }
                        AMediaExtractor_advance(mc->extractor);
                    }
                    // No audio input buffer free: the sample stays; retry.
                }
                else
                {
                    AMediaExtractor_advance(mc->extractor);  // drop
                }
            }
            else
            {
                // Sample for a track we do not decode (subtitles, ...).
                AMediaExtractor_advance(mc->extractor);
            }
        }

        // 4b) Drain one video output buffer.
        if (mc->videoCodec && !mc->videoEof)
        {
            AMediaCodecBufferInfo info{};
            const ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(mc->videoCodec, &info, 0);
            if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
            {
                UpdateVideoOutputFormat(mc);
            }
            else if (outIndex >= 0)
            {
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
                {
                    mc->videoEof = true;
                    AMediaCodec_releaseOutputBuffer(mc->videoCodec, static_cast<size_t>(outIndex), false);
                }
                else
                {
                    const double ptsAbs = static_cast<double>(info.presentationTimeUs) / 1000000.0;
                    if (mc->firstVideoPts < 0.0)
                        mc->firstVideoPts = ptsAbs;
                    const double ptsRel = ptsAbs - mc->firstVideoPts;
                    if (mediaTimeRel < 0 || ptsRel <= mediaTimeRel)
                    {
                        size_t bufSize = 0;
                        uint8_t* buf = AMediaCodec_getOutputBuffer(mc->videoCodec, static_cast<size_t>(outIndex), &bufSize);
                        // The valid frame is buf + info.offset, info.size
                        // long (offset is 0 on every decoder seen so far,
                        // but the info is authoritative, not the pointer).
                        if (buf && info.size > 0 && static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= bufSize)
                            PresentVideoFrameMediaCodecLocked(mc, buf + info.offset, static_cast<size_t>(info.size), ptsAbs);
                        AMediaCodec_releaseOutputBuffer(mc->videoCodec, static_cast<size_t>(outIndex), false);
                        presented = true;
                        // Continue: another frame may also be due.
                    }
                    else
                    {
                        // Decoded ahead of the media time — hold this one
                        // frame for the next call and stop reading.
                        size_t bufSize = 0;
                        uint8_t* buf = AMediaCodec_getOutputBuffer(mc->videoCodec, static_cast<size_t>(outIndex), &bufSize);
                        mc->pendingBufIndex = outIndex;
                        mc->pendingBuf = (buf && static_cast<size_t>(info.offset) <= bufSize)
                            ? buf + info.offset : buf;
                        mc->pendingBufSize = (info.size > 0) ? static_cast<size_t>(info.size) : bufSize;
                        mc->pendingPtsRel = ptsRel;
                        return presented;
                    }
                }
            }
        }

        // 4c) Drain one audio output buffer (only while the ring has room;
        //     the decoder's output queue provides the backpressure).
        if (mc->audioCodec && !mc->audioEof && mc->audioStream)
        {
            const size_t ringLevel = mc->audioStream->Readable() + mc->audioBacklog.size() / 2;
            if (ringLevel < kMediaCodecAudioRingTarget)
            {
                AMediaCodecBufferInfo info{};
                const ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(mc->audioCodec, &info, 0);
                if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
                {
                    UpdateAudioOutputFormat(mc);
                }
                else if (outIndex >= 0)
                {
                    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
                    {
                        mc->audioEof = true;
                        AMediaCodec_releaseOutputBuffer(mc->audioCodec, static_cast<size_t>(outIndex), false);
                    }
                    else
                    {
                        size_t bufSize = 0;
                        uint8_t* buf = AMediaCodec_getOutputBuffer(mc->audioCodec, static_cast<size_t>(outIndex), &bufSize);
                        if (buf && info.size > 0 && static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= bufSize)
                            ResampleAndQueueAudioMediaCodecLocked(mc, buf + info.offset, static_cast<size_t>(info.size));
                        AMediaCodec_releaseOutputBuffer(mc->audioCodec, static_cast<size_t>(outIndex), false);
                    }
                }
            }
        }

        // Both decoders drained and the file exhausted: nothing more to do.
        if (mc->inputEof && mc->videoEof && (mc->audioEof || !mc->audioCodec))
            break;
    }

    // 5) Whatever audio was decoded, push it into the ring now.
    DrainAudioBacklogMediaCodecLocked(mc);
    return presented;
}
