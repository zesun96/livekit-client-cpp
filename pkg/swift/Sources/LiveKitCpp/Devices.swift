import Foundation

public struct MediaDevice {
    public let id: String
    public let label: String
    public let kind: lk_media_device_kind_t
    public let isDefault: Bool
}

public struct ScreenSource {
    public let id: String
    public let label: String
    public let info: lk_screen_source_info_t
}

extension LiveKit {
    public static func mediaDevices() throws -> [MediaDevice] {
        var list: OpaquePointer?
        try LiveKitError.check(lk_media_device_list_create(&list))
        defer { lk_media_device_list_destroy(list) }
        return try (0..<lk_media_device_list_count(list)).map { index in
            var info = lk_media_device_info_t(); info.struct_size = MemoryLayout<lk_media_device_info_t>.size
            try LiveKitError.check(lk_media_device_list_info(list, index, &info))
            return try MediaDevice(id: readString { lk_media_device_list_id(list, index, $0, $1) },
                label: readString { lk_media_device_list_label(list, index, $0, $1) }, kind: info.kind, isDefault: info.is_default != 0)
        }
    }

    public static func screenSources() throws -> [ScreenSource] {
        var list: OpaquePointer?
        try LiveKitError.check(lk_screen_source_list_create(&list))
        defer { lk_screen_source_list_destroy(list) }
        return try (0..<lk_screen_source_list_count(list)).map { index in
            var info = lk_screen_source_info_t(); info.struct_size = MemoryLayout<lk_screen_source_info_t>.size
            try LiveKitError.check(lk_screen_source_list_info(list, index, &info))
            return try ScreenSource(id: readString { lk_screen_source_list_id(list, index, $0, $1) },
                label: readString { lk_screen_source_list_label(list, index, $0, $1) }, info: info)
        }
    }
}
