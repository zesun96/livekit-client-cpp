import Foundation

public struct ParticipantPermissions: Sendable {
    public let canSubscribe: Bool
    public let canPublish: Bool
    public let canPublishData: Bool
    public let publishSources: [lk_track_source_t]
    public let hidden: Bool
    public let recorder: Bool
    public let canUpdateMetadata: Bool
    public let agent: Bool
    public let canSubscribeMetrics: Bool
    public let canManageAgentSession: Bool

    init(_ value: lk_participant_permissions_t) {
        canSubscribe = value.can_subscribe != 0; canPublish = value.can_publish != 0
        canPublishData = value.can_publish_data != 0
        publishSources = value.can_publish_sources.map { Array(UnsafeBufferPointer(start: $0, count: value.can_publish_source_count)) } ?? []
        hidden = value.hidden != 0; recorder = value.recorder != 0
        canUpdateMetadata = value.can_update_metadata != 0; agent = value.agent != 0
        canSubscribeMetrics = value.can_subscribe_metrics != 0
        canManageAgentSession = value.can_manage_agent_session != 0
    }
}

public final class LocalParticipantSnapshot: NativeObject {
    public var participant: Participant { get throws {
        try Participant(sid: readString { lk_local_participant_snapshot_sid(handle, $0, $1) },
            identity: readString { lk_local_participant_snapshot_identity(handle, $0, $1) },
            name: readString { lk_local_participant_snapshot_name(handle, $0, $1) },
            metadata: readString { lk_local_participant_snapshot_metadata(handle, $0, $1) })
    } }
    public var permissions: ParticipantPermissions { get throws {
        var value = lk_participant_permissions_t()
        try LiveKitError.check(lk_local_participant_snapshot_permissions(handle, &value)); return ParticipantPermissions(value)
    } }
    public var attributes: [String: String] { get throws {
        var result: [String: String] = [:]
        for index in 0..<lk_local_participant_snapshot_attribute_count(handle) {
            let key = try readString { lk_local_participant_snapshot_attribute_key(handle, index, $0, $1) }
            result[key] = try readString { lk_local_participant_snapshot_attribute_value(handle, index, $0, $1) }
        }
        return result
    } }
}

public final class RemoteParticipantSnapshot: NativeObject {
    public var participant: Participant { get throws {
        try Participant(sid: readString { lk_remote_participant_snapshot_sid(handle, $0, $1) },
            identity: readString { lk_remote_participant_snapshot_identity(handle, $0, $1) },
            name: readString { lk_remote_participant_snapshot_name(handle, $0, $1) },
            metadata: readString { lk_remote_participant_snapshot_metadata(handle, $0, $1) })
    } }
    public var permissions: ParticipantPermissions { get throws {
        var value = lk_participant_permissions_t()
        try LiveKitError.check(lk_remote_participant_snapshot_permissions(handle, &value)); return ParticipantPermissions(value)
    } }
    public var attributes: [String: String] { get throws {
        var result: [String: String] = [:]
        for index in 0..<lk_remote_participant_snapshot_attribute_count(handle) {
            let key = try readString { lk_remote_participant_snapshot_attribute_key(handle, index, $0, $1) }
            result[key] = try readString { lk_remote_participant_snapshot_attribute_value(handle, index, $0, $1) }
        }
        return result
    } }
    public func publications() throws -> [RemotePublicationSnapshot] {
        try (0..<lk_remote_participant_snapshot_publication_count(handle)).map { index in
            var value: OpaquePointer?
            try LiveKitError.check(lk_remote_participant_snapshot_publication_at(handle, index, &value))
            return RemotePublicationSnapshot(try requireHandle(value), owner: self)
        }
    }
}

public final class RemotePublicationSnapshot: NativeObject {
    public var sid: String { get throws { try readString { lk_remote_track_publication_snapshot_sid(handle, $0, $1) } } }
    public var name: String { get throws { try readString { lk_remote_track_publication_snapshot_name(handle, $0, $1) } } }
    public var mimeType: String { get throws { try readString { lk_remote_track_publication_snapshot_mime_type(handle, $0, $1) } } }
    public var info: lk_remote_track_publication_snapshot_info_t { get throws {
        var value = lk_remote_track_publication_snapshot_info_t(); lk_remote_track_publication_snapshot_info_init(&value)
        try LiveKitError.check(lk_remote_track_publication_snapshot_info(handle, &value)); return value
    } }
    public func track() throws -> RemoteTrackSnapshot? {
        var value: OpaquePointer?
        try LiveKitError.check(lk_remote_track_publication_snapshot_track(handle, &value))
        return value.map { RemoteTrackSnapshot($0, owner: self) }
    }
}

public final class RemoteTrackSnapshot: NativeObject {
    public var sid: String { get throws { try readString { lk_remote_track_snapshot_sid(handle, $0, $1) } } }
    public var name: String { get throws { try readString { lk_remote_track_snapshot_name(handle, $0, $1) } } }
    public var info: lk_remote_track_snapshot_info_t { get throws {
        var value = lk_remote_track_snapshot_info_t(); lk_remote_track_snapshot_info_init(&value)
        try LiveKitError.check(lk_remote_track_snapshot_info(handle, &value)); return value
    } }
    public func rtcStats() throws -> RTCStatsSnapshot {
        var value: OpaquePointer?
        try LiveKitError.check(lk_remote_track_snapshot_create_rtc_stats_snapshot(handle, &value))
        return RTCStatsSnapshot(try requireHandle(value), destroy: lk_rtc_stats_snapshot_destroy)
    }
}

