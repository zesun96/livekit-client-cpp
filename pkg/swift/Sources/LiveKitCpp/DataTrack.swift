import Foundation

public final class DataTrackSchema: NativeObject {
    public var name: String { get throws { try readString { lk_data_track_schema_name(handle, $0, $1) } } }
    public var encoding: lk_data_track_schema_encoding_t { lk_data_track_schema_encoding(handle) }
    public var definition: Data { get throws { try readBytes { lk_data_track_schema_definition(handle, $0, $1) } } }
}

public final class LocalDataTrack: NativeObject {
    public var sid: String { get throws { try readString { lk_local_data_track_sid(handle, $0, $1) } } }
    public var name: String { get throws { try readString { lk_local_data_track_name(handle, $0, $1) } } }
    public var isPublished: Bool { lk_local_data_track_is_published(handle) != 0 }
    public func push(_ data: Data, userTimestamp: UInt64? = nil) throws {
        try data.withUnsafeBytes {
            try checkDataTrack(lk_local_data_track_try_push(handle, $0.bindMemory(to: UInt8.self).baseAddress, $0.count, userTimestamp == nil ? 0 : 1, userTimestamp ?? 0))
        }
    }
    public func unpublish() throws { try checkDataTrack(lk_local_data_track_unpublish(handle)) }
}

public struct DataTrackFrame: Sendable {
    public let data: Data
    public let userTimestamp: UInt64?
}

public final class DataTrackReader: NativeObject {
    public func close() { lk_data_track_reader_close(handle) }
    public var isClosed: Bool { lk_data_track_reader_is_closed(handle) != 0 }
    public var droppedFrames: Int { lk_data_track_reader_dropped_frames(handle) }
    public func read(timeoutMilliseconds: UInt32 = 0) throws -> FrameRead<DataTrackFrame> {
        var frame: OpaquePointer?
        switch lk_data_track_reader_read_for(handle, timeoutMilliseconds, &frame) {
        case LK_DATA_TRACK_READ_EMPTY: return .empty
        case LK_DATA_TRACK_READ_CLOSED: return .closed
        case LK_DATA_TRACK_READ_FRAME:
            defer { lk_data_track_frame_destroy(frame) }
            _ = try requireHandle(frame)
            return .frame(try DataTrackFrame(data: readBytes { lk_data_track_frame_data(frame, $0, $1) },
                userTimestamp: lk_data_track_frame_has_user_timestamp(frame) != 0 ? lk_data_track_frame_user_timestamp(frame) : nil))
        default: throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Invalid DataTrack reader")
        }
    }
}

extension Room {
    public func storeDataTrackSchema(id: inout lk_data_track_schema_id_t, definition: Data) throws {
        try withNativeHandle { room in
            try definition.withUnsafeBytes { try checkDataTrack(lk_room_store_data_track_schema(room, &id, $0.bindMemory(to: UInt8.self).baseAddress, $0.count)) }
        }
    }

    public func dataTrackSchema(participant: String, id: inout lk_data_track_schema_id_t) throws -> DataTrackSchema {
        try withNativeHandle { room in
            var schema: OpaquePointer?
            try participant.withCString { try checkDataTrack(lk_room_get_data_track_schema(room, $0, &id, &schema)) }
            return DataTrackSchema(try requireHandle(schema), destroy: lk_data_track_schema_destroy)
        }
    }

    public func publishDataTrack(options: inout lk_data_track_publish_options_t) throws -> LocalDataTrack {
        try withNativeHandle { room in
            var track: OpaquePointer?
            try checkDataTrack(lk_room_publish_data_track(room, &options, &track))
            let value = LocalDataTrack(try requireHandle(track), destroy: { _ = lk_local_data_track_destroy($0) })
            retainContext(value)
            return value
        }
    }

    public func subscribeDataTrack(participant: String, trackSID: String, options: inout lk_data_track_subscription_options_t) throws -> DataTrackReader {
        try withNativeHandle { room in
            return try withStrings([participant, trackSID]) { strings, _ in
                var reader: OpaquePointer?
                try checkDataTrack(lk_room_subscribe_data_track(room, strings![0], strings![1], &options, &reader))
                return DataTrackReader(try requireHandle(reader), destroy: lk_data_track_reader_destroy)
            }
        }
    }

    public func updateDataTrackSubscription(participant: String, trackSID: String, options: inout lk_data_track_subscription_options_t) throws {
        try withNativeHandle { room in
            try withStrings([participant, trackSID]) { strings, _ in
                try checkDataTrack(lk_room_update_data_track_subscription_options(room, strings![0], strings![1], &options))
            }
        }
    }
}
