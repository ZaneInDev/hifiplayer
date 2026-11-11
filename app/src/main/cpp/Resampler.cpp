#include "Resampler.h"
#include <cmath>
#include <algorithm>

namespace hifi {

// Zeroth-order modified Bessel function, needed for the Kaiser window.
static double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    double xHalf = x / 2.0;
    for (int k = 1; k < 32; k++) {
        term *= (xHalf / k) * (xHalf / k);
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

static double sinc(double x) {
    if (std::abs(x) < 1e-9) return 1.0;
    double px = M_PI * x;
    return std::sin(px) / px;
}

Resampler::Resampler(int32_t inputRate, int32_t outputRate, int32_t channels)
        : inputRate_(inputRate), outputRate_(outputRate), channels_(channels) {
    ratio_ = static_cast<double>(outputRate_) / static_cast<double>(inputRate_);
    buildKernel();

    history_.resize(channels_);
    for (auto &h : history_) {
        h.assign(kTapsPerPhase, 0.0f); // zero-padded history at start
    }
}

void Resampler::buildKernel() {
    // Kaiser window beta=8.6 gives ~90dB stopband attenuation — well below the noise
    // floor of any real source material, so no audible aliasing artifacts.
    const double beta = 8.6;
    const double i0Beta = besselI0(beta);

    // Cutoff relative to the lower of the two rates, to satisfy Nyquist for both.
    double cutoff = std::min(1.0, ratio_) * 0.95; // slight guard band below Nyquist

    kernel_.resize(static_cast<size_t>(kPhases) * kTapsPerPhase);

    for (int32_t phase = 0; phase < kPhases; phase++) {
        double fracOffset = static_cast<double>(phase) / kPhases;
        for (int32_t tap = 0; tap < kTapsPerPhase; tap++) {
            // Center the kernel around kTapsPerPhase/2
            double x = (tap - kTapsPerPhase / 2.0) + fracOffset;
            double windowArg = (x + kTapsPerPhase / 2.0) / kTapsPerPhase; // 0..1 approx
            double k = 2.0 * windowArg - 1.0;
            double window = (std::abs(k) <= 1.0)
                             ? besselI0(beta * std::sqrt(std::max(0.0, 1.0 - k * k))) / i0Beta
                             : 0.0;
            double h = cutoff * sinc(cutoff * x) * window;
            kernel_[static_cast<size_t>(phase) * kTapsPerPhase + tap] = static_cast<float>(h);
        }
    }
}

int32_t Resampler::estimateInputFramesNeeded(int32_t outputFrames) const {
    // +kTapsPerPhase guard for filter history/edge taps.
    return static_cast<int32_t>(std::ceil(outputFrames / ratio_)) + kTapsPerPhase;
}

int32_t Resampler::process(const float *input, int32_t inputFrames, float *output, int32_t outputFrames) {
    if (inputFrames <= 0) return 0;

    // Build a working buffer per channel: [history][new input]
    std::vector<std::vector<float>> working(channels_);
    for (int32_t c = 0; c < channels_; c++) {
        working[c].reserve(history_[c].size() + inputFrames);
        working[c] = history_[c];
        for (int32_t f = 0; f < inputFrames; f++) {
            working[c].push_back(input[f * channels_ + c]);
        }
    }

    int32_t producedFrames = 0;
    double inputPos = static_cast<double>(kTapsPerPhase / 2); // start past the history guard

    while (producedFrames < outputFrames) {
        double srcIndexF = inputPos;
        auto srcIndex = static_cast<int32_t>(std::floor(srcIndexF));
        double frac = srcIndexF - srcIndex;

        // Not enough look-ahead samples to safely filter this output frame yet.
        if (srcIndex + kTapsPerPhase / 2 >= static_cast<int32_t>(working[0].size())) {
            break;
        }

        auto phase = static_cast<int32_t>(frac * kPhases);
        phase = std::clamp(phase, 0, kPhases - 1);

        for (int32_t c = 0; c < channels_; c++) {
            double acc = 0.0;
            const float *kernelRow = &kernel_[static_cast<size_t>(phase) * kTapsPerPhase];
            for (int32_t tap = 0; tap < kTapsPerPhase; tap++) {
                int32_t sampleIdx = srcIndex - kTapsPerPhase / 2 + tap;
                if (sampleIdx >= 0 && sampleIdx < static_cast<int32_t>(working[c].size())) {
                    acc += working[c][sampleIdx] * kernelRow[tap];
                }
            }
            output[producedFrames * channels_ + c] = static_cast<float>(acc);
        }

        producedFrames++;
        inputPos += 1.0 / ratio_;
    }

    // Save tail of this buffer as history for next call (seamless continuation).
    for (int32_t c = 0; c < channels_; c++) {
        size_t sz = working[c].size();
        size_t histLen = std::min<size_t>(kTapsPerPhase, sz);
        history_[c].assign(working[c].end() - static_cast<long>(histLen), working[c].end());
        if (history_[c].size() < static_cast<size_t>(kTapsPerPhase)) {
            history_[c].insert(history_[c].begin(), kTapsPerPhase - history_[c].size(), 0.0f);
        }
    }

    return producedFrames;
}

} // namespace hifi
