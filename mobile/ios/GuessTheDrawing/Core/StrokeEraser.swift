import Foundation

/// Desktop-compatible circular stroke cutting in canvas pixel coordinates.
enum StrokeEraser {
    struct Cut { let fragments: [[CGPoint]]; let changed: Bool }
    static func sweep(from: CGPoint, to: CGPoint, radius: CGFloat) -> [CGPoint] {
        let steps = max(1, Int(ceil(hypot(to.x - from.x, to.y - from.y) / max(1, radius * 0.5))))
        return (0...steps).map { i in
            let t = CGFloat(i) / CGFloat(steps)
            return CGPoint(x: from.x + (to.x - from.x) * t, y: from.y + (to.y - from.y) * t)
        }
    }
    static func cut(_ path: [CGPoint], center: CGPoint, radius: CGFloat) -> Cut {
        guard !path.isEmpty else { return Cut(fragments: [], changed: false) }
        if path.count == 1 {
            return hypot(path[0].x - center.x, path[0].y - center.y) <= radius
                ? Cut(fragments: [], changed: true) : Cut(fragments: [path], changed: false)
        }
        var fragments: [[CGPoint]] = [], current: [CGPoint] = []
        var changed = false
        func append(_ p: CGPoint) {
            if current.isEmpty || hypot(current.last!.x - p.x, current.last!.y - p.y) > 0.001 { current.append(p) }
        }
        func finish() { if !current.isEmpty { fragments.append(current) }; current = [] }
        for i in 0..<(path.count - 1) {
            let start = path[i], end = path[i + 1]
            let dx = end.x - start.x, dy = end.y - start.y
            let ox = start.x - center.x, oy = start.y - center.y
            let a = dx * dx + dy * dy, b = 2 * (ox * dx + oy * dy), c = ox * ox + oy * oy - radius * radius
            var intersects: Bool
            var enter: CGFloat = 0, exit: CGFloat = 1
            if a < 0.000001 { intersects = c <= 0 } else {
                let discriminant = b * b - 4 * a * c
                if discriminant >= 0 {
                    let root = sqrt(discriminant)
                    let first = (-b - root) / (2 * a), second = (-b + root) / (2 * a)
                    intersects = second >= 0 && first <= 1
                    if intersects { enter = min(1, max(0, first)); exit = min(1, max(0, second)) }
                } else { intersects = c <= 0 }
            }
            if !intersects { append(start); append(end); continue }
            changed = true
            if enter > 0 { append(start); append(CGPoint(x: start.x + dx * enter, y: start.y + dy * enter)) }
            finish()
            if exit < 1 { append(CGPoint(x: start.x + dx * exit, y: start.y + dy * exit)); append(end) } else { finish() }
        }
        finish()
        return Cut(fragments: fragments, changed: changed)
    }
}
