import Foundation
import XCTest
import LiveKitCpp

/// Opt-in only; does not start a server or generate/use credentials automatically.
final class IntegrationTests: XCTestCase {
    func testTwoParticipantDataAndRPC() throws {
        let environment = ProcessInfo.processInfo.environment
        guard environment["LIVEKIT_SWIFT_INTEGRATION"] == "1" else {
            throw XCTSkip("Set LIVEKIT_SWIFT_INTEGRATION=1 to enable server integration tests")
        }
        guard let url = environment["LIVEKIT_URL"], let firstToken = environment["LIVEKIT_TOKEN_A"],
              let secondToken = environment["LIVEKIT_TOKEN_B"] else {
            throw XCTSkip("Set LIVEKIT_URL and LIVEKIT_TOKEN_A/B for two identities in the same room")
        }
        try LiveKit.initialize()
        defer { try? LiveKit.shutdown() }
        try runPeers(url: url, firstToken: firstToken, secondToken: secondToken)
    }

    private func runPeers(url: String, firstToken: String, secondToken: String) throws {
        let first = try Room(); let second = try Room()
        defer { first.close(); second.close() }
        let received = expectation(description: "Peer receives copied data")
        second.onEvent { event in
            if case let .data(data, topic, _, _) = event, topic == "swift-integration" {
                XCTAssertEqual(data, Data([1, 0, 2]))
                received.fulfill()
            }
        }
        try second.registerRPC(method: "swift.echo") { RPCResponse(payload: $0.payload) }
        try first.connect(url: url, token: firstToken)
        try second.connect(url: url, token: secondToken)
        let deadline = Date().addingTimeInterval(10)
        while Date() < deadline {
            if try first.isConnected && second.isConnected && !first.remoteParticipants().isEmpty { break }
            Thread.sleep(forTimeInterval: 0.01)
        }
        XCTAssertTrue(try first.isConnected); XCTAssertTrue(try second.isConnected)
        try first.publishData(Data([1, 0, 2]), topic: "swift-integration")
        wait(for: [received], timeout: 10)
        let identity = try second.localParticipant().identity
        let response = try first.performRPC(destination: identity, method: "swift.echo", payload: "Swift RPC")
        XCTAssertTrue(response.isSuccess); XCTAssertEqual(response.payload, "Swift RPC")
        let snapshot = try first.remoteSnapshots().first
        try first.disconnect(); try second.disconnect()
        first.close(); second.close()
        XCTAssertEqual(try snapshot?.participant.identity, identity)
    }
}
