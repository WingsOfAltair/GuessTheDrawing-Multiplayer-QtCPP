package com.guessthedrawing.mobile

import android.os.SystemClock
import android.view.*
import android.view.inspector.WindowInspector
import android.widget.*
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
@SdkSuppress(minSdkVersion = 30)
class CommunityUsabilityTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun main(action: () -> Unit) = instrumentation.runOnMainSync(action)
    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    private fun views() = WindowInspector.getGlobalWindowViews().flatMap(::descendants)
    private fun await(description: String, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 10000
        while (SystemClock.uptimeMillis() < deadline) {
            var done = false; main { done = condition() }; if (done) return
            Thread.sleep(50)
        }
        fail(description)
    }
    private fun tap(view: View) {
        val position = IntArray(2); var x = 0f; var y = 0f
        main { view.getLocationOnScreen(position); x = position[0] + view.width / 2f; y = position[1] + view.height / 2f }
        val time = SystemClock.uptimeMillis()
        listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP).forEach { action ->
            val event = MotionEvent.obtain(time, SystemClock.uptimeMillis(), action, x, y, 0)
            instrumentation.sendPointerSync(event); event.recycle()
        }
    }
    @Test fun categoryCountsAndKeyboardInDynamicallyCreatedSheets() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        try {
            lateinit var social: CommunityClient
            scenario.onActivity { activity ->
                activity.selectLanguage("en"); social = activity.client.community
                social.handle(json("type" to "SocialListResult",
                    "friends" to JSONArray().put(json("profile" to json("username" to "friend", "displayName" to "Friend"))),
                    "requests" to JSONArray().put(json("senderUsername" to "pending", "status" to "pending")),
                    "notifications" to JSONArray().put(json("kind" to "friend_request_accepted")).put(json("kind" to "notice")),
                    "blocked" to JSONArray().put("blocked")))
                social.handle(json("type" to "UserSearchResult", "users" to JSONArray().put(json("profile" to json("username" to "found")))))
                activity.communityUI.hub("Find players")
            }
            await("Community menu appears") { views().filterIsInstance<Spinner>().any { it.adapter.count == 6 } }
            lateinit var chooser: Spinner
            main { chooser = views().filterIsInstance<Spinner>().last { it.adapter.count == 6 }; chooser.performClick() }
            await("All category counts appear before choosing a category") {
                val labels = views().filterIsInstance<TextView>().map { it.text.toString() }
                listOf("Friends (1)", "Requests (2)", "Inbox (0)", "Notifications (2)", "Find players (1)", "Blocked (1)").all { it in labels }
            }
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
            lateinit var search: EditText
            main { search = views().filterIsInstance<EditText>().first { it.hint == "Username or display name" } }
            tap(search)
            await("Tapping Find players opens the keyboard") { search.hasFocus() && search.rootWindowInsets?.isVisible(WindowInsets.Type.ime()) == true }
            main {
                search.setText("kept while results refresh")
                social.handle(json("type" to "UserSearchResult", "users" to JSONArray()))
                assertEquals("kept while results refresh", search.text.toString())
                assertEquals("Find players (0)", (chooser.adapter.getView(4, null, chooser) as TextView).text.toString())
            }
            scenario.onActivity { it.communityUI.close(); it.communityUI.inbox("friend") }
            lateinit var compose: EditText
            await("Private message composer appears") { views().filterIsInstance<EditText>().any { it.hint == "Private message" } }
            main { compose = views().filterIsInstance<EditText>().first { it.hint == "Private message" } }
            tap(compose)
            await("Tapping Private message opens the keyboard") { compose.hasFocus() && compose.rootWindowInsets?.isVisible(WindowInsets.Type.ime()) == true }
            main {
                val visible = android.graphics.Rect()
                assertTrue("Composer remains visible above keyboard", compose.getGlobalVisibleRect(visible) && visible.height() >= compose.height)
            }
        } finally { scenario.onActivity { it.communityUI.close() }; scenario.close() }
    }
}
