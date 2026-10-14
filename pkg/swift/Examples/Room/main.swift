import Foundation
import LiveKitCpp

@main
struct RoomExample {
    static func main() async throws {
        guard let url = ProcessInfo.processInfo.environment["LIVEKIT_URL"],
              let token = ProcessInfo.processInfo.environment["LIVEKIT_TOKEN"] else {
            print("Set LIVEKIT_URL and LIVEKIT_TOKEN to run this example.")
            return
        }
        try LiveKit.initialize()
        do {
            let room = try Room()
            defer { room.close() }
            room.onEvent { print("\($0)") }
            try await room.connectAsync(url: url, token: token)
            // Connection completion can precede the connected state transition.
            for _ in 0..<100 {
                if try room.isConnected { break }
                try await Task.sleep(nanoseconds: 100_000_000)
            }
            guard try room.isConnected else {
                throw LiveKitError(code: LK_STATUS_OPERATION_FAILED.rawValue, message: "Connection timed out")
            }
            print("Room: \(try room.name), identity: \(try room.localParticipant().identity)")
            try await room.publishDataAsync(Data("Hello from Swift".utf8), topic: "swift")
            try await Task.sleep(nanoseconds: 2_000_000_000)
            try await room.disconnectAsync()
        } catch {
            try? LiveKit.shutdown()
            throw error
        }
        try LiveKit.shutdown()
    }
}
