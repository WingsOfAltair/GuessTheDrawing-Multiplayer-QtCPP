import XCTest
@testable import GTDCore

final class LocalizationTests: XCTestCase {
    func testDirectionAndNumericInput() {
        let previous = L10n.language
        defer { L10n.language = previous }
        XCTAssertEqual(L10n.languages.count, 12)
        for (code, _) in L10n.languages {
            L10n.language = code
            XCTAssertEqual(L10n.rtl, code == "ar")
        }
        XCTAssertEqual(L10n.integer("٤٥٤٥٠"), 45450)
        XCTAssertEqual(L10n.integer("१२३"), 123)
        XCTAssertNil(L10n.integer("12x"))
    }
    func testInterpolationPreservesUserTextAndBidiBoundaries() {
        let previous = L10n.language
        defer { L10n.language = previous }
        L10n.language = "ar"
        XCTAssertEqual(L10n.format("#%1 / @%2", [12, "name"]), "\u{2068}#12\u{2069} / \u{2068}@name\u{2069}")
        let result = L10n.format("%2 / %1 / %10", ["player_42", "Room %1", "3", "4", "5", "6", "7", "8", "9", "ten"])
        XCTAssertEqual(result, "\u{2068}Room %1\u{2069} / \u{2068}player_42\u{2069} / \u{2068}ten\u{2069}")
        L10n.language = "en"
        XCTAssertEqual(L10n.format("%2 / %1", ["مرحبا", "Room %1"]), "Room %1 / مرحبا")
        XCTAssertEqual(L10n.text("192.168.1.29"), "192.168.1.29")
    }
}
