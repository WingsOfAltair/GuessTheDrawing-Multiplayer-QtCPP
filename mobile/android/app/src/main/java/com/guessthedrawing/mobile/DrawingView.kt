package com.guessthedrawing.mobile

import android.content.Context
import android.graphics.*
import android.os.SystemClock
import android.util.Base64
import android.view.MotionEvent
import android.view.View
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.util.UUID
import kotlin.math.*
import kotlin.concurrent.thread

class DrawingView(context: Context, val client: GameClient) : View(context) {
    var tool = "freehand"
    var ink = "#202124"
    var strokeSize = 6
    var filled = false
    var selectedLayer = ""
    var requestText: (Float, Float) -> Unit = { _, _ -> }
    private var draft: JSONObject? = null
    private var original: JSONObject? = null
    private var start = PointF()
    private var lastSent = 0L
    private var filling = false
    private val history = ArrayDeque<Pair<JSONObject, JSONObject>>()
    private val redo = ArrayDeque<Pair<JSONObject, JSONObject>>()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val images = android.util.LruCache<String, Bitmap>(12)
    init { setBackgroundColor(Color.WHITE); contentDescription = "Shared drawing canvas"; isFocusable = true }
    fun resetEditing() { draft = null; original = null; history.clear(); redo.clear(); invalidate() }
    private fun layer(): JSONObject? {
        val layers = client.scene.layers
        if (layers.none { it.optString("id") == selectedLayer })
            selectedLayer = layers.firstOrNull()?.optString("id") ?: ""
        return layers.find { it.optString("id") == selectedLayer }
    }
    private fun editable() = client.canDraw && layer()?.optBoolean("locked", false) == false &&
        layer()?.optBoolean("visible", true) == true
    private fun upsert(o: JSONObject) = json("action" to "object_upsert", "object" to o)
    private fun deletion(id: String) = json("action" to "object_delete", "objectId" to id)
    fun undo() {
        if (!client.canDraw || history.isEmpty()) return
        val entry = history.removeLast(); client.operation(entry.second); redo.addLast(entry)
    }
    fun redo() {
        if (!client.canDraw || redo.isEmpty()) return
        val entry = redo.removeLast(); client.operation(entry.first); history.addLast(entry)
    }
    private fun commit(forward: JSONObject, inverse: JSONObject) {
        client.operation(forward); history.addLast(forward.copy() to inverse.copy()); redo.clear()
        if (history.size > 50) history.removeFirst()
    }
    private fun newObject(x: Float, y: Float, kind: String = tool) = json(
        "id" to UUID.randomUUID().toString(), "layerId" to selectedLayer, "type" to kind,
        "color" to ink, "fillColor" to ink, "strokeWidth" to strokeSize, "fontSize" to 32,
        "filled" to filled, "x" to x, "y" to y, "width" to 0, "height" to 0, "points" to JSONArray())
    private fun point(x: Float, y: Float) = json("x" to x, "y" to y)
    private fun bounds(o: JSONObject, w: Float, h: Float) = RectF(
        o.optDouble("x").toFloat() * w, o.optDouble("y").toFloat() * h,
        (o.optDouble("x") + o.optDouble("width")).toFloat() * w,
        (o.optDouble("y") + o.optDouble("height")).toFloat() * h)
    private fun geometry(o: JSONObject) {
        val points = o.array("points").objects()
        if (points.isEmpty()) return
        val x = points.minOf { it.optDouble("x") }; val y = points.minOf { it.optDouble("y") }
        o.put("x", x).put("y", y).put("width", points.maxOf { it.optDouble("x") } - x)
            .put("height", points.maxOf { it.optDouble("y") } - y)
    }
    override fun onDraw(canvas: Canvas) { super.onDraw(canvas); render(canvas, width.toFloat(), height.toFloat()) }
    fun render(canvas: Canvas, w: Float, h: Float) {
        canvas.drawColor(Color.WHITE)
        val active = draft
        client.scene.visibleObjects().forEach {
            if (it.optString("id") != active?.optString("id")) drawObject(canvas, it, w, h)
        }
        if (active != null && editable()) drawObject(canvas, active, w, h)
    }
    private fun drawObject(c: Canvas, o: JSONObject, w: Float, h: Float) {
        val type = o.optString("type")
        paint.reset(); paint.isAntiAlias = true
        paint.color = runCatching { Color.parseColor(o.optString("color", "#202124")) }.getOrDefault(Color.BLACK)
        paint.strokeWidth = max(1f, o.optInt("strokeWidth", 4) * min(w, h) / 600f)
        paint.strokeJoin = Paint.Join.ROUND
        paint.strokeCap = if (type == "freehand") Paint.Cap.BUTT else Paint.Cap.ROUND
        paint.style = if (o.optBoolean("filled")) Paint.Style.FILL_AND_STROKE else Paint.Style.STROKE
        val rect = bounds(o, w, h)
        val points = o.array("points").objects().take(2000).map {
            PointF(it.optDouble("x").toFloat() * w, it.optDouble("y").toFloat() * h)
        }
        when (type) {
            "rectangle" -> c.drawRect(rect, paint)
            "rounded_rectangle" -> {
                val r = min(16f, min(rect.width(), rect.height()) / 4)
                c.drawRoundRect(rect, r, r, paint)
            }
            "ellipse" -> c.drawOval(rect, paint)
            "fill" -> {
                val data = o.optString("imageData")
                if (data.length > 700 * 1024) return
                val key = o.optString("id") + data.hashCode()
                val bitmap = images.get(key) ?: runCatching {
                    val bytes = Base64.decode(data, Base64.DEFAULT)
                    val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options)
                    if (options.outWidth !in 1..512 || options.outHeight !in 1..512) null
                    else BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.also { images.put(key, it) }
                }.getOrNull()
                if (bitmap != null) c.drawBitmap(bitmap, null, rect, Paint(Paint.FILTER_BITMAP_FLAG))
            }
            "text" -> {
                paint.style = Paint.Style.FILL; paint.textSize = max(8f, o.optInt("fontSize", 24) * w / 1024)
                c.save(); c.clipRect(rect)
                val textPaint = android.text.TextPaint(paint)
                val layout = android.text.StaticLayout.Builder.obtain(o.optString("text"), 0,
                    o.optString("text").length, textPaint, max(1, rect.width().toInt())).build()
                c.translate(rect.left, rect.top); layout.draw(c); c.restore()
            }
            "airbrush" -> {
                var state = 2166136261L
                o.optString("id").toByteArray(Charsets.UTF_8).forEach { state = ((state xor (it.toLong() and 255)) * 16777619) and 0xffffffffL }
                fun next(): Long { state = (state * 1664525 + 1013904223) and 0xffffffffL; return state }
                val radius = max(2f, paint.strokeWidth * 2)
                val color = paint.color; paint.style = Paint.Style.FILL
                points.forEach { point ->
                    repeat(o.optInt("strokeWidth", 4).coerceIn(8, 32)) {
                        val angle = next() / 4294967296.0 * PI * 2
                        val distance = sqrt(next() / 4294967296.0) * radius
                        val size = 0.5 + next() / 4294967296.0 * 1.2
                        paint.color = color; paint.alpha = 55 + (next() % 100).toInt()
                        c.drawCircle((point.x + cos(angle) * distance).toFloat(),
                            (point.y + sin(angle) * distance).toFloat(), size.toFloat(), paint)
                    }
                }
            }
            else -> {
                if (points.isEmpty()) return
                val path = Path(); path.moveTo(points[0].x, points[0].y)
                if (type == "curve" && points.size > 2) {
                    for (i in 0 until points.lastIndex) {
                        val a = points[max(0, i - 1)]; val b = points[i]
                        val e = points[i + 1]; val n = points[min(points.lastIndex, i + 2)]
                        path.cubicTo(b.x + (e.x - a.x) / 6, b.y + (e.y - a.y) / 6,
                            e.x - (n.x - b.x) / 6, e.y - (n.y - b.y) / 6, e.x, e.y)
                    }
                } else points.drop(1).forEach { path.lineTo(it.x, it.y) }
                if (type == "polygon") path.close() else paint.style = Paint.Style.STROKE
                c.drawPath(path, paint)
                if (points.size == 1) c.drawPoint(points[0].x, points[0].y, paint)
            }
        }
    }
    private fun hit(x: Float, y: Float): JSONObject? = client.scene.visibleObjects().asReversed().find {
        if (it.optString("layerId") != selectedLayer) false
        else {
            val rect = bounds(it, 1f, 1f)
            rect.inset(-0.025f, -0.025f)
            rect.contains(x, y)
        }
    }
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (!editable() || filling) { draft = null; return false }
        val x = (e.x / max(1, width)).coerceIn(0f, 1f)
        val y = (e.y / max(1, height)).coerceIn(0f, 1f)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent.requestDisallowInterceptTouchEvent(true); start = PointF(x, y); original = null
                when (tool) {
                    "text" -> requestText(x, y)
                    "pick" -> {
                        val bitmap = snapshot(); val pixel = bitmap.getPixel((x * (bitmap.width - 1)).toInt(), (y * (bitmap.height - 1)).toInt())
                        ink = String.format("#%06X", pixel and 0xffffff); bitmap.recycle()
                    }
                    "fill" -> floodFill(x, y)
                    "eraser" -> hit(x, y)?.let { commit(deletion(it.optString("id")), upsert(it)) }
                    "move" -> { original = hit(x, y)?.copy(); draft = original?.copy() }
                    else -> {
                        if (client.scene.objects.size >= 1000) { client.notice("Canvas has reached its object limit."); return true }
                        draft = newObject(x, y)
                        if (tool in listOf("freehand", "brush", "airbrush", "curve", "polygon", "line"))
                            draft?.put("points", JSONArray().put(point(x, y)))
                        lastSent = 0
                    }
                }
                invalidate(); return true
            }
            MotionEvent.ACTION_MOVE, MotionEvent.ACTION_UP -> {
                val d = draft
                if (d != null) {
                    when (tool) {
                        "move" -> original?.let { old ->
                            val dx = (x - start.x).coerceIn(-old.optDouble("x").toFloat(), (1 - old.optDouble("x") - old.optDouble("width")).toFloat())
                            val dy = (y - start.y).coerceIn(-old.optDouble("y").toFloat(), (1 - old.optDouble("y") - old.optDouble("height")).toFloat())
                            d.put("x", old.optDouble("x") + dx).put("y", old.optDouble("y") + dy)
                            d.put("points", JSONArray(old.array("points").objects().map {
                                json("x" to it.optDouble("x") + dx, "y" to it.optDouble("y") + dy)
                            }))
                        }
                        "line" -> { d.put("points", JSONArray().put(point(start.x, start.y)).put(point(x, y))); geometry(d) }
                        "freehand", "brush", "airbrush", "curve", "polygon" -> {
                            val points = d.array("points")
                            if (points.length() < 2000) points.put(point(x, y))
                            geometry(d)
                        }
                        else -> d.put("x", min(start.x, x)).put("y", min(start.y, y))
                            .put("width", abs(start.x - x)).put("height", abs(start.y - y))
                    }
                    if (e.actionMasked == MotionEvent.ACTION_UP) {
                        if (tool != "polygon" || d.array("points").length() >= 3)
                            commit(upsert(d), original?.let { upsert(it) } ?: deletion(d.optString("id")))
                        draft = null; original = null
                    } else if (tool in listOf("freehand", "brush", "airbrush", "curve") && SystemClock.uptimeMillis() - lastSent >= 60) {
                        client.operation(upsert(d.copy())); lastSent = SystemClock.uptimeMillis()
                    }
                }
                if (e.actionMasked == MotionEvent.ACTION_UP) { parent.requestDisallowInterceptTouchEvent(false); performClick() }
                invalidate(); return true
            }
            MotionEvent.ACTION_CANCEL -> {
                draft?.let { d ->
                    if (client.scene.objects.any { it.optString("id") == d.optString("id") })
                        commit(upsert(d), original?.let { upsert(it) } ?: deletion(d.optString("id")))
                }
                draft = null; original = null; parent.requestDisallowInterceptTouchEvent(false); invalidate(); return true
            }
        }
        return true
    }
    override fun performClick(): Boolean { super.performClick(); return true }
    fun addText(x: Float, y: Float, text: String) {
        if (!editable() || text.isBlank()) return
        val o = newObject(x, y, "text").put("text", text.take(256)).put("width", 1 - x).put("height", min(0.3f, 1 - y))
        commit(upsert(o), deletion(o.optString("id")))
    }
    fun snapshot(): Bitmap {
        val w = max(1, width); val h = max(1, height)
        val factor = 512f / max(w, h)
        val bitmap = Bitmap.createBitmap(max(1, (w * factor).toInt()), max(1, (h * factor).toInt()), Bitmap.Config.ARGB_8888)
        render(Canvas(bitmap), bitmap.width.toFloat(), bitmap.height.toFloat())
        return bitmap
    }
    private fun floodFill(x: Float, y: Float) {
        val bitmap = snapshot(); val w = bitmap.width; val h = bitmap.height
        val o = newObject(0f, 0f, "fill").put("width", 1).put("height", 1)
        val revision = client.scene.revision
        val fillColor = Color.parseColor(ink)
        filling = true
        thread(name = "gtd-fill", isDaemon = true) {
        val result = runCatching {
        val pixels = IntArray(w * h); bitmap.getPixels(pixels, 0, w, 0, 0, w, h); bitmap.recycle()
        val startIndex = (y * (h - 1)).toInt() * w + (x * (w - 1)).toInt()
        val target = pixels[startIndex]; val color = fillColor
        if (target == color) return@runCatching null
        val output = IntArray(w * h); val visited = BooleanArray(w * h)
        val queue = IntArray(w * h); var head = 0; var tail = 1
        queue[0] = startIndex; visited[startIndex] = true
        while (head < tail) {
            val index = queue[head++]; if (pixels[index] != target) continue
            output[index] = color
            fun enqueue(next: Int) { if (!visited[next]) { visited[next] = true; queue[tail++] = next } }
            if (index % w > 0) enqueue(index - 1)
            if (index % w < w - 1) enqueue(index + 1)
            if (index >= w) enqueue(index - w)
            if (index < w * (h - 1)) enqueue(index + w)
        }
        val fill = Bitmap.createBitmap(output, w, h, Bitmap.Config.ARGB_8888)
        val stream = ByteArrayOutputStream(); fill.compress(Bitmap.CompressFormat.PNG, 100, stream); fill.recycle()
        Base64.encodeToString(stream.toByteArray(), Base64.NO_WRAP)
        }
        post {
            filling = false
            val data = result.getOrNull()
            if (isAttachedToWindow && editable() && revision == client.scene.revision && data != null) {
                o.put("imageData", data); commit(upsert(o), deletion(o.optString("id")))
            }
        }
        }
    }
}
