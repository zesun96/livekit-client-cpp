package io.livekit.client;

/** Run after setting LIVEKIT_CLIENT_CPP_LIBRARY and -Dlivekit.java.library. */
public final class OfflineSmoke {
    public static void main(String[] args) {
        if (Room.version().isBlank()) throw new AssertionError("Empty SDK version");
        try (Room room = new Room()) {
            MediaDevices.listDevices();
            MediaDevices.listScreenSources();
            if (room.isConnected()) throw new AssertionError("New room is connected");
            if (room.remoteParticipants().size() != 0) throw new AssertionError("Unexpected participant");
            if (!room.remoteDataTracks().isEmpty()) throw new AssertionError("Unexpected DataTrack");
            if (room.state() != 3) throw new AssertionError("New room is not disconnected");
            ConnectionOptions options = new ConnectionOptions(true, false, false, 3, 15_000,
                new IceServer[] { new IceServer(new String[] { "stun:example.invalid:3478" }, "u", "p") },
                3, 1, new E2eeOptions(new byte[32], new byte[16]));
            try {
                room.connect("", "", options);
                throw new AssertionError("Empty URL was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 1) throw expected;
            }
            try {
                room.publishDataTrack("offline", 0);
                throw new AssertionError("Disconnected DataTrack publish was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            try {
                room.subscribeDataTrack("remote", "DT_offline", 8);
                throw new AssertionError("Disconnected DataTrack subscription was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            room.registerRpcMethod("echo", invocation -> invocation.payload());
            room.unregisterRpcMethod("echo");
            try (AudioSource audio = new AudioSource(48_000, 1, 1_000);
                 VideoSource video = new VideoSource(false)) {
                audio.capture(new short[480]);
                video.captureI420(new byte[6], 2, 2, 0);
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
        }
        System.out.println("Java JNI offline smoke passed");
    }
}
