package com.guessthedrawing.mobile

import org.json.JSONArray
import org.json.JSONObject

class CanvasScene {
    val layers = mutableListOf<JSONObject>()
    val objects = mutableListOf<JSONObject>()
    var revision = 0
        private set
    var resetCount = 0
        private set
    fun replace(newLayers: JSONArray, newObjects: JSONArray) {
        layers.clear(); layers.addAll(newLayers.objects().take(32).map { it.copy() })
        objects.clear(); objects.addAll(newObjects.objects().take(1000).map { it.copy() })
        revision++; resetCount++
    }
    private fun upsert(list: MutableList<JSONObject>, item: JSONObject, limit: Int) {
        if (item.optString("id").isEmpty()) return
        val index = list.indexOfFirst { it.optString("id") == item.optString("id") }
        if (index >= 0) list[index] = item.copy() else if (list.size < limit) list.add(item.copy())
    }
    fun apply(op: JSONObject) {
        if (op.optString("action") in listOf("clear", "clear_layer", "layer_delete")) resetCount++
        when (op.optString("action")) {
            "object_upsert" -> upsert(objects, op.obj("object"), 1000)
            "object_delete" -> objects.removeAll { it.optString("id") == op.optString("objectId") }
            "layer_upsert" -> upsert(layers, op.obj("layer"), 32)
            "layer_delete" -> {
                layers.removeAll { it.optString("id") == op.optString("layerId") }
                objects.removeAll { it.optString("layerId") == op.optString("layerId") }
            }
            "layer_reorder" -> {
                val ids = op.array("layerIds")
                for (i in 0 until ids.length())
                    layers.find { it.optString("id") == ids.optString(i) }?.put("order", i)
            }
            "clear_layer" -> objects.removeAll { it.optString("layerId") == op.optString("layerId") }
            "clear" -> objects.clear()
        }
        layers.sortBy { it.optInt("order") }
        revision++
    }
    fun visibleObjects(): List<JSONObject> = layers.filter { it.optBoolean("visible", true) }
        .sortedBy { it.optInt("order") }.flatMap { layer ->
            objects.filter { it.optString("layerId") == layer.optString("id") }
        }
}
