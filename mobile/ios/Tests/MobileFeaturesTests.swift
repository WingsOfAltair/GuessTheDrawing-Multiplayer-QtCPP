import XCTest
@testable import GTDCore

final class MobileFeaturesTests: XCTestCase {
    func testCurveHitsAndControlPointsUseDesktopGeometry() throws {
        let points = [CGPoint(x: 0.1, y: 0.4), CGPoint(x: 0.9, y: 0.4)]
        let hit = try XCTUnwrap(CurveGeometry.insertion(points, touch: CGPoint(x: 0.5, y: 0.41), size: CGSize(width: 400, height: 900), radius: 22))
        XCTAssertEqual(hit.index, 1); XCTAssertEqual(hit.point.x, 0.5, accuracy: 0.001); XCTAssertEqual(hit.point.y, 0.4, accuracy: 0.001)
        XCTAssertNil(CurveGeometry.insertion(points, touch: CGPoint(x: 0.5, y: 0.5), size: CGSize(width: 400, height: 900), radius: 22))
        let bent = [points[0], CGPoint(x: 0.45, y: 0.75), points[1]]
        XCTAssertEqual(CurveGeometry.sample(bent, segment: 0, t: 1), bent[1]); XCTAssertTrue(CurveGeometry.valid(bent))
        XCTAssertFalse(CurveGeometry.valid([points[0], points[0]]))
    }
    func testVoicePacketizerPreservesFragmentsAndRejectsMalformedFrames() throws {
        let data = Data((0..<1920).map { UInt8($0 % 256) })
        var packetizer = VoicePacketizer(), frames: [Data] = []
        for byte in data { frames += packetizer.append(Data([byte])) }
        XCTAssertEqual(frames.count, 3); XCTAssertEqual(frames.reduce(Data(), +), data)
        for frame in frames { XCTAssertEqual(frame.count, 640); XCTAssertEqual(VoicePcm.decode(frame.base64EncodedString()), frame) }
        XCTAssertNil(VoicePcm.decode(Data(repeating: 0, count: 638).base64EncodedString()))
        XCTAssertNil(VoicePcm.decode(String(repeating: "!", count: 856)))
    }
    func testDesktopThemesAndStaffHierarchy() throws {
        let path = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("fixtures/themes.json")
        let fixtures = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: path)) as? [JSON])
        XCTAssertEqual(ThemePalette.all.count, 13)
        for (theme, fixture) in zip(ThemePalette.all, fixtures) {
            XCTAssertEqual(theme.id, fixture.string("id")); XCTAssertEqual([theme.window, theme.surface, theme.base, theme.text, theme.accent, theme.accentText, theme.button, theme.disabledText], fixture["colors"] as? [String])
        }
        XCTAssertEqual(CommunityPolicy.level(["serverRole": "moderator"]), 1)
        XCTAssertEqual(CommunityPolicy.level(["serverRole": "admin"]), 2)
        XCTAssertEqual(CommunityPolicy.level(["masterSuperAdmin": true]), 3)
    }
}
