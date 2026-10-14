import Foundation

public struct StreamEvent: Sendable {
    public let type: Int32
    public let streamID: String
    public let topic: String
    public let participantIdentity: String
    public let mimeType: String
    public let name: String
    public let content: Data
    public let chunkIndex: UInt64
    public let totalSize: UInt64?
    public let reason: String
    public let attributes: [String: String]
    public let timestamp: Int64
    public let replyToStreamID: String
    public let attachedStreamIDs: [String]

    public var text: String { String(decoding: content, as: UTF8.self) }

    init(_ value: lk_text_stream_event_t) {
        type = Int32(truncatingIfNeeded: value.type.rawValue); streamID = copyString(value.stream_id)
        topic = copyString(value.topic); participantIdentity = copyString(value.participant_identity)
        mimeType = copyString(value.mime_type); name = ""
        content = value.content.map { Data(bytes: $0, count: value.content_size) } ?? Data()
        chunkIndex = value.chunk_index; totalSize = value.has_total_size != 0 ? value.total_size : nil
        reason = copyString(value.reason); attributes = copyAttributes(value.attributes, value.attribute_count)
        timestamp = value.timestamp; replyToStreamID = copyString(value.reply_to_stream_id)
        attachedStreamIDs = value.attached_stream_ids.map { strings in (0..<value.attached_stream_id_count).map { copyString(strings[$0]) } } ?? []
    }

    init(_ value: lk_byte_stream_event_t) {
        type = Int32(truncatingIfNeeded: value.type.rawValue); streamID = copyString(value.stream_id)
        topic = copyString(value.topic); participantIdentity = copyString(value.participant_identity)
        mimeType = copyString(value.mime_type); name = copyString(value.name)
        content = value.content.map { Data(bytes: $0, count: value.content_size) } ?? Data()
        chunkIndex = value.chunk_index; totalSize = value.has_total_size != 0 ? value.total_size : nil
        reason = copyString(value.reason); attributes = copyAttributes(value.attributes, value.attribute_count)
        timestamp = value.timestamp; replyToStreamID = ""; attachedStreamIDs = []
    }
}

func copyAttributes(_ values: UnsafePointer<lk_attribute_t>?, _ count: Int) -> [String: String] {
    var result: [String: String] = [:]
    if let values { for index in 0..<count { result[copyString(values[index].key)] = copyString(values[index].value) } }
    return result
}

public struct StreamProgress: Sendable {
    public let bytesSent: UInt64
    public let totalSize: UInt64?
}

public struct StreamCompletion: Sendable {
    public let status: Int32
    public let streamID: String
    public let bytesSent: UInt64
    public let totalSize: UInt64?
    public let reason: String
    public let errorDomain: Int32
    public let errorCode: Int32
}

final class StreamContext {
    let queue = DispatchQueue(label: "livekit.swift.stream")
    let handler: @Sendable (StreamEvent) -> Void
    init(_ handler: @escaping @Sendable (StreamEvent) -> Void) { self.handler = handler }
    func emit(_ event: StreamEvent) { let handler = handler; queue.async { handler(event) } }
}

final class WriterContext {
    let queue = DispatchQueue(label: "livekit.swift.writer")
    let progress: (@Sendable (StreamProgress) -> Void)?
    let completion: (@Sendable (StreamCompletion) -> Void)?
    init(progress: (@Sendable (StreamProgress) -> Void)?, completion: (@Sendable (StreamCompletion) -> Void)?) {
        self.progress = progress; self.completion = completion
    }
}

private let writerProgress: lk_data_stream_progress_callback = { user, sent, hasTotal, total in
    guard let user else { return }
    let context = Unmanaged<WriterContext>.fromOpaque(user).takeUnretainedValue()
    if let handler = context.progress {
        let value = StreamProgress(bytesSent: sent, totalSize: hasTotal != 0 ? total : nil)
        context.queue.async { handler(value) }
    }
}

