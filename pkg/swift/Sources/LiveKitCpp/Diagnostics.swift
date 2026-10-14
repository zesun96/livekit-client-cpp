import Foundation

public struct LogRecord: Sendable {
    public let level: Int32
    public let source: Int32
    public let message: String
    public let file: String
    public let line: Int32
}

public struct TraceRecord: Sendable {
    public let phase: Int32
    public let category: UInt64
    public let name: String
    public let timestampMicroseconds: UInt64
    public let threadID: UInt64
    public let correlationID: UInt64
}

final class LogContext {
    let queue = DispatchQueue(label: "livekit.swift.logs")
    let handler: @Sendable (LogRecord) -> Void
    init(_ handler: @escaping @Sendable (LogRecord) -> Void) { self.handler = handler }
}

final class TraceContext {
    let queue = DispatchQueue(label: "livekit.swift.traces")
    let handler: @Sendable (TraceRecord) -> Void
    init(_ handler: @escaping @Sendable (TraceRecord) -> Void) { self.handler = handler }
}

/// Configuration is process-wide. Use these wrappers consistently instead of mixing raw sinks.
public enum Diagnostics {
    private static let lock = NSLock()
    private static var logContext: LogContext?
    private static var traceContext: TraceContext?

    public static func setLogOptions(_ options: inout lk_log_options_t) throws { try LiveKitError.check(lk_log_set_options(&options)) }
    public static func setTraceOptions(_ options: inout lk_trace_options_t) throws { try LiveKitError.check(lk_trace_set_options(&options)) }

    public static func onLog(_ handler: (@Sendable (LogRecord) -> Void)?) throws {
        lock.lock(); defer { lock.unlock() }
        try LiveKitError.check(lk_log_set_callback(nil, nil))
        logContext = nil
        guard let handler else { return }
        let context = LogContext(handler)
        try LiveKitError.check(lk_log_set_callback({ user, record in
            guard let user, let record else { return }
            let context = Unmanaged<LogContext>.fromOpaque(user).takeUnretainedValue()
            let value = record.pointee
            let copy = LogRecord(level: Int32(truncatingIfNeeded: value.level.rawValue), source: Int32(truncatingIfNeeded: value.source.rawValue),
                message: copyString(value.message), file: copyString(value.file), line: value.line)
            let handler = context.handler
            context.queue.async { handler(copy) }
        }, Unmanaged.passUnretained(context).toOpaque()))
        logContext = context
    }

    public static func onTrace(_ handler: (@Sendable (TraceRecord) -> Void)?) throws {
        lock.lock(); defer { lock.unlock() }
        try LiveKitError.check(lk_trace_stop())
        traceContext = nil
        guard let handler else { return }
        let context = TraceContext(handler)
        try LiveKitError.check(lk_trace_set_callback({ user, record in
            guard let user, let record else { return }
            let context = Unmanaged<TraceContext>.fromOpaque(user).takeUnretainedValue()
            let value = record.pointee
            let copy = TraceRecord(phase: Int32(truncatingIfNeeded: value.phase.rawValue), category: UInt64(value.category.rawValue),
                name: copyString(value.name), timestampMicroseconds: value.timestamp_us, threadID: value.thread_id, correlationID: value.correlation_id)
            let handler = context.handler
            context.queue.async { handler(copy) }
        }, Unmanaged.passUnretained(context).toOpaque()))
        traceContext = context
    }

    public static func startJSONTrace(path: String) throws {
        lock.lock(); defer { lock.unlock() }
        try LiveKitError.check(lk_trace_stop())
        traceContext = nil
        try path.withCString { try LiveKitError.check(lk_trace_start_json_file($0)) }
    }
    public static func stopTrace() throws { try onTrace(nil) }
}

extension Room {
    public func setAudioOutputDevice(_ id: String) throws {
        try withNativeHandle { room in try id.withCString { try LiveKitError.check(lk_room_set_audio_output_device(room, $0)) } }
    }
    public func setSpeakerVolume(_ volume: Float) throws { try withNativeHandle { try LiveKitError.check(lk_room_set_speaker_volume($0, volume)) } }
    public func setSpeakerMuted(_ muted: Bool) throws { try withNativeHandle { try LiveKitError.check(lk_room_set_speaker_muted($0, muted ? 1 : 0)) } }
    public var playbackStats: lk_audio_playback_stats_t { get throws {
        try withNativeHandle { room in
            var value = lk_audio_playback_stats_t(); lk_audio_playback_stats_init(&value)
            try LiveKitError.check(lk_room_audio_playback_stats(room, &value)); return value
        }
    } }
}
