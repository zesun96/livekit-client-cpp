package io.livekit.client;

/** Run after setting LIVEKIT_CLIENT_CPP_LIBRARY and -Dlivekit.java.library. */
public final class OfflineSmoke {
    public static void main(String[] args) {
        if (Room.version().isBlank()) throw new AssertionError("Empty SDK version");
        try (Room room = new Room()) {
            MediaDevices.listScreenSources();
            if (room.isConnected()) throw new AssertionError("New room is connected");
            if (room.remoteParticipants().size() != 0) throw new AssertionError("Unexpected participant");
            if (room.state() != 3) throw new AssertionError("New room is not disconnected");
            room.registerRpcMethod("echo", invocation -> invocation.payload());
            room.unregisterRpcMethod("echo");
            try (AudioSource audio = new AudioSource(48_000, 1, 1_000);
                 VideoSource video = new VideoSource(false)) {
                audio.capture(new short[480]);
                video.captureI420(new byte[6], 2, 2, 0);
            }
        }
        System.out.println("Java JNI offline smoke passed");
    }
}
