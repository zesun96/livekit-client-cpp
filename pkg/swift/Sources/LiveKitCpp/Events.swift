import Foundation

public struct Participant: Sendable, Equatable {
    public let sid: String
    public let identity: String
    public let name: String
    public let metadata: String

    init(_ value: lk_participant_info_t) {
        sid = copyString(value.sid)
        identity = copyString(value.identity)
        name = copyString(value.name)
        metadata = copyString(value.metadata)
    }

    init(sid: String, identity: String, name: String, metadata: String) {
        self.sid = sid; self.identity = identity; self.name = name; self.metadata = metadata
    }
}

public struct TrackPublication: Sendable {
    public let sid: String
    public let name: String
    public let mimeType: String
    public let kind: Int32
    public let source: Int32
    public let width: UInt32
    public let height: UInt32
    public let isMuted: Bool
    init(_ value: lk_track_publication_info_t) {
        sid = copyString(value.sid); name = copyString(value.name); mimeType = copyString(value.mime_type)
        kind = Int32(truncatingIfNeeded: value.kind.rawValue); source = Int32(truncatingIfNeeded: value.source.rawValue)
        width = value.width; height = value.height; isMuted = value.is_muted != 0
    }
}

public enum TrackEventKind: Sendable {
    case published, unpublished, muted, unmuted, subscribed, unsubscribed
    case localPublished, localUnpublished, localSubscribed
}

public enum RoomEvent: Sendable {
    case connected
    case disconnected(reason: Int32)
    case reconnecting
    case reconnected
    case stateChanged(Int32)
    case participantConnected(Participant)
    case participantDisconnected(Participant)
    case metadataChanged(String)
    case data(Data, topic: String, participantIdentity: String, reliable: Bool)
    case text(String, topic: String, participantIdentity: String, streamID: String)
    case bytes(Data, topic: String, participantIdentity: String, streamID: String, name: String, mimeType: String)
    case chat(id: String, message: String, timestamp: Int64, participantIdentity: String)
    case track(TrackEventKind, TrackPublication, Participant)
    case subscriptionPermission(TrackPublication, Participant, allowed: Bool)
    case subscriptionFailed(TrackPublication, Participant, error: Int32)
    case subscriptionStatus(TrackPublication, Participant, status: Int32)
    case trackStreamState(TrackPublication, Participant, state: Int32)
    case participantMetadataChanged(Participant, previous: String)
    case participantNameChanged(Participant)
    case participantAttributesChanged(Participant, changes: [String: String])
    case participantPermissionsChanged(Participant, previous: ParticipantPermissions, current: ParticipantPermissions)
    case encryptionChanged(trackID: String, participantIdentity: String, state: Int32, enabled: Bool, keyIndex: Int)
    case connectionQuality(Participant, quality: Int32)
    case activeSpeakers([Participant])
    case participantsUpdated([Participant])
    case recordingChanged(Bool)
    case tokenRefreshed
    case audioFrame(AudioFrame, TrackPublication, Participant)
    case videoFrame(VideoFrame, TrackPublication, Participant)
    case dataTrackFrame(DataTrackFrame, trackSID: String, participantIdentity: String)
    case dataTrackPublished(trackSID: String, participantIdentity: String)
    case dataTrackUnpublished(trackSID: String, participantIdentity: String)
}

func copyString(_ value: UnsafePointer<CChar>?) -> String {
    value.map { String(cString: $0) } ?? ""
}

// The room retains this context until lk_room_destroy has joined native callbacks.
// User code runs on a separate serial queue, so it can safely close the room.
final class EventContext: @unchecked Sendable {
    let queue = DispatchQueue(label: "livekit.swift.events")
    private let lock = NSLock()
    private var handler: (@Sendable (RoomEvent) -> Void)?
    private var mediaCapacity = 16
    private var pendingMedia = 0
    private var droppedMedia = 0

    func configureMedia(capacity: Int) {
        lock.lock(); defer { lock.unlock() }
        mediaCapacity = max(0, capacity)
    }

    var droppedFrames: Int {
        lock.lock(); defer { lock.unlock() }
        return droppedMedia
    }

    func setHandler(_ value: (@Sendable (RoomEvent) -> Void)?) {
        lock.lock(); defer { lock.unlock() }
        handler = value
    }

    func emit(_ event: RoomEvent, media: Bool = false) {
        lock.lock()
        let callback = handler
        guard let callback else { lock.unlock(); return }
        if media {
            guard pendingMedia < mediaCapacity else { droppedMedia += 1; lock.unlock(); return }
            pendingMedia += 1
        }
        // Enqueue under the lock to preserve admission order across native threads.
        queue.async {
            callback(event)
            if media {
                self.lock.lock(); self.pendingMedia -= 1; self.lock.unlock()
            }
        }
        lock.unlock()
    }
}

