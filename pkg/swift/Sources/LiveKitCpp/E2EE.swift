import Foundation

public struct FrameCryptor {
    public let trackID: String
    public let participantIdentity: String
    public let info: lk_frame_cryptor_info_t
}

extension Room {
    public var isE2EEConfigured: Bool { get throws { try withNativeHandle { lk_room_e2ee_is_configured($0) != 0 } } }
    public var isE2EEEnabled: Bool { get throws { try withNativeHandle { lk_room_e2ee_is_enabled($0) != 0 } } }
    public func setE2EEEnabled(_ enabled: Bool) throws { try withNativeHandle { try LiveKitError.check(lk_room_e2ee_set_enabled($0, enabled ? 1 : 0)) } }
    public func setEncryptionKey(_ key: Data, index: Int = 0, participant: String? = nil) throws {
        try withNativeHandle { room in
            try key.withUnsafeBytes { bytes in
                let pointer = bytes.bindMemory(to: UInt8.self).baseAddress
                if let participant {
                    try participant.withCString { try LiveKitError.check(lk_room_e2ee_set_participant_key(room, $0, pointer, bytes.count, index)) }
                } else { try LiveKitError.check(lk_room_e2ee_set_shared_key(room, pointer, bytes.count, index)) }
            }
        }
    }
    public func exportEncryptionKey(index: Int = 0, participant: String? = nil) throws -> Data {
        try withNativeHandle { room in
            if let participant {
                return try participant.withCString { identity in try readBytes { lk_room_e2ee_export_participant_key(room, identity, index, $0, $1) } }
            }
            return try readBytes { lk_room_e2ee_export_shared_key(room, index, $0, $1) }
        }
    }
    public func ratchetEncryptionKey(index: Int = 0, participant: String? = nil) throws {
        try withNativeHandle { room in
            if let participant {
                try participant.withCString { try LiveKitError.check(lk_room_e2ee_ratchet_participant_key(room, $0, index)) }
            } else { try LiveKitError.check(lk_room_e2ee_ratchet_shared_key(room, index)) }
        }
    }
    public func removeEncryptionKey(index: Int = 0, participant: String? = nil) throws {
        try withNativeHandle { room in
            if let participant {
                try participant.withCString { try LiveKitError.check(lk_room_e2ee_remove_participant_key(room, $0, index)) }
            } else { try LiveKitError.check(lk_room_e2ee_remove_shared_key(room, index)) }
        }
    }
    public func clearEncryptionKeys(participant: String? = nil) throws {
        try withNativeHandle { room in
            if let participant {
                try participant.withCString { try LiveKitError.check(lk_room_e2ee_remove_participant_keys(room, $0)) }
            } else { try LiveKitError.check(lk_room_e2ee_clear_keys(room)) }
        }
    }
    public var dataKeyIndex: Int { get throws { try withNativeHandle { lk_room_e2ee_data_key_index($0) } } }
    public func setDataKeyIndex(_ index: Int) throws { try withNativeHandle { try LiveKitError.check(lk_room_e2ee_set_data_key_index($0, index)) } }
    public func setFrameCryptorEnabled(trackID: String, direction: lk_frame_cryptor_direction_t, enabled: Bool) throws {
        try withNativeHandle { room in
            try trackID.withCString { try LiveKitError.check(lk_room_e2ee_set_frame_cryptor_enabled(room, $0, direction, enabled ? 1 : 0)) }
        }
    }
    public func setFrameCryptorKeyIndex(trackID: String, direction: lk_frame_cryptor_direction_t, index: Int) throws {
        try withNativeHandle { room in
            try trackID.withCString { try LiveKitError.check(lk_room_e2ee_set_frame_cryptor_key_index(room, $0, direction, index)) }
        }
    }
    public func setParticipantEncryptionEnabled(_ identity: String, enabled: Bool) throws -> Int {
        try withNativeHandle { room in
            var count = 0
            try identity.withCString { try LiveKitError.check(lk_room_e2ee_set_participant_enabled(room, $0, enabled ? 1 : 0, &count)) }
            return count
        }
    }
    public func frameCryptors() throws -> [FrameCryptor] {
        try withNativeHandle { room in
            var list: OpaquePointer?
            try LiveKitError.check(lk_frame_cryptor_list_create(room, &list))
            defer { lk_frame_cryptor_list_destroy(list) }
            return try (0..<lk_frame_cryptor_list_count(list)).map { index in
                var value = lk_frame_cryptor_info_t(); lk_frame_cryptor_info_init(&value)
                try LiveKitError.check(lk_frame_cryptor_list_info(list, index, &value))
                return try FrameCryptor(trackID: readString { lk_frame_cryptor_list_track_id(list, index, $0, $1) },
                    participantIdentity: readString { lk_frame_cryptor_list_participant_identity(list, index, $0, $1) }, info: value)
            }
        }
    }
}
