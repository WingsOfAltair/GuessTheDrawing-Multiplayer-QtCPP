package com.guessthedrawing.mobile

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/** One reader and one bounded writer; callbacks always arrive on the UI thread. */
class Connection(
    private val onReady: () -> Unit,
    private val onMessage: (JSONObject) -> Unit,
    private val onClosed: (String) -> Unit
) {
    private val ui = Handler(Looper.getMainLooper())
    private val active = AtomicBoolean(false)
    private data class Outgoing(val bytes: ByteArray, val closeAfter: Boolean = false)
    private val outgoing = ArrayBlockingQueue<Outgoing>(128)
    @Volatile private var cancelled = false
    @Volatile private var socket: Socket? = null
    @Volatile var connected = false
        private set
    fun connect(host: String, port: Int) {
        check(active.compareAndSet(false, true))
        thread(name = "gtd-reader", isDaemon = true) {
            try {
                val current = Socket()
                socket = current
                if (!active.get()) { current.close(); return@thread }
                current.connect(InetSocketAddress(host, port), 8_000)
                current.tcpNoDelay = true
                current.keepAlive = true
                connected = true
                ui.post { if (active.get()) onReady() }
                thread(name = "gtd-writer", isDaemon = true) {
                    try {
                        val stream = current.getOutputStream()
                        while (active.get()) {
                            val frame = outgoing.poll(1, java.util.concurrent.TimeUnit.SECONDS) ?: continue
                            stream.write(frame.bytes)
                            if (frame.closeAfter) { close(); break }
                        }
                    } catch (error: Exception) { fail(error.message ?: "Send failed") }
                }
                val decoder = FrameDecoder()
                val buffer = ByteArray(8192)
                val input = current.getInputStream()
                while (active.get()) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    val frames = decoder.feed(buffer.copyOf(count))
                    ui.post { if (!cancelled) frames.forEach(onMessage) }
                }
                fail("Connection closed")
            } catch (error: Exception) { fail(error.message ?: "Connection failed") }
        }
    }
    fun send(message: JSONObject): Boolean {
        if (!active.get() || !connected) return false
        if (!outgoing.offer(Outgoing(Protocol.encode(message)))) {
            fail("Connection is too slow; reconnecting")
            return false
        }
        return true
    }
    private fun fail(reason: String) {
        if (!active.getAndSet(false)) return
        connected = false
        runCatching { socket?.close() }
        outgoing.clear()
        ui.post { if (!cancelled) onClosed(reason) }
    }
    fun finish(message: JSONObject) {
        if (!connected || !outgoing.offer(Outgoing(Protocol.encode(message), true))) close()
        else ui.postDelayed({ close() }, 2000)
    }
    fun close() {
        cancelled = true
        active.set(false)
        connected = false
        runCatching { socket?.close() }
        outgoing.clear()
    }
}
