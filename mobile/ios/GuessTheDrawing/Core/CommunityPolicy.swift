import Foundation

enum CommunityPolicy {
    static func level(_ profile: JSON) -> Int {
        if profile.bool("masterSuperAdmin") { return 3 }
        switch profile.string("serverRole", "player") { case "admin": return 2; case "moderator": return 1; default: return 0 }
    }
    static let durationUnits: [(String, Int)] = [("Seconds", 1), ("Minutes", 60), ("Hours", 3600), ("Days", 86400),
        ("Weeks", 604800), ("Months (30 days)", 2592000), ("Years (365 days)", 31536000)]
}
