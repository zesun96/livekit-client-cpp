package io.livekit.client;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;

final class Native {
    static {
        String client = System.getenv("LIVEKIT_CLIENT_CPP_LIBRARY");
        if (client != null && !client.isBlank()) System.load(Path.of(client).toAbsolutePath().toString());
        String bridge = System.getProperty("livekit.java.library");
        if (bridge != null && !bridge.isBlank()) System.load(Path.of(bridge).toAbsolutePath().toString());
        else System.loadLibrary("livekitclient_java");
    }

    private Native() {}

    static byte[] utf8(String value) { return value == null ? null : value.getBytes(StandardCharsets.UTF_8); }
    static String string(byte[] value) { return value == null ? "" : new String(value, StandardCharsets.UTF_8); }

    static native String version();
    static native long create(Room room);
    static native void destroy(long handle);
    static native void connect(long handle, byte[] url, byte[] token);
    static native void connectWithOptions(long handle, byte[] url, byte[] token, ConnectionOptions options);
    static native void disconnect(long handle);
    static native int state(long handle);
    static native int disconnectReason(long handle);
    static native boolean connected(long handle);
    static native byte[] roomString(long handle, int field);
    static native byte[] localString(long handle, int field);
    static native void setLocalString(long handle, int field, byte[] value);
    static native void publishData(long handle, byte[] data, byte[] topic, boolean reliable, byte[][] destinations);
    static native void sendText(long handle, byte[] text);
    static native void sendBytes(long handle, byte[] data);
    static native void sendFile(long handle, byte[] path);
    static native void sendDtmf(long handle, int code, byte[] digit);
    static native String sendChat(long handle, byte[] message);
    static native Participant[] remoteParticipants(long handle);
    static native void setTrackSubscribed(long handle, byte[] participantSid, byte[] trackSid, boolean subscribed);
    static native RpcResult performRpc(long handle, byte[] destination, byte[] method, byte[] payload, int timeoutMs);
    static native void registerRpc(long handle, byte[] method);
    static native void unregisterRpc(long handle, byte[] method);
    static native boolean e2eeEnabled(long handle);
    static native void setE2eeEnabled(long handle, boolean enabled);
    static native void setSharedKey(long handle, byte[] key, int index);
    static native byte[] exportSharedKey(long handle, int index);
    static native void ratchetSharedKey(long handle, int index);
    static native long createAudioSource(int sampleRate, int channels, int queueMs);
    static native void destroyAudioSource(long source);
    static native void captureAudio(long source, short[] samples, int samplesPerChannel);
    static native long createVideoSource(boolean screencast);
    static native void destroyVideoSource(long source);
    static native void captureI420(long source, byte[] data, int width, int height, long timestampUs);
    static native long createTrack(long room, byte[] label, long source, boolean video);
    static native void publishTrack(long room, long track, int source);
    static native void unpublishTrack(long track);
    static native void destroyTrack(long track);
    static native void muteTrack(long track, boolean muted);
    static native byte[] trackStats(long track);
    static native MediaDevice[] mediaDevices();
    static native ScreenSource[] screenSources();
    static native long publishDataTrack(long room, byte[] name, int frameEncoding);
    static native void pushDataTrack(long track, byte[] data, boolean hasTimestamp, long timestamp);
    static native byte[] dataTrackSid(long track);
    static native void unpublishDataTrack(long track);
    static native void destroyDataTrack(long track);
    static native long subscribeDataTrack(long room, byte[] participantIdentity, byte[] trackSid,
                                          int bufferCapacity);
    static native RemoteDataTrack[] remoteDataTracks(long room);
    static native DataTrackFrame readDataTrack(long reader, int timeoutMs);
    static native void destroyDataTrackReader(long reader);
    static native long createAudioStream(long room, byte[] participantIdentity, byte[] trackSid, int capacity);
    static native long createVideoStream(long room, byte[] participantIdentity, byte[] trackSid, int capacity);
    static native AudioFrame readAudioStream(long stream, int timeoutMs);
    static native VideoFrame readVideoStream(long stream, int timeoutMs);
    static native long audioStreamDroppedFrames(long stream);
    static native long videoStreamDroppedFrames(long stream);
    static native void destroyAudioStream(long stream);
    static native void destroyVideoStream(long stream);
    static native RemoteTrackPublication[] remoteTrackPublications(long room);
}
