import Foundation

public struct RPCResponse: Sendable {
    public let payload: String
    public let errorCode: UInt32
    public let errorMessage: String
    public let errorData: String
    public var isSuccess: Bool { errorCode == 0 }

    public init(payload: String = "", errorCode: UInt32 = 0, errorMessage: String = "", errorData: String = "") {
        self.payload = payload; self.errorCode = errorCode
        self.errorMessage = errorMessage; self.errorData = errorData
    }
}

public struct RPCInvocation: Sendable {
    public let requestID: String
    public let callerIdentity: String
    public let payload: String
    public let responseTimeoutMilliseconds: UInt32
}

final class RPCContext {
    let handler: @Sendable (RPCInvocation) -> RPCResponse
    let strings = CallbackStrings()
    init(_ handler: @escaping @Sendable (RPCInvocation) -> RPCResponse) { self.handler = handler }
}

public final class TextStreamWriter: NativeObject {
    public var id: String { get throws { try readString { lk_text_stream_writer_id(handle, $0, $1) } } }
    public var isClosed: Bool { lk_text_stream_writer_is_closed(handle) != 0 }
    public func write(_ text: String) throws {
        try text.withCString { try LiveKitError.check(lk_text_stream_writer_write(handle, $0, text.utf8.count)) }
    }
    public func close() throws { try LiveKitError.check(lk_text_stream_writer_close(handle)) }
    public func cancel(reason: String = "") throws {
        try reason.withCString { try LiveKitError.check(lk_text_stream_writer_cancel(handle, $0)) }
    }
    public func info() throws -> StreamWriterInfo {
        var result: OpaquePointer?
        try LiveKitError.check(lk_text_stream_writer_create_info_snapshot(handle, &result))
        return StreamWriterInfo(try requireHandle(result), destroy: lk_data_stream_writer_info_snapshot_destroy)
    }
}

public final class ByteStreamWriter: NativeObject {
    public var id: String { get throws { try readString { lk_byte_stream_writer_id(handle, $0, $1) } } }
    public var isClosed: Bool { lk_byte_stream_writer_is_closed(handle) != 0 }
    public func write(_ data: Data) throws {
        try data.withUnsafeBytes { try LiveKitError.check(lk_byte_stream_writer_write(handle, $0.bindMemory(to: UInt8.self).baseAddress, $0.count)) }
    }
    public func close() throws { try LiveKitError.check(lk_byte_stream_writer_close(handle)) }
    public func cancel(reason: String = "") throws {
        try reason.withCString { try LiveKitError.check(lk_byte_stream_writer_cancel(handle, $0)) }
    }
    public func info() throws -> StreamWriterInfo {
        var result: OpaquePointer?
        try LiveKitError.check(lk_byte_stream_writer_create_info_snapshot(handle, &result))
        return StreamWriterInfo(try requireHandle(result), destroy: lk_data_stream_writer_info_snapshot_destroy)
    }
}

public final class StreamWriterInfo: NativeObject {
    public var values: lk_data_stream_writer_info_t { get throws {
        var info = lk_data_stream_writer_info_t()
        lk_data_stream_writer_info_init(&info)
        try LiveKitError.check(lk_data_stream_writer_info_snapshot_info(handle, &info))
        return info
    } }
    public var streamID: String { get throws { try readString { lk_data_stream_writer_info_snapshot_stream_id(handle, $0, $1) } } }
    public var topic: String { get throws { try readString { lk_data_stream_writer_info_snapshot_topic(handle, $0, $1) } } }
}

extension Room {
    public func publishDTMF(code: UInt32, digit: String) throws {
        try withNativeHandle { room in try digit.withCString { try LiveKitError.check(lk_room_publish_dtmf(room, code, $0)) } }
    }

