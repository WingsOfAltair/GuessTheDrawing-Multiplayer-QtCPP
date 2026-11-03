import XCTest
@testable import GTDCore

final class ProtocolTests: XCTestCase {
    private func fixture() throws -> JSON {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("fixtures/protocol.json")
        return try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? JSON)
    }
    func testFragmentedUTF8AndCoalescedFrames() throws {
        let data = try Wire.encode(Wire.message("ChatMessage", ["text": "مرحبا 🎨"]))
        var decoder = FrameDecoder(), frames: [JSON] = []
        for byte in data { frames += try decoder.feed(Data([byte])) }
        XCTAssertEqual(frames.count, 1); XCTAssertEqual(frames.first?.string("text"), "مرحبا 🎨")
        var batch = FrameDecoder()
        XCTAssertEqual(try batch.feed(data + data).count, 2)
    }
    func testMaximumFrameAndOversize() throws {
        let prefix = #"{"version":1,"type":"Padding","text":""#
        let suffix = "\"}"
        let data = Data((prefix + String(repeating: "x", count: Wire.maxFrame - prefix.utf8.count - suffix.utf8.count) + suffix + "\n").utf8)
        var decoder = FrameDecoder()
        XCTAssertEqual(try decoder.feed(data).count, 1)
        XCTAssertThrowsError(try decoder.feed(Data(repeating: 32, count: Wire.maxFrame + 1)))
    }
    func testRejectsUnsupportedProtocol() {
        var decoder = FrameDecoder()
        XCTAssertThrowsError(try decoder.feed(Data("{\"version\":2,\"type\":\"Hello\"}\n".utf8)))
    }
    func testPrivateRoomProofMatchesQtCompatibleUTF8Fixture() throws {
        let value = try fixture().object("passkey")
        XCTAssertEqual(try Wire.passkeyProof(password: value.string("password"), salt: value.string("salt"), nonce: value.string("nonce")), value.string("proof"))
    }
    func testCanvasLayerOrderingVisibilityAndDeletion() throws {
        let value = try fixture(); var scene = CanvasScene()
        scene.replace(layers: value.objects("layers"), objects: value.objects("objects"))
        XCTAssertEqual(scene.visibleObjects.map { $0.string("id") }, ["stroke-a", "rect-b"])
        scene.apply(["action": "layer_reorder", "layerIds": ["top", "base"]])
        XCTAssertEqual(scene.visibleObjects.map { $0.string("id") }, ["rect-b", "stroke-a"])
        var layer = scene.layers[0]; layer["visible"] = false
        scene.apply(["action": "layer_upsert", "layer": layer]); XCTAssertEqual(scene.visibleObjects.count, 1)
        scene.apply(["action": "layer_delete", "layerId": "base"]); XCTAssertEqual(scene.objects.count, 1)
        scene.apply(["action": "clear"]); XCTAssertTrue(scene.objects.isEmpty); XCTAssertEqual(scene.layers.count, 1)
    }
}
