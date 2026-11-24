// MiniaudioStream.cpp — implementation of MiniaudioStream.h (Linux /
// Android only). This file also holds the single MINIAUDIO_IMPLEMENTATION
// for the whole engine: miniaudio.h is included with the implementation
// exactly once, here, so every other file only sees the declarations.
//
// NOTE: PLATFORM_LINUX / PLATFORM_ANDROID are defined by Platform.h, so
// Platform.h must be included BEFORE the guard below is evaluated —
// testing the macros first makes this whole file compile to nothing.
#include "Platform.h"

#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)

#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioStream.h"

#include <algorithm>
#include <cstring>

namespace Miniaudio {

// Test hook storage (see SetGlobalOutputTap in the header). Set before any
// Stream is created; the Stream constructor copies it into its own tap.
static std::function<void(const int16_t* frames, size_t frameCount)> g_globalTap;

// Live-stream registry for SetTapOnAllStreams: every Stream registers
// itself on construction and unregisters on destruction, so a recorder
// can install a tap on streams that already exist (music already playing).
static std::vector<Stream*> g_liveStreams;
static std::mutex g_liveStreamsMutex;

void SetGlobalOutputTap(std::function<void(const int16_t* frames, size_t frameCount)> tap)
{
    g_globalTap = std::move(tap);
}

void SetTapOnAllStreams(std::function<void(const int16_t* frames, size_t frameCount)> tap)
{
    // Future streams pick the tap up from the global hook at construction.
    g_globalTap = tap;
    // Live streams get it now. The registry mutex is held for the whole
    // iteration, so a Stream cannot unregister (be destroyed) mid-loop.
    std::lock_guard<std::mutex> lock(g_liveStreamsMutex);
    for (Stream* s : g_liveStreams)
    {
        if (s)
            s->SetOutputTap(tap);
    }
}

AudioRing::AudioRing(size_t capacityFrames)
    : m_data(capacityFrames * 2, 0)
    , m_capacity(capacityFrames)
{
}

size_t AudioRing::Write(const int16_t* frames, size_t frameCount)
{
    if (m_capacity == 0 || frames == nullptr || frameCount == 0) return 0;
    const size_t read = m_read.load(std::memory_order_acquire);
    const size_t written = m_write.load(std::memory_order_relaxed);
    const size_t free = m_capacity - (written - read);
    const size_t n = std::min(frameCount, free);
    if (n == 0) return 0;
    // Two chunks at most: up to the end of the buffer, then from the start.
    const size_t first = std::min(n, m_capacity - (written % m_capacity));
    std::memcpy(m_data.data() + (written % m_capacity) * 2, frames, first * 2 * sizeof(int16_t));
    if (n > first) {
        std::memcpy(m_data.data(), frames + first * 2, (n - first) * 2 * sizeof(int16_t));
    }
    m_write.store(written + n, std::memory_order_release);
    return n;
}

size_t AudioRing::Read(int16_t* frames, size_t frameCount)
{
    if (m_capacity == 0 || frames == nullptr || frameCount == 0) return 0;
    const size_t written = m_write.load(std::memory_order_acquire);
    const size_t read = m_read.load(std::memory_order_relaxed);
    const size_t avail = written - read;
    const size_t n = std::min(frameCount, avail);
    if (n == 0) return 0;
    const size_t first = std::min(n, m_capacity - (read % m_capacity));
    std::memcpy(frames, m_data.data() + (read % m_capacity) * 2, first * 2 * sizeof(int16_t));
    if (n > first) {
        std::memcpy(frames + first * 2, m_data.data(), (n - first) * 2 * sizeof(int16_t));
    }
    m_read.store(read + n, std::memory_order_release);
    return n;
}

size_t AudioRing::Readable() const
{
    const size_t written = m_write.load(std::memory_order_acquire);
    const size_t read = m_read.load(std::memory_order_acquire);
    return written - read;
}

Stream::Stream()
    : m_ring(kSampleRate)   // one second of stereo s16 frames
{
    if (g_globalTap) m_tap = g_globalTap;   // test hook (see SetGlobalOutputTap)
    {
        std::lock_guard<std::mutex> lock(g_liveStreamsMutex);
        g_liveStreams.push_back(this);
    }
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = kChannels;
    config.sampleRate = kSampleRate;
    config.dataCallback = &Stream::DataCallback;
    config.pUserData = this;

    // First try the default backends (ALSA / PulseAudio / JACK / ...).
    if (ma_device_init(nullptr, &config, &m_device) == MA_SUCCESS) {
        m_deviceInit = true;
        return;
    }
    // No real audio device (headless machine, container): fall back to
    // the null backend so the pipeline still runs and the data callback
    // still fires. The engine never fails to start for lack of a sound
    // card, and the mixed output is still observable through the tap.
    ma_backend nullBackends[] = { ma_backend_null };
    if (ma_context_init(nullBackends, 1, nullptr, &m_context) == MA_SUCCESS) {
        m_hasContext = true;
        // The context is passed to ma_device_init directly (ma_device_config
        // has no pContext member); see the call below.
        if (ma_device_init(&m_context, &config, &m_device) == MA_SUCCESS) {
            m_deviceInit = true;
            m_nullBackend = true;
        }
    }
}

Stream::~Stream()
{
    {
        std::lock_guard<std::mutex> lock(g_liveStreamsMutex);
        auto it = std::find(g_liveStreams.begin(), g_liveStreams.end(), this);
        if (it != g_liveStreams.end())
            g_liveStreams.erase(it);
    }
    Stop();
    if (m_deviceInit) {
        ma_device_uninit(&m_device);
        m_deviceInit = false;
    }
    if (m_hasContext) {
        ma_context_uninit(&m_context);
        m_hasContext = false;
    }
}

bool Stream::Start()
{
    if (!m_deviceInit || m_started) return m_deviceInit;
    if (ma_device_start(&m_device) != MA_SUCCESS) return false;
    m_started = true;
    return true;
}

void Stream::Stop()
{
    if (!m_deviceInit || !m_started) return;
    ma_device_stop(&m_device);
    m_started = false;
}

void Stream::SetOutputTap(std::function<void(const int16_t* frames, size_t frameCount)> tap)
{
    std::lock_guard<std::mutex> lock(m_tapMutex);
    m_tap = std::move(tap);
}

void Stream::DataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
    (void)input;
    Stream* self = static_cast<Stream*>(device->pUserData);
    if (self == nullptr) return;
    self->Drain(static_cast<int16_t*>(output), frameCount);
}

void Stream::Drain(int16_t* out, size_t frameCount)
{
    const size_t got = m_ring.Read(out, frameCount);
    if (got < frameCount) {
        // Underrun: the producer has not mixed enough yet. Silence the
        // rest instead of repeating stale audio.
        std::memset(out + got * 2, 0, (frameCount - got) * 2 * sizeof(int16_t));
    }
    // Copy the tap under its mutex, then call it outside the lock: the tap
    // may be installed or removed from another thread at any moment, and
    // the tap itself must never run while the mutex is held (it may call
    // back into this Stream).
    std::function<void(const int16_t* frames, size_t frameCount)> tap;
    {
        std::lock_guard<std::mutex> lock(m_tapMutex);
        tap = m_tap;
    }
    if (tap) tap(out, frameCount);
}

} // namespace Miniaudio

#endif // PLATFORM_LINUX || PLATFORM_ANDROID
