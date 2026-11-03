package com.guessthedrawing.mobile

import android.annotation.SuppressLint
import android.media.*
import android.util.Base64
import org.json.JSONObject
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ConcurrentHashMap
import kotlin.concurrent.thread

class VoiceChat(private val client: GameClient) {
    @Volatile var recording = false
        private set
    var listening = true
    private var recorder: AudioRecord? = null
    private data class Speaker(val track: AudioTrack, val queue: ArrayBlockingQueue<ByteArray>, @Volatile var active: Boolean = true)
    private val speakers = ConcurrentHashMap<String, Speaker>()
    @SuppressLint("MissingPermission") // Activity requests RECORD_AUDIO before entering this method.
    fun start(): Boolean {
        stopRecording()
        if (!client.canChat) return false
        return runCatching {
            val bufferSize = maxOf(6400, AudioRecord.getMinBufferSize(16000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT))
            val input = AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION, 16000,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, bufferSize)
            if (input.state != AudioRecord.STATE_INITIALIZED) { input.release(); return false }
            recorder = input; input.startRecording(); recording = true
            thread(name = "gtd-microphone", isDaemon = true) {
                try {
                    val frame = ByteArray(640)
                    var offset = 0
                    while (recording && recorder === input) {
                        val n = input.read(frame, offset, frame.size - offset)
                        if (n < 0) break
                        offset += n
                        if (offset == frame.size) {
                            client.sendGame("VoiceFrame", json("sampleRate" to 16000,
                                "audio" to Base64.encodeToString(frame, Base64.NO_WRAP)))
                            offset = 0
                        }
                    }
                } catch (_: Exception) {
                    // Audio routes and permissions can disappear while a read is in progress.
                } finally {
                    runCatching { input.stop() }; input.release()
                    if (recorder === input) { recorder = null; recording = false }
                }
            }
            true
        }.getOrElse { stopRecording(); false }
    }
    fun stopRecording() {
        recording = false
        val input = recorder; recorder = null
        runCatching { input?.stop() }
    }
    fun receive(m: JSONObject) {
        if (!listening || m.optInt("sampleRate") != 16000) return
        val text = m.optString("audio"); if (text.length > 16384) return
        val bytes = runCatching { Base64.decode(text, Base64.DEFAULT) }.getOrNull() ?: return
        if (bytes.isEmpty() || bytes.size > 8192 || bytes.size % 2 != 0) return
        val user = m.optString("username")
        if (user.isEmpty() || speakers.size >= 32 && !speakers.containsKey(user)) return
        val speaker = speakers[user] ?: runCatching {
            val track = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(16000).setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(maxOf(6400, AudioTrack.getMinBufferSize(16000, AudioFormat.CHANNEL_OUT_MONO,
                    AudioFormat.ENCODING_PCM_16BIT))).build()
            val entry = Speaker(track, ArrayBlockingQueue(16)); speakers[user] = entry
            thread(name = "gtd-speaker", isDaemon = true) {
                try {
                    track.play()
                    while (entry.active) {
                        val frame = entry.queue.poll(1, java.util.concurrent.TimeUnit.SECONDS) ?: continue
                        track.write(frame, 0, frame.size)
                    }
                } catch (_: Exception) {
                    // A route change must not terminate the game.
                } finally {
                    entry.active = false
                    speakers.remove(user, entry)
                    runCatching { track.stop() }; track.release()
                }
            }
            entry
        }.getOrNull() ?: return
        if (!speaker.queue.offer(bytes)) { speaker.queue.clear(); speaker.queue.offer(bytes) }
    }
    fun stopPlayback() { speakers.values.forEach { it.active = false; it.queue.clear() }; speakers.clear() }
    fun close() { stopRecording(); stopPlayback() }
}
