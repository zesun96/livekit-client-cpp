import Foundation
import LiveKitCpp

@main
struct FeatureExamples {
    static let environment = ProcessInfo.processInfo.environment

    static func main() async throws {
        let feature = CommandLine.arguments.dropFirst().first ?? "help"
        if feature == "help" {
            print("Features: devices, data, streams, datatrack, token, rpc-call, rpc-server, e2ee, audio, video, readers, recording, microphone, system-audio, camera, screen")
            return
        }
        try LiveKit.initialize()
        do { try await run(feature) }
        catch { try? LiveKit.shutdown(); throw error }
        try LiveKit.shutdown()
    }

    static func run(_ feature: String) async throws {
        if feature == "devices" {
            for device in try LiveKit.mediaDevices() { print("\(device.kind): \(device.id) \(device.label)") }
            for source in try LiveKit.screenSources() { print("Screen: \(source.id) \(source.label)") }
            return
        }
        guard let url = environment["LIVEKIT_URL"], let token = environment["LIVEKIT_TOKEN"] else {
            print("Set LIVEKIT_URL and LIVEKIT_TOKEN."); return
        }
        let room = try Room()
        defer { room.close() }
        // Avoid dumping payloads or token data from native events into example logs.
        room.onEvent { event in
            switch event {
            case .connected: print("Connected")
            case .data(let data, let topic, let identity, _): print("Data: \(data.count) bytes from \(identity) on \(topic)")
            case .text(_, let topic, let identity, _): print("Text from \(identity) on \(topic)")
            default: break
            }
        }
        if feature == "token" {
            var fetch = lk_token_source_fetch_options_t(); lk_token_source_fetch_options_init(&fetch)
            var options = lk_room_connect_options_t(); lk_room_connect_options_init(&options)
            try room.connect(tokenSource: { _ in TokenResponse(serverURL: url, token: token) }, fetchOptions: &fetch, options: &options)
        } else if feature == "e2ee" {
            guard let secret = environment["LIVEKIT_E2EE_KEY"], !secret.isEmpty else { print("Set LIVEKIT_E2EE_KEY."); return }
            var e2ee = lk_e2ee_options_t(); lk_e2ee_options_init(&e2ee); e2ee.enabled = 1
            var options = lk_room_connect_options_t(); lk_room_connect_options_init(&options)
            try Data(secret.utf8).withUnsafeBytes { bytes in
                e2ee.shared_key = bytes.bindMemory(to: UInt8.self).baseAddress; e2ee.shared_key_size = bytes.count
                try withUnsafePointer(to: &e2ee) { pointer in
                    options.e2ee_options = pointer
                    try room.connect(url: url, token: token, options: &options)
                }
            }
        } else { try await room.connectAsync(url: url, token: token) }
        for _ in 0..<100 {
            if try room.isConnected { break }
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        guard try room.isConnected else { throw LiveKitError(code: 3, message: "Room connection timed out") }
        switch feature {
        case "data", "token", "e2ee":
            try room.publishData(Data("Swift data".utf8), topic: "swift")
            try room.sendText("Swift text", topic: "swift-text")
            try room.sendBytes(Data([1, 2, 3]), topic: "swift-bytes", name: "example.bin")
            if let path = environment["LIVEKIT_FILE_PATH"] { try room.sendFile(path: path, topic: "swift-file") }
        case "streams":
            try room.registerTextStream(topic: "swift-text") { print("Text stream chunk: \($0.content.count) bytes") }
            try room.registerByteStream(topic: "swift-bytes") { print("Byte stream chunk: \($0.content.count) bytes") }
            let text = try room.streamText(topic: "swift-text", onComplete: { print("Text stream complete: \($0.status)") })
            try text.write("first "); try text.write("second"); try text.close()
            let bytes = try room.streamBytes(topic: "swift-bytes", name: "stream.bin")
            try bytes.write(Data([1, 2, 3])); try bytes.close()
        case "datatrack":
            try "swift-schema".withCString { name in
                var id = lk_data_track_schema_id_t(); lk_data_track_schema_id_init(&id)
                id.name = name; id.encoding = LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA
                try room.storeDataTrackSchema(id: &id, definition: Data("{\"type\":\"object\"}".utf8))
                var options = lk_data_track_publish_options_t(); lk_data_track_publish_options_init(&options)
                options.name = name; options.has_schema = 1; options.schema = id
                let track = try room.publishDataTrack(options: &options)
                try track.push(Data("{\"value\":1}".utf8), userTimestamp: 1)
                try track.unpublish()
            }
        case "rpc-call":
            guard let peer = environment["LIVEKIT_PEER_IDENTITY"] else { print("Set LIVEKIT_PEER_IDENTITY."); return }
            let response = try await room.performRPCAsync(destination: peer, method: "swift.echo", payload: "hello")
            print("RPC success: \(response.isSuccess), payload bytes: \(response.payload.utf8.count)")
        case "rpc-server":
            try room.registerRPC(method: "swift.echo") { RPCResponse(payload: $0.payload) }
            try await Task.sleep(nanoseconds: 30_000_000_000)
        case "audio":
            let source = try AudioSource()
            let track = try room.createAudioTrack(label: "swift-audio", source: source)
            var options = lk_track_publish_options_t(); lk_track_publish_options_init(&options); options.source = LK_TRACK_SOURCE_MICROPHONE
            try room.publish(track, options: &options)
            for _ in 0..<100 { try source.capture([Int16](repeating: 0, count: 480)); try await Task.sleep(nanoseconds: 10_000_000) }
        case "video":
            let source = try VideoSource()
            let track = try room.createVideoTrack(label: "swift-video", source: source)
            var options = lk_track_publish_options_t(); lk_track_publish_options_init(&options); options.source = LK_TRACK_SOURCE_CAMERA
            try room.publish(track, options: &options)
            let pixels = [UInt8](repeating: 128, count: 64 * 64 * 4)
            for index in 0..<30 {
                try pixels.withUnsafeBufferPointer { bytes in
                    var frame = lk_video_frame_input_t(); lk_video_frame_input_init(&frame)
                    frame.data = bytes.baseAddress; frame.data_size = bytes.count; frame.width = 64; frame.height = 64
                    frame.format = LK_VIDEO_BUFFER_RGBA; frame.timestamp_us = Int64(index) * 33333
                    try source.capture(&frame)
                }
                try await Task.sleep(nanoseconds: 33_333_333)
            }
        case "readers", "recording":
            guard let peer = environment["LIVEKIT_PEER_IDENTITY"], let sid = environment["LIVEKIT_TRACK_SID"] else {
                print("Set LIVEKIT_PEER_IDENTITY and LIVEKIT_TRACK_SID for a published remote track."); return
            }
            if feature == "recording" {
                guard let path = environment["LIVEKIT_RECORDING_PATH"] else { print("Set LIVEKIT_RECORDING_PATH."); return }
                let recorder = try room.record(participant: peer, trackSID: sid, outputPath: path)
                try await Task.sleep(nanoseconds: 5_000_000_000)
                recorder.stop(); print("Recorded frames: \(try recorder.stats.frames_written)")
            } else {
                if environment["LIVEKIT_TRACK_KIND"] == "video" {
                    let reader = try room.videoStream(participant: peer, trackSID: sid)
                    defer { reader.close() }
                    if case let .frame(frame) = try reader.read(timeoutMilliseconds: 5000) { print("Video: \(frame.width)x\(frame.height)") }
                } else {
                    let reader = try room.audioStream(participant: peer, trackSID: sid)
                    defer { reader.close() }
                    if case let .frame(frame) = try reader.read(timeoutMilliseconds: 5000) { print("Audio samples: \(frame.samples.count)") }
                }
            }
        case "microphone", "system-audio", "camera", "screen":
            try await capture(room: room, feature: feature)
        default: print("Unknown feature: \(feature)")
        }
        try await Task.sleep(nanoseconds: 1_000_000_000)
        try await room.disconnectAsync()
    }

    static func capture(room: Room, feature: String) async throws {
        let deviceID = environment["LIVEKIT_DEVICE_ID"] ?? ""
        var publish = lk_track_publish_options_t(); lk_track_publish_options_init(&publish)
        if feature == "microphone" || feature == "system-audio" {
            let source = try deviceID.withCString { id -> AudioSource in
                if feature == "microphone" {
                    var options = lk_microphone_capture_options_t(); lk_microphone_capture_options_init(&options); options.device_id = id
                    return try AudioSource.microphone(options: &options)
                }
                var options = lk_system_audio_capture_options_t(); lk_system_audio_capture_options_init(&options); options.device_id = id
                return try AudioSource.systemAudio(options: &options)
            }
            let track = try room.createAudioTrack(label: feature, source: source)
            publish.source = feature == "microphone" ? LK_TRACK_SOURCE_MICROPHONE : LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO
            try room.publish(track, options: &publish); try source.start()
            defer { try? source.stop() }
            try await Task.sleep(nanoseconds: 5_000_000_000)
        } else {
            let source = try deviceID.withCString { id -> VideoSource in
                if feature == "camera" {
                    var options = lk_camera_capture_options_t(); lk_camera_capture_options_init(&options); options.device_id = id
                    return try VideoSource.camera(options: &options)
                }
                var options = lk_screen_capture_options_t(); lk_screen_capture_options_init(&options); options.source_id = id
                return try VideoSource.screen(options: &options)
            }
            let track = try room.createVideoTrack(label: feature, source: source)
            publish.source = feature == "camera" ? LK_TRACK_SOURCE_CAMERA : LK_TRACK_SOURCE_SCREEN_SHARE
            try room.publish(track, options: &publish); try source.start()
            defer { try? source.stop() }
            try await Task.sleep(nanoseconds: 5_000_000_000)
        }
    }
}
