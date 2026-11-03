package com.guessthedrawing.mobile

import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Runs against disposable real Qt services started by tools/server_smoke.py. */
@RunWith(AndroidJUnit4::class)
class GameIntegrationTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun main(action: () -> Unit) = instrumentation.runOnMainSync(action)
    private fun await(description: String, condition: () -> Boolean) {
        val deadline = System.currentTimeMillis() + 20000
        while (System.currentTimeMillis() < deadline) {
            var done = false
            main { done = condition() }
            if (done) return
            Thread.sleep(50)
        }
        fail("Timed out: $description")
    }
    private fun descendants(v: View): List<View> = listOf(v) +
        if (v is ViewGroup) (0 until v.childCount).flatMap { descendants(v.getChildAt(it)) } else emptyList()
    @Test fun nativeLoginAndCrossClientRound() {
        val port = InstrumentationRegistry.getArguments().getString("masterPort")?.toIntOrNull()
        assumeTrue("Start with mobile/tools/server_smoke.py --android", port != null)
        val suffix = System.nanoTime().toString().takeLast(8)
        lateinit var player: GameClient
        lateinit var peer: GameClient
        lateinit var watcher: GameClient
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        try {
            scenario.onActivity { activity ->
                player = activity.client
                val views = descendants(activity.window.decorView)
                fun fill(hint: String, text: String) {
                    views.filterIsInstance<EditText>().first { it.hint.toString() == hint }.setText(text)
                }
                fill("Master server address", "127.0.0.1"); fill("Port", port.toString())
                fill("Username", "android_a$suffix"); fill("Password", "MobileTest123!")
                views.filterIsInstance<CheckBox>().first { it.text == "Create an account" }.isChecked = true
                fill("Display name", "Android player")
                views.filterIsInstance<Button>().first { it.text == "Continue" }.performClick()
                peer = GameClient(); watcher = GameClient()
                peer.authenticate("127.0.0.1", port!!, "android_b$suffix", "MobileTest123!", "Second player", true)
                watcher.authenticate("127.0.0.1", port, "android_c$suffix", "MobileTest123!", "Watcher", true)
            }
            await("native login and room list") { player.page == "lobby" && player.rooms.length() > 0 && peer.rooms.length() > 0 && watcher.rooms.length() > 0 }
            main {
                player.join(player.rooms.getJSONObject(0), false, "mobile-room-passkey")
                peer.join(peer.rooms.getJSONObject(0), false, "mobile-room-passkey")
            }
            await("round starts") { player.roundActive && peer.roundActive && (player.canDraw || peer.canDraw) }
            val drawing = if (player.canDraw) player else peer
            val guessing = if (drawing === player) peer else player
            await("word assigned") { drawing.word.isNotEmpty() }
            main {
                assertFalse(drawing.canChat); assertTrue(guessing.canGuess)
                drawing.operation(json("action" to "object_upsert", "object" to json(
                    "id" to "android-test-stroke", "layerId" to drawing.scene.layers.first().optString("id"),
                    "type" to "freehand", "color" to "#5146D8", "fillColor" to "#ffffff",
                    "x" to 0.1, "y" to 0.2, "width" to 0.6, "height" to 0.5,
                    "strokeWidth" to 6, "fontSize" to 24, "filled" to false,
                    "points" to JSONArray().put(json("x" to 0.1, "y" to 0.2)).put(json("x" to 0.7, "y" to 0.7)))))
            }
            await("canvas relayed") { guessing.scene.objects.any { it.optString("id") == "android-test-stroke" } }
            main { watcher.join(watcher.rooms.getJSONObject(0), true, "mobile-room-passkey") }
            await("late spectator snapshot") { watcher.spectator && watcher.connected && watcher.scene.objects.any { it.optString("id") == "android-test-stroke" } }
            main { watcher.sendText("Native mobile chat", false) }
            await("room chat") { guessing.messages.any { it.contains("Native mobile chat") } }
            main { guessing.sendText(drawing.word, true) }
            await("correct guess") { guessing.messages.any { it.contains("guessed correctly") } }
            val screenshot = instrumentation.uiAutomation.takeScreenshot()
            if (android.os.Build.VERSION.SDK_INT >= 29) {
                val resolver = instrumentation.targetContext.contentResolver
                val values = android.content.ContentValues().apply {
                    put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, "native-game.png")
                    put(android.provider.MediaStore.Images.Media.MIME_TYPE, "image/png")
                    put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/GTDTest")
                }
                val uri = resolver.insert(android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)!!
                resolver.openOutputStream(uri)!!.use { screenshot.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
            }
            screenshot.recycle()
        } finally {
            main {
                runCatching { peer.close() }
                runCatching { watcher.close() }
            }
            scenario.close()
        }
    }
}