    public func sendChatMessage(_ message: String) throws -> (id: String, timestamp: Int64) {
        try withNativeHandle { room in
            var id = [CChar](repeating: 0, count: 128)
            var timestamp: Int64 = 0
            try message.withCString { text in
                try id.withUnsafeMutableBufferPointer { try LiveKitError.check(lk_room_send_chat_message(room, text, $0.baseAddress, $0.count, &timestamp)) }
            }
            return (id.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }, timestamp)
        }
    }

    public func editChatMessage(id: String, timestamp: Int64, message: String) throws {
        try withNativeHandle { room in
            try withStrings([id, message]) { strings, _ in
                try LiveKitError.check(lk_room_edit_chat_message(room, strings![0], timestamp, strings![1]))
            }
        }
    }
    public func sendText(_ text: String, topic: String = "", destinations: [String] = []) throws {
        try withNativeHandle { room in
            var options = lk_text_send_options_t(); lk_text_send_options_init(&options)
            try topic.withCString { topic in
                options.topic = topic
                try withStrings(destinations) { pointers, count in
                    options.destination_identities = pointers; options.destination_identity_count = count
                    try text.withCString { try LiveKitError.check(lk_room_send_text(room, $0, &options)) }
                }
            }
        }
    }

    public func sendBytes(_ data: Data, topic: String = "", name: String = "", mimeType: String = "application/octet-stream") throws {
        try withNativeHandle { room in
            var options = lk_byte_send_options_t(); lk_byte_send_options_init(&options)
            try withStrings([topic, name, mimeType]) { strings, _ in
                options.topic = strings![0]; options.name = strings![1]; options.mime_type = strings![2]
                try data.withUnsafeBytes { try LiveKitError.check(lk_room_send_bytes(room, $0.bindMemory(to: UInt8.self).baseAddress, $0.count, &options)) }
            }
        }
    }

    public func sendFile(path: String, topic: String = "") throws {
        try withNativeHandle { room in
            var options = lk_file_send_options_t(); lk_file_send_options_init(&options)
            try topic.withCString { topic in
                options.topic = topic
                try path.withCString { try LiveKitError.check(lk_room_send_file(room, $0, &options)) }
            }
        }
    }

    public func streamText(options: inout lk_stream_text_options_t) throws -> TextStreamWriter {
        try withNativeHandle { room in
            var result: OpaquePointer?
            try LiveKitError.check(lk_room_stream_text(room, &options, &result))
            return TextStreamWriter(try requireHandle(result), destroy: lk_text_stream_writer_destroy)
        }
    }

    public func streamBytes(options: inout lk_stream_bytes_options_t) throws -> ByteStreamWriter {
        try withNativeHandle { room in
            var result: OpaquePointer?
            try LiveKitError.check(lk_room_stream_bytes(room, &options, &result))
            return ByteStreamWriter(try requireHandle(result), destroy: lk_byte_stream_writer_destroy)
        }
    }

    public func performRPC(destination: String, method: String, payload: String, timeoutMilliseconds: UInt32 = 10000) throws -> RPCResponse {
        try withNativeHandle { room in
            var options = lk_rpc_perform_options_t(); lk_rpc_perform_options_init(&options)
            options.response_timeout_ms = timeoutMilliseconds
            return try withStrings([destination, method, payload]) { strings, _ in
                options.destination_identity = strings![0]; options.method = strings![1]; options.payload = strings![2]
                var result: OpaquePointer?
                try LiveKitError.check(lk_room_perform_rpc(room, &options, &result))
                defer { lk_rpc_result_destroy(result) }
                return try RPCResponse(payload: readString { lk_rpc_result_payload(result, $0, $1) },
                    errorCode: lk_rpc_result_error_code(result),
                    errorMessage: readString { lk_rpc_result_error_message(result, $0, $1) },
                    errorData: readString { lk_rpc_result_error_data(result, $0, $1) })
            }
        }
    }

    public func performRPCAsync(destination: String, method: String, payload: String, timeoutMilliseconds: UInt32 = 10000) async throws -> RPCResponse {
        try await performAsync { try self.performRPC(destination: destination, method: method, payload: payload, timeoutMilliseconds: timeoutMilliseconds) }
    }

    /// Runs on a native thread. Return quickly and do not call room operations from this handler.
    public func registerRPC(method: String, handler: @escaping @Sendable (RPCInvocation) -> RPCResponse) throws {
        try withNativeHandle { room in
            let context = RPCContext(handler)
            try method.withCString { method in
                try LiveKitError.check(lk_room_register_rpc_method_out(room, method, { user, invocation, result in
                    guard let user, let invocation, let result else { return }
                    let context = Unmanaged<RPCContext>.fromOpaque(user).takeUnretainedValue()
                    let value = invocation.pointee
                    let response = context.handler(RPCInvocation(requestID: copyString(value.request_id),
                        callerIdentity: copyString(value.caller_identity), payload: copyString(value.payload),
                        responseTimeoutMilliseconds: value.response_timeout_ms))
                    result.pointee = lk_rpc_handler_result_t(payload: context.strings.pointer(response.payload),
                        error_code: response.errorCode, error_message: context.strings.pointer(response.errorMessage),
                        error_data: context.strings.pointer(response.errorData))
                }, Unmanaged.passUnretained(context).toOpaque()))
            }
            retainContext(context)
        }
    }

    public func unregisterRPC(method: String) throws {
        try withNativeHandle { room in try method.withCString { try LiveKitError.check(lk_room_unregister_rpc_method(room, $0)) } }
    }
}
