#ifndef HIFIPLAYER_AUDIOENGINE_H
#define HIFIPLAYER_AUDIOENGINE_H

#include <oboe/Oboe.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include "Resampler.h"

namespace hifi {

// Decoded source format, read from the file header before playback starts.
struct SourceFormat {
    int32_t sampleRate = 44100;
    int32_t channelCount = 2;
    int32_t bitDepth = 16; // bits per sample in the source file
};

/**
 * AudioEngine owns the entire real-time playback path:
 *
 *   PCM file --> [float32 decode] --> [Resampler if needed] --> Oboe AAudio stream (exclusive, MMAP)
 *
 * Design choices for "clarity without EQ coloration":
 *  - Internal pipeline is always 32-bit float, regardless of source bit depth.
 *    This avoids any intermediate integer truncation/dither between decode and output.
 *  - We request PerformanceMode::LowLatency + SharingMode::Exclusive, which on
 *    API 27+ hardware routes through AAudio's MMAP path — bypassing AudioFlinger's
 *    software mixer entirely when the device/HAL supports it. That mixer is the
 *    single biggest source of "muddiness" (resampling + mixing + volume curves
 *    applied even at single-stream playback).
 *  - We set the stream's sample rate to MATCH the source's native rate whenever
 *    the device supports it, so Android's own resampler is never invoked. Only
 *    when the HAL forces a different rate do we fall back to our own high-quality
 *    sinc resampler (see Resampler.h) instead of letting AudioFlinger's resampler
 *    (linear/low-order) degrade the signal.
 *  - No EQ, no loudness normalization, no dynamic range processing anywhere in
 *    this path. Gain is a single linear multiply (volume), nothing else touches
 *    sample values.
 */
class AudioEngine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    // Opens a raw/decoded PCM source (produced by the Kotlin-side MediaExtractor+decoder,
    // written to a pipe/temp buffer as interleaved PCM — see JNI bridge).
    bool open(const std::string &pcmPath, const SourceFormat &format);

    bool start();
    void pause();
    void stop();
    void close();

    void setVolume(float linearGain); // 0.0 - 1.0, single multiply, no curve shaping
    void seekToFrame(int64_t frame);

    int64_t getCurrentFrame() const { return currentFrame_.load(); }
    int64_t getTotalFrames() const { return totalFrames_; }
    bool isPlaying() const { return playing_.load(); }

    // USB direct-output control (see UsbDirectOutput.cpp for the AAudio device-id plumbing)
    void setPreferredDeviceId(int32_t deviceId); // from AudioManager USB device enumeration
    void setExclusiveMode(bool exclusive);

    // oboe::AudioStreamDataCallback
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override;

    // oboe::AudioStreamErrorCallback
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool openStream();
    void closeStream();
    int32_t readSourceFrames(float *outBuffer, int32_t numFrames);

    std::shared_ptr<oboe::AudioStream> stream_;
    std::ifstream pcmFile_;
    SourceFormat sourceFormat_{};

    std::unique_ptr<Resampler> resampler_;
    bool needsResample_ = false;

    std::atomic<bool> playing_{false};
    std::atomic<int64_t> currentFrame_{0};
    int64_t totalFrames_ = 0;

    std::atomic<float> volume_{1.0f};
    int32_t preferredDeviceId_ = oboe::kUnspecified;
    bool exclusiveMode_ = true;

    // Scratch buffers reused per callback to avoid any allocation on the audio thread.
    std::vector<float> sourceScratch_;
    std::vector<float> resampleScratch_;
};

} // namespace hifi

#endif // HIFIPLAYER_AUDIOENGINE_H
