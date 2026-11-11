#ifndef HIFIPLAYER_RESAMPLER_H
#define HIFIPLAYER_RESAMPLER_H

#include <vector>
#include <cstdint>

namespace hifi {

/**
 * Windowed-sinc band-limited resampler.
 *
 * This exists ONLY as a fallback for the rare case where the AAudio exclusive
 * stream can't be opened at the source's native sample rate (some HALs force
 * 48000 Hz regardless of request). In that case we must resample somewhere —
 * and Android's AudioFlinger resampler (used in the shared mixer path) is a
 * comparatively low-order linear/cubic interpolator that smears high-frequency
 * content, which is exactly the "muddiness" being avoided here.
 *
 * This implementation uses a Kaiser-windowed sinc kernel (64 taps/phase),
 * which gives a much steeper anti-alias cutoff and flatter passband than
 * a linear or cubic interpolator, at the cost of more CPU — an acceptable
 * trade for a dedicated playback app that isn't doing anything else.
 *
 * No psychoacoustic shaping, no noise injection: this is strictly a
 * mathematically correct band-limited rate conversion.
 */
class Resampler {
public:
    Resampler(int32_t inputRate, int32_t outputRate, int32_t channels);

    // Returns how many input frames are needed to safely produce outputFrames.
    int32_t estimateInputFramesNeeded(int32_t outputFrames) const;

    // Consumes up to inputFrames of input, writes up to outputFrames of output.
    // Returns frames actually written.
    int32_t process(const float *input, int32_t inputFrames, float *output, int32_t outputFrames);

private:
    static constexpr int32_t kTapsPerPhase = 64;
    static constexpr int32_t kPhases = 256; // sub-sample phase resolution

    void buildKernel();
    float interpolateTap(int32_t phase, int32_t tapIndex) const;

    int32_t inputRate_;
    int32_t outputRate_;
    int32_t channels_;
    double ratio_; // outputRate / inputRate

    std::vector<float> kernel_; // [phase][tap], Kaiser-windowed sinc

    // Circular history buffer per channel so we can resume filtering seamlessly
    // across callback boundaries without clicks/discontinuities.
    std::vector<std::vector<float>> history_;
    double phaseAccumulator_ = 0.0;
};

} // namespace hifi

#endif // HIFIPLAYER_RESAMPLER_H
