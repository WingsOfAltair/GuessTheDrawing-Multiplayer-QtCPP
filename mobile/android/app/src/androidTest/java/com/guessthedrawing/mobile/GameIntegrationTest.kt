package com.guessthedrawing.mobile

import android.view.View
import android.view.ViewGroup
import android.view.MotionEvent
import android.os.SystemClock
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
                activity.selectLanguage("en")
                player = activity.client
                val views = descendants(activity.window.decorView)
                fun fill(hint: String, text: String) {
                    views.filterIsInstance<EditText>().first { it.hint.toString() == hint }.setText(text)
                }
                fill("Master server address", "127.0.0.1"); fill("Port", port.toString())
                fill("Username", "mobile_test_admin"); fill("Password", "MobileTest123!")
                views.filterIsInstance<Button>().first { it.text == "Continue" }.performClick()
                peer = GameClient(); watcher = GameClient()
                peer.authenticate("127.0.0.1", port!!, "android_b$suffix", "MobileTest123!", "Second player", true)
                watcher.authenticate("127.0.0.1", port, "android_c$suffix", "MobileTest123!", "Watcher", true)
            }
            await("native login and room list") { player.page == "lobby" && player.rooms.length() > 0 && peer.rooms.length() > 0 && watcher.rooms.length() > 0 }
            verifySocial(player, peer, watcher)
            main {
                player.join(player.rooms.getJSONObject(0), false, "mobile-room-passkey")
                peer.join(peer.rooms.getJSONObject(0), false, "mobile-room-passkey")
            }
            await("round starts") { player.roundActive && peer.roundActive && (player.canDraw || peer.canDraw) }
            val drawing = if (player.canDraw) player else peer
            val guessing = if (drawing === player) peer else player
            await("word assigned") { drawing.word.isNotEmpty() }
            // Language changes during a live round keep the session and canvas geometry intact.
            scenario.onActivity { activity ->
                activity.selectLanguage("ar")
                assertTrue(player.connected)
                assertSame(player, activity.client)
            }
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
            verifyCurve(drawing, guessing, watcher)
            saveScreenshot("localization-ar-game.png")
            scenario.onActivity { it.communityUI.profile(peer.username) }
            saveScreenshot("localization-ar-profile.png")
            scenario.onActivity { it.communityUI.close(); it.selectLanguage("en") }
            // Multiple simulated players share this process. Keep the Activity's listener
            // from competing with the test speaker for the same device's audio focus.
            scenario.onActivity { it.voice.listening = false }
            try { verifyVoice(guessing, watcher) }
            finally { scenario.onActivity { it.voice.listening = true } }
            verifyModeration(player, watcher)
            main { player.community.action("RoomInviteSend", peer.username) }
            await("room invitation") { peer.community.invitations.isNotEmpty() }
            main { watcher.sendText("Native mobile chat", false) }
            await("room chat") { guessing.messages.any { it.contains("Native mobile chat") } }
            main { guessing.sendText(drawing.word, true) }
            await("correct guess") { guessing.messages.any { it.contains("guessed correctly") } }
            scenario.onActivity { activity ->
                activity.appearance.select("ocean")
                activity.onConfigurationChanged(android.content.res.Configuration(activity.resources.configuration))
            }
            saveScreenshot("native-game.png")
            scenario.onActivity { activity ->
                activity.communityUI.profile(peer.username)
            }
            await("touch profile loads") { player.community.profiles[peer.username]?.optString("relationship") == "friend" }
            saveScreenshot("native-profile.png")
        } finally {
            main {
                runCatching { peer.close() }
                runCatching { watcher.close() }
            }
            scenario.close()
        }
    }
    private fun saveScreenshot(name: String) {
        instrumentation.waitForIdleSync()
        Thread.sleep(500) // Let native entrance animations finish before visual QA.
        val screenshot = instrumentation.uiAutomation.takeScreenshot()
        if (android.os.Build.VERSION.SDK_INT >= 29) {
            val resolver = instrumentation.targetContext.contentResolver
            val values = android.content.ContentValues().apply {
                put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, name)
                put(android.provider.MediaStore.Images.Media.MIME_TYPE, "image/png")
                put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/GTDTest")
            }
            val uri = resolver.insert(android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)!!
            resolver.openOutputStream(uri)!!.use { screenshot.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
        }
        screenshot.recycle()
    }
    private fun verifySocial(a: GameClient, b: GameClient, c: GameClient) {
        main { b.saveProfile("Second player", "JO", "", ""); a.community.search(b.username); a.community.action("FriendRequestSend", b.username) }
        await("country flag profile saved") { b.profile.optString("countryCode") == "JO" }
        await("search profiles") { a.community.users.any { it.obj("profile").optString("username") == b.username } }
        await("incoming friend request") { b.community.requests.any { it.optString("senderUsername") == a.username && it.optString("status") == "pending" } }
        main {
            val request = b.community.requests.first { it.optString("senderUsername") == a.username }
            b.community.action("FriendRequestAccept", fields = json("friendRequestId" to request.optLong("id")))
        }
        await("friends and accepted notification") { a.community.friends.any { it.obj("profile").optString("username") == b.username } && a.community.notifications.any { it.optString("kind") == "friend_request_accepted" } }
        main {
            val notification = a.community.notifications.first { it.optString("kind") == "friend_request_accepted" }
            a.community.action("NotificationClear", fields = json("notificationId" to notification.optLong("id")))
            a.community.profile(b.username)
            a.community.action("DirectMessageSend", b.username, json("body" to "Mobile private message"))
        }
        await("notification dismissed") { a.community.notifications.none { it.optString("kind") == "friend_request_accepted" } }
        await("full player profile") { a.community.profiles[b.username]?.optString("relationship") == "friend" }
        await("private message received") { b.community.messages[a.username].orEmpty().any { it.optString("body") == "Mobile private message" } }
        main { b.community.inbox(a.username) }
        await("inbox history and unread count") { b.community.messages[a.username].orEmpty().isNotEmpty() && b.community.conversations.any { it.optString("username") == a.username && it.optInt("unreadCount") == 0 } }
        main {
            val message = b.community.messages.getValue(a.username).first()
            b.community.action("InboxMessageDelete", fields = json("messageId" to message.optLong("id")))
            c.community.action("FriendRequestSend", a.username)
        }
        await("message deleted locally") { b.community.messages[a.username].orEmpty().isEmpty() }
        await("second incoming friend request") { a.community.requests.any { it.optString("senderUsername") == c.username && it.optString("status") == "pending" } }
        main {
            val request = a.community.requests.first { it.optString("senderUsername") == c.username }
            a.community.action("FriendRequestDeny", fields = json("friendRequestId" to request.optLong("id")))
        }
        await("request denied") { c.community.requests.any { it.optString("recipientUsername") == a.username && it.optString("status") == "denied" } }
        main {
            val request = c.community.requests.first { it.optString("status") == "denied" }
            c.community.action("FriendRequestClear", fields = json("friendRequestId" to request.optLong("id")))
            c.community.action("FriendRequestNotificationClear", a.username)
            c.community.action("BlockSet", b.username, json("blocked" to true))
        }
        await("block saved and request cleared") { b.username in c.community.blocked && c.community.requests.none { it.optString("status") == "denied" } }
        main { c.community.action("BlockSet", b.username, json("blocked" to false)) }
        await("unblock saved") { b.username !in c.community.blocked }
    }
    private fun verifyCurve(drawing: GameClient, peer: GameClient, watcher: GameClient) {
        lateinit var canvas: DrawingView
        var id = ""
        main {
            canvas = DrawingView(instrumentation.targetContext, drawing).apply { layout(0, 0, 800, 600); tool = "curve" }
            // Attach to a harmless parent so parent interception follows the real touch path.
            android.widget.FrameLayout(instrumentation.targetContext).addView(canvas)
            fun event(action: Int, x: Float, y: Float) {
                val e = MotionEvent.obtain(SystemClock.uptimeMillis(), SystemClock.uptimeMillis(), action, x*800, y*600, 0)
                canvas.onTouchEvent(e); e.recycle()
            }
            val count = drawing.scene.objects.size
            event(MotionEvent.ACTION_DOWN, .1f, .3f); event(MotionEvent.ACTION_MOVE, .9f, .3f); event(MotionEvent.ACTION_UP, .9f, .3f)
            assertTrue(canvas.curvePending); assertEquals(count, drawing.scene.objects.size)
            event(MotionEvent.ACTION_DOWN, .5f, .3f); event(MotionEvent.ACTION_MOVE, .5f, .7f); event(MotionEvent.ACTION_UP, .5f, .7f)
            assertTrue(canvas.curvePending); canvas.finishCurve()
            val curve = drawing.scene.objects.last { it.optString("type") == "curve" }; id = curve.optString("id")
            assertEquals(3, curve.array("points").length()); assertEquals(.7, curve.array("points").getJSONObject(1).optDouble("y"), .001)
            event(MotionEvent.ACTION_DOWN, .2f, .2f); event(MotionEvent.ACTION_UP, .8f, .6f); canvas.cancelCurve()
            assertEquals(count + 1, drawing.scene.objects.size)
        }
        await("curve reaches player and spectator") { peer.scene.objects.any { it.optString("id") == id } && watcher.scene.objects.any { it.optString("id") == id } }
        main { canvas.undo() }
        await("curve is one undo transaction") { peer.scene.objects.none { it.optString("id") == id } }
        main { canvas.redo() }
        await("curve redo") { peer.scene.objects.any { it.optString("id") == id && it.array("points").length() == 3 } }
    }
    private fun verifyVoice(speaker: GameClient, listener: GameClient) {
        instrumentation.uiAutomation.grantRuntimePermission(instrumentation.targetContext.packageName, android.Manifest.permission.RECORD_AUDIO)
        lateinit var capture: VoiceChat
        lateinit var playback: VoiceChat
        var received = 0
        var latestFrame: org.json.JSONObject? = null
        main {
            capture = VoiceChat(instrumentation.targetContext, speaker)
            playback = VoiceChat(instrumentation.targetContext, listener)
            listener.voiceReceived = { frame ->
                if (frame.optString("username") == speaker.username && VoicePcm.decode(frame.optString("audio")) != null) { received++; latestFrame = frame }
            }
            assertTrue("AudioRecord starts", capture.start())
        }
        try {
            await("microphone frames accepted and relayed by Qt") {
                assertTrue("Capture stopped: ${speaker.messages.lastOrNull()}", capture.recording); received >= 3
            }
            var beforeRestart = 0
            main {
                capture.stopRecording(); assertFalse(capture.recording)
                latestFrame?.let { playback.receive(it) }; playback.listening = false; playback.close()
                beforeRestart = received
                assertTrue("Microphone restarts", capture.start())
            }
            await("microphone restarts without stale worker") { received >= beforeRestart + 3 }
        } finally { main { capture.close(); playback.close(); listener.voiceReceived = {} } }
    }
    private fun verifyModeration(admin: GameClient, target: GameClient) {
        main { assertTrue(admin.community.canModerate); admin.community.refreshAdmin() }
        await("staff management data") { admin.community.admin.has("bans") }
        main { admin.community.roomRequest("ModerationRequest", target.username, json("action" to "mute", "reason" to "Mobile integration test", "textChat" to true, "voiceChat" to true, "expiresAt" to System.currentTimeMillis()/1000 + 120)) }
        await("server mute appears") { admin.community.admin.array("mutes").objects().any { it.optString("targetUsername") == target.username } && !target.canSpeak }
        main {
            val mute = admin.community.admin.array("mutes").objects().first { it.optString("targetUsername") == target.username }
            admin.community.roomRequest("ModerationRequest", target.username, json("action" to "unmute", "sanctionId" to mute.optLong("id")))
        }
        await("server mute revoked") { admin.community.admin.array("mutes").objects().none { it.optString("targetUsername") == target.username } && target.canSpeak }
        main { admin.community.roomRequest("StaffRoleRequest", target.username, json("role" to "moderator")) }
        await("staff role assigned") { target.community.level == 1 && admin.community.admin.array("staff").objects().any { it.optString("username") == target.username } }
        main { admin.community.roomRequest("StaffRoleRequest", target.username, json("role" to "player")) }
        await("staff role removed") { target.community.level == 0 }
        main { admin.community.roomRequest("ModerationRequest", target.username, json("action" to "ban", "reason" to "Temporary mobile test ban", "expiresAt" to System.currentTimeMillis()/1000 + 120)) }
        await("temporary ban enforced") { !target.connected && admin.community.admin.array("bans").objects().any { it.optString("targetUsername") == target.username } }
        main {
            val ban = admin.community.admin.array("bans").objects().first { it.optString("targetUsername") == target.username }
            admin.community.roomRequest("ModerationRequest", target.username, json("action" to "unban", "sanctionId" to ban.optLong("id")))
        }
        await("ban revoked") { admin.community.admin.array("bans").objects().none { it.optString("targetUsername") == target.username } }
        main { target.join(target.rooms.getJSONObject(0), true, "mobile-room-passkey") }
        await("unbanned player can rejoin") { target.connected && target.spectator }
        main { admin.community.roomRequest("ModerationRequest", target.username, json("action" to "kick", "reason" to "Mobile test kick")) }
        await("kick enforced") { !target.connected }
        main { target.join(target.rooms.getJSONObject(0), true, "mobile-room-passkey") }
        await("kicked player can rejoin") { target.connected && target.spectator }
        // Ordinary users cannot moderate; the UI permission model mirrors server authority.
        main { assertFalse(target.community.canModerate) }
    }
}
