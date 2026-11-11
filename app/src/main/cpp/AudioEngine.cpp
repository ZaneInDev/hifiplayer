#include "AudioEngine.h"
#include <android/log.h>
#include <algorithm>
#include <cstring>

#define TAG "HiFi.AudioEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace hifi {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    close();
}

bool AudioEngine::open(const std::string &pcmPath, const SourceFormat &format) {
    close();

    sourceFormat_ = format;
    pcmFile_.open(pcmPath, std::ios::binary);
    if (!pcmFile_.is_open()) {
        LOGE("Failed to open PCM source: %s", pcmPath.c_str());
        return false;
    }

    pcmFile_.seekg(0, std::ios::end);
    std::streamoff bytes = pcmFile_.tellg();
    pcmFile_.seekg(0, std::ios::beg);

    int32_t bytesPerSample = sourceFormat_.bitDepth / 8;
    int32_t frameSize = bytesPerSample * sourceFormat_.channelCount;
    totalFrames_ = frameSize > 0 ? static_cast<int64_t>(bytes / frameSize) : 0;
    currentFrame_.store(0);

    return openStream();
}

bool AudioEngine::openStream() {
    oboe::AudioStreamBuilder builder;

    builder.setDirection(oboe::Direction::Output)
            ->setFormat(oboe::AudioFormat::Float)         // 32-bit float throughout — no int truncation
            ->setChannelCount(sourceFormat_.channelCount)
            ->setSampleRate(sourceFormat_.sampleRate)      // ask for native rate; avoids AudioFlinger resample
            ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Best)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setUsage(oboe::Usage::Media)
            ->setContentType(oboe::ContentType::Music)
            ->setDataCallback(this)
            ->setErrorCallback(this)
            // Exclusive + MMAP is the actual "bit-perfect direct path" request.
            // Oboe transparently falls back to Shared mode if the HAL can't honor it,
            // which we detect below via afterOpen inspection.
            ->setSharingMode(exclusiveMode_ ? oboe::SharingMode::Exclusive : oboe::SharingMode::Shared);

    if (preferredDeviceId_ != oboe::kUnspecified) {
        // Routes directly to a specific device (e.g. an enumerated USB DAC) instead of
        // the default output, which is required for bit-perfect USB passthrough.
        builder.setDeviceId(preferredDeviceId_);
    }

    oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
        LOGE("Failed to open stream: %s", oboe::convertToText(result));
        // Fall back to Shared mode once if Exclusive was refused outright.
        if (exclusiveMode_) {
            LOGW("Retrying with Shared sharing mode");
            exclusiveMode_ = false;
            return openStream();
        }
        return false;
    }

    auto actualSharing = stream_->getSharingMode();
    auto actualPerf = stream_->getPerformanceMode();
    LOGI("Stream opened: sharing=%s perf=%s rate=%d framesPerBurst=%d",
         oboe::convertToText(actualSharing),
         oboe::convertToText(actualPerf),
         stream_->getSampleRate(),
         stream_->getFramesPerBurst());

    // If the device granted a different rate than our source, we must resample
    // ourselves at high quality rather than let the shared mixer degrade it.
    int32_t deviceRate = stream_->getSampleRate();
    if (deviceRate != sourceFormat_.sampleRate) {
        LOGW("Device rate %d != source rate %d — enabling high-quality resampler",
             deviceRate, sourceFormat_.sampleRate);
        resampler_ = std::make_unique<Resampler>(sourceFormat_.sampleRate, deviceRate,
                                                  sourceFormat_.channelCount);
        needsResample_ = true;
    } else {
        needsResample_ = false;
        resampler_.reset();
    }

    // Set the buffer size to the minimum burst-aligned size reported by the stream
    // for lowest achievable latency without underruns (not a quality knob, but keeps
    // playback responsive which matters for gapless/seek behavior).
    stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);

    return true;
}

void AudioEngine::closeStream() {
    if (stream_) {
        stream_->stop();
        stream_->close();
        stream_.reset();
    }
}

bool AudioEngine::start() {
    if (!stream_) return false;
    oboe::Result result = stream_->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("requestStart failed: %s", oboe::convertToText(result));
        return false;
    }
    playing_.store(true);
    return true;
}

void AudioEngine::pause() {
    if (stream_) {
        stream_->requestPause();
    }
    playing_.store(false);
}

void AudioEngine::stop() {
    if (stream_) {
        stream_->requestStop();
    }
    playing_.store(false);
}

void AudioEngine::close() {
    closeStream();
    if (pcmFile_.is_open()) {
        pcmFile_.close();
    }
    resampler_.reset();
    currentFrame_.store(0);
    totalFrames_ = 0;
}

void AudioEngine::setVolume(float linearGain) {
    volume_.store(std::clamp(linearGain, 0.0f, 1.0f));
}

void AudioEngine::seekToFrame(int64_t frame) {
    if (!pcmFile_.is_open()) return;
    frame = std::clamp<int64_t>(frame, 0, totalFrames_);
    int32_t bytesPerSample = sourceFormat_.bitDepth / 8;
    int32_t frameSize = bytesPerSample * sourceFormat_.channelCount;
    pcmFile_.clear();
    pcmFile_.seekg(frame * frameSize, std::ios::beg);
    currentFrame_.store(frame);
}

