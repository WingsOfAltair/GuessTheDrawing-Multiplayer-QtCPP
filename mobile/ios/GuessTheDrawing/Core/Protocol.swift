import Foundation
import CommonCrypto
import CryptoKit

typealias JSON = [String: Any]
extension Dictionary where Key == String, Value == Any {
    func string(_ key: String, _ fallback: String = "") -> String { self[key] as? String ?? fallback }
    func int(_ key: String, _ fallback: Int = 0) -> Int { (self[key] as? NSNumber)?.intValue ?? fallback }
    func number(_ key: String, _ fallback: Double = 0) -> Double { (self[key] as? NSNumber)?.doubleValue ?? fallback }
    func bool(_ key: String, _ fallback: Bool = false) -> Bool { self[key] as? Bool ?? fallback }
    func object(_ key: String) -> JSON { self[key] as? JSON ?? [:] }
    func objects(_ key: String) -> [JSON] { self[key] as? [JSON] ?? [] }
}
enum WireError: LocalizedError {
    case invalidFrame, oversized, invalidChallenge
    var errorDescription: String? {
        switch self {
        case .invalidFrame: return "The server sent an invalid protocol message."
        case .oversized: return "Server frame exceeds 1 MiB."
        case .invalidChallenge: return "The private room challenge is invalid."
        }
    }
}
enum Wire {
    static let maxFrame = 1024 * 1024
    static var appVersion: String { Bundle.main.object(forInfoDictionaryKey: "GTDGameVersion") as? String ?? "1.0.0" }
    static func message(_ type: String, _ fields: JSON = [:], version: String = appVersion) -> JSON {
        var result = fields
        result["type"] = type; result["version"] = 1; result["appVersion"] = version
        return result
    }
    static func encode(_ message: JSON) throws -> Data {
        var data = try JSONSerialization.data(withJSONObject: message, options: [.sortedKeys])
        guard data.count <= maxFrame else { throw WireError.oversized }
        data.append(10)
        return data
    }
    static func passkeyProof(password: String, salt: String, nonce: String) throws -> String {
        guard let saltData = Data(base64Encoded: salt), (16...96).contains(saltData.count),
              let nonceData = Data(base64Encoded: nonce), nonceData.count == 32 else { throw WireError.invalidChallenge }
        let passwordData = Array(password.utf8)
        var key = [UInt8](repeating: 0, count: 32)
        let result = passwordData.withUnsafeBytes { passwordBytes in
            saltData.withUnsafeBytes { saltBytes in
                CCKeyDerivationPBKDF(CCPBKDFAlgorithm(kCCPBKDF2),
                    passwordBytes.bindMemory(to: Int8.self).baseAddress, passwordData.count,
                    saltBytes.bindMemory(to: UInt8.self).baseAddress, saltData.count,
                    CCPseudoRandomAlgorithm(kCCPRFHmacAlgSHA256), 210_000, &key, 32)
            }
        }
        guard result == kCCSuccess else { throw WireError.invalidChallenge }
        defer { key.withUnsafeMutableBytes { $0.initializeMemory(as: UInt8.self, repeating: 0) } }
        return Data(HMAC<SHA256>.authenticationCode(for: nonceData, using: SymmetricKey(data: key))).base64EncodedString()
    }
}
struct FrameDecoder {
    private var pending = Data()
    mutating func feed(_ incoming: Data) throws -> [JSON] {
        var frames: [JSON] = []
        for byte in incoming {
            if byte == 10 {
                defer { pending.removeAll(keepingCapacity: true) }
                if pending.allSatisfy({ $0 == 32 || $0 == 13 || $0 == 9 }) { continue }
                guard String(data: pending, encoding: .utf8) != nil,
                      let message = try JSONSerialization.jsonObject(with: pending) as? JSON,
                      message.int("version", -1) == 1, (1...64).contains(message.string("type").count)
                else { throw WireError.invalidFrame }
                frames.append(message)
            } else {
                guard pending.count < Wire.maxFrame else { throw WireError.oversized }
                pending.append(byte)
            }
        }
        return frames
    }
}
