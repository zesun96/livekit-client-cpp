import Foundation

/// Native operations are serialized. Async operations retain the room until completion.
public final class Room: @unchecked Sendable {
    private let lock = NSRecursiveLock()
    private let worker = DispatchQueue(label: "livekit.swift.room")
    private let events = EventContext()
    private var handle: OpaquePointer?
    private var tracks: [LocalTrack] = []
    private var retainedContexts: [AnyObject] = []

    public init() throws {
        try LiveKitError.check(lk_room_create(&handle))
        var callbacks = roomCallbacks(events)
        do { try LiveKitError.check(lk_room_set_callbacks(handle, &callbacks)) }
        catch { lk_room_destroy(handle); handle = nil; throw error }
    }

    deinit { close() }

    /// Events contain copied values. Avoid capturing the room strongly in its handler.
    /// Already queued events can still arrive after close or handler replacement.
    public func onEvent(_ handler: (@Sendable (RoomEvent) -> Void)?) { events.setHandler(handler) }

    /// Audio, video and DataTrack frame events share this bounded queue (default 16).
    /// New frames are dropped when full; capacity zero disables frame event delivery.
    public func configureMediaEvents(capacity: Int) { events.configureMedia(capacity: capacity) }
    public var droppedMediaEvents: Int { events.droppedFrames }

    /// The pointer is borrowed for this closure only. Do not destroy it or replace callbacks.
    /// Pointer fields in native options must remain alive for the duration of the call.
    public func withNativeHandle<T>(_ body: (OpaquePointer) throws -> T) throws -> T {
        lock.lock(); defer { lock.unlock() }
        guard let handle else {
            throw LiveKitError(code: LK_STATUS_INVALID_STATE.rawValue, message: "Room is closed")
        }
        return try body(handle)
    }

    /// Idempotent. Joins native callbacks before releasing their context and retained tracks.
    public func close() {
        lock.lock(); defer { lock.unlock() }
        guard let current = handle else { return }
        handle = nil
        lk_room_destroy(current)
        tracks.removeAll()
        retainedContexts.removeAll()
    }

    public func connect(url: String, token: String) throws {
        try withNativeHandle { room in
            try url.withCString { url in
                try token.withCString { token in try LiveKitError.check(lk_room_connect(room, url, token)) }
            }
        }
    }

    public func connect(url: String, token: String, options: inout lk_room_connect_options_t) throws {
        try withNativeHandle { room in
            try url.withCString { url in
                try token.withCString { token in try LiveKitError.check(lk_room_connect_with_options(room, url, token, &options)) }
            }
        }
    }

    public func disconnect() throws { try withNativeHandle { try LiveKitError.check(lk_room_disconnect($0)) } }

    /// Returns when the native connect call finishes. Observe connected events/state for readiness.
    /// Cancellation does not interrupt an in-flight native operation.
    public func connectAsync(url: String, token: String) async throws {
        try await performAsync { try self.connect(url: url, token: token) }
    }

    public func disconnectAsync() async throws { try await performAsync { try self.disconnect() } }

