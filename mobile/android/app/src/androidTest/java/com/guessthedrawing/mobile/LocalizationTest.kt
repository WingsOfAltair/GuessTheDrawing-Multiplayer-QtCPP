package com.guessthedrawing.mobile

import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.TextView
import android.graphics.Bitmap
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Exercises compiled resources, native view direction, language persistence and literal user data. */
@RunWith(AndroidJUnit4::class)
class LocalizationTest {
    private fun descendants(v: View): List<View> = listOf(v) +
        if (v is ViewGroup) (0 until v.childCount).flatMap { descendants(v.getChildAt(it)) } else emptyList()

    @Test fun allLanguagesAndArabicLayout() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        var original = "en"
        lateinit var client: GameClient
        try {
            scenario.onActivity { activity ->
                original = L10n.language
                client = activity.client
                assertEquals(12, L10n.languages.size)
            }
            for ((code, _) in L10n.languages) {
                scenario.onActivity { activity ->
                    activity.selectLanguage(code)
                    assertSame("Changing language must keep the game client", client, activity.client)
                    assertEquals(code, activity.getSharedPreferences("appearance", 0).getString("language", null))
                    val expected = if (code == "ar") View.LAYOUT_DIRECTION_RTL else View.LAYOUT_DIRECTION_LTR
                    assertEquals(expected, activity.window.decorView.layoutDirection)
                    assertEquals(expected, activity.resources.configuration.layoutDirection)
                    val texts = descendants(activity.window.decorView).filterIsInstance<TextView>()
                    assertTrue("Translated login button for $code", texts.any { it.text.toString() == L10n.exact("Continue") })
                    if (code != "en") assertNotEquals("Translated profile for $code", "Player profile", L10n.exact("Player profile"))
                    val host = texts.filterIsInstance<EditText>().first { it.hint.toString() == L10n.exact("Master server address") }
                    assertEquals(View.TEXT_DIRECTION_LTR, host.textDirection)
                    assertEquals("ar uses RTL; canvas never mirrors", View.LAYOUT_DIRECTION_LTR, DrawingView(activity, client).layoutDirection)
                    assertEquals("Cancel", activity.label("Cancel", translate = false).text.toString())
                    assertEquals("192.168.1.29", L10n.text("192.168.1.29"))
                    val formatted = L10n.format("@%1 invited you to %2", "player_42", "Room %1")
                    assertTrue(formatted.contains("player_42")); assertTrue(formatted.contains("Room %1"))
                    if (code == "ar") {
                        assertTrue(formatted.contains("\u2068@player_42\u2069"))
                        assertEquals("\u2068#12\u2069 / \u2068@name\u2069", L10n.format("#%1 / @%2", 12, "name"))
                        assertEquals("45450", L10n.digits("٤٥٤٥٠"))
                        assertEquals("123", L10n.digits("१२३"))
                    }
                }
                val instrumentation = InstrumentationRegistry.getInstrumentation()
                instrumentation.waitForIdleSync()
                if (code in listOf("en", "ar", "de", "zh", "hi")) {
                    Thread.sleep(300) // Allow the layout and native window animations to settle.
                    val bitmap = instrumentation.uiAutomation.takeScreenshot()
                    if (android.os.Build.VERSION.SDK_INT >= 29) {
                        val resolver = instrumentation.targetContext.contentResolver
                        val values = android.content.ContentValues().apply {
                            put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, "language-$code.png")
                            put(android.provider.MediaStore.Images.Media.MIME_TYPE, "image/png")
                            put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/GTDLocalization")
                        }
                        val uri = resolver.insert(android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)!!
                        resolver.openOutputStream(uri)!!.use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
                    }
                    bitmap.recycle()
                }
            }
            scenario.recreate()
            scenario.onActivity { assertEquals("hi", L10n.language) }
        } finally {
            scenario.onActivity { it.selectLanguage(original) }
            scenario.close()
        }
    }
}
