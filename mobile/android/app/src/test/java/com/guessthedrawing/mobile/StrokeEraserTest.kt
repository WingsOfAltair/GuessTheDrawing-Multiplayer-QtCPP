package com.guessthedrawing.mobile

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.hypot

class StrokeEraserTest {
    private fun p(x: Double, y: Double = 0.0) = StrokeEraser.Point(x, y)
    @Test fun cutsTheMiddleOfASegmentWithoutRemovingTheEnds() {
        val cut = StrokeEraser.cut(listOf(p(0.0), p(100.0)), p(50.0), 10.0)
        assertTrue(cut.changed); assertEquals(2, cut.fragments.size)
        assertEquals(listOf(p(0.0), p(40.0)), cut.fragments[0])
        assertEquals(listOf(p(60.0), p(100.0)), cut.fragments[1])
    }
    @Test fun handlesMissesDotsAndRepeatedPoints() {
        val path = listOf(p(0.0), p(100.0))
        val miss = StrokeEraser.cut(path, p(50.0, 30.0), 10.0)
        assertFalse(miss.changed); assertEquals(listOf(path), miss.fragments)
        assertTrue(StrokeEraser.cut(listOf(p(5.0)), p(5.0), 10.0).fragments.isEmpty())
        assertTrue(StrokeEraser.cut(listOf(p(5.0), p(5.0)), p(5.0), 10.0).fragments.isEmpty())
        assertTrue(StrokeEraser.cut(path, p(50.0), 100.0).fragments.isEmpty())
    }
    @Test fun aFastDragHasNoGaps() {
        val sweep = StrokeEraser.sweep(p(0.0, 10.0), p(200.0, 70.0), 12.0)
        assertEquals(p(0.0, 10.0), sweep.first()); assertEquals(p(200.0, 70.0), sweep.last())
        sweep.zipWithNext().forEach { (a, b) -> assertTrue(hypot(a.x-b.x, a.y-b.y) <= 6.0) }
        var fragments = listOf(listOf(p(100.0, -100.0), p(100.0, 200.0)))
        sweep.forEach { center -> fragments = fragments.flatMap { StrokeEraser.cut(it, center, 12.0).fragments } }
        assertEquals(2, fragments.size)
        assertTrue(fragments[0].last().y < 40); assertTrue(fragments[1].first().y > 40)
    }
}
