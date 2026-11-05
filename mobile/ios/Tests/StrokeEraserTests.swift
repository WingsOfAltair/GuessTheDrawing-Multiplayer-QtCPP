import XCTest
@testable import GTDCore

final class StrokeEraserTests: XCTestCase {
    private func p(_ x: CGFloat, _ y: CGFloat = 0) -> CGPoint { CGPoint(x: x, y: y) }
    func testCutsMiddleWithoutRemovingEnds() {
        let cut = StrokeEraser.cut([p(0), p(100)], center: p(50), radius: 10)
        XCTAssertTrue(cut.changed)
        XCTAssertEqual(cut.fragments, [[p(0), p(40)], [p(60), p(100)]])
    }
    func testMissesDotsAndRepeatedPoints() {
        let path = [p(0), p(100)]
        let miss = StrokeEraser.cut(path, center: p(50, 30), radius: 10)
        XCTAssertFalse(miss.changed); XCTAssertEqual(miss.fragments, [path])
        XCTAssertTrue(StrokeEraser.cut([p(5)], center: p(5), radius: 10).fragments.isEmpty)
        XCTAssertTrue(StrokeEraser.cut([p(5), p(5)], center: p(5), radius: 10).fragments.isEmpty)
        XCTAssertTrue(StrokeEraser.cut(path, center: p(50), radius: 100).fragments.isEmpty)
    }
    func testFastDragHasNoGaps() {
        let sweep = StrokeEraser.sweep(from: p(0, 10), to: p(200, 70), radius: 12)
        XCTAssertEqual(sweep.first, p(0, 10)); XCTAssertEqual(sweep.last, p(200, 70))
        for (a, b) in zip(sweep, sweep.dropFirst()) { XCTAssertLessThanOrEqual(hypot(a.x-b.x, a.y-b.y), 6) }
        var fragments = [[p(100, -100), p(100, 200)]]
        for center in sweep { fragments = fragments.flatMap { StrokeEraser.cut($0, center: center, radius: 12).fragments } }
        XCTAssertEqual(fragments.count, 2)
        XCTAssertLessThan(fragments[0].last!.y, 40); XCTAssertGreaterThan(fragments[1].first!.y, 40)
    }
}
