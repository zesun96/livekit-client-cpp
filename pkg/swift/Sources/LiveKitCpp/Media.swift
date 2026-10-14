import Foundation

/// A track retains its source; the room retains its tracks through native destruction.
public final class LocalTrack {
    let handle: OpaquePointer
    private let source: AnyObject

    init(handle: OpaquePointer, source: AnyObject) { self.handle = handle; self.source = source }
    deinit { lk_local_track_destroy(handle) }

    public func setMuted(_ muted: Bool) throws { try LiveKitError.check(lk_local_track_set_muted(handle, muted ? 1 : 0)) }
    public func unpublish(stop: Bool = true) throws { try LiveKitError.check(lk_local_track_unpublish(handle, stop ? 1 : 0)) }
}

public final class AudioSource {
    enum CaptureKind { case external, microphone, systemAudio }
    private let kind: CaptureKind
    let handle: OpaquePointer
    public let sampleRate: UInt32
    public let channels: UInt32

    public init(sampleRate: UInt32 = 48000, channels: UInt32 = 1, queueMilliseconds: UInt32 = 1000) throws {
        var options = lk_audio_source_options_t()
        lk_audio_source_options_init(&options)
        options.sample_rate = sampleRate
        options.num_channels = channels
        options.queue_size_ms = queueMilliseconds
        var source: OpaquePointer?
        try LiveKitError.check(lk_audio_source_create(&options, &source))
        handle = source!
        self.sampleRate = sampleRate
        self.channels = channels
        kind = .external
    }

    private init(handle: OpaquePointer, kind: CaptureKind) {
        self.handle = handle; self.kind = kind
        sampleRate = 0; channels = 0
    }

    public static func microphone(options: inout lk_microphone_capture_options_t) throws -> AudioSource {
        var handle: OpaquePointer?
        try LiveKitError.check(lk_audio_source_create_microphone(&options, &handle))
        return AudioSource(handle: try requireHandle(handle), kind: .microphone)
    }

    public static func systemAudio(options: inout lk_system_audio_capture_options_t) throws -> AudioSource {
        var handle: OpaquePointer?
        try LiveKitError.check(lk_audio_source_create_system_audio(&options, &handle))
        return AudioSource(handle: try requireHandle(handle), kind: .systemAudio)
    }

    public func start() throws {
        switch kind {
        case .microphone: try LiveKitError.check(lk_audio_source_microphone_start(handle))
        case .systemAudio: try LiveKitError.check(lk_audio_source_system_audio_start(handle))
        case .external: throw LiveKitError(code: LK_STATUS_INVALID_STATE.rawValue, message: "External source has no capture device")
        }
    }

    public func stop() throws {
        switch kind {
        case .microphone: try LiveKitError.check(lk_audio_source_microphone_stop(handle))
        case .systemAudio: try LiveKitError.check(lk_audio_source_system_audio_stop(handle))
        case .external: break
        }
    }

    public func switchDevice(_ id: String) throws {
        try id.withCString {
            switch kind {
            case .microphone: try LiveKitError.check(lk_audio_source_microphone_switch_device(handle, $0))
            case .systemAudio: try LiveKitError.check(lk_audio_source_system_audio_switch_device(handle, $0))
            case .external: throw LiveKitError(code: LK_STATUS_INVALID_STATE.rawValue, message: "External source has no capture device")
            }
        }
    }

    public var queuedMilliseconds: UInt32 { get throws {
        var value: UInt32 = 0
        try LiveKitError.check(lk_audio_source_queued_duration_ms(handle, &value)); return value
    } }
    public func clearQueue() throws { try LiveKitError.check(lk_audio_source_clear_queue(handle)) }
    public func waitForPlayout(timeoutMilliseconds: UInt32) throws { try LiveKitError.check(lk_audio_source_wait_for_playout(handle, timeoutMilliseconds)) }
    public func setMicrophoneMuted(_ muted: Bool) throws { try LiveKitError.check(lk_audio_source_microphone_set_muted(handle, muted ? 1 : 0)) }
    public func setMicrophoneVolume(_ volume: Float) throws { try LiveKitError.check(lk_audio_source_microphone_set_volume(handle, volume)) }
    public func setMicrophoneProcessing(options: inout lk_audio_source_options_t) throws { try LiveKitError.check(lk_audio_source_microphone_set_processing_options(handle, &options)) }
    public var microphoneStats: lk_microphone_processing_stats_t { get throws {
        var value = lk_microphone_processing_stats_t(); lk_microphone_processing_stats_init(&value)
        try LiveKitError.check(lk_audio_source_microphone_processing_stats(handle, &value)); return value
    } }