public struct RTCStats {
    public let info: lk_rtc_track_stats_t
    public let id: String
    public let kind: String
    public let rid: String
    public let codecMimeType: String
    public let codecImplementation: String
    public let qualityLimitationReason: String
}

public final class RTCStatsSnapshot: NativeObject {
    public func values() throws -> [RTCStats] {
        try (0..<lk_rtc_stats_snapshot_count(handle)).map { index in
            var value = lk_rtc_track_stats_t(); lk_rtc_track_stats_init(&value)
            try LiveKitError.check(lk_rtc_stats_snapshot_info(handle, index, &value))
            return try RTCStats(info: value,
                id: readString { lk_rtc_stats_snapshot_id(handle, index, $0, $1) },
                kind: readString { lk_rtc_stats_snapshot_kind(handle, index, $0, $1) },
                rid: readString { lk_rtc_stats_snapshot_rid(handle, index, $0, $1) },
                codecMimeType: readString { lk_rtc_stats_snapshot_codec_mime_type(handle, index, $0, $1) },
                codecImplementation: readString { lk_rtc_stats_snapshot_codec_implementation(handle, index, $0, $1) },
                qualityLimitationReason: readString { lk_rtc_stats_snapshot_quality_limitation_reason(handle, index, $0, $1) })
        }
    }
}

public final class RemoteDataTrackSnapshot: NativeObject {
    public var sid: String { get throws { try readString { lk_remote_data_track_snapshot_sid(handle, $0, $1) } } }
    public var name: String { get throws { try readString { lk_remote_data_track_snapshot_name(handle, $0, $1) } } }
    public var participantIdentity: String { get throws { try readString { lk_remote_data_track_snapshot_publisher_identity(handle, $0, $1) } } }
    public var info: lk_data_track_snapshot_info_t { get throws {
        var value = lk_data_track_snapshot_info_t(); lk_data_track_snapshot_info_init(&value)
        try LiveKitError.check(lk_remote_data_track_snapshot_info(handle, &value)); return value
    } }
}

extension LocalTrack {
    public func rtcStats() throws -> RTCStatsSnapshot {
        var value: OpaquePointer?
        try LiveKitError.check(lk_local_track_create_rtc_stats_snapshot(handle, &value))
        return RTCStatsSnapshot(try requireHandle(value), destroy: lk_rtc_stats_snapshot_destroy)
    }
    public func updateEncoding(_ encoding: inout lk_video_encoding_t, backup: Bool = false) throws {
        try LiveKitError.check(lk_local_video_track_update_encoding(handle, &encoding, backup ? 1 : 0))
    }
    public func updateDegradationPreference(_ preference: lk_video_degradation_preference_t) throws {
        try LiveKitError.check(lk_local_video_track_update_degradation_preference(handle, preference))
    }
}

extension Room {
    public func localSnapshot() throws -> LocalParticipantSnapshot {
        try withNativeHandle { room in
            var value: OpaquePointer?
            try LiveKitError.check(lk_room_create_local_participant_snapshot(room, &value))
            return LocalParticipantSnapshot(try requireHandle(value), destroy: lk_local_participant_snapshot_destroy)
        }
    }
    public func remoteSnapshots() throws -> [RemoteParticipantSnapshot] {
        try withNativeHandle { room in
            var list: OpaquePointer?
            try LiveKitError.check(lk_room_create_remote_participant_snapshot(room, &list))
            let owner = NativeObject(try requireHandle(list), destroy: lk_remote_participant_list_destroy)
            return try (0..<lk_remote_participant_list_count(list)).map { index in
                var value: OpaquePointer?
                try LiveKitError.check(lk_remote_participant_list_at(list, index, &value))
                return RemoteParticipantSnapshot(try requireHandle(value), owner: owner)
            }
        }
    }
    public func remoteDataTracks() throws -> [RemoteDataTrackSnapshot] {
        try withNativeHandle { room in
            var list: OpaquePointer?
            try LiveKitError.check(lk_room_create_remote_data_track_snapshot(room, &list))
            let owner = NativeObject(try requireHandle(list), destroy: lk_remote_data_track_list_destroy)
            return try (0..<lk_remote_data_track_list_count(list)).map { index in
                var value: OpaquePointer?
                try LiveKitError.check(lk_remote_data_track_list_at(list, index, &value))
                return RemoteDataTrackSnapshot(try requireHandle(value), owner: owner)
            }
        }
    }
    public func setAttributes(_ values: [String: String]) throws {
        try withNativeHandle { room in
            try withAttributes(values) { try LiveKitError.check(lk_local_participant_set_attributes(room, $0, $1)) }
        }
    }
    public func setSubscribed(participantSID: String, trackSID: String, subscribed: Bool) throws {
        try withNativeHandle { room in
            try withStrings([participantSID, trackSID]) { strings, _ in
                try LiveKitError.check(lk_room_set_remote_track_subscribed(room, strings![0], strings![1], subscribed ? 1 : 0))
            }
        }
    }
    public func updateTrackSettings(participantSID: String, trackSID: String, settings: inout lk_remote_track_settings_t) throws {
        try withNativeHandle { room in
            try withStrings([participantSID, trackSID]) { strings, _ in
                try LiveKitError.check(lk_room_update_remote_track_settings(room, strings![0], strings![1], &settings))
            }
        }
    }
    public func setSubscriptionPermissions(allAllowed: Bool, permissions: [lk_participant_track_permission_t]) throws {
        try withNativeHandle { room in
            try permissions.withUnsafeBufferPointer { try LiveKitError.check(lk_room_set_track_subscription_permissions(room, allAllowed ? 1 : 0, $0.baseAddress, $0.count)) }
        }
    }
}
