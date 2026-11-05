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
        set(value) { if (field != value) { finishCurve(); finishErasing() }; field = value; editingChanged() }
    var ink = "#202124"
    var strokeSize = 6
    var filled = false
    var selectedLayer = ""
        set(value) { if (field != value) { finishCurve(); finishErasing() }; field = value }
    var editingChanged: () -> Unit = {}
    val curvePending get() = draft?.optString("type") == "curve"
    private var curveHandle = -1
    private var choosingLayer = false
    var requestText: (Float, Float) -> Unit = { _, _ -> }
    private var draft: JSONObject? = null
    private var original: JSONObject? = null
    private var start = PointF()
    private var lastSent = 0L
    private var filling = false
    private data class Edit(val forward: List<JSONObject>, val inverse: List<JSONObject>)
    private val history = ArrayDeque<Edit>()
    private val redo = ArrayDeque<Edit>()
    // null means that a fragment was created during this gesture.
    private val eraseBefore = linkedMapOf<String, JSONObject?>()
    private var eraseLast: PointF? = null
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val images = android.util.LruCache<String, Bitmap>(12)
    init {
        layoutDirection = View.LAYOUT_DIRECTION_LTR
        contentDescription = L10n.text("Shared drawing canvas")
        setBackgroundColor(Color.WHITE); isFocusable = true
    }
    fun resetEditing() { cancelCurve(); draft = null; original = null; eraseBefore.clear(); eraseLast = null; history.clear(); redo.clear(); invalidate() }
    private fun layer(): JSONObject? {
        val layers = client.scene.layers
        if (!choosingLayer && layers.none { it.optString("id") == selectedLayer }) {
            choosingLayer = true
            cancelCurve()
            selectedLayer = layers.firstOrNull()?.optString("id") ?: ""
            choosingLayer = false
        }
        return layers.find { it.optString("id") == selectedLayer }
    }
    private fun editable() = client.canDraw && layer()?.optBoolean("locked", false) == false &&
        layer()?.optBoolean("visible", true) == true
    private fun upsert(o: JSONObject) = json("action" to "object_upsert", "object" to o)
    private fun deletion(id: String) = json("action" to "object_delete", "objectId" to id)
    fun undo() {
        finishErasing()
        if (curvePending) { cancelCurve(); return }
        if (!client.canDraw || history.isEmpty()) return
        val entry = history.removeLast(); entry.inverse.forEach(client::operation); redo.addLast(entry)
    }
    fun redo() {
        finishCurve(); finishErasing()
        if (!client.canDraw || redo.isEmpty()) return
        val entry = redo.removeLast(); entry.forward.forEach(client::operation); history.addLast(entry)
    }
    private fun commit(forward: JSONObject, inverse: JSONObject) {
        client.operation(forward); remember(Edit(listOf(forward.copy()), listOf(inverse.copy())))
    }
    private fun remember(edit: Edit) {
        history.addLast(edit); redo.clear()
        if (history.size > 50) history.removeFirst()
    }
    private fun eraseOperation(operation: JSONObject, id: String) {
        if (!eraseBefore.containsKey(id)) eraseBefore[id] = client.scene.objects.find { it.optString("id") == id }?.copy()
        client.operation(operation)
    }
    private fun finishErasing() {
        if (eraseBefore.isNotEmpty()) {
            val forward = mutableListOf<JSONObject>(); val inverse = mutableListOf<JSONObject>()
            eraseBefore.forEach { (id, before) ->
                val after = client.scene.objects.find { it.optString("id") == id }
                if (before != null || after != null) {
                    forward.add(after?.let { upsert(it.copy()) } ?: deletion(id))
                    inverse.add(before?.let { upsert(it.copy()) } ?: deletion(id))
                }
            }
            // Delete temporary fragments before restoring originals at the canvas object limit.
            if (forward.isNotEmpty()) remember(Edit(forward.sortedBy { it.optString("action") != "object_delete" },
                inverse.sortedBy { it.optString("action") != "object_delete" }))
        }
        eraseBefore.clear(); eraseLast = null
    }
    private fun eraseTo(x: Float, y: Float) {
        val last = eraseLast ?: PointF(x, y)
        val radius = max(6f, strokeSize * 2f) * resources.displayMetrics.density
        val centers = StrokeEraser.sweep(StrokeEraser.Point(last.x * width.toDouble(), last.y * height.toDouble()),
            StrokeEraser.Point(x * width.toDouble(), y * height.toDouble()), radius.toDouble())
        eraseLast = PointF(x, y)
        if (tool == "eraser") {
            centers.forEach { p -> hit((p.x / width).toFloat(), (p.y / height).toFloat())?.let {
                eraseOperation(deletion(it.optString("id")), it.optString("id"))
            } }
            return
        }
        for (objectToCut in client.scene.objects.toList()) {
            if (objectToCut.optString("layerId") != selectedLayer) continue
            val id = objectToCut.optString("id")
            if (objectToCut.optString("type") in listOf("freehand", "brush", "airbrush", "curve")) {
                var fragments = listOf(objectToCut.array("points").objects().map {
                    StrokeEraser.Point(it.optDouble("x") * width, it.optDouble("y") * height)
                })
                var changed = false
                centers.forEach { center -> fragments = fragments.flatMap {
                    val cut = StrokeEraser.cut(it, center, radius.toDouble()); changed = changed || cut.changed; cut.fragments
                } }
                if (!changed) continue
                if (client.scene.objects.size - 1 + fragments.size > 1000) { client.notice("Canvas has reached its object limit."); continue }
                if (fragments.isEmpty()) eraseOperation(deletion(id), id)
                fragments.forEachIndexed { index, points ->
                    val fragment = objectToCut.copy().put("id", if (index == 0) id else UUID.randomUUID().toString())
                    fragment.put("points", JSONArray(points.map { point((it.x / width).toFloat(), (it.y / height).toFloat()) }))
                    geometry(fragment); eraseOperation(upsert(fragment), fragment.optString("id"))
                }
            } else eraseRaster(objectToCut, centers, radius)
        }
    }
    // The bounded 512px result must be applied in gesture order before the next touch/undo.
    @android.annotation.SuppressLint("ThreadConstraint")
    private fun eraseRaster(o: JSONObject, centers: List<StrokeEraser.Point>, radius: Float) {
        val box = bounds(o, width.toFloat(), height.toFloat())
        val padding = radius + o.optInt("strokeWidth", 4) * min(width, height) / 300f
        box.inset(-padding, -padding)
        if (centers.none { box.contains(it.x.toFloat(), it.y.toFloat()) }) return
        // Render only this object on transparency, so erasing reveals the objects underneath it.
        val factor = 512f / max(width, height)
        val bitmap = Bitmap.createBitmap(max(1, (width * factor).toInt()), max(1, (height * factor).toInt()), Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        drawObject(canvas, o, bitmap.width.toFloat(), bitmap.height.toFloat())
        val before = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(before, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val clear = Paint(Paint.ANTI_ALIAS_FLAG).apply { xfermode = PorterDuffXfermode(PorterDuff.Mode.CLEAR) }
        centers.forEach { canvas.drawCircle((it.x * factor).toFloat(), (it.y * factor).toFloat(), radius * factor, clear) }
        val pixels = IntArray(before.size)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        if (before.contentEquals(pixels)) { bitmap.recycle(); return }
        var left = bitmap.width; var top = bitmap.height; var right = -1; var bottom = -1
        pixels.forEachIndexed { index, color -> if (Color.alpha(color) > 0) {
            val x = index % bitmap.width; val y = index / bitmap.width
            left = min(left, x); right = max(right, x); top = min(top, y); bottom = max(bottom, y)
        } }
        val id = o.optString("id")
        if (right < left) { bitmap.recycle(); eraseOperation(deletion(id), id); return }
        val cropped = Bitmap.createBitmap(bitmap, left, top, right - left + 1, bottom - top + 1)
        val stream = ByteArrayOutputStream(); cropped.compress(Bitmap.CompressFormat.PNG, 100, stream)
        val data = Base64.encodeToString(stream.toByteArray(), Base64.NO_WRAP)
        val replacement = o.copy().put("type", "fill").put("filled", true).put("points", JSONArray()).put("text", "")
            .put("x", left.toDouble() / bitmap.width).put("y", top.toDouble() / bitmap.height)
            .put("width", cropped.width.toDouble() / bitmap.width).put("height", cropped.height.toDouble() / bitmap.height)
            .put("imageData", data)
        if (cropped !== bitmap) cropped.recycle()
        bitmap.recycle()
        if (data.length <= 700 * 1024) eraseOperation(upsert(replacement), id)
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
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas); render(canvas, width.toFloat(), height.toFloat())
        if (curvePending && editable()) curvePoints().forEach {
            paint.style = Paint.Style.FILL; paint.color = Color.WHITE
            val radius = 6 * resources.displayMetrics.density
            canvas.drawCircle(it.x * width, it.y * height, radius, paint)
            paint.style = Paint.Style.STROKE; paint.color = Color.rgb(26, 115, 232); paint.strokeWidth = 2 * resources.displayMetrics.density
            canvas.drawCircle(it.x * width, it.y * height, radius, paint)
        }
    }
    private fun curvePoints() = draft?.array("points")?.objects()?.map {
        CurvePoint(it.optDouble("x").toFloat(), it.optDouble("y").toFloat())
    } ?: emptyList()
    private fun setCurvePoints(points: List<CurvePoint>) {
        draft?.let { it.put("points", JSONArray(points.map { p -> point(p.x, p.y) })); geometry(it) }
    }
    fun cancelCurve() {
        if (curvePending) { draft = null; original = null; curveHandle = -1; invalidate(); editingChanged() }
    }
    fun finishCurve() {
        if (!curvePending) return
        val objectToSave = draft
        val points = curvePoints()
        draft = null; curveHandle = -1
        if (objectToSave != null && editable() && CurveGeometry.valid(points))
            commit(upsert(objectToSave), deletion(objectToSave.optString("id")))
        invalidate(); editingChanged()
    }
    private fun beginCurve(x: Float, y: Float) {
        val p = CurvePoint(x, y)
        if (!curvePending) {
            if (client.scene.objects.size >= 1000) { client.notice("Canvas has reached its object limit."); return }
            draft = newObject(x, y, "curve"); setCurvePoints(listOf(p, p)); curveHandle = 1
        } else {
            val points = curvePoints().toMutableList()
            val radius = 22 * resources.displayMetrics.density
            curveHandle = points.indices.minByOrNull { i -> hypot((points[i].x-x)*width, (points[i].y-y)*height) } ?: -1
            if (curveHandle < 0 || hypot((points[curveHandle].x-x)*width, (points[curveHandle].y-y)*height) > radius) {
                val hit = CurveGeometry.insertion(points, p, width.toFloat(), height.toFloat(), radius)
                if (hit == null) { finishCurve(); return }
                points.add(hit.index, hit.point); curveHandle = hit.index; setCurvePoints(points)
            }
        }
        invalidate(); editingChanged()
    }
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
    // Eraser rasterization is capped at 512px and ordered synchronously with undo/redo.
    @android.annotation.SuppressLint("ThreadConstraint")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (!editable() || filling) { finishErasing(); cancelCurve(); draft = null; return false }
        val x = (e.x / max(1, width)).coerceIn(0f, 1f)
        val y = (e.y / max(1, height)).coerceIn(0f, 1f)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent.requestDisallowInterceptTouchEvent(true); start = PointF(x, y); original = null
                when (tool) {
                    "curve" -> beginCurve(x, y)
                    "text" -> requestText(x, y)
                    "pick" -> {
                        val bitmap = snapshot(); val pixel = bitmap.getPixel((x * (bitmap.width - 1)).toInt(), (y * (bitmap.height - 1)).toInt())
                        ink = String.format("#%06X", pixel and 0xffffff); bitmap.recycle()
                    }
                    "fill" -> floodFill(x, y)
                    "stroke_eraser", "eraser" -> { finishErasing(); eraseTo(x, y) }
                    "move" -> { original = hit(x, y)?.copy(); draft = original?.copy() }
                    else -> {
                        if (client.scene.objects.size >= 1000) { client.notice("Canvas has reached its object limit."); return true }
                        draft = newObject(x, y)
                        if (tool in listOf("freehand", "brush", "airbrush", "polygon", "line"))
                            draft?.put("points", JSONArray().put(point(x, y)))
                        lastSent = 0
                    }
                }
                invalidate(); return true
            }
            MotionEvent.ACTION_MOVE, MotionEvent.ACTION_UP -> {
                if (tool == "stroke_eraser" || tool == "eraser") {
                    eraseTo(x, y)
                    if (e.actionMasked == MotionEvent.ACTION_UP) {
                        finishErasing(); parent.requestDisallowInterceptTouchEvent(false); performClick()
                    }
                    invalidate(); return true
                }
                if (tool == "curve") {
                    val points = curvePoints().toMutableList()
                    if (curveHandle in points.indices) { points[curveHandle] = CurvePoint(x, y); setCurvePoints(points) }
                    if (e.actionMasked == MotionEvent.ACTION_UP) {
                        curveHandle = -1
                        if (!CurveGeometry.valid(points)) cancelCurve() else if (points.size == 4) finishCurve()
                        parent.requestDisallowInterceptTouchEvent(false); performClick(); editingChanged()
                    }
                    invalidate(); return true
                }
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
                        "freehand", "brush", "airbrush", "polygon" -> {
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
                    } else if (tool in listOf("freehand", "brush", "airbrush") && SystemClock.uptimeMillis() - lastSent >= 60) {
                        client.operation(upsert(d.copy())); lastSent = SystemClock.uptimeMillis()
                    }
                }
                if (e.actionMasked == MotionEvent.ACTION_UP) { parent.requestDisallowInterceptTouchEvent(false); performClick() }
                invalidate(); return true
            }
            MotionEvent.ACTION_CANCEL -> {
                finishErasing(); cancelCurve()
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
