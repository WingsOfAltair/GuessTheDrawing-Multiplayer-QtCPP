import Foundation

struct CanvasScene {
    private(set) var layers: [JSON] = []
    private(set) var objects: [JSON] = []
    private(set) var revision = 0
    private(set) var resetCount = 0
    mutating func replace(layers: [JSON], objects: [JSON]) {
        self.layers = Array(layers.prefix(32)); self.objects = Array(objects.prefix(1000)); revision += 1; resetCount += 1
    }
    private func upsert(_ list: [JSON], _ item: JSON, limit: Int) -> [JSON] {
        guard !item.string("id").isEmpty else { return list }
        var result = list
        if let index = result.firstIndex(where: { $0.string("id") == item.string("id") }) { result[index] = item }
        else if result.count < limit { result.append(item) }
        return result
    }
    mutating func apply(_ op: JSON) {
        if ["clear", "clear_layer", "layer_delete"].contains(op.string("action")) { resetCount += 1 }
        switch op.string("action") {
        case "object_upsert": objects = upsert(objects, op.object("object"), limit: 1000)
        case "object_delete": objects.removeAll { $0.string("id") == op.string("objectId") }
        case "layer_upsert": layers = upsert(layers, op.object("layer"), limit: 32)
        case "layer_delete":
            layers.removeAll { $0.string("id") == op.string("layerId") }
            objects.removeAll { $0.string("layerId") == op.string("layerId") }
        case "layer_reorder":
            for (index, id) in (op["layerIds"] as? [String] ?? []).enumerated() {
                if let i = layers.firstIndex(where: { $0.string("id") == id }) { layers[i]["order"] = index }
            }
        case "clear_layer": objects.removeAll { $0.string("layerId") == op.string("layerId") }
        case "clear": objects.removeAll()
        default: break
        }
        layers = layers.enumerated().sorted {
            $0.element.int("order") == $1.element.int("order") ? $0.offset < $1.offset : $0.element.int("order") < $1.element.int("order")
        }.map(\.element)
        revision += 1
    }
    var visibleObjects: [JSON] {
        layers.filter { $0.bool("visible", true) }.flatMap { layer in
            objects.filter { $0.string("layerId") == layer.string("id") }
        }
    }
}