private let writerCompletion: lk_data_stream_completion_callback = { user, completion in
    guard let user, let completion else { return }
    let context = Unmanaged<WriterContext>.fromOpaque(user).takeUnretainedValue()
    if let handler = context.completion {
        let value = completion.pointee
        let copy = StreamCompletion(status: Int32(truncatingIfNeeded: value.status.rawValue), streamID: copyString(value.stream_id),
            bytesSent: value.bytes_sent, totalSize: value.has_total_size != 0 ? value.total_size : nil, reason: copyString(value.reason),
            errorDomain: Int32(truncatingIfNeeded: value.error_domain.rawValue), errorCode: value.error_code)
        context.queue.async { handler(copy) }
    }
}

extension Room {
    public func registerTextStream(topic: String, handler: @escaping @Sendable (StreamEvent) -> Void) throws {
        try withNativeHandle { room in
            let context = StreamContext(handler)
            try topic.withCString { topic in
                try LiveKitError.check(lk_room_register_text_stream_handler(room, topic, { user, _, event in
                    guard let user, let event else { return }
                    Unmanaged<StreamContext>.fromOpaque(user).takeUnretainedValue().emit(StreamEvent(event.pointee))
                }, Unmanaged.passUnretained(context).toOpaque()))
            }
            retainContext(context)
        }
    }
    public func registerByteStream(topic: String, handler: @escaping @Sendable (StreamEvent) -> Void) throws {
        try withNativeHandle { room in
            let context = StreamContext(handler)
            try topic.withCString { topic in
                try LiveKitError.check(lk_room_register_byte_stream_handler(room, topic, { user, _, event in
                    guard let user, let event else { return }
                    Unmanaged<StreamContext>.fromOpaque(user).takeUnretainedValue().emit(StreamEvent(event.pointee))
                }, Unmanaged.passUnretained(context).toOpaque()))
            }
            retainContext(context)
        }
    }
    public func unregisterTextStream(topic: String) throws {
        try withNativeHandle { room in try topic.withCString { try LiveKitError.check(lk_room_unregister_text_stream_handler(room, $0)) } }
    }
    public func unregisterByteStream(topic: String) throws {
        try withNativeHandle { room in try topic.withCString { try LiveKitError.check(lk_room_unregister_byte_stream_handler(room, $0)) } }
    }
    public func streamText(topic: String, onProgress: (@Sendable (StreamProgress) -> Void)? = nil,
                           onComplete: (@Sendable (StreamCompletion) -> Void)? = nil) throws -> TextStreamWriter {
        try withNativeHandle { room in
            let context = WriterContext(progress: onProgress, completion: onComplete)
            var options = lk_stream_text_options_t(); lk_stream_text_options_init(&options)
            options.on_progress = writerProgress; options.on_complete = writerCompletion
            options.progress_user_data = Unmanaged.passUnretained(context).toOpaque()
            options.completion_user_data = options.progress_user_data
            var writer: OpaquePointer?
            try topic.withCString {
                options.topic = $0
                try LiveKitError.check(lk_room_stream_text(room, &options, &writer))
            }
            return TextStreamWriter(try requireHandle(writer), owner: context, destroy: lk_text_stream_writer_destroy)
        }
    }
    public func streamBytes(topic: String, name: String = "", mimeType: String = "application/octet-stream",
                            onProgress: (@Sendable (StreamProgress) -> Void)? = nil,
                            onComplete: (@Sendable (StreamCompletion) -> Void)? = nil) throws -> ByteStreamWriter {
        try withNativeHandle { room in
            let context = WriterContext(progress: onProgress, completion: onComplete)
            var options = lk_stream_bytes_options_t(); lk_stream_bytes_options_init(&options)
            options.on_progress = writerProgress; options.on_complete = writerCompletion
            options.progress_user_data = Unmanaged.passUnretained(context).toOpaque()
            options.completion_user_data = options.progress_user_data
            var writer: OpaquePointer?
            try withStrings([topic, name, mimeType]) { strings, _ in
                options.topic = strings![0]; options.name = strings![1]; options.mime_type = strings![2]
                try LiveKitError.check(lk_room_stream_bytes(room, &options, &writer))
            }
            return ByteStreamWriter(try requireHandle(writer), owner: context, destroy: lk_byte_stream_writer_destroy)
        }
    }
}
