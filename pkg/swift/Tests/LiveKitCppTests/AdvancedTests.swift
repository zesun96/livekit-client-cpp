import Foundation
import XCTest
@testable import LiveKitCpp

final class AdvancedTests: XCTestCase {
    func testBorrowedSnapshotsRetainOwner() {
        var releases = 0
        var parent: NativeObject? = NativeObject(OpaquePointer(bitPattern: 1)!, destroy: { _ in releases += 1 })
        var child: NativeObject? = NativeObject(OpaquePointer(bitPattern: 2)!, owner: parent)
        parent = nil
        XCTAssertEqual(releases, 0)
        XCTAssertEqual(child?.withNativeHandle { UInt(bitPattern: $0) }, 2)
        child = nil
        XCTAssertEqual(releases, 1)
    }

    func testCallbackStringPointersSurviveOtherResponses() {
        let pool = CallbackStrings()
        let first = pool.pointer("first")
        let second = pool.pointer("second")
        XCTAssertEqual(String(cString: first), "first")
        XCTAssertEqual(String(cString: second), "second")
        XCTAssertEqual(pool.pointer("first"), first)
    }

    func testAttributesPreserveUnicodeAndEmptyValues() throws {
        let attributes = ["名字": "Swift 房间", "remove": ""]
        try withAttributes(attributes) { values, count in
            XCTAssertEqual(copyAttributes(values, count), attributes)
            try LiveKitError.check(LK_STATUS_OK)
        }
    }

    func testFrameQueueDropsNewFramesAtCapacity() {
        let context = EventContext()
        context.configureMedia(capacity: 1)
        let firstEntered = expectation(description: "First handler entered")
        let released = DispatchSemaphore(value: 0)
        let drained = expectation(description: "Queue drained")
        context.setHandler { _ in
            firstEntered.fulfill()
            _ = released.wait(timeout: .now() + 5)
        }
        context.emit(.dataTrackFrame(DataTrackFrame(data: Data([1]), userTimestamp: nil), trackSID: "track", participantIdentity: "peer"), media: true)
        wait(for: [firstEntered], timeout: 2)
        context.emit(.dataTrackFrame(DataTrackFrame(data: Data([2]), userTimestamp: nil), trackSID: "track", participantIdentity: "peer"), media: true)
        XCTAssertEqual(context.droppedFrames, 1)
        released.signal()
        context.queue.async { drained.fulfill() }
        wait(for: [drained], timeout: 2)
    }

    func testVideoMetadataIsCopiedBeforeBorrowExpires() {
        var frameBytes: [UInt8] = [1, 2, 3, 4]
        var userBytes: [UInt8] = [5, 6]
        var copy: VideoFrame?
        frameBytes.withUnsafeBufferPointer { bytes in
            userBytes.withUnsafeBufferPointer { user in
                let frame = lk_video_frame_t(data: bytes.baseAddress, data_size: bytes.count, width: 2, height: 2, timestamp_us: 123)
                var metadata = lk_video_frame_metadata_t(); lk_video_frame_metadata_init(&metadata)
                metadata.has_user_timestamp_us = 1; metadata.user_timestamp_us = 456
                metadata.has_frame_id = 1; metadata.frame_id = 7
                metadata.has_user_data = 1; metadata.user_data = user.baseAddress; metadata.user_data_size = user.count
                copy = VideoFrame(frame, metadata: metadata)
            }
        }
        frameBytes = [9]; userBytes = [9]
        XCTAssertEqual(copy?.data, Data([1, 2, 3, 4]))
        XCTAssertEqual(copy?.userData, Data([5, 6]))
        XCTAssertEqual(copy?.frameID, 7)
        XCTAssertEqual(copy?.userTimestampMicroseconds, 456)
    }

    func testLocalSnapshotSurvivesRoomDestruction() throws {
        try LiveKit.initialize()
        defer { try? LiveKit.shutdown() }
        do {
            let room = try Room()
            let snapshot = try room.localSnapshot()
            let before = try snapshot.participant
            room.close()
            XCTAssertEqual(try snapshot.participant, before)
            XCTAssertEqual(try snapshot.attributes, [:])
            _ = try snapshot.permissions
        }
    }

    func testRoomRetainsRegisteredCallbacksUntilClose() throws {
        final class Lifetime {}
        try LiveKit.initialize()
        defer { try? LiveKit.shutdown() }
        let room = try Room()
        weak var weakValue: Lifetime?
        do {
            let value = Lifetime(); weakValue = value
            try room.registerRPC(method: "echo") { _ in
                _ = value
                return RPCResponse(payload: "echo")
            }
            try room.unregisterRPC(method: "echo")
            try room.registerTextStream(topic: "text") { _ in _ = value }
            try room.unregisterTextStream(topic: "text")
            try room.registerByteStream(topic: "bytes") { _ in _ = value }
            try room.unregisterByteStream(topic: "bytes")
        }
        XCTAssertNotNil(weakValue)
        room.close()
        XCTAssertNil(weakValue)
    }

    func testClosedRoomRejectsAdvancedOperations() throws {
        try LiveKit.initialize()
        defer { try? LiveKit.shutdown() }
        let room = try Room(); room.close()
        XCTAssertThrowsError(try room.sendText("hello"))
        XCTAssertThrowsError(try room.streamBytes(topic: "bytes"))
        XCTAssertThrowsError(try room.performRPC(destination: "peer", method: "echo", payload: "hello"))
        XCTAssertThrowsError(try room.setEncryptionKey(Data([1])))
        XCTAssertThrowsError(try room.audioStream(participant: "peer", trackSID: "track"))
    }

    func testStreamEventCopiesEmbeddedNULAndAttributes() {
        var bytes: [UInt8] = [65, 0, 66]
        var copy: StreamEvent?
        bytes.withUnsafeBufferPointer { buffer in
            withAttributes(["key": "value"]) { values, count in
                var event = lk_byte_stream_event_t()
                event.content = buffer.baseAddress; event.content_size = buffer.count
                event.attributes = values; event.attribute_count = count
                event.has_total_size = 1; event.total_size = 3
                copy = StreamEvent(event)
            }
        }
        bytes = [9]
        XCTAssertEqual(copy?.content, Data([65, 0, 66]))
        XCTAssertEqual(copy?.attributes, ["key": "value"])
        XCTAssertEqual(copy?.totalSize, 3)
    }

    func testDiagnosticsCanUnregisterWithoutInitialization() throws {
        try Diagnostics.onLog { _ in }
        try Diagnostics.onLog(nil)
        try Diagnostics.onTrace { _ in }
        try Diagnostics.stopTrace()
    }
}
