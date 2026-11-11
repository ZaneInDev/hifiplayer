package com.hifi.player.audio

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager

/**
 * Enumerates connected USB audio output devices (DACs/USB DSD/USB headsets) and
 * exposes their natively-supported sample rates and encodings.
 *
 * This is the SDK-side half of the "bit-perfect USB" feature described in
 * UsbDirectOutput.h: we find the device here, read what it actually supports,
 * and hand its AudioDeviceInfo.id down to the native engine so Oboe can open
 * an exclusive stream pinned directly to it — bypassing the default/shared
 * output path entirely.
 */
class UsbAudioDeviceManager(context: Context) {

    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    data class UsbDacInfo(
        val deviceInfo: AudioDeviceInfo,
        val productName: String,
        val supportedSampleRates: IntArray,
        val supportedEncodings: IntArray
    )

    /** Returns all currently connected USB output devices (DAC, headset, etc.). */
    fun listUsbOutputDevices(): List<UsbDacInfo> {
        val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        return devices.filter {
            it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                    it.type == AudioDeviceInfo.TYPE_USB_HEADSET ||
                    it.type == AudioDeviceInfo.TYPE_USB_ACCESSORY
        }.map { info ->
            UsbDacInfo(
                deviceInfo = info,
                productName = info.productName?.toString() ?: "USB Audio Device",
                supportedSampleRates = info.sampleRates ?: intArrayOf(),
                supportedEncodings = info.encodings ?: intArrayOf()
            )
        }
    }

    /**
     * Picks the best matching native sample rate for a given source rate from a
     * USB device's advertised capabilities. Returns the source rate unchanged if
     * the device claims to support it directly (true bit-perfect path); otherwise
     * returns the closest supported rate so the native resampler fallback can
     * at least do a clean, deliberate conversion rather than an implicit one.
     */
    fun resolveNativeRate(dac: UsbDacInfo, sourceSampleRate: Int): Int {
        if (dac.supportedSampleRates.isEmpty()) return sourceSampleRate
        if (dac.supportedSampleRates.contains(sourceSampleRate)) return sourceSampleRate
        return dac.supportedSampleRates.minByOrNull { kotlin.math.abs(it - sourceSampleRate) }
            ?: sourceSampleRate
    }

    /** True if the device advertises a high-res-capable encoding (24/32-bit or float). */
    fun supportsHighRes(dac: UsbDacInfo): Boolean {
        val highResEncodings = setOf(
            21,
            22,
            4
        )
        return dac.supportedEncodings.any { it in highResEncodings }
    }
}