    deinit { _ = lk_audio_source_destroy(handle) }

    /// Signed 16-bit interleaved PCM. The array must contain complete channel groups.
    public func capture(_ samples: [Int16]) throws {
        guard channels > 0, !samples.isEmpty, samples.count % Int(channels) == 0,
              samples.count / Int(channels) <= Int(UInt32.max) else {
            throw LiveKitError(code: LK_STATUS_INVALID_ARGUMENT.rawValue, message: "Invalid interleaved PCM sample count")
        }
        try samples.withUnsafeBufferPointer {
            try LiveKitError.check(lk_audio_source_capture_frame(handle, $0.baseAddress, UInt32(samples.count / Int(channels))))
        }
    }
}

public final class VideoSource {
    enum CaptureKind { case external, camera, screen }
    private let kind: CaptureKind
    let handle: OpaquePointer

    public init(isScreenCast: Bool = false) throws {
        var options = lk_video_source_options_t()
        lk_video_source_options_init(&options)
        options.is_screencast = isScreenCast ? 1 : 0
        var source: OpaquePointer?
        try LiveKitError.check(lk_video_source_create(&options, &source))
        handle = source!
        kind = .external
    }

    private init(handle: OpaquePointer, kind: CaptureKind) { self.handle = handle; self.kind = kind }

    public static func camera(options: inout lk_camera_capture_options_t) throws -> VideoSource {
        var handle: OpaquePointer?
        try LiveKitError.check(lk_video_source_create_camera(&options, &handle))
        return VideoSource(handle: try requireHandle(handle), kind: .camera)
    }

    public static func screen(options: inout lk_screen_capture_options_t) throws -> VideoSource {
        var handle: OpaquePointer?
        try LiveKitError.check(lk_video_source_create_screen(&options, &handle))
        return VideoSource(handle: try requireHandle(handle), kind: .screen)
    }

    public func start() throws {
        switch kind {
        case .camera: try LiveKitError.check(lk_video_source_camera_start(handle))
        case .screen: try LiveKitError.check(lk_video_source_screen_start(handle))
        case .external: throw LiveKitError(code: LK_STATUS_INVALID_STATE.rawValue, message: "External source has no capture device")
        }
    }

    public func stop() throws {
        switch kind {
        case .camera: try LiveKitError.check(lk_video_source_camera_stop(handle))
        case .screen: try LiveKitError.check(lk_video_source_screen_stop(handle))
        case .external: break
        }
    }

    public func switchDevice(_ id: String) throws {
        try id.withCString {
            switch kind {
            case .camera: try LiveKitError.check(lk_video_source_camera_switch_device(handle, $0))
            case .screen: try LiveKitError.check(lk_video_source_screen_switch_source(handle, $0))
            case .external: throw LiveKitError(code: LK_STATUS_INVALID_STATE.rawValue, message: "External source has no capture device")
            }
        }
    }

    public var dimensions: (width: UInt32, height: UInt32) { get throws {
        var width: UInt32 = 0; var height: UInt32 = 0
        try LiveKitError.check(lk_video_source_dimensions(handle, &width, &height)); return (width, height)
    } }

    deinit { _ = lk_video_source_destroy(handle) }

    /// Plane buffers referenced by frame must remain alive until this call returns.
    public func capture(_ frame: inout lk_video_frame_input_t) throws {
        try LiveKitError.check(lk_video_source_capture_frame(handle, &frame))
    }
}
