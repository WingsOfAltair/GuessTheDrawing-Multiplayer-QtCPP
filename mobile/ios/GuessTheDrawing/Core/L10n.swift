import Foundation

enum L10n {
    static let languages = [("en", "English"), ("es", "Español"), ("fr", "Français"), ("de", "Deutsch"),
        ("pt", "Português"), ("it", "Italiano"), ("ru", "Русский"), ("zh", "中文（简体）"),
        ("ja", "日本語"), ("ko", "한국어"), ("ar", "العربية"), ("hi", "हिन्दी")]
    static var language: String = {
        let preferred = UserDefaults.standard.string(forKey: "language") ?? Locale.preferredLanguages.first ?? "en"
        let code = preferred.split(separator: "-").first.map(String.init) ?? "en"
        return languages.contains { $0.0 == code } ? code : "en"
    }()
    static var locale: Locale { Locale(identifier: language == "zh" ? "zh-Hans" : language) }
    static var rtl: Bool { language == "ar" }
    private static var bundle: Bundle {
        let code = language == "zh" ? "zh-Hans" : language
        return Bundle.main.path(forResource: code, ofType: "lproj").flatMap(Bundle.init(path:)) ?? .main
    }
    static func exact(_ source: String) -> String { bundle.localizedString(forKey: source, value: source, table: "Localizable") }
    private static let placeholder = try! NSRegularExpression(pattern: "%([0-9]+)")
    private static let formattedArgument = try! NSRegularExpression(pattern: "[@#]?%([0-9]+)")
    private static let patterns: [(String, NSRegularExpression, [Int])] = LocalizationKeys.templates.sorted { $0.count > $1.count }.map { source in
        let ns = source as NSString
        let matches = placeholder.matches(in: source, range: NSRange(location: 0, length: ns.length))
        var expression = "^", end = 0
        var indices: [Int] = []
        for match in matches {
            expression += NSRegularExpression.escapedPattern(for: ns.substring(with: NSRange(location: end, length: match.range.location-end))) + "(.+?)"
            indices.append(Int(ns.substring(with: match.range(at: 1)))!); end = NSMaxRange(match.range)
        }
        expression += NSRegularExpression.escapedPattern(for: ns.substring(from: end)) + "$"
        return (source, try! NSRegularExpression(pattern: expression, options: [.dotMatchesLineSeparators]), indices)
    }
    static func format(_ source: String, _ arguments: [Any]) -> String {
        let translated = exact(source), ns = translated as NSString
        var result = translated
        for match in formattedArgument.matches(in: translated, range: NSRange(location: 0, length: ns.length)).reversed() {
            let index = Int(ns.substring(with: match.range(at: 1)))!-1
            let prefix = ns.substring(with: match.range).prefix { $0 != "%" }
            if arguments.indices.contains(index), let range = Range(match.range, in: result) { result.replaceSubrange(range, with: isolate(String(prefix) + String(describing: arguments[index]))) }
        }
        return result
    }
    static func text(_ source: String) -> String {
        guard language != "en" else { return source }
        let direct = exact(source)
        if direct != source { return direct }
        let ns = source as NSString
        for (key, regex, indices) in patterns {
            guard let match = regex.firstMatch(in: source, range: NSRange(location: 0, length: ns.length)) else { continue }
            var args = Array(repeating: "", count: indices.max() ?? 0)
            for (i, index) in indices.enumerated() { args[index-1] = ns.substring(with: match.range(at: i+1)) }
            return format(key, args)
        }
        return source
    }
    static func detail(_ value: String) -> String {
        value.components(separatedBy: " · ").map { part in
            part.range(of: "^#[0-9]+$", options: .regularExpression) != nil ? isolate(part) : text(part)
        }.joined(separator: " · ")
    }
    static func isolate(_ value: String) -> String { rtl && !value.isEmpty ? "\u{2068}" + value + "\u{2069}" : value }
    static func integer(_ value: String) -> Int? { Int(value.map { $0.wholeNumberValue.map(String.init) ?? String($0) }.joined()) }
}
func L(_ source: String) -> String { L10n.text(source) }
