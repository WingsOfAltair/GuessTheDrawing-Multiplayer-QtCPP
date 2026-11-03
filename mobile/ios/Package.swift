// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "GTDCore",
    platforms: [.macOS(.v13), .iOS(.v16)],
    products: [.library(name: "GTDCore", targets: ["GTDCore"])],
    targets: [
        .target(name: "GTDCore", path: "GuessTheDrawing/Core"),
        .testTarget(name: "GTDCoreTests", dependencies: ["GTDCore"], path: "Tests")
    ]
)
