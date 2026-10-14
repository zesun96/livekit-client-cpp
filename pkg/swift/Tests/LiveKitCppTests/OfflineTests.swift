import Foundation
import XCTest
@testable import LiveKitCpp

final class OfflineTests: XCTestCase {
    func testStringGetterRetriesGrowth() throws {
        var calls = 0
        let value = try readString { buffer, capacity in
            calls += 1
            if buffer == nil { return 2 }
            if capacity < 6 { return 6 }
            for (index, byte) in "hello\0".utf8.enumerated() { buffer![index] = CChar(byte) }
            return 6
        }
        XCTAssertEqual(value, "hello")
        XCTAssertEqual(calls, 3)
    }

    func testNativeLifecycleAndClosedRoom() throws {
        try LiveKit.initialize()
        defer { try? LiveKit.shutdown() }
        XCTAssertFalse(try LiveKit.version().isEmpty)
        let room = try Room()
        XCTAssertFalse(try room.isConnected)
        XCTAssertEqual(try room.remoteParticipants(), [])
        room.close()
        room.close()
        XCTAssertThrowsError(try room.disconnect()) { error in
            XCTAssertEqual((error as? LiveKitError)?.code, LK_STATUS_INVALID_STATE.rawValue)
        }
    }

    func testAsyncFailureAfterClose() async throws {
        try LiveKit.initialize()
        defer { try? LiveKit.shutdown() }
        let room = try Room()
        room.close()
        do {
            try await room.connectAsync(url: "ws://localhost:1", token: "test")
            XCTFail("Closed room should reject connect")
        } catch {
            XCTAssertEqual((error as? LiveKitError)?.code, LK_STATUS_INVALID_STATE.rawValue)
        }
    }

    func testSourcesAndNativeFailurePropagation() throws {
        try LiveKit.initialize()
        defer { try? LiveKit.shutdown() }
        do {
            let room = try Room()
            defer { room.close() }
            let audio = try AudioSource(channels: 2)
            XCTAssertThrowsError(try audio.capture([1, 2, 3]))
            try audio.capture([Int16](repeating: 0, count: 960))
            // The native track factory is unavailable before room connection.
            XCTAssertThrowsError(try room.createAudioTrack(label: "audio", source: audio)) { error in
                XCTAssertFalse((error as? LiveKitError)?.message.isEmpty ?? true)
            }
            let video = try VideoSource()
            var frame = lk_video_frame_input_t()
            lk_video_frame_input_init(&frame)
            XCTAssertThrowsError(try video.capture(&frame))
        }
    }

    func testCallbackCopiesBorrowedBytes() {
        let context = EventContext()
        let delivered = expectation(description: "Copied data arrives")
        context.setHandler { event in
            if case let .data(bytes, topic, identity, reliable) = event {
                XCTAssertEqual(bytes, Data([1, 2, 3]))
                XCTAssertEqual(topic, "topic")
                XCTAssertEqual(identity, "peer")
                XCTAssertTrue(reliable)
                delivered.fulfill()
            }
        }
        let callbacks = roomCallbacks(context)
        var bytes: [UInt8] = [1, 2, 3]
        bytes.withUnsafeBufferPointer { data in
            "topic".withCString { topic in
                "peer".withCString { identity in
                    var event = lk_data_received_t(data: data.baseAddress, data_size: data.count, topic: topic, participant_identity: identity, reliable: 1)
                    callbacks.on_data_received?(callbacks.user_data, nil, &event)
                }
            }
        }
        bytes = [9, 9, 9]
        wait(for: [delivered], timeout: 2)
    }
}
