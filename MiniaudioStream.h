// MiniaudioStream.h
//
// Shared miniaudio streaming helper (Linux / Android only; Windows keeps
// DirectSound). One small piece of code serves all five tracker players
// (MOD / IT / S3M / MPTM / XM) and the SoundManager's streaming needs, so
// the miniaudio boilerplate is written once instead of six times.
//
// Architecture: each tracker player mixes PCM on its own playback thread
// (FillAudioBuffer) and the miniaudio data callback runs on miniaudio's
// audio thread. The two threads share ONE single-producer /
// single-consumer ring buffer of stereo s16 frames: FillAudioBuffer is
// the producer, the data callback is the consumer. The audio thread only
// drains the ring, so it never touches player state (MixAudio is not
// thread-safe — it is only ever called from the playback thread, exactly
// as on Windows). On underrun the callback outputs silence.
//
// If no real audio device exists (a headless machine, a container), the
// null backend is used instead, so the pipeline still runs and the data
// callback still fires — the engine never fails to start for lack of a
// sound card, and headless verification can still observe the mixed
// output through the tap below.
//
// The tap is a test / metering hook: it receives each mixed output block
// on the audio thread. It is how the headless test proves the tracker
// players produce non-silent audio without a speaker.
#pragma once

#include "Platform.h"

#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
#include "miniaudio.h"

namespace Miniaudio {

// All tracker players render the same format: 44100 Hz, stereo, s16.
constexpr ma_uint32 kSampleRate = 44100;
constexpr ma_uint32 kChannels = 2;

// Single-producer / single-consumer ring of stereo s16 frames. Lock-free:
// the producer only writes m_write (release) and the consumer only writes
// m_read (release); both read the other index with acquire. Indices are
// monotonic (they wrap only after 2^64 frames), so "readable" and "free"
// are simple differences and the capacity need not be a power of two.
class AudioRing {
public:
    explicit AudioRing(size_t capacityFrames);
    // Producer: copy up to frameCount frames into the ring. Returns the
    // frames actually written; the rest are dropped (the ring is full,
    // which means the producer is running ahead of the consumer).
    size_t Write(const int16_t* frames, size_t frameCount);
    // Consumer: copy up to frameCount frames out of the ring. Returns the
    // frames actually read; the caller zero-fills the remainder (underrun).
    size_t Read(int16_t* frames, size_t frameCount);
    size_t Readable() const;
    size_t Capacity() const { return m_capacity; }
    // Discard all buffered frames (e.g. TTS Stop). Only call while the
    // consumer is stopped; the producer must not be writing concurrently.
    void Clear() { m_read.store(m_write.load(std::memory_order_relaxed), std::memory_order_relaxed); }
private:
    std::vector<int16_t> m_data;        // capacity * 2 int16s (stereo interleaved)
    size_t m_capacity = 0;              // in frames
    std::atomic<size_t> m_write{ 0 };   // producer index (monotonic, in frames)
    std::atomic<size_t> m_read{ 0 };    // consumer index (monotonic, in frames)
};

// A miniaudio playback device fed by a producer thread through the ring.
class Stream {
public:
    Stream();
    ~Stream();
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    // True when the device was created (real device or null backend).
    bool IsReady() const { return m_deviceInit; }
    // True when the fallback null backend is in use (no real audio device).
    bool IsNullBackend() const { return m_nullBackend; }
    bool IsRunning() const { return m_started; }

    // Start / stop playback. Start is idempotent; it returns false when
    // there is no device to start.
    bool Start();
    void Stop();

    // Producer: copy frames into the ring (see AudioRing::Write).
    size_t Write(const int16_t* frames, size_t frameCount) { return m_ring.Write(frames, frameCount); }
    size_t Readable() const { return m_ring.Readable(); }
    // Free frames in the ring (for producer backpressure).
    size_t Writable() const { return m_ring.Capacity() - m_ring.Readable(); }
    // Discard all buffered frames (see AudioRing::Clear).
    void Clear() { m_ring.Clear(); }

    // Test / metering tap: called on the audio thread with each mixed
    // output block, after the ring has been drained into it.
    // Thread-safe: the tap may be installed or removed while the device
    // is running (the recorder installs it when recording starts).
    void SetOutputTap(std::function<void(const int16_t* frames, size_t frameCount)> tap);

    // Drive the data callback directly (headless test path): fills `out`
    // with `frameCount` frames exactly as the audio thread would,
    // including the tap.
    void DrainForTest(int16_t* out, size_t frameCount) { Drain(out, frameCount); }
private:
    static void DataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
    // The shared drain: copy from the ring, zero-fill on underrun, tap.
    void Drain(int16_t* out, size_t frameCount);

    ma_context m_context{};
    bool m_hasContext = false;          // a null-backend context was created
    ma_device m_device{};
    bool m_deviceInit = false;
    bool m_started = false;
    bool m_nullBackend = false;
    AudioRing m_ring;
    std::function<void(const int16_t* frames, size_t frameCount)> m_tap;
    std::mutex m_tapMutex;              // guards m_tap (see SetOutputTap / Drain)
};

// Test hook (headless verification only): a tap installed here is attached
// to every Stream created afterwards, so a test can observe the mixed output
// of a tracker player without reaching into the player's private stream.
// Never installed by the engine itself; null by default (no effect).
void SetGlobalOutputTap(std::function<void(const int16_t* frames, size_t frameCount)> tap);

// Recorder hook: install `tap` on every live Stream AND on every Stream
// created afterwards (via the global tap), so a recorder started while
// music is already playing still captures it. Pass nullptr to remove the
// tap from all streams. Thread-safe.
void SetTapOnAllStreams(std::function<void(const int16_t* frames, size_t frameCount)> tap);

} // namespace Miniaudio

#endif // PLATFORM_LINUX || PLATFORM_ANDROID
