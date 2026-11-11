#ifndef HIFIPLAYER_USBDIRECTOUTPUT_H
#define HIFIPLAYER_USBDIRECTOUTPUT_H

#include <cstdint>
#include <string>

namespace hifi {

/**
 * Thin native-side helper for USB DAC bit-perfect output.
 *
 * The actual USB device enumeration (VID/PID, supported sample rates/formats)
 * is done on the Kotlin side via android.media.AudioDeviceInfo /
 * AudioManager.getDevices(GET_DEVICES_OUTPUTS), because that API surface is
 * only exposed in the SDK, not in the NDK. Once Kotlin identifies a
 * TYPE_USB_DEVICE / TYPE_USB_HEADSET AudioDeviceInfo and reads its supported
 * sample rates and encodings, it passes the chosen deviceId down through JNI
 * to AudioEngine::setPreferredDeviceId(), which is forwarded directly into
 * oboe::AudioStreamBuilder::setDeviceId(). That is what gives us a direct,
 * exclusive route to the USB DAC instead of going through the default mixed
 * output — this is the actual mechanism of "bit-perfect" on Android: skip
 * the shared mixer, open an exclusive MMAP stream pinned to that device ID,
 * at the sample rate/format the DAC natively advertises.
 *
 * This header exists to document that contract and hold any small native-side
 * validation helpers.
 */
class UsbDirectOutput {
public:
    // Sanity-checks a (sampleRate, bitDepth) pair against USB Audio Class 2.0
    // common native rates. Used to warn (not silently resample) if the chosen
    // device/source combination will NOT be bit-perfect.
    static bool isLikelyNativeRate(int32_t sampleRate);

    static std::string describeRoute(bool exclusiveGranted, int32_t requestedRate, int32_t actualRate);
};

} // namespace hifi

#endif // HIFIPLAYER_USBDIRECTOUTPUT_H
