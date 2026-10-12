package io.livekit.client;

/** Run after setting LIVEKIT_CLIENT_CPP_LIBRARY and -Dlivekit.java.library. */
public final class OfflineSmoke {
    public static void main(String[] args) {
        if (Room.version().isBlank()) throw new AssertionError("Empty SDK version");
        LogOptions logBefore = Diagnostics.logOptions();
        Diagnostics.setLogOptions(new LogOptions(2, 3, 3));
        if (!Diagnostics.logOptions().equals(new LogOptions(2, 3, 3)))
            throw new AssertionError("Log options did not round-trip");
        Diagnostics.setLogOptions(logBefore);
        TraceOptions traceBefore = Diagnostics.traceOptions();
        Diagnostics.setTraceOptions(true, 1);
        if (!Diagnostics.traceOptions().equals(new TraceOptions(true, 1)))
            throw new AssertionError("Trace options did not round-trip");
        Diagnostics.setTraceOptions(traceBefore.enabled(), traceBefore.categoryMask());
        try (Room room = new Room()) {
            java.util.concurrent.atomic.AtomicInteger subscribed =
                new java.util.concurrent.atomic.AtomicInteger();
            room.addListener(new Room.Listener() {
                @Override public void onTrackSubscribed(RemoteTrackPublication track) {
                    if (!track.sid().equals("track-1")) throw new AssertionError("Wrong track SID");
                    subscribed.incrementAndGet();
                }
            });
            room.onNativeTrack(4, new RemoteTrackPublication("remote", "track-1", "mic",
                "audio/opus", 1, 1, false, 0, 0));
            if (subscribed.get() != 1) throw new AssertionError("Track listener was not called");
            java.util.concurrent.atomic.AtomicReference<String> roomSid =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onRoomSidChanged(String previousSid, String sid) {
                    if (!previousSid.equals("old")) throw new AssertionError("Wrong previous SID");
                    roomSid.set(sid);
                }
            });
            room.onNativeRoomString(1, "old", "new");
            if (!"new".equals(roomSid.get())) throw new AssertionError("Room SID listener failed");
            MediaDevices.listDevices();
            MediaDevices.listScreenSources();
            if (room.isConnected()) throw new AssertionError("New room is connected");
            if (room.isRecording()) throw new AssertionError("New room is recording");
            AudioPlaybackStats playback = room.audioPlaybackStats();
            if (playback.queuedFrames() != 0 || playback.playedFrames() != 0)
                throw new AssertionError("New room has playback frames");
            if (room.remoteParticipants().size() != 0) throw new AssertionError("Unexpected participant");
            if (!room.remoteTrackPublications().isEmpty()) throw new AssertionError("Unexpected track");
            if (!room.remoteDataTracks().isEmpty()) throw new AssertionError("Unexpected DataTrack");
            if (room.state() != 3) throw new AssertionError("New room is not disconnected");
            try {
                room.setLocalAttributes(java.util.Map.of("offline", "test"));
            } catch (LiveKitException expected) {
                // Some SDK builds require a connected participant before updating attributes.
            }
            ConnectionOptions options = new ConnectionOptions(true, false, false, 3, 15_000,
                new IceServer[] { new IceServer(new String[] { "stun:example.invalid:3478" }, "u", "p") },
                3, 1, new E2eeOptions(new byte[32], new byte[16]));
            try {
                room.connect("", "", options);
                throw new AssertionError("Empty URL was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 1) throw expected;
            }
            java.util.concurrent.atomic.AtomicBoolean tokenRequested =
                new java.util.concurrent.atomic.AtomicBoolean();
            try {
                room.connectWithTokenSource(request -> {
                    tokenRequested.set(true);
                    throw new IllegalStateException("Offline token source");
                });
                throw new AssertionError("Failed token source was accepted");
            } catch (LiveKitException expected) {
                if (!tokenRequested.get()) throw new AssertionError("Token source was not called");
            }
            try {
                room.publishDataTrack("offline", 0);
                throw new AssertionError("Disconnected DataTrack publish was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            try {
                room.publishDataTrack(new DataTrackPublishOptions("offline", 8, "custom",
                    "schema", 8, "custom-schema"));
                throw new AssertionError("Disconnected custom DataTrack publish was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            try {
                room.storeDataTrackSchema(new DataTrackSchema("offline", 7, "", new byte[] { 1 }));
            } catch (LiveKitException expected) {
                // Schema storage may require a connected participant.
            }
            try {
                room.createAudioStream("remote", "TR_offline", 4);
                throw new AssertionError("Unavailable audio stream was accepted");
            } catch (LiveKitException expected) {
                // The remote track is absent in an offline room.
            }
            try {
                room.startTrackRecording("remote", "TR_offline", "offline.wav");
                throw new AssertionError("Unavailable remote recording was accepted");
            } catch (LiveKitException expected) {
                // Recording needs a remote track.
            }
            try (TextStreamWriter writer = room.streamText("offline")) {
                if (writer.id().isEmpty()) throw new AssertionError("Text stream has no ID");
                writer.cancel("offline");
            } catch (LiveKitException expected) {
                // A disconnected participant may reject opening the stream.
            }
            try (ByteStreamWriter writer = room.streamBytes("offline", "application/octet-stream", "test")) {
                if (writer.id().isEmpty()) throw new AssertionError("Byte stream has no ID");
                writer.cancel("offline");
            } catch (LiveKitException expected) {
                // A disconnected participant may reject opening the stream.
            }
            try {
                room.createVideoStream("remote", "TR_offline", 4);
                throw new AssertionError("Unavailable video stream was accepted");
            } catch (LiveKitException expected) {
                // The remote track is absent in an offline room.
            }
            try {
                room.subscribeDataTrack("remote", "DT_offline", 8);
                throw new AssertionError("Disconnected DataTrack subscription was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            room.registerRpcMethod("echo", invocation -> invocation.payload());
            room.unregisterRpcMethod("echo");
            room.registerTextStreamHandler("offline-text");
            room.unregisterTextStreamHandler("offline-text");
            room.registerByteStreamHandler("offline-bytes");
            room.unregisterByteStreamHandler("offline-bytes");
            try (AudioSource audio = new AudioSource(48_000, 1, 1_000);
                 VideoSource video = new VideoSource(false)) {
                audio.capture(new short[480]);
                video.captureI420(new byte[6], 2, 2, 0);
                try {
                    audio.startCapture();
                    throw new AssertionError("Synthetic audio source started device capture");
                } catch (IllegalStateException expected) { }
                try {
                    video.startCapture();
                    throw new AssertionError("Synthetic video source started device capture");
                } catch (IllegalStateException expected) { }
                try {
                    room.createAudioTrack("offline audio", audio);
                    throw new AssertionError("Disconnected audio track creation was accepted");
                } catch (LiveKitException expected) {
                    // Failed creation must release the source reference.
                }
                try {
                    room.createVideoTrack("offline video", video);
                    throw new AssertionError("Disconnected video track creation was accepted");
                } catch (LiveKitException expected) {
                    // Failed creation must release the source reference.
                }
            }
            try {
                AudioSource.createMicrophone(null, 1, true, true, true);
                throw new AssertionError("Invalid microphone queue was accepted");
            } catch (IllegalArgumentException expected) { }
            try {
                VideoSource.createScreen("", 15, true);
                throw new AssertionError("Empty screen source was accepted");
            } catch (IllegalArgumentException expected) { }
        }
        System.out.println("Java JNI offline smoke passed");
    }
}
