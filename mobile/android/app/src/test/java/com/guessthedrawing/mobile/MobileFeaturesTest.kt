package com.guessthedrawing.mobile

import java.util.Base64
import org.json.JSONArray
import org.junit.Assert.*
import org.junit.Test

class MobileFeaturesTest {
    @Test fun curveHitUsesScreenDistanceAndPreservesPointOrder() {
        val points = listOf(CurvePoint(.1f,.4f), CurvePoint(.9f,.4f))
        val hit = CurveGeometry.insertion(points, CurvePoint(.5f,.41f), 400f, 900f, 22f)!!
        assertEquals(1, hit.index); assertEquals(.5f, hit.point.x, .001f); assertEquals(.4f, hit.point.y, .001f)
        assertNull(CurveGeometry.insertion(points, CurvePoint(.5f,.5f), 400f, 900f, 22f))
        val bent = listOf(points[0], CurvePoint(.45f,.75f), points[1])
        assertEquals(bent[1], CurveGeometry.sample(bent, 0, 1f))
        assertEquals(bent[1], CurveGeometry.sample(bent, 1, 0f))
        assertTrue(CurveGeometry.valid(bent))
        assertFalse(CurveGeometry.valid(listOf(points[0], points[0])))
        assertNull(CurveGeometry.insertion(bent + points[1], CurvePoint(.5f,.4f), 400f, 900f, 22f))
    }
    @Test fun voiceFramesAreCanonicalLittleEndianAndExactlyTwentyMilliseconds() {
        val samples = ShortArray(960) { if (it % 2 == 0) -32768 else 32767 }
        val frames = VoiceFramer(16000).append(samples)
        assertEquals(3, frames.size)
        assertArrayEquals(byteArrayOf(0, -128, -1, 127), frames[0].take(4).toByteArray())
        for (frame in frames) { assertEquals(640, frame.size); assertArrayEquals(frame, VoicePcm.decode(Base64.getEncoder().encodeToString(frame))) }
        assertNull(VoicePcm.decode(Base64.getEncoder().encodeToString(ByteArray(638))))
        assertNull(VoicePcm.decode("!".repeat(856)))
    }
    @Test fun hardwareBufferBoundariesDoNotChangeResampling() {
        for (rate in listOf(16000, 44100, 48000)) {
            val samples = ShortArray(rate) { (12000 * kotlin.math.sin(it * 2 * Math.PI * 440 / rate)).toInt().toShort() }
            val whole = VoiceFramer(rate).append(samples).flatMap { it.toList() }.toByteArray()
            val framer = VoiceFramer(rate)
            val split = samples.toList().chunked(137).flatMap { framer.append(it.toShortArray()) }.flatMap { it.toList() }.toByteArray()
            assertEquals(50 * 640, whole.size); assertArrayEquals(whole, split)
        }
    }
    @Test fun allDesktopPalettesMatchFixture() {
        val fixture = JSONArray(javaClass.getResource("/themes.json")!!.readText())
        assertEquals(13, ThemePalette.all.size)
        ThemePalette.all.forEachIndexed { i, palette ->
            val p = fixture.getJSONObject(i)
            assertEquals(p.getString("id"), palette.id)
            val colors = listOf(palette.window, palette.surface, palette.base, palette.text, palette.accent, palette.accentText, palette.button, palette.disabledText)
            colors.forEachIndexed { j, color -> assertEquals(p.getJSONArray("colors").getString(j), "#%06x".format(color and 0xffffff)) }
        }
    }
    @Test fun staffHierarchyAndLongDurationsUseSafeIntegerRange() {
        assertEquals(0, CommunityPolicy.level(json("serverRole" to "player")))
        assertEquals(1, CommunityPolicy.level(json("serverRole" to "moderator")))
        assertEquals(2, CommunityPolicy.level(json("serverRole" to "admin")))
        assertEquals(3, CommunityPolicy.level(json("serverRole" to "player", "masterSuperAdmin" to true)))
        assertEquals(3153600000000L, CommunityPolicy.durationUnits.getValue("Years (365 days)") * 100000)
    }
}
