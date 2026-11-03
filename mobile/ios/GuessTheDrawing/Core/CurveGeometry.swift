import Foundation

enum CurveGeometry {
    struct Hit { let index: Int; let point: CGPoint }
    static func sample(_ points: [CGPoint], segment i: Int, t: CGFloat) -> CGPoint {
        let a = points[max(0, i - 1)], b = points[i], e = points[i + 1], n = points[min(points.count - 1, i + 2)]
        let u = 1 - t
        func value(_ a: CGFloat, _ b: CGFloat, _ e: CGFloat, _ n: CGFloat) -> CGFloat {
            let c1 = b + (e - a) / 6, c2 = e - (n - b) / 6
            return u*u*u*b + 3*u*u*t*c1 + 3*u*t*t*c2 + t*t*t*e
        }
        return CGPoint(x: value(a.x, b.x, e.x, n.x), y: value(a.y, b.y, e.y, n.y))
    }
    static func insertion(_ points: [CGPoint], touch: CGPoint, size: CGSize, radius: CGFloat) -> Hit? {
        guard (2...3).contains(points.count) else { return nil }
        var best = radius * radius, result: Hit?
        for i in 0..<(points.count - 1) {
            var a = points[i]
            for step in 1...32 {
                let b = sample(points, segment: i, t: CGFloat(step) / 32)
                let dx = (b.x-a.x)*size.width, dy = (b.y-a.y)*size.height, length = dx*dx+dy*dy
                let dot = (touch.x-a.x)*size.width*dx + (touch.y-a.y)*size.height*dy
                let t = length == 0 ? 0 : min(1, max(0, dot/length))
                let p = CGPoint(x: a.x+(b.x-a.x)*t, y: a.y+(b.y-a.y)*t)
                let distance = pow((touch.x-p.x)*size.width, 2) + pow((touch.y-p.y)*size.height, 2)
                if distance < best { best = distance; result = Hit(index: i+1, point: p) }
                a = b
            }
        }
        return result
    }
    static func valid(_ points: [CGPoint]) -> Bool {
        guard (2...4).contains(points.count), let a = points.first, let b = points.last else { return false }
        return hypot(b.x-a.x, b.y-a.y) > 0.001
    }
}
