package com.hifi.player.audio

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import com.hifi.player.Track
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * High-level playback orchestrator used by the UI layer. Wires together:
 *  - PcmDecoder (compressed source -> raw PCM scratch file)
 *  - NativeAudioEngine (JNI into the Oboe/AAudio exclusive-MMAP C++ engine)
 *  - UsbAudioDeviceManager (optional bit-perfect USB DAC routing)
 */
class PlaybackController(private val context: Context) {

    private val engine = NativeAudioEngine()
    private val decoder = PcmDecoder(context)
    private val usbManager = UsbAudioDeviceManager(context)
    private val mainHandler = Handler(Looper.getMainLooper())
    private val scope = CoroutineScope(Dispatchers.Main)

    private var currentTrack: Track? = null
    private var currentPcmFile: File? = null
    private var progressRunnable: Runnable? = null

    var onPlaybackStateChanged: ((isPlaying: Boolean) -> Unit)? = null
    var onProgressUpdate: ((currentMs: Long, totalMs: Long) -> Unit)? = null
    var onTrackLoaded: ((Track, routeDescription: String) -> Unit)? = null
    var onError: ((String) -> Unit)? = null

    /** Preferred USB DAC, or null to use the default output device. */
    var preferredUsbDevice: UsbAudioDeviceManager.UsbDacInfo? = null
        set(value) {
            field = value
            engine.nativeSetPreferredDeviceId(value?.deviceInfo?.id ?: android.media.AudioDeviceInfo.TYPE_UNKNOWN)
        }

    var exclusiveModeEnabled: Boolean = true
        set(value) {
            field = value
            engine.nativeSetExclusiveMode(value)
        }

    fun availableUsbDacs(): List<UsbAudioDeviceManager.UsbDacInfo> = usbManager.listUsbOutputDevices()

    fun load(track: Track) {
        scope.launch {
            try {
                val info = withContext(Dispatchers.IO) {
                    val scratchDir = File(context.cacheDir, "pcm_scratch").apply { mkdirs() }
                    // Clean up previous scratch file before decoding the next one.
                    currentPcmFile?.delete()
                    decoder.decode(track.uri, scratchDir)
                }

                if (info == null) {
                    onError?.invoke("Unsupported or unreadable audio source: ${track.title}")
                    return@launch
                }

                currentPcmFile = info.pcmFile
                currentTrack = track

                engine.nativeClose()

                // If a USB DAC is selected, resolve the best native rate for it first.
                val targetRate = preferredUsbDevice?.let { dac ->
                    usbManager.resolveNativeRate(dac, info.sampleRate)
                } ?: info.sampleRate

                val opened = withContext(Dispatchers.IO) {
                    engine.nativeOpen(info.pcmFile.absolutePath, targetRate, info.channelCount, info.bitDepth)
                }

                if (!opened) {
                    onError?.invoke("Failed to open audio engine for: ${track.title}")
                    return@launch
                }

                val routeDesc = buildString {
                    append(if (preferredUsbDevice != null) "USB: ${preferredUsbDevice?.productName}" else "Internal DAC")
                    append(" • ${info.sampleRate}Hz/${info.bitDepth}-bit")
                    if (exclusiveModeEnabled) append(" • Exclusive/MMAP requested")
                }

                onTrackLoaded?.invoke(track, routeDesc)
                startProgressUpdates()

            } catch (e: Exception) {
                onError?.invoke("Error loading track: ${e.message}")
            }
        }
    }

    fun play() {
        if (engine.nativeStart()) {
            onPlaybackStateChanged?.invoke(true)
        }
    }

    fun pause() {
        engine.nativePause()
        onPlaybackStateChanged?.invoke(false)
    }

    fun stop() {
        engine.nativeStop()
        onPlaybackStateChanged?.invoke(false)
        stopProgressUpdates()
    }

    fun seekTo(positionMs: Long) {
        val track = currentTrack ?: return
        val totalFrames = engine.nativeGetTotalFrames()
        if (track.durationMs <= 0) return
        val targetFrame = (positionMs.toDouble() / track.durationMs.toDouble() * totalFrames).toLong()
        engine.nativeSeekToFrame(targetFrame)
    }

    fun setVolume(linearGain: Float) {
        engine.nativeSetVolume(linearGain.coerceIn(0f, 1f))
    }

    fun isPlaying(): Boolean = engine.nativeIsPlaying()

    fun release() {
        stopProgressUpdates()
        engine.nativeClose()
        currentPcmFile?.delete()
    }

    private fun startProgressUpdates() {
        stopProgressUpdates()
        progressRunnable = object : Runnable {
            override fun run() {
                val track = currentTrack
                if (track != null) {
                    val current = engine.nativeGetCurrentFrame()
                    val total = engine.nativeGetTotalFrames()
                    if (total > 0) {
                        val currentMs = (current.toDouble() / total.toDouble() * track.durationMs).toLong()
                        onProgressUpdate?.invoke(currentMs, track.durationMs)
                    }
                }
                if (engine.nativeIsPlaying()) {
                    mainHandler.postDelayed(this, 250L)
                }
            }
        }
        mainHandler.post(progressRunnable!!)
    }

    private fun stopProgressUpdates() {
        progressRunnable?.let { mainHandler.removeCallbacks(it) }
        progressRunnable = null
    }
}
