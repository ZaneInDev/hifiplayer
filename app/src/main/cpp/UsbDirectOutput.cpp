#include "UsbDirectOutput.h"
#include <array>
#include <algorithm>
#include <sstream>

namespace hifi {

bool UsbDirectOutput::isLikelyNativeRate(int32_t sampleRate) {
    static constexpr std::array<int32_t, 8> kCommonRates = {
            44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
    };
    return std::find(kCommonRates.begin(), kCommonRates.end(), sampleRate) != kCommonRates.end();
}

std::string UsbDirectOutput::describeRoute(bool exclusiveGranted, int32_t requestedRate, int32_t actualRate) {
    std::ostringstream oss;
    oss << (exclusiveGranted ? "EXCLUSIVE/MMAP" : "SHARED (fallback)") << " @ " << actualRate << "Hz";
    if (requestedRate != actualRate) {
        oss << " [resampled from " << requestedRate << "Hz — not bit-perfect]";
    } else {
        oss << " [bit-perfect]";
    }
    return oss.str();
}

} // namespace hifi
