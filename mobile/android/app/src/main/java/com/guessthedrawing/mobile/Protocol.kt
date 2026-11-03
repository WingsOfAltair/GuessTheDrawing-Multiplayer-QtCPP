package com.guessthedrawing.mobile

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.util.Base64
import javax.crypto.Mac
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

fun json(vararg fields: Pair<String, Any?>) = JSONObject().apply {
    fields.forEach { (k, v) -> put(k, v ?: JSONObject.NULL) }
}
fun JSONArray.objects(): List<JSONObject> = (0 until length()).mapNotNull { optJSONObject(it) }
fun JSONObject.array(key: String) = optJSONArray(key) ?: JSONArray()
fun JSONObject.obj(key: String) = optJSONObject(key) ?: JSONObject()
fun JSONObject.copy() = JSONObject(toString())

object Protocol {
    const val MAX_FRAME = 1024 * 1024
    fun message(type: String, fields: JSONObject = JSONObject(), version: String = BuildConfig.GAME_VERSION) =
        fields.copy().put("type", type).put("version", 1).put("appVersion", version)
    fun encode(message: JSONObject): ByteArray {
        val bytes = message.toString().toByteArray(Charsets.UTF_8)
        require(bytes.size <= MAX_FRAME) { "Message is too large" }
        return bytes + byteArrayOf(10)
    }
    fun passkeyProof(password: String, saltText: String, nonceText: String): String {
        val salt = Base64.getDecoder().decode(saltText)
        val nonce = Base64.getDecoder().decode(nonceText)
        require(salt.size in 16..96 && nonce.size == 32) { "Invalid room challenge" }
        val spec = PBEKeySpec(password.toCharArray(), salt, 210_000, 256)
        val verifier = try { SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).encoded }
        finally { spec.clearPassword() }
        return try {
            val mac = Mac.getInstance("HmacSHA256")
            mac.init(SecretKeySpec(verifier, "HmacSHA256"))
            Base64.getEncoder().encodeToString(mac.doFinal(nonce))
        } finally { verifier.fill(0) }
    }
}

/** TCP reads can split a UTF-8 character, or contain any number of JSON frames. */
class FrameDecoder {
    private val pending = ByteArrayOutputStream()
    fun feed(bytes: ByteArray): List<JSONObject> {
        val messages = mutableListOf<JSONObject>()
        for (byte in bytes) {
            if (byte == 10.toByte()) {
                if (pending.size() > 0) {
                    val decoder = Charsets.UTF_8.newDecoder()
                        .onMalformedInput(CodingErrorAction.REPORT)
                    val text = decoder.decode(ByteBuffer.wrap(pending.toByteArray())).toString().trim()
                    pending.reset()
                    if (text.isEmpty()) continue
                    val message = JSONObject(text)
                    require(message.optInt("version", -1) == 1 &&
                        message.optString("type").length in 1..64) { "Unsupported server protocol" }
                    messages.add(message)
                }
            } else {
                require(pending.size() < Protocol.MAX_FRAME) { "Server frame exceeds 1 MiB" }
                pending.write(byte.toInt())
            }
        }
        return messages
    }
}