private func context(_ pointer: UnsafeMutableRawPointer?) -> EventContext? {
    pointer.map { Unmanaged<EventContext>.fromOpaque($0).takeUnretainedValue() }
}

func roomCallbacks(_ owner: EventContext) -> lk_room_callbacks_t {
    var callbacks = lk_room_callbacks_t()
    lk_room_callbacks_init(&callbacks)
    callbacks.user_data = Unmanaged.passUnretained(owner).toOpaque()
    callbacks.on_connected = { user, _ in context(user)?.emit(.connected) }
    callbacks.on_disconnected_with_reason = { user, _, reason in context(user)?.emit(.disconnected(reason: Int32(truncatingIfNeeded: reason.rawValue))) }
    callbacks.on_reconnecting = { user, _ in context(user)?.emit(.reconnecting) }
    callbacks.on_reconnected = { user, _ in context(user)?.emit(.reconnected) }
    callbacks.on_connection_state_changed = { user, _, state in context(user)?.emit(.stateChanged(Int32(truncatingIfNeeded: state.rawValue))) }
    callbacks.on_participant_connected = { user, _, value in
        if let value { context(user)?.emit(.participantConnected(Participant(value.pointee))) }
    }
    callbacks.on_participant_disconnected = { user, _, value in
        if let value { context(user)?.emit(.participantDisconnected(Participant(value.pointee))) }
    }
    callbacks.on_room_metadata_changed = { user, _, value in context(user)?.emit(.metadataChanged(copyString(value))) }
    callbacks.on_data_received = { user, _, value in
        guard let value else { return }
        let event = value.pointee
        let bytes = event.data.map { Data(bytes: $0, count: event.data_size) } ?? Data()
        context(user)?.emit(.data(bytes, topic: copyString(event.topic), participantIdentity: copyString(event.participant_identity), reliable: event.reliable != 0))
    }
    callbacks.on_text_received = { user, _, event in
        guard let event else { return }; let value = event.pointee
        context(user)?.emit(.text(copyString(value.text), topic: copyString(value.topic), participantIdentity: copyString(value.participant_identity), streamID: copyString(value.stream_id)))
    }
    callbacks.on_byte_received = { user, _, event in
        guard let event else { return }; let value = event.pointee
        context(user)?.emit(.bytes(value.data.map { Data(bytes: $0, count: value.data_size) } ?? Data(),
            topic: copyString(value.topic), participantIdentity: copyString(value.participant_identity),
            streamID: copyString(value.stream_id), name: copyString(value.name), mimeType: copyString(value.mime_type)))
    }
    callbacks.on_chat_message_received = { user, _, event in
        guard let event else { return }; let value = event.pointee
        context(user)?.emit(.chat(id: copyString(value.id), message: copyString(value.message), timestamp: value.timestamp, participantIdentity: copyString(value.participant_identity)))
    }
    callbacks.on_track_published = { user, _, track, participant in emitTrack(user, track, participant, .published) }
    callbacks.on_track_unpublished = { user, _, track, participant in emitTrack(user, track, participant, .unpublished) }
    callbacks.on_track_muted = { user, _, track, participant in emitTrack(user, track, participant, .muted) }
    callbacks.on_track_unmuted = { user, _, track, participant in emitTrack(user, track, participant, .unmuted) }
    callbacks.on_track_subscribed = { user, _, track, participant in emitTrack(user, track, participant, .subscribed) }
    callbacks.on_track_unsubscribed = { user, _, track, participant in emitTrack(user, track, participant, .unsubscribed) }
    callbacks.on_local_track_published = { user, _, track, participant in emitTrack(user, track, participant, .localPublished) }
    callbacks.on_local_track_unpublished = { user, _, track, participant in emitTrack(user, track, participant, .localUnpublished) }
    callbacks.on_local_track_subscribed = { user, _, track, participant in emitTrack(user, track, participant, .localSubscribed) }
    callbacks.on_track_subscription_permission_changed = { user, _, track, participant, allowed in
        guard let track, let participant else { return }
        context(user)?.emit(.subscriptionPermission(TrackPublication(track.pointee), Participant(participant.pointee), allowed: allowed != 0))
    }
    callbacks.on_track_subscription_failed = { user, _, track, participant, error in
        guard let track, let participant else { return }
        context(user)?.emit(.subscriptionFailed(TrackPublication(track.pointee), Participant(participant.pointee), error: Int32(truncatingIfNeeded: error.rawValue)))
    }
    callbacks.on_track_subscription_status_changed = { user, _, track, participant, status in
        guard let track, let participant else { return }
        context(user)?.emit(.subscriptionStatus(TrackPublication(track.pointee), Participant(participant.pointee), status: Int32(truncatingIfNeeded: status.rawValue)))
    }
    callbacks.on_track_stream_state_changed = { user, _, track, participant, state in
        guard let track, let participant else { return }
        context(user)?.emit(.trackStreamState(TrackPublication(track.pointee), Participant(participant.pointee), state: Int32(truncatingIfNeeded: state.rawValue)))
    }
    callbacks.on_audio_frame = { user, _, track, participant, frame in
        guard let track, let participant, let frame else { return }
        context(user)?.emit(.audioFrame(AudioFrame(frame.pointee), TrackPublication(track.pointee), Participant(participant.pointee)), media: true)
    }
    callbacks.on_video_frame_with_metadata = { user, _, track, participant, frame, metadata in
        guard let track, let participant, let frame else { return }
        context(user)?.emit(.videoFrame(VideoFrame(frame.pointee, metadata: metadata?.pointee), TrackPublication(track.pointee), Participant(participant.pointee)), media: true)
    }
    callbacks.on_data_track_frame = { user, _, track, participant, frame in
        guard let frame else { return }; let value = frame.pointee
        context(user)?.emit(.dataTrackFrame(DataTrackFrame(data: value.data.map { Data(bytes: $0, count: value.data_size) } ?? Data(),
            userTimestamp: value.has_user_timestamp != 0 ? value.user_timestamp : nil),
            trackSID: copyString(track?.pointee.sid), participantIdentity: copyString(participant?.pointee.identity)), media: true)
    }
    callbacks.on_data_track_published = { user, _, track, participant in
        context(user)?.emit(.dataTrackPublished(trackSID: copyString(track?.pointee.sid), participantIdentity: copyString(participant?.pointee.identity)))
    }
    callbacks.on_data_track_unpublished = { user, _, track, participant in
        context(user)?.emit(.dataTrackUnpublished(trackSID: copyString(track?.pointee.sid), participantIdentity: copyString(participant?.pointee.identity)))
    }
    callbacks.on_participant_metadata_changed = { user, _, previous, participant in
        if let participant { context(user)?.emit(.participantMetadataChanged(Participant(participant.pointee), previous: copyString(previous))) }
    }
    callbacks.on_participant_name_changed = { user, _, _, participant in
        if let participant { context(user)?.emit(.participantNameChanged(Participant(participant.pointee))) }
    }
    callbacks.on_participant_attributes_changed = { user, _, attributes, count, participant in
        guard let participant else { return }
        var changes: [String: String] = [:]
        if let attributes { for index in 0..<count { changes[copyString(attributes[index].key)] = copyString(attributes[index].value) } }
        context(user)?.emit(.participantAttributesChanged(Participant(participant.pointee), changes: changes))
    }
    callbacks.on_participant_permissions_changed = { user, _, previous, current, participant in
        guard let previous, let current, let participant else { return }
        context(user)?.emit(.participantPermissionsChanged(Participant(participant.pointee),
            previous: ParticipantPermissions(previous.pointee), current: ParticipantPermissions(current.pointee)))
    }
    callbacks.on_encryption_state_changed = { user, _, value in
        guard let value else { return }; let state = value.pointee
        context(user)?.emit(.encryptionChanged(trackID: copyString(state.track_id), participantIdentity: copyString(state.participant_identity),
            state: Int32(truncatingIfNeeded: state.state.rawValue), enabled: state.enabled != 0, keyIndex: state.key_index))
    }
    callbacks.on_connection_quality_changed = { user, _, quality, participant in
        if let participant { context(user)?.emit(.connectionQuality(Participant(participant.pointee), quality: Int32(truncatingIfNeeded: quality.rawValue))) }
    }
    callbacks.on_active_speakers_changed = { user, _, participants, count in
        let values = participants.map { pointer in (0..<count).map { Participant(pointer[$0]) } } ?? []
        context(user)?.emit(.activeSpeakers(values))
    }
    callbacks.on_participants_updated = { user, _, participants, count in
        let values = participants.map { pointer in (0..<count).map { Participant(pointer[$0]) } } ?? []
        context(user)?.emit(.participantsUpdated(values))
    }
    callbacks.on_recording_status_changed = { user, _, value in context(user)?.emit(.recordingChanged(value != 0)) }
    callbacks.on_token_refreshed = { user, _ in context(user)?.emit(.tokenRefreshed) }
    return callbacks
}

private func emitTrack(_ user: UnsafeMutableRawPointer?, _ track: UnsafePointer<lk_track_publication_info_t>?,
                       _ participant: UnsafePointer<lk_participant_info_t>?, _ kind: TrackEventKind) {
    guard let track, let participant else { return }
    context(user)?.emit(.track(kind, TrackPublication(track.pointee), Participant(participant.pointee)))
}
