package com.guessthedrawing.mobile

import kotlin.math.*

/** Matches the desktop's circular stroke cutter; input and output are canvas pixel coordinates. */
object StrokeEraser {
    data class Point(val x: Double, val y: Double)
    data class Cut(val fragments: List<List<Point>>, val changed: Boolean)
    fun sweep(from: Point, to: Point, radius: Double): List<Point> {
        val steps = max(1, ceil(hypot(to.x - from.x, to.y - from.y) / max(1.0, radius * 0.5)).toInt())
        return (0..steps).map { i -> val t = i.toDouble() / steps; Point(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t) }
    }
    fun cut(path: List<Point>, center: Point, radius: Double): Cut {
        if (path.isEmpty()) return Cut(emptyList(), false)
        if (path.size == 1) return if (hypot(path[0].x - center.x, path[0].y - center.y) <= radius)
            Cut(emptyList(), true) else Cut(listOf(path), false)
        val fragments = mutableListOf<List<Point>>()
        var current = mutableListOf<Point>()
        var changed = false
        fun append(p: Point) { if (current.isEmpty() || hypot(current.last().x - p.x, current.last().y - p.y) > 0.001) current.add(p) }
        fun finish() { if (current.isNotEmpty()) fragments.add(current); current = mutableListOf() }
        for (i in 0 until path.lastIndex) {
            val start = path[i]; val end = path[i + 1]
            val dx = end.x - start.x; val dy = end.y - start.y
            val ox = start.x - center.x; val oy = start.y - center.y
            val a = dx * dx + dy * dy; val b = 2 * (ox * dx + oy * dy); val c = ox * ox + oy * oy - radius * radius
            var intersects: Boolean
            var enter = 0.0; var exit = 1.0
            if (a < 0.000001) intersects = c <= 0 else {
                val discriminant = b * b - 4 * a * c
                if (discriminant >= 0) {
                    val root = sqrt(discriminant)
                    val first = (-b - root) / (2 * a); val second = (-b + root) / (2 * a)
                    intersects = second >= 0 && first <= 1
                    if (intersects) { enter = first.coerceIn(0.0, 1.0); exit = second.coerceIn(0.0, 1.0) }
                } else intersects = c <= 0
            }
            if (!intersects) { append(start); append(end); continue }
            changed = true
            if (enter > 0) { append(start); append(Point(start.x + dx * enter, start.y + dy * enter)) }
            finish()
            if (exit < 1) { append(Point(start.x + dx * exit, start.y + dy * exit)); append(end) } else finish()
        }
        finish()
        return Cut(fragments, changed)
    }
}