void AudioEngine::setPreferredDeviceId(int32_t deviceId) {
    preferredDeviceId_ = deviceId;
    // Re-open on the new device if we're already streaming (USB DAC hot-plug case).
    if (stream_) {
        bool wasPlaying = playing_.load();
        int64_t savedFrame = currentFrame_.load();
        closeStream();
        openStream();
        seekToFrame(savedFrame);
        if (wasPlaying) start();
    }
}

void AudioEngine::setExclusiveMode(bool exclusive) {
    exclusiveMode_ = exclusive;
}

// Reads raw source samples (any bit depth) and converts to float32 in [-1, 1].
// This is the ONLY place bit-depth conversion happens, and it is a pure linear
// scale — no noise shaping, no dither, no compression.
int32_t AudioEngine::readSourceFrames(float *outBuffer, int32_t numFrames) {
    if (!pcmFile_.is_open()) return 0;

    int32_t channels = sourceFormat_.channelCount;
    int32_t bytesPerSample = sourceFormat_.bitDepth / 8;
    int32_t frameSize = bytesPerSample * channels;

    std::vector<uint8_t> raw(static_cast<size_t>(numFrames) * frameSize);
    pcmFile_.read(reinterpret_cast<char *>(raw.data()), raw.size());
    std::streamsize bytesRead = pcmFile_.gcount();
    int32_t framesRead = static_cast<int32_t>(bytesRead / frameSize);

    for (int32_t f = 0; f < framesRead; f++) {
        for (int32_t c = 0; c < channels; c++) {
            size_t byteOffset = static_cast<size_t>(f) * frameSize + static_cast<size_t>(c) * bytesPerSample;
            float sample = 0.0f;

            switch (sourceFormat_.bitDepth) {
                case 16: {
                    int16_t s16;
                    std::memcpy(&s16, &raw[byteOffset], 2);
                    sample = static_cast<float>(s16) / 32768.0f;
                    break;
                }
                case 24: {
                    // 24-bit packed little-endian -> sign-extended 32-bit
                    int32_t s24 = (raw[byteOffset]) | (raw[byteOffset + 1] << 8) | (raw[byteOffset + 2] << 16);
                    if (s24 & 0x800000) s24 |= 0xFF000000; // sign extend
                    sample = static_cast<float>(s24) / 8388608.0f;
                    break;
                }
                case 32: {
                    int32_t s32;
                    std::memcpy(&s32, &raw[byteOffset], 4);
                    sample = static_cast<float>(s32) / 2147483648.0f;
                    break;
                }
                default:
                    sample = 0.0f;
            }
            outBuffer[f * channels + c] = sample;
        }
    }

    currentFrame_.fetch_add(framesRead);
    return framesRead;
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) {
    auto *output = static_cast<float *>(audioData);
    int32_t channels = stream->getChannelCount();
    float gain = volume_.load();

    if (!playing_.load()) {
        std::memset(output, 0, static_cast<size_t>(numFrames) * channels * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    if (needsResample_ && resampler_) {
        // Pull enough source frames to produce numFrames of output at the target rate.
        int32_t neededSourceFrames = resampler_->estimateInputFramesNeeded(numFrames);
        if (static_cast<int32_t>(sourceScratch_.size()) < neededSourceFrames * channels) {
            sourceScratch_.resize(neededSourceFrames * channels);
        }
        int32_t gotSource = readSourceFrames(sourceScratch_.data(), neededSourceFrames);

        if (static_cast<int32_t>(resampleScratch_.size()) < numFrames * channels) {
            resampleScratch_.resize(numFrames * channels);
        }
        int32_t produced = resampler_->process(sourceScratch_.data(), gotSource,
                                                 resampleScratch_.data(), numFrames);

        for (int32_t i = 0; i < produced * channels; i++) {
            output[i] = resampleScratch_[i] * gain;
        }
        for (int32_t i = produced * channels; i < numFrames * channels; i++) {
            output[i] = 0.0f; // underrun at EOF: silence, not garbage
        }

        if (gotSource == 0 && produced == 0) {
            playing_.store(false);
            return oboe::DataCallbackResult::Stop;
        }
    } else {
        int32_t got = readSourceFrames(output, numFrames);
        for (int32_t i = 0; i < got * channels; i++) {
            output[i] *= gain;
        }
        for (int32_t i = got * channels; i < numFrames * channels; i++) {
            output[i] = 0.0f;
        }

        if (got == 0) {
            playing_.store(false);
            return oboe::DataCallbackResult::Stop;
        }
    }

    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) {
    LOGE("Stream error after close: %s", oboe::convertToText(error));
    // Typical cause: USB DAC unplugged, or audio focus/device change forcing a stream
    // disconnect. Attempt a clean reopen on the default device.
    (void) stream;
    preferredDeviceId_ = oboe::kUnspecified;
    openStream();
}

} // namespace hifi