    func performAsync<T: Sendable>(_ operation: @escaping @Sendable () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<T, Error>) in
            worker.async {
                do { continuation.resume(returning: try operation()) }
                catch { continuation.resume(throwing: error) }
            }
        }
    }

    func retainContext(_ value: AnyObject) { retainedContexts.append(value) }

    public var state: lk_room_state_t { get throws { try withNativeHandle { lk_room_state($0) } } }
    public var isConnected: Bool { get throws { try withNativeHandle { lk_room_is_connected($0) != 0 } } }
    public var sid: String { get throws { try withNativeHandle { room in try readString { lk_room_sid(room, $0, $1) } } } }
    public var name: String { get throws { try withNativeHandle { room in try readString { lk_room_name(room, $0, $1) } } } }
    public var metadata: String { get throws { try withNativeHandle { room in try readString { lk_room_metadata(room, $0, $1) } } } }

    public func setName(_ name: String) throws {
        try withNativeHandle { room in try name.withCString { try LiveKitError.check(lk_local_participant_set_name(room, $0)) } }
    }

    public func setMetadata(_ metadata: String) throws {
        try withNativeHandle { room in try metadata.withCString { try LiveKitError.check(lk_local_participant_set_metadata(room, $0)) } }
    }

    public func publishData(_ data: Data, topic: String = "", reliable: Bool = true) throws {
        try withNativeHandle { room in
            var options = lk_data_publish_options_t()
            lk_data_publish_options_init(&options)
            options.reliable = reliable ? 1 : 0
            try topic.withCString { topic in
                options.topic = topic
                try data.withUnsafeBytes { bytes in
                    try LiveKitError.check(lk_room_publish_data(room, bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count, &options))
                }
            }
        }
    }

    public func publishDataAsync(_ data: Data, topic: String = "", reliable: Bool = true) async throws {
        try await performAsync { try self.publishData(data, topic: topic, reliable: reliable) }
    }

    public func localParticipant() throws -> Participant {
        try withNativeHandle { room in
            var snapshot: OpaquePointer?
            try LiveKitError.check(lk_room_create_local_participant_snapshot(room, &snapshot))
            defer { lk_local_participant_snapshot_destroy(snapshot) }
            return try Participant(
                sid: readString { lk_local_participant_snapshot_sid(snapshot, $0, $1) },
                identity: readString { lk_local_participant_snapshot_identity(snapshot, $0, $1) },
                name: readString { lk_local_participant_snapshot_name(snapshot, $0, $1) },
                metadata: readString { lk_local_participant_snapshot_metadata(snapshot, $0, $1) })
        }
    }

    public func remoteParticipants() throws -> [Participant] {
        try withNativeHandle { room in
            var list: OpaquePointer?
            try LiveKitError.check(lk_room_create_remote_participant_snapshot(room, &list))
            defer { lk_remote_participant_list_destroy(list) }
            return try (0..<lk_remote_participant_list_count(list)).map { index in
                var snapshot: OpaquePointer?
                try LiveKitError.check(lk_remote_participant_list_at(list, index, &snapshot))
                return try Participant(
                    sid: readString { lk_remote_participant_snapshot_sid(snapshot, $0, $1) },
                    identity: readString { lk_remote_participant_snapshot_identity(snapshot, $0, $1) },
                    name: readString { lk_remote_participant_snapshot_name(snapshot, $0, $1) },
                    metadata: readString { lk_remote_participant_snapshot_metadata(snapshot, $0, $1) })
            }
        }
    }

    public func createAudioTrack(label: String, source: AudioSource) throws -> LocalTrack {
        try withNativeHandle { room in
            var track: OpaquePointer?
            try label.withCString { try LiveKitError.check(lk_room_create_audio_track(room, $0, source.handle, &track)) }
            let result = LocalTrack(handle: track!, source: source)
            tracks.append(result)
            return result
        }
    }

    public func createVideoTrack(label: String, source: VideoSource) throws -> LocalTrack {
        try withNativeHandle { room in
            var track: OpaquePointer?
            try label.withCString { try LiveKitError.check(lk_room_create_video_track(room, $0, source.handle, &track)) }
            let result = LocalTrack(handle: track!, source: source)
            tracks.append(result)
            return result
        }
    }

    public func publish(_ track: LocalTrack, options: inout lk_track_publish_options_t) throws {
        try withNativeHandle { room in
            guard tracks.contains(where: { $0 === track }) else {
                throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Track belongs to another room")
            }
            try LiveKitError.check(lk_local_track_publish(room, track.handle, &options))
        }
    }

    public func republishTracks() throws { try withNativeHandle { try LiveKitError.check(lk_room_republish_all_tracks($0)) } }

    public func publishScreenShare(_ track: LocalTrack, video: Bool, options: inout lk_track_publish_options_t) throws {
        try withNativeHandle { room in
            guard tracks.contains(where: { $0 === track }) else {
                throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Track belongs to another room")
            }
            try LiveKitError.check(video ? lk_local_track_publish_screen_share_video(room, track.handle, &options)
                                        : lk_local_track_publish_screen_share_audio(room, track.handle, &options))
        }
    }
}
