import Foundation

/// An ARC-owned native resource. Operations must be serialized by the caller.
/// The raw pointer is borrowed only for withNativeHandle's closure.
public class NativeObject {
    let handle: OpaquePointer
    private let destroy: ((OpaquePointer) -> Void)?
    private let owner: AnyObject?

    init(_ handle: OpaquePointer, owner: AnyObject? = nil, destroy: ((OpaquePointer) -> Void)? = nil) {
        self.handle = handle
        self.owner = owner
        self.destroy = destroy
    }

    deinit { destroy?(handle) }

    public func withNativeHandle<T>(_ body: (OpaquePointer) throws -> T) rethrows -> T {
        try body(handle)
    }
}

func requireHandle(_ pointer: OpaquePointer?) throws -> OpaquePointer {
    guard let pointer else {
        throw LiveKitError(code: LK_STATUS_OPERATION_FAILED.rawValue, message: "Native operation returned no handle")
    }
    return pointer
}

func checkDataTrack(_ status: lk_data_track_error_code_t) throws {
    if status != LK_DATA_TRACK_ERROR_NONE {
        throw LiveKitError(code: Int32(truncatingIfNeeded: status.rawValue), message: copyString(lk_last_error()))
    }
}

func readBytes(_ getter: (UnsafeMutablePointer<UInt8>?, Int) -> Int) throws -> Data {
    var capacity = getter(nil, 0)
    for _ in 0..<8 {
        if capacity == 0 { return Data() }
        var buffer = [UInt8](repeating: 0, count: capacity)
        let count = buffer.withUnsafeMutableBufferPointer { getter($0.baseAddress, $0.count) }
        if count > capacity { capacity = count; continue }
        return Data(buffer.prefix(count))
    }
    throw LiveKitError(code: LK_STATUS_OPERATION_FAILED.rawValue, message: "Byte getter changed repeatedly")
}

func withStrings<T>(_ strings: [String], _ body: (UnsafePointer<UnsafePointer<CChar>?>?, Int) throws -> T) rethrows -> T {
    let buffers = strings.map { string -> UnsafeMutablePointer<CChar> in
        let bytes = Array(string.utf8CString)
        let buffer = UnsafeMutablePointer<CChar>.allocate(capacity: bytes.count)
        bytes.withUnsafeBufferPointer { buffer.initialize(from: $0.baseAddress!, count: $0.count) }
        return buffer
    }
    defer { for buffer in buffers { buffer.deallocate() } }
    let pointers = buffers.map { Optional(UnsafePointer($0)) }
    return try pointers.withUnsafeBufferPointer { try body($0.baseAddress, $0.count) }
}

func withAttributes<T>(_ attributes: [String: String], _ body: (UnsafePointer<lk_attribute_t>?, Int) throws -> T) rethrows -> T {
    let entries = attributes.sorted { $0.key < $1.key }
    return try withStrings(entries.flatMap { [$0.key, $0.value] }) { pointers, _ in
        let values = entries.indices.map { lk_attribute_t(key: pointers![$0 * 2], value: pointers![$0 * 2 + 1]) }
        return try values.withUnsafeBufferPointer { try body($0.baseAddress, $0.count) }
    }
}

// Native token/RPC results are copied after the callback returns. Intern immutable buffers
// until room destruction so concurrent invocations never overwrite an earlier response.
final class CallbackStrings {
    private let lock = NSLock()
    private var buffers: [String: UnsafeMutablePointer<CChar>] = [:]

    func pointer(_ value: String) -> UnsafePointer<CChar> {
        lock.lock(); defer { lock.unlock() }
        if let existing = buffers[value] { return UnsafePointer(existing) }
        let bytes = Array(value.utf8CString)
        let pointer = UnsafeMutablePointer<CChar>.allocate(capacity: bytes.count)
        bytes.withUnsafeBufferPointer { pointer.initialize(from: $0.baseAddress!, count: $0.count) }
        buffers[value] = pointer
        return UnsafePointer(pointer)
    }

    deinit { for buffer in buffers.values { buffer.deallocate() } }
}
