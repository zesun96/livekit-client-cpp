import Foundation

public enum FrameRead<Frame> {
    case frame(Frame)
    case empty
    case closed
}

public struct AudioFrame: Sendable {
    public let samples: [Int16]
    public let sampleRate: UInt32
    public let channels: UInt32
    public let samplesPerChannel: UInt32

    init(_ frame: lk_audio_frame_t) {
        samples = frame.data.map { Array(UnsafeBufferPointer(start: $0, count: frame.sample_count)) } ?? []
        sampleRate = frame.sample_rate; channels = frame.num_channels
        samplesPerChannel = frame.samples_per_channel
    }
}

public struct VideoFrame: Sendable {
    /// Tightly packed native I420 frame bytes.
    public let data: Data
    public let width: UInt32
    public let height: UInt32
    public let timestampMicroseconds: Int64
    public let userTimestampMicroseconds: UInt64?
    public let frameID: UInt32?
    public let userData: Data?

    init(_ frame: lk_video_frame_t, metadata: lk_video_frame_metadata_t? = nil) {
        data = frame.data.map { Data(bytes: $0, count: frame.data_size) } ?? Data()
        width = frame.width; height = frame.height; timestampMicroseconds = frame.timestamp_us
        userTimestampMicroseconds = metadata.flatMap { $0.has_user_timestamp_us != 0 ? $0.user_timestamp_us : nil }
        frameID = metadata.flatMap { $0.has_frame_id != 0 ? $0.frame_id : nil }
        if let metadata, metadata.has_user_data != 0 {
            userData = metadata.user_data.map { Data(bytes: $0, count: metadata.user_data_size) } ?? Data()
        } else { userData = nil }
    }
}

public final class AudioStream: NativeObject {
    public func close() { lk_audio_stream_close(handle) }
    public var isClosed: Bool { lk_audio_stream_is_closed(handle) != 0 }
    public var droppedFrames: Int { lk_audio_stream_dropped_frames(handle) }
    public func read(timeoutMilliseconds: UInt32 = 0) throws -> FrameRead<AudioFrame> {
        var frame: OpaquePointer?
        let status = lk_audio_stream_read_for(handle, timeoutMilliseconds, &frame)
        switch status {
        case LK_MEDIA_STREAM_READ_EMPTY: return .empty
        case LK_MEDIA_STREAM_READ_CLOSED: return .closed
        case LK_MEDIA_STREAM_READ_FRAME:
            defer { lk_owned_audio_frame_destroy(frame) }
            var value = lk_audio_frame_t()
            try LiveKitError.check(lk_owned_audio_frame_data(try requireHandle(frame), &value))
            return .frame(AudioFrame(value))
        default: throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Invalid audio reader")
        }
    }
}

public final class VideoStream: NativeObject {
    public func close() { lk_video_stream_close(handle) }
    public var isClosed: Bool { lk_video_stream_is_closed(handle) != 0 }
    public var droppedFrames: Int { lk_video_stream_dropped_frames(handle) }
    public func read(timeoutMilliseconds: UInt32 = 0) throws -> FrameRead<VideoFrame> {
        var frame: OpaquePointer?
        let status = lk_video_stream_read_for(handle, timeoutMilliseconds, &frame)
        switch status {
        case LK_MEDIA_STREAM_READ_EMPTY: return .empty
        case LK_MEDIA_STREAM_READ_CLOSED: return .closed
        case LK_MEDIA_STREAM_READ_FRAME:
            defer { lk_owned_video_frame_destroy(frame) }
            var value = lk_video_frame_t()
            var metadata = lk_video_frame_metadata_t(); lk_video_frame_metadata_init(&metadata)
            try LiveKitError.check(lk_owned_video_frame_data(try requireHandle(frame), &value))
            try LiveKitError.check(lk_owned_video_frame_metadata(frame, &metadata))
            return .frame(VideoFrame(value, metadata: metadata))
        default: throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Invalid video reader")
        }
    }
}

public final class TrackRecorder: NativeObject {
    public func stop() { lk_track_recorder_stop(handle) }
    public var stats: lk_track_recorder_stats_t { get throws {
        var value = lk_track_recorder_stats_t(); lk_track_recorder_stats_init(&value)
        try LiveKitError.check(lk_track_recorder_get_stats(handle, &value)); return value
    } }
    public var outputPath: String { get throws { try readString { lk_track_recorder_output_path(handle, $0, $1) } } }
    public var error: String { get throws { try readString { lk_track_recorder_error(handle, $0, $1) } } }
}

extension Room {
    public func audioStream(participant: String, trackSID: String, capacity: Int = 16) throws -> AudioStream {
        guard capacity > 0 else { throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Reader capacity must be positive") }
        return try withNativeHandle { room in
            var options = lk_media_stream_options_t(); lk_media_stream_options_init(&options)
            options.capacity = capacity
            return try withStrings([participant, trackSID]) { strings, _ in
                var stream: OpaquePointer?
                try LiveKitError.check(lk_room_create_audio_stream(room, strings![0], strings![1], &options, &stream))
                return AudioStream(try requireHandle(stream), destroy: lk_audio_stream_destroy)
            }
        }
    }

    public func videoStream(participant: String, trackSID: String, capacity: Int = 16) throws -> VideoStream {
        guard capacity > 0 else { throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Reader capacity must be positive") }
        return try withNativeHandle { room in
            var options = lk_media_stream_options_t(); lk_media_stream_options_init(&options)
            options.capacity = capacity
            return try withStrings([participant, trackSID]) { strings, _ in
                var stream: OpaquePointer?
                try LiveKitError.check(lk_room_create_video_stream(room, strings![0], strings![1], &options, &stream))
                return VideoStream(try requireHandle(stream), destroy: lk_video_stream_destroy)
            }
        }
    }

    public func record(participant: String, trackSID: String, outputPath: String, capacity: Int = 120) throws -> TrackRecorder {
        guard capacity > 0 else { throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Recorder capacity must be positive") }
        return try withNativeHandle { room in
            var options = lk_track_recorder_options_t(); lk_track_recorder_options_init(&options)
            options.queue_capacity = capacity
            return try withStrings([participant, trackSID, outputPath]) { strings, _ in
                options.output_path = strings![2]
                var recorder: OpaquePointer?
                try LiveKitError.check(lk_room_start_track_recording(room, strings![0], strings![1], &options, &recorder))
                return TrackRecorder(try requireHandle(recorder), destroy: lk_track_recorder_destroy)
            }
        }
    }
}
