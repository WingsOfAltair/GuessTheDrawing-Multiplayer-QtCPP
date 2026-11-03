package com.guessthedrawing.mobile

import kotlin.math.*

/** Normalized Catmull-Rom points, identical to drawing_canvas.cpp. */
data class CurvePoint(val x: Float, val y: Float)
object CurveGeometry {
    data class Hit(val index: Int, val point: CurvePoint)
    fun sample(points: List<CurvePoint>, i: Int, t: Float): CurvePoint {
        val a = points[max(0, i - 1)]; val b = points[i]
        val e = points[i + 1]; val n = points[min(points.lastIndex, i + 2)]
        val u = 1 - t
        fun value(a: Float, b: Float, e: Float, n: Float) =
            u*u*u*b + 3*u*u*t*(b+(e-a)/6) + 3*u*t*t*(e-(n-b)/6) + t*t*t*e
        return CurvePoint(value(a.x, b.x, e.x, n.x), value(a.y, b.y, e.y, n.y))
    }
    fun insertion(points: List<CurvePoint>, touch: CurvePoint, width: Float, height: Float, radius: Float): Hit? {
        if (points.size !in 2..3) return null
        var best = radius * radius; var result: Hit? = null
        for (i in 0 until points.lastIndex) {
            var a = points[i]
            for (step in 1..32) {
                val b = sample(points, i, step / 32f)
                val dx = (b.x-a.x)*width; val dy = (b.y-a.y)*height
                val length = dx*dx+dy*dy
                val t = if (length == 0f) 0f else (((touch.x-a.x)*width*dx+(touch.y-a.y)*height*dy)/length).coerceIn(0f, 1f)
                val p = CurvePoint(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t)
                val distance = ((touch.x-p.x)*width).pow(2)+((touch.y-p.y)*height).pow(2)
                if (distance < best) { best = distance; result = Hit(i+1, p) }
                a = b
            }
        }
        return result
    }
    fun valid(points: List<CurvePoint>) = points.size in 2..4 &&
        hypot(points.last().x-points.first().x, points.last().y-points.first().y) > 0.001f
}
