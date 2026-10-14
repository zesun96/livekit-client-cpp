@_exported import CLiveKit
import Foundation

public struct LiveKitError: Error, CustomStringConvertible, Sendable {
    public let code: Int32
    public let message: String
    public var description: String { "LiveKit (\(code)): \(message)" }

    public init<Code: BinaryInteger>(code: Code, message: String) {
        self.code = Int32(truncatingIfNeeded: code); self.message = message
    }

    public static func check(_ status: lk_status_t) throws {
        guard status == LK_STATUS_OK else {
            // Error storage belongs to this thread. Copy before any other C call.
            throw LiveKitError(code: status.rawValue, message: lk_last_error().map { String(cString: $0) } ?? "Native operation failed")
        }
    }
}

public enum LiveKit {
    public static func initialize() throws { try LiveKitError.check(lk_init()) }
    /// Destroy every native object before calling shutdown.
    public static func shutdown() throws { try LiveKitError.check(lk_shutdown()) }
    public static func version() throws -> String { try readString(lk_version) }
}

/// Copy a C string getter, retrying when a mutable native value grows between calls.
func readString(_ getter: (UnsafeMutablePointer<CChar>?, Int) -> Int) throws -> String {
    var capacity = getter(nil, 0)
    for _ in 0..<8 {
        guard capacity > 0 else {
            throw LiveKitError(code: LK_STATUS_OPERATION_FAILED.rawValue, message: "String getter failed")
        }
        var buffer = [CChar](repeating: 0, count: capacity)
        let needed = buffer.withUnsafeMutableBufferPointer { getter($0.baseAddress, $0.count) }
        if needed > capacity { capacity = needed; continue }
        guard needed > 0 else {
            throw LiveKitError(code: LK_STATUS_OPERATION_FAILED.rawValue, message: "String getter failed")
        }
        return buffer.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
    }
    throw LiveKitError(code: LK_STATUS_OPERATION_FAILED.rawValue, message: "String changed repeatedly while reading")
}
