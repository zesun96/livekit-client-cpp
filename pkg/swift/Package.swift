// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "LiveKitCpp",
    products: [
        .library(name: "LiveKitCpp", targets: ["LiveKitCpp"]),
        .library(name: "CLiveKit", targets: ["CLiveKit"]),
        .executable(name: "livekit-room", targets: ["RoomExample"]),
        .executable(name: "livekit-features", targets: ["FeatureExamples"])
    ],
    targets: [
        .systemLibrary(name: "CLiveKit"),
        .target(name: "LiveKitCpp", dependencies: ["CLiveKit"], linkerSettings: [.linkedLibrary("livekitclient")]),
        .executableTarget(name: "RoomExample", dependencies: ["LiveKitCpp"], path: "Examples/Room"),
        .executableTarget(name: "FeatureExamples", dependencies: ["LiveKitCpp"], path: "Examples/Features"),
        .testTarget(name: "LiveKitCppTests", dependencies: ["LiveKitCpp"])
    ]
)
