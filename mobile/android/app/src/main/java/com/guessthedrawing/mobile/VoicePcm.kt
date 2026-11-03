package com.guessthedrawing.mobile

import java.util.Base64
import kotlin.math.roundToInt
import kotlin.math.abs
import kotlin.math.log10

object VoicePcm {
    const val SAMPLE_RATE = 16000
    const val FRAME_BYTES = 640
    fun decode(encoded: String): ByteArray? {
        if (encoded.length != 856) return null
        val bytes = runCatching { Base64.getDecoder().decode(encoded) }.getOrNull() ?: return null
        return bytes.takeIf { it.size == FRAME_BYTES && Base64.getEncoder().encodeToString(it) == encoded }
    }
}

/** Measures real captured samples; opening AudioRecord alone does not prove that input exists. */
class VoiceInputMeter(private val sampleRate: Int) {
    init { require(sampleRate > 0) }
    var level = 0; private set
    private var silentSamples = 0L
    val noSignal get() = silentSamples >= sampleRate * 3L
    fun accept(samples: ShortArray, count: Int) {
        require(count in 0..samples.size)
        if (count == 0) return
        var peak = 0
        for (i in 0 until count) peak = maxOf(peak, abs(samples[i].toInt()))
        silentSamples = if (peak == 0) minOf(silentSamples + count, sampleRate * 3L) else 0
        // A logarithmic meter makes normal speech visible without altering the transmitted PCM.
        level = if (peak == 0) 0 else ((1 + log10(peak / 32768.0) / 3) * 100).roundToInt().coerceIn(0, 100)
    }
}

/** Stateful mono resampling; partial hardware buffers never become wire frames. */
class VoiceFramer(private val inputRate: Int) {
    init { require(inputRate > 0) }
    private var index = 0L
    private var next = 0.0
    private var previous: Short = 0
    private var frame = ByteArray(VoicePcm.FRAME_BYTES)
    private var used = 0
    fun append(samples: ShortArray, count: Int = samples.size): List<ByteArray> {
        require(count in 0..samples.size)
        val result = mutableListOf<ByteArray>()
        for (i in 0 until count) {
            val current = samples[i]
            while (next <= index + 0.000001) {
                val fraction = if (index == 0L) 1.0 else (next - (index - 1)).coerceIn(0.0, 1.0)
                val sample = (previous + (current.toInt() - previous.toInt()) * fraction).roundToInt().coerceIn(-32768, 32767)
                frame[used++] = sample.toByte(); frame[used++] = (sample shr 8).toByte()
                if (used == frame.size) { result.add(frame); frame = ByteArray(VoicePcm.FRAME_BYTES); used = 0 }
                next += inputRate.toDouble() / VoicePcm.SAMPLE_RATE
            }
            previous = current; index++
        }
        return result
    }
}
