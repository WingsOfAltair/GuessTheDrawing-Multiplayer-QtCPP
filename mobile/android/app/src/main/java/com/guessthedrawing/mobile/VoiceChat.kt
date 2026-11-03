package com.guessthedrawing.mobile

import android.annotation.SuppressLint
import android.content.Context
import android.media.*
import android.media.audiofx.AcousticEchoCanceler
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import org.json.JSONObject
import java.util.Base64
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

/** UI-thread control; hardware reads/writes run on workers. */
class VoiceChat(context: Context, private val client: GameClient) {
    private val manager = context.getSystemService(AudioManager::class.java)
    private val ui = Handler(Looper.getMainLooper())
    var changed: () -> Unit = {}
    @Volatile var recording = false; private set
    var inputLevel = 0; private set
    var inputHasNoSignal = false; private set
    val inputStatus: String get() = when {
        inputHasNoSignal && (Build.HARDWARE.contains("ranchu") || Build.HARDWARE.contains("goldfish")) ->
            "No microphone signal. Emulator: Extended Controls > Microphone > Enable Host Microphone Access."
        inputHasNoSignal -> "No microphone signal. Check the device's microphone access and input."
        inputLevel > 0 -> "Microphone input detected"
        else -> "Microphone listening…"
    }
    var listening = true
        set(value) { field = value; if (!value) stopPlayback(); changed() }
    var foreground = true
        set(value) { field = value; if (!value) close() }
    @Volatile private var recorder: AudioRecord? = null
    private var captureEpoch = 0
    private var routed = false
    private var previousMode = AudioManager.MODE_NORMAL
    private var previousSpeaker = false
    private var playbackErrorAt = 0L
    private val attributes = AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build()
    private val focus = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
        .setAudioAttributes(attributes).setOnAudioFocusChangeListener({ change ->
            if (change < 0) { close(); client.notice("Voice paused by another audio session. Tap Mic to speak again.") }
        }, ui).build()
    private val devices = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(added: Array<out AudioDeviceInfo>) { if (routed) selectRoute() }
        override fun onAudioDevicesRemoved(removed: Array<out AudioDeviceInfo>) {
            if (routed) { selectRoute(); stopPlayback() }
        }
    }
    private class Speaker(val track: AudioTrack) {
        val queue = ArrayBlockingQueue<ByteArray>(12)
        @Volatile var active = true
        @Volatile var lastReceived = SystemClock.elapsedRealtime()
        fun stop() { active = false; queue.clear(); runCatching { track.pause(); track.flush() } }
    }
    private val speakers = ConcurrentHashMap<String, Speaker>()
    @Suppress("DEPRECATION")
    private fun selectRoute() {
        if (Build.VERSION.SDK_INT >= 31) {
            val available = manager.availableCommunicationDevices
            val types = listOf(AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_USB_HEADSET,
                AudioDeviceInfo.TYPE_BLE_HEADSET, AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
                AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_BUILTIN_SPEAKER)
            val device = types.firstNotNullOfOrNull { type -> available.firstOrNull { it.type == type } }
            if (device != null && manager.communicationDevice?.id != device.id) manager.setCommunicationDevice(device)
        } else {
            val headset = manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).any {
                it.type in listOf(AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
                    AudioDeviceInfo.TYPE_USB_HEADSET, AudioDeviceInfo.TYPE_BLUETOOTH_SCO)
            }
            manager.isSpeakerphoneOn = !headset
        }
    }
    @Suppress("DEPRECATION")
    private fun configureAudio() {
        if (routed) return
        check(manager.requestAudioFocus(focus) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) { "Audio is in use by another app" }
        previousMode = manager.mode; previousSpeaker = manager.isSpeakerphoneOn; routed = true
        manager.mode = AudioManager.MODE_IN_COMMUNICATION
        manager.registerAudioDeviceCallback(devices, ui); selectRoute()
    }
    @Suppress("DEPRECATION")
    private fun releaseAudio() {
        if (!routed) return
        routed = false; manager.unregisterAudioDeviceCallback(devices)
        if (Build.VERSION.SDK_INT >= 31) manager.clearCommunicationDevice() else manager.isSpeakerphoneOn = previousSpeaker
        manager.mode = previousMode; manager.abandonAudioFocusRequest(focus)
    }
    @SuppressLint("MissingPermission") // MainActivity requests permission before start.
    fun start(): Boolean {
        stopRecording()
        if (!foreground || !client.canSpeak) return false
        return try {
            configureAudio()
            var input: AudioRecord? = null
            for (source in listOf(MediaRecorder.AudioSource.VOICE_COMMUNICATION, MediaRecorder.AudioSource.MIC)) {
                for (rate in listOf(16000, 48000, 44100)) {
                    var candidate: AudioRecord? = null
                    try {
                        val minimum = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
                        if (minimum <= 0) continue
                        candidate = AudioRecord(source, rate, AudioFormat.CHANNEL_IN_MONO,
                            AudioFormat.ENCODING_PCM_16BIT, maxOf(minimum * 2, rate / 5 * 2))
                        check(candidate.state == AudioRecord.STATE_INITIALIZED)
                        candidate.startRecording(); check(candidate.recordingState == AudioRecord.RECORDSTATE_RECORDING)
                        input = candidate; break
                    } catch (_: Exception) { runCatching { candidate?.release() } }
                }
                if (input != null) break
            }
            val active = checkNotNull(input) { "No usable microphone input" }
            val echo = if (AcousticEchoCanceler.isAvailable()) runCatching {
                AcousticEchoCanceler.create(active.audioSessionId)?.apply { enabled = true }
            }.getOrNull() else null
            recorder = active; recording = true
            val epoch = ++captureEpoch
            val pending = AtomicInteger(0)
            thread(name = "gtd-microphone", isDaemon = true) {
                var failure: Exception? = null
                try {
                    val framer = VoiceFramer(active.sampleRate)
                    val meter = VoiceInputMeter(active.sampleRate)
                    var lastMeterUpdate = 0L
                    val samples = ShortArray(maxOf(320, active.sampleRate / 50))
                    while (recorder === active && recording) {
                        val count = active.read(samples, 0, samples.size, AudioRecord.READ_BLOCKING)
                        check(count > 0) { "Microphone read failed ($count)" }
                        meter.accept(samples, count)
                        val measuredAt = SystemClock.elapsedRealtime()
                        if (measuredAt - lastMeterUpdate >= 150) {
                            lastMeterUpdate = measuredAt
                            val level = meter.level; val noSignal = meter.noSignal
                            ui.post {
                                if (captureEpoch == epoch && recording && SystemClock.elapsedRealtime() - measuredAt < 200) {
                                    inputLevel = level; inputHasNoSignal = noSignal; changed()
                                }
                            }
                        }
                        for (frame in framer.append(samples, count)) {
                            if (pending.get() >= 8) continue
                            pending.incrementAndGet()
                            val capturedAt = SystemClock.elapsedRealtime()
                            ui.post {
                                pending.decrementAndGet()
                                if (captureEpoch == epoch && recording && foreground && client.canSpeak &&
                                    SystemClock.elapsedRealtime() - capturedAt < 200) {
                                    client.sendGame("VoiceFrame", json("sampleRate" to VoicePcm.SAMPLE_RATE,
                                        "audio" to Base64.getEncoder().encodeToString(frame)))
                                }
                            }
                        }
                    }
                } catch (error: Exception) { failure = error }
                finally {
                    runCatching { active.stop() }; echo?.release(); active.release()
                    ui.post {
                        if (recorder === active) {
                            recorder = null; recording = false; captureEpoch++
                            inputLevel = 0; inputHasNoSignal = false
                            if (speakers.isEmpty()) releaseAudio()
                            changed(); failure?.let { client.notice("Microphone stopped: ${it.message}. Tap Mic to retry.") }
                        }
                    }
                }
            }
            changed(); true
        } catch (error: Exception) {
            stopRecording(); if (speakers.isEmpty()) releaseAudio()
            client.notice("Could not start microphone: ${error.message}"); false
        }
    }
    fun stopRecording() {
        captureEpoch++
        val active = recorder; recorder = null
        val wasRecording = recording; recording = false
        inputLevel = 0; inputHasNoSignal = false
        runCatching { active?.stop() }
        if (speakers.isEmpty()) releaseAudio()
        if (wasRecording) changed()
    }
    fun receive(message: JSONObject) {
        if (!foreground || !listening || !client.connected || message.optInt("sampleRate") != VoicePcm.SAMPLE_RATE) return
        val bytes = VoicePcm.decode(message.optString("audio")) ?: return
        val user = message.optString("username")
        if (user.isBlank() || user == client.username || (speakers[user] == null && speakers.size >= 32)) return
        try {
            configureAudio()
            val speaker = speakers[user]?.takeIf { it.active } ?: run {
                val track = AudioTrack.Builder().setAudioAttributes(attributes)
                    .setAudioFormat(AudioFormat.Builder().setSampleRate(VoicePcm.SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(maxOf(2560, AudioTrack.getMinBufferSize(VoicePcm.SAMPLE_RATE,
                        AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT))).build()
                if (track.state != AudioTrack.STATE_INITIALIZED) { track.release(); error("Speaker is unavailable") }
                val entry = Speaker(track); speakers[user] = entry
                thread(name = "gtd-speaker", isDaemon = true) {
                    try {
                        track.play()
                        while (entry.active) {
                            val frame = entry.queue.poll(1, TimeUnit.SECONDS)
                            if (frame == null) {
                                if (SystemClock.elapsedRealtime() - entry.lastReceived > 3000) ui.post {
                                    if (speakers[user] === entry && SystemClock.elapsedRealtime() - entry.lastReceived > 3000) {
                                        speakers.remove(user, entry); entry.stop()
                                        if (!recording && speakers.isEmpty()) releaseAudio()
                                    }
                                }
                                continue
                            }
                            var offset = 0
                            while (offset < frame.size && entry.active) {
                                val written = track.write(frame, offset, frame.size-offset, AudioTrack.WRITE_BLOCKING)
                                check(written > 0) { "Speaker write failed ($written)" }; offset += written
                            }
                        }
                    } catch (error: Exception) { if (entry.active) ui.post { reportPlaybackError(error) } }
                    finally {
                        entry.active = false; speakers.remove(user, entry)
                        runCatching { track.stop() }; track.release()
                        ui.post { if (!recording && speakers.isEmpty()) releaseAudio() }
                    }
                }
                entry
            }
            speaker.lastReceived = SystemClock.elapsedRealtime()
            if (!speaker.queue.offer(bytes)) { speaker.queue.clear(); speaker.queue.offer(bytes) }
        } catch (error: Exception) { reportPlaybackError(error) }
    }
    private fun reportPlaybackError(error: Exception) {
        if (SystemClock.elapsedRealtime() - playbackErrorAt > 5000) {
            playbackErrorAt = SystemClock.elapsedRealtime(); client.notice("Room audio: ${error.message}. Check output and volume.")
        }
        if (!recording && speakers.isEmpty()) releaseAudio()
    }
    fun stopPlayback() {
        speakers.values.forEach { it.stop() }; speakers.clear()
        if (!recording) releaseAudio()
    }
    fun close() { stopRecording(); stopPlayback(); releaseAudio() }
}
