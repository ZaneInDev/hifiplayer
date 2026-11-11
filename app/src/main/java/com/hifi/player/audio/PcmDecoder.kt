package com.hifi.player.audio

import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.content.Context
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer

/**
 * Decodes a compressed audio source (FLAC, MP3, AAC, WAV, etc.) via Android's
 * MediaCodec into a raw interleaved PCM scratch file, preserving the source's
 * native sample rate and bit depth as closely as the platform decoder allows.
 *
 * Why decode to a flat file instead of streaming PCM frame-by-frame from
 * MediaCodec directly into the native engine? Two reasons:
 *  1. It decouples decode speed from the real-time audio callback — the native
 *     engine just does fast buffered file reads, so there's zero risk of a
 *     MediaCodec hiccup causing an audio glitch.
 *  2. It gives us a trivial, sample-accurate seek: seeking is just an fseek to
 *     frame * frameSize in the native layer, no decoder state to rewind.
 *
 * Trade-off: full-file decode before playback starts. For music (not infinite
 * streams) this is a negligible delay (typically <1s even for FLAC) and is the
 * same approach used by several bit-perfect-focused players.
 *
 * IMPORTANT for "no muddying": we deliberately do NOT request a specific
 * PCM encoding that would force 16-bit output. We read whatever MediaCodec's
 * decoder natively emits (KEY_PCM_ENCODING) and preserve full precision —
 * most platform decoders (incl. FLAC) emit 16-bit PCM, but high-res sources
 * on API 31+ devices with ENCODING_PCM_FLOAT support pass through comparably.
 */
class PcmDecoder(private val context: Context) {

    data class DecodedInfo(
        val pcmFile: File,
        val sampleRate: Int,
        val channelCount: Int,
        val bitDepth: Int
    )

    fun decode(uri: Uri, scratchDir: File): DecodedInfo? {
        val extractor = MediaExtractor()
        extractor.setDataSource(context, uri, null)

        var audioTrackIndex = -1
        var format: MediaFormat? = null
        for (i in 0 until extractor.trackCount) {
            val f = extractor.getTrackFormat(i)
            val mime = f.getString(MediaFormat.KEY_MIME) ?: continue
            if (mime.startsWith("audio/")) {
                audioTrackIndex = i
                format = f
                break
            }
        }

        if (audioTrackIndex < 0 || format == null) {
            extractor.release()
            return null
        }

        extractor.selectTrack(audioTrackIndex)
        val mime = format.getString(MediaFormat.KEY_MIME)!!
        val sourceSampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        val sourceChannels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)

        val codec = MediaCodec.createDecoderByType(mime)
        codec.configure(format, null, null, 0)
        codec.start()

        val outFile = File(scratchDir, "decoded_${System.currentTimeMillis()}.pcm")
        val outStream = FileOutputStream(outFile)

        var detectedBitDepth = 16 // MediaCodec default output encoding is almost always 16-bit PCM
        val bufferInfo = MediaCodec.BufferInfo()
        var sawInputEos = false
        var sawOutputEos = false

        try {
            while (!sawOutputEos) {
                if (!sawInputEos) {
                    val inIndex = codec.dequeueInputBuffer(10_000)
                    if (inIndex >= 0) {
                        val inputBuffer: ByteBuffer = codec.getInputBuffer(inIndex)!!
                        val sampleSize = extractor.readSampleData(inputBuffer, 0)
                        if (sampleSize < 0) {
                            codec.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            sawInputEos = true
                        } else {
                            val presentationTimeUs = extractor.sampleTime
                            codec.queueInputBuffer(inIndex, 0, sampleSize, presentationTimeUs, 0)
                            extractor.advance()
                        }
                    }
                }

                val outIndex = codec.dequeueOutputBuffer(bufferInfo, 10_000)
                when {
                    outIndex >= 0 -> {
                        val outBuffer: ByteBuffer = codec.getOutputBuffer(outIndex)!!

                        // Query the decoder's actual output PCM encoding when available (API 24+).
                        val outFormat = codec.getOutputFormat(outIndex)
                        if (outFormat.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                            detectedBitDepth = when (outFormat.getInteger(MediaFormat.KEY_PCM_ENCODING)) {
                                android.media.AudioFormat.ENCODING_PCM_FLOAT -> 32
                                android.media.AudioFormat.ENCODING_PCM_32BIT -> 32
                                android.media.AudioFormat.ENCODING_PCM_24BIT_PACKED -> 24
                                else -> 16
                            }
                        }

                        if (bufferInfo.size > 0) {
                            val chunk = ByteArray(bufferInfo.size)
                            outBuffer.position(bufferInfo.offset)
                            outBuffer.limit(bufferInfo.offset + bufferInfo.size)
                            outBuffer.get(chunk)
                            outStream.write(chunk)
                        }
                        codec.releaseOutputBuffer(outIndex, false)

                        if (bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) {
                            sawOutputEos = true
                        }
                    }
                    outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val newFormat = codec.outputFormat
                        if (newFormat.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                            detectedBitDepth = when (newFormat.getInteger(MediaFormat.KEY_PCM_ENCODING)) {
                                android.media.AudioFormat.ENCODING_PCM_FLOAT -> 32
                                android.media.AudioFormat.ENCODING_PCM_32BIT -> 32
                                android.media.AudioFormat.ENCODING_PCM_24BIT_PACKED -> 24
                                else -> 16
                            }
                        }
                    }
                }
            }
        } finally {
            outStream.flush()
            outStream.close()
            codec.stop()
            codec.release()
            extractor.release()
        }

        return DecodedInfo(
            pcmFile = outFile,
            sampleRate = sourceSampleRate,
            channelCount = sourceChannels,
            bitDepth = detectedBitDepth
        )
    }
}
