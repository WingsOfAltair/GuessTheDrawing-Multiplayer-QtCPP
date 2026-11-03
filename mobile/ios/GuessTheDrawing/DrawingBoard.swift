import SwiftUI
import UIKit
import ImageIO

@MainActor
final class DrawingTools: ObservableObject {
    @Published var tool = "freehand" { willSet { if newValue != tool { finishCurve() } } }
    @Published var color = Color(red: 0.125, green: 0.13, blue: 0.14)
    @Published var width = 6
    @Published var filled = false
    @Published var layerID = "" { willSet { if newValue != layerID { finishCurve() } } }
    @Published var askText = false
    @Published var curvePending = false
    var finishCurve: () -> Void = {}
    var cancelCurve: () -> Void = {}
    var undo: () -> Void = {}
    var redo: () -> Void = {}
    var addText: (String) -> Void = { _ in }
    var snapshot: () -> UIImage? = { nil }
}
extension UIColor {
    convenience init(hex: String) {
        let value = UInt64(hex.trimmingCharacters(in: CharacterSet(charactersIn: "#")), radix: 16) ?? 0x202124
        self.init(red: CGFloat((value >> 16) & 255) / 255, green: CGFloat((value >> 8) & 255) / 255,
                  blue: CGFloat(value & 255) / 255, alpha: hex.count == 9 ? CGFloat((value >> 24) & 255) / 255 : 1)
    }
    var hex: String {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        getRed(&r, green: &g, blue: &b, alpha: &a)
        return String(format: "#%02X%02X%02X", Int(r * 255), Int(g * 255), Int(b * 255))
    }
}
@MainActor
struct DrawingBoard: UIViewRepresentable {
    @ObservedObject var client: GameClient
    @ObservedObject var tools: DrawingTools
    func makeUIView(context: Context) -> BoardView {
        let view = BoardView(client: client, tools: tools)
        tools.undo = { [weak view] in view?.undo() }
        tools.redo = { [weak view] in view?.redo() }
        tools.snapshot = { [weak view] in view?.snapshot() }
        tools.addText = { [weak view] in view?.addText($0) }
        tools.finishCurve = { [weak view] in view?.finishCurve() }
        tools.cancelCurve = { [weak view] in view?.cancelCurve() }
        return view
    }
    func updateUIView(_ view: BoardView, context: Context) { view.refresh() }
}
@MainActor
final class BoardView: UIView {
    let client: GameClient
    let tools: DrawingTools
    private var draft: JSON?
    private var original: JSON?
    private var start = CGPoint.zero
    private var textPoint = CGPoint.zero
    private var lastSent = Date.distantPast
    private var roundKey = ""
    private var curveHandle: Int?
    private var history: [(JSON, JSON)] = []
    private var redoHistory: [(JSON, JSON)] = []
    private let images = NSCache<NSString, UIImage>()
    init(client: GameClient, tools: DrawingTools) {
        self.client = client; self.tools = tools
        super.init(frame: .zero)
        backgroundColor = .white; isMultipleTouchEnabled = false; isOpaque = true
        accessibilityLabel = "Shared drawing canvas"
        images.countLimit = 12
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is unavailable") }
    func refresh() {
        let key = "\(client.drawer):\(client.roundActive):\(client.canDraw):\(client.scene.resetCount)"
        if key != roundKey || !editable {
            draft = nil; original = nil; curveHandle = nil
            if key != roundKey { history = []; redoHistory = []; roundKey = key }
            // updateUIView must not publish a SwiftUI change synchronously.
            if tools.curvePending { DispatchQueue.main.async { [weak self] in self?.publishCurveState() } }
        }
        setNeedsDisplay()
    }
    private var selectedLayer: JSON? {
        client.scene.layers.first { $0.string("id") == tools.layerID } ?? client.scene.layers.first
    }
    private var editable: Bool { client.canDraw && selectedLayer?.bool("locked") == false && selectedLayer?.bool("visible", true) == true }
    private func upsert(_ object: JSON) -> JSON { ["action": "object_upsert", "object": object] }
    private func deletion(_ id: String) -> JSON { ["action": "object_delete", "objectId": id] }
    private func commit(_ forward: JSON, inverse: JSON) {
        client.operation(forward); history.append((forward, inverse)); redoHistory = []
        if history.count > 50 { history.removeFirst() }
    }
    func undo() {
        if draft?.string("type") == "curve" { cancelCurve(); return }
        guard client.canDraw, let entry = history.popLast() else { return }
        client.operation(entry.1); redoHistory.append(entry)
    }
    func redo() {
        finishCurve()
        guard client.canDraw, let entry = redoHistory.popLast() else { return }
        client.operation(entry.0); history.append(entry)
    }
    private func newObject(_ p: CGPoint, type: String? = nil) -> JSON {
        ["id": UUID().uuidString, "layerId": selectedLayer?.string("id") ?? "", "type": type ?? tools.tool,
         "color": UIColor(tools.color).hex, "fillColor": UIColor(tools.color).hex,
         "strokeWidth": tools.width, "fontSize": 32, "filled": tools.filled,
         "x": p.x, "y": p.y, "width": 0, "height": 0, "points": [JSON]()]
    }
    private func point(_ p: CGPoint) -> JSON { ["x": p.x, "y": p.y] }
    private func location(_ touch: UITouch) -> CGPoint {
        let p = touch.location(in: self)
        return CGPoint(x: min(1, max(0, p.x / max(1, bounds.width))), y: min(1, max(0, p.y / max(1, bounds.height))))
    }
    private func geometry(_ object: inout JSON) {
        let points = object.objects("points")
        guard let x = points.map({ $0.number("x") }).min(), let y = points.map({ $0.number("y") }).min() else { return }
        object["x"] = x; object["y"] = y
        object["width"] = (points.map { $0.number("x") }.max() ?? x) - x
        object["height"] = (points.map { $0.number("y") }.max() ?? y) - y
    }
    private func rect(_ o: JSON, size: CGSize) -> CGRect {
        CGRect(x: CGFloat(o.number("x")) * size.width, y: CGFloat(o.number("y")) * size.height,
               width: CGFloat(o.number("width")) * size.width, height: CGFloat(o.number("height")) * size.height)
    }
    override func draw(_ rect: CGRect) {
        guard let context = UIGraphicsGetCurrentContext() else { return }
        render(context, size: bounds.size)
        if draft?.string("type") == "curve", editable {
            context.setLineWidth(2); context.setStrokeColor(UIColor.systemBlue.cgColor)
            context.setFillColor(UIColor.white.cgColor)
            for p in curvePoints {
                context.addEllipse(in: CGRect(x: p.x*bounds.width-6, y: p.y*bounds.height-6, width: 12, height: 12))
                context.drawPath(using: .fillStroke)
            }
        }
    }
    private var curvePoints: [CGPoint] {
        (draft?.objects("points") ?? []).map { CGPoint(x: $0.number("x"), y: $0.number("y")) }
    }
    private func setCurvePoints(_ points: [CGPoint]) {
        guard var object = draft else { return }
        object["points"] = points.map(point); geometry(&object); draft = object
    }
    private func publishCurveState() {
        let pending = draft?.string("type") == "curve"
        if tools.curvePending != pending { tools.curvePending = pending }
    }
    func cancelCurve() {
        guard draft?.string("type") == "curve" else { return }
        draft = nil; curveHandle = nil; publishCurveState(); setNeedsDisplay()
    }
    func finishCurve() {
        guard let object = draft, object.string("type") == "curve" else { return }
        let points = curvePoints
        draft = nil; curveHandle = nil
        if editable, CurveGeometry.valid(points) { commit(upsert(object), inverse: deletion(object.string("id"))) }
        publishCurveState(); setNeedsDisplay()
    }
    private func beginCurve(_ p: CGPoint) {
        if draft?.string("type") != "curve" {
            guard client.scene.objects.count < 1000 else { client.notice("Canvas has reached its object limit."); return }
            draft = newObject(p, type: "curve"); setCurvePoints([p, p]); curveHandle = 1
        } else {
            var points = curvePoints
            func distance(_ i: Int) -> CGFloat { hypot((points[i].x-p.x)*bounds.width, (points[i].y-p.y)*bounds.height) }
            if let nearest = points.indices.min(by: { distance($0) < distance($1) }), distance(nearest) <= 22 {
                curveHandle = nearest
            } else if let hit = CurveGeometry.insertion(points, touch: p, size: bounds.size, radius: 22) {
                points.insert(hit.point, at: hit.index); curveHandle = hit.index; setCurvePoints(points)
            } else { finishCurve(); return }
        }
        publishCurveState(); setNeedsDisplay()
    }
    private func render(_ c: CGContext, size: CGSize) {
        c.setFillColor(UIColor.white.cgColor); c.fill(CGRect(origin: .zero, size: size))
        for object in client.scene.visibleObjects where object.string("id") != draft?.string("id") { paint(object, in: c, size: size) }
        if let draft, editable { paint(draft, in: c, size: size) }
    }
    private func paint(_ o: JSON, in c: CGContext, size: CGSize) {
        c.saveGState(); defer { c.restoreGState() }
        let type = o.string("type"), color = UIColor(hex: o.string("color", "#202124"))
        let width = max(1, CGFloat(o.int("strokeWidth", 4)) * min(size.width, size.height) / 600)
        c.setStrokeColor(color.cgColor); c.setFillColor(color.cgColor); c.setLineWidth(width)
        c.setLineJoin(.round); c.setLineCap(type == "freehand" ? .butt : .round)
        let box = rect(o, size: size)
        let points = o.objects("points").prefix(2000).map { CGPoint(x: CGFloat($0.number("x")) * size.width, y: CGFloat($0.number("y")) * size.height) }
        let mode: CGPathDrawingMode = o.bool("filled") ? .fillStroke : .stroke
        switch type {
        case "rectangle": c.addRect(box); c.drawPath(using: mode)
        case "rounded_rectangle":
            let radius = min(16, min(box.width, box.height) / 4)
            c.addPath(UIBezierPath(roundedRect: box, cornerRadius: radius).cgPath); c.drawPath(using: mode)
        case "ellipse": c.addEllipse(in: box); c.drawPath(using: mode)
        case "fill":
            let text = o.string("imageData")
            guard text.count <= 700 * 1024 else { return }
            let key = (o.string("id") + String(text.hashValue)) as NSString
            var image = images.object(forKey: key)
            if image == nil, let data = Data(base64Encoded: text), let source = CGImageSourceCreateWithData(data as CFData, nil),
               let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
               let w = properties[kCGImagePropertyPixelWidth] as? Int, let h = properties[kCGImagePropertyPixelHeight] as? Int,
               (1...512).contains(w), (1...512).contains(h) {
                image = UIImage(data: data)
                if let image { images.setObject(image, forKey: key) }
            }
            image?.draw(in: box)
        case "text":
            c.clip(to: box)
            let style = NSMutableParagraphStyle(); style.lineBreakMode = .byWordWrapping
            (o.string("text") as NSString).draw(in: box, withAttributes: [
                .font: UIFont.systemFont(ofSize: max(8, CGFloat(o.int("fontSize", 24)) * size.width / 1024)),
                .foregroundColor: color, .paragraphStyle: style])
        case "airbrush":
            var random: UInt32 = 2166136261
            for byte in o.string("id").utf8 { random = (random ^ UInt32(byte)) &* 16777619 }
            func next() -> UInt32 { random = random &* 1664525 &+ 1013904223; return random }
            let radius = max(2, width * 2)
            for p in points {
                for _ in 0..<max(8, min(32, o.int("strokeWidth", 4))) {
                    let angle = Double(next()) / 4294967296 * .pi * 2
                    let distance = CGFloat(sqrt(Double(next()) / 4294967296)) * radius
                    let dot = CGFloat(0.5 + Double(next()) / 4294967296 * 1.2)
                    c.setFillColor(color.withAlphaComponent(CGFloat(55 + next() % 100) / 255).cgColor)
                    c.fillEllipse(in: CGRect(x: p.x + CGFloat(cos(angle)) * distance - dot,
                        y: p.y + CGFloat(sin(angle)) * distance - dot, width: dot * 2, height: dot * 2))
                }
            }
        default:
            guard let first = points.first else { return }
            c.move(to: first)
            if type == "curve", points.count > 2 {
                for i in 0..<(points.count - 1) {
                    let a = points[max(0, i - 1)], b = points[i], e = points[i + 1], n = points[min(points.count - 1, i + 2)]
                    c.addCurve(to: e, control1: CGPoint(x: b.x + (e.x - a.x) / 6, y: b.y + (e.y - a.y) / 6),
                               control2: CGPoint(x: e.x - (n.x - b.x) / 6, y: e.y - (n.y - b.y) / 6))
                }
            } else { for p in points.dropFirst() { c.addLine(to: p) } }
            if type == "polygon" { c.closePath(); c.drawPath(using: mode) } else { c.strokePath() }
            if points.count == 1 { c.fillEllipse(in: CGRect(x: first.x - width / 2, y: first.y - width / 2, width: width, height: width)) }
        }
    }
    private func hit(_ p: CGPoint) -> JSON? {
        client.scene.visibleObjects.reversed().first {
            $0.string("layerId") == selectedLayer?.string("id") && rect($0, size: CGSize(width: 1, height: 1)).insetBy(dx: -0.025, dy: -0.025).contains(p)
        }
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard editable, let touch = touches.first else { return }
        start = location(touch); original = nil
        switch tools.tool {
        case "curve": beginCurve(start)
        case "text": textPoint = start; tools.askText = true
        case "eraser": if let old = hit(start) { commit(deletion(old.string("id")), inverse: upsert(old)) }
        case "move": original = hit(start); draft = original
        case "fill": floodFill(start)
        default:
            guard client.scene.objects.count < 1000 else { client.notice("Canvas has reached its object limit."); return }
            var object = newObject(start)
            if ["freehand", "brush", "airbrush", "polygon", "line"].contains(tools.tool) { object["points"] = [point(start)] }
            draft = object; lastSent = .distantPast
        }
        setNeedsDisplay()
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }; updateStroke(location(touch), ended: false)
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }; updateStroke(location(touch), ended: true)
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        cancelCurve()
        if let draft, client.canDraw, client.scene.objects.contains(where: { $0.string("id") == draft.string("id") }) {
            commit(upsert(draft), inverse: original.map(upsert) ?? deletion(draft.string("id")))
        }
        draft = nil; original = nil; setNeedsDisplay()
    }
    private func updateStroke(_ p: CGPoint, ended: Bool) {
        guard editable, var object = draft else { cancelCurve(); draft = nil; return }
        if tools.tool == "curve" {
            var points = curvePoints
            if let i = curveHandle, points.indices.contains(i) { points[i] = p; setCurvePoints(points) }
            if ended {
                curveHandle = nil
                if !CurveGeometry.valid(points) { cancelCurve() } else if points.count == 4 { finishCurve() }
            }
            publishCurveState(); setNeedsDisplay(); return
        }
        switch tools.tool {
        case "move":
            if let old = original {
                let dx = min(1 - old.number("x") - old.number("width"), max(-old.number("x"), Double(p.x - start.x)))
                let dy = min(1 - old.number("y") - old.number("height"), max(-old.number("y"), Double(p.y - start.y)))
                object["x"] = old.number("x") + dx; object["y"] = old.number("y") + dy
                object["points"] = old.objects("points").map { ["x": $0.number("x") + dx, "y": $0.number("y") + dy] }
            }
        case "line": object["points"] = [point(start), point(p)]; geometry(&object)
        case "freehand", "brush", "airbrush", "polygon":
            var points = object.objects("points"); if points.count < 2000 { points.append(point(p)) }
            object["points"] = points; geometry(&object)
        default:
            object["x"] = min(start.x, p.x); object["y"] = min(start.y, p.y)
            object["width"] = abs(start.x - p.x); object["height"] = abs(start.y - p.y)
        }
        draft = object
        if ended {
            if tools.tool != "polygon" || object.objects("points").count >= 3 {
                commit(upsert(object), inverse: original.map(upsert) ?? deletion(object.string("id")))
            }
            draft = nil; original = nil
        } else if ["freehand", "brush", "airbrush"].contains(tools.tool), Date().timeIntervalSince(lastSent) >= 0.06 {
            client.operation(upsert(object)); lastSent = Date()
        }
        setNeedsDisplay()
    }
    func addText(_ text: String) {
        guard editable, !text.isEmpty else { return }
        var object = newObject(textPoint, type: "text")
        object["text"] = String(text.prefix(256)); object["width"] = 1 - textPoint.x; object["height"] = min(0.3, 1 - textPoint.y)
        commit(upsert(object), inverse: deletion(object.string("id")))
    }
    func snapshot() -> UIImage {
        let factor = 512 / max(1, max(bounds.width, bounds.height))
        let size = CGSize(width: max(1, floor(bounds.width * factor)), height: max(1, floor(bounds.height * factor)))
        let format = UIGraphicsImageRendererFormat(); format.scale = 1; format.opaque = true
        return UIGraphicsImageRenderer(size: size, format: format).image { render($0.cgContext, size: size) }
    }
    private func floodFill(_ p: CGPoint) {
        guard let cg = snapshot().cgImage else { return }
        let w = cg.width, h = cg.height
        var pixels = [UInt32](repeating: 0, count: w * h)
        let flags = CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue
        pixels.withUnsafeMutableBytes { bytes in
            let c = CGContext(data: bytes.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                              space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: flags)
            c?.translateBy(x: 0, y: CGFloat(h)); c?.scaleBy(x: 1, y: -1); c?.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
        }
        let first = Int(p.y * CGFloat(h - 1)) * w + Int(p.x * CGFloat(w - 1))
        let target = pixels[first]
        let ink = UInt32(UIColor(tools.color).hex.dropFirst(), radix: 16) ?? 0
        let color = 0xff000000 | ink
        guard target != color else { return }
        var output = [UInt32](repeating: 0, count: w * h), visited = [Bool](repeating: false, count: w * h)
        var queue = [first], head = 0; visited[first] = true
        while head < queue.count {
            let index = queue[head]; head += 1
            guard pixels[index] == target else { continue }
            output[index] = color
            func enqueue(_ index: Int) { if !visited[index] { visited[index] = true; queue.append(index) } }
            if index % w > 0 { enqueue(index - 1) }; if index % w < w - 1 { enqueue(index + 1) }
            if index >= w { enqueue(index - w) }; if index < w * (h - 1) { enqueue(index + w) }
        }
        let image: UIImage? = output.withUnsafeMutableBytes { bytes in
            guard let c = CGContext(data: bytes.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                   space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: flags), let cg = c.makeImage() else { return nil }
            return UIImage(cgImage: cg)
        }
        guard let png = image?.pngData() else { return }
        var object = newObject(.zero, type: "fill")
        object["width"] = 1; object["height"] = 1; object["imageData"] = png.base64EncodedString()
        commit(upsert(object), inverse: deletion(object.string("id")))
    }
}
