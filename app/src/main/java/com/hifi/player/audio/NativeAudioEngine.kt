package com.hifi.player.audio

/**
 * Thin Kotlin wrapper over the native (C++/Oboe) audio engine.
 *
 * All real-time playback — decoding to float32, exclusive/MMAP AAudio streaming,
 * resampling fallback, USB device routing — happens natively. This class only
 * marshals calls across JNI; it holds no audio state itself.
 */
class NativeAudioEngine {

    companion object {
        init {
            System.loadLibrary("hifiplayer")
        }
    }

    /**
     * Opens a raw interleaved PCM file (already decoded from the compressed
     * source format by [PcmDecoder]) for playback.
     *
     * @param pcmPath absolute path to the raw PCM scratch file
     * @param sampleRate source sample rate in Hz (e.g. 44100, 96000)
     * @param channelCount number of interleaved channels (1 or 2 typical)
     * @param bitDepth bits per sample in the PCM file (16, 24, or 32)
     */
    external fun nativeOpen(pcmPath: String, sampleRate: Int, channelCount: Int, bitDepth: Int): Boolean

    external fun nativeStart(): Boolean
    external fun nativePause()
    external fun nativeStop()
    external fun nativeClose()

    /** Linear gain, 0.0–1.0. A single multiply — no loudness curve, no compression. */
    external fun nativeSetVolume(gain: Float)

    external fun nativeSeekToFrame(frame: Long)
    external fun nativeGetCurrentFrame(): Long
    external fun nativeGetTotalFrames(): Long
    external fun nativeIsPlaying(): Boolean

    /** Pass an [android.media.AudioDeviceInfo.getId] to pin output to a specific device (e.g. USB DAC). */
    external fun nativeSetPreferredDeviceId(deviceId: Int)

    /** Request AAudio exclusive (MMAP) sharing mode. Engine falls back to Shared if refused by the HAL. */
    external fun nativeSetExclusiveMode(exclusive: Boolean)
}
