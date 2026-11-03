package com.guessthedrawing.mobile

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class ProtocolTest {
    private fun fixture() = JSONObject(javaClass.getResource("/protocol.json")!!.readText())
    @Test fun fragmentedUtf8AndCoalescedFrames() {
        val message = Protocol.message("ChatMessage", json("text" to "مرحبا 🎨"))
        val bytes = Protocol.encode(message)
        val decoder = FrameDecoder()
        val frames = bytes.flatMap { decoder.feed(byteArrayOf(it)) }
        assertEquals(1, frames.size)
        assertEquals("مرحبا 🎨", frames[0].getString("text"))
        assertEquals(2, FrameDecoder().feed(bytes + bytes).size)
    }
    @Test fun exactMaximumFrameIsAccepted() {
        val prefix = """{"version":1,"type":"Padding","text":""""
        val suffix = "\"}"
        val bytes = (prefix + "x".repeat(Protocol.MAX_FRAME - prefix.length - suffix.length) + suffix + "\n").toByteArray()
        assertEquals(1, FrameDecoder().feed(bytes).size)
    }
    @Test(expected = IllegalArgumentException::class)
    fun oversizedUnterminatedFrameIsRejected() { FrameDecoder().feed(ByteArray(Protocol.MAX_FRAME + 1) { 32 }) }
    @Test(expected = IllegalArgumentException::class)
    fun unsupportedVersionIsRejected() { FrameDecoder().feed("{\"version\":2,\"type\":\"Hello\"}\n".toByteArray()) }
    @Test fun passkeyMatchesQtCompatibleUtf8Fixture() {
        val f = fixture().getJSONObject("passkey")
        assertEquals(f.getString("proof"), Protocol.passkeyProof(f.getString("password"), f.getString("salt"), f.getString("nonce")))
    }
    @Test fun canvasOperationsPreserveLayerVisibilityAndOrdering() {
        val f = fixture(); val scene = CanvasScene()
        scene.replace(f.getJSONArray("layers"), f.getJSONArray("objects"))
        assertEquals(listOf("stroke-a", "rect-b"), scene.visibleObjects().map { it.getString("id") })
        scene.apply(json("action" to "layer_reorder", "layerIds" to JSONArray(listOf("top", "base"))))
        assertEquals(listOf("rect-b", "stroke-a"), scene.visibleObjects().map { it.getString("id") })
        scene.apply(json("action" to "layer_upsert", "layer" to scene.layers[0].copy().put("visible", false)))
        assertEquals(1, scene.visibleObjects().size)
        scene.apply(json("action" to "layer_delete", "layerId" to "base"))
        assertEquals(1, scene.objects.size)
        scene.apply(json("action" to "clear"))
        assertTrue(scene.objects.isEmpty()); assertEquals(1, scene.layers.size)
    }
}
