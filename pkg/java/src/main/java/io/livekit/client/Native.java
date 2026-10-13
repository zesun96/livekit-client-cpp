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
    static native LogOptions logOptions();
    static native void setLogOptions(int liveKitLevel, int webRtcLevel, int webSocketLevel);
    static native void setTraceOptions(boolean enabled, long categoryMask);
    static native TraceOptions traceOptions();
    static native void startTraceJson(byte[] path);
    static native void stopTrace();
    static native long create(Room room);
    static native void destroy(long handle);
    static native void setFrameCallbacksEnabled(long handle, boolean enabled);
    static native void connect(long handle, byte[] url, byte[] token);
    static native void connectWithOptions(long handle, byte[] url, byte[] token, ConnectionOptions options);
    static native void connectWithTokenSource(long handle, byte[][] fields,
                                              byte[][] attributeKeys, byte[][] attributeValues,
                                              ConnectionOptions options);
    static native void disconnect(long handle);
    static native int state(long handle);
    static native int disconnectReason(long handle);
    static native boolean connected(long handle);
    static native AudioPlaybackStats audioPlaybackStats(long handle);
    static native void setAudioOutputDevice(long handle, byte[] deviceId);
    static native byte[] audioOutputDevice(long handle);
    static native void setSpeakerVolume(long handle, float volume);
    static native float speakerVolume(long handle);
    static native void setSpeakerMuted(long handle, boolean muted);
    static native boolean speakerMuted(long handle);
    static native void republishAllTracks(long handle);
    static native byte[] roomString(long handle, int field);
    static native byte[] localString(long handle, int field);
    static native void setLocalString(long handle, int field, byte[] value);
    static native void setLocalAttributes(long handle, byte[][] keys, byte[][] values);
    static native boolean isRecording(long handle);
    static native void publishData(long handle, byte[] data, byte[] topic, boolean reliable, byte[][] destinations);
    static native void sendText(long handle, byte[] text);
    static native void sendBytes(long handle, byte[] data);
    static native void sendFile(long handle, byte[] path);
    static native void sendWithOptions(long handle, int kind, byte[] value, byte[][] fields,
                                       byte[][] destinations, byte[][] attributeKeys,
                                       byte[][] attributeValues, byte[][] attachedStreamIds,
                                       int chunkSize, boolean compress);
    static native long createStreamWriter(long room, boolean text, byte[][] fields,
                                          byte[][] destinations, byte[][] attributeKeys,
                                          byte[][] attributeValues, byte[][] attachedStreamIds,
                                          long totalSize, boolean hasTotalSize, int chunkSize,
                                          boolean compress, boolean update, int version);
    static native byte[] streamWriterId(long writer, boolean text);
    static native boolean streamWriterClosed(long writer, boolean text);
    static native StreamWriterInfo streamWriterInfo(long writer, boolean text);
    static native void writeStream(long writer, boolean text, byte[] data);
    static native void closeStreamWriter(long writer, boolean text);
    static native void cancelStream(long writer, boolean text, byte[] reason);
    static native void destroyStreamWriter(long writer, boolean text);
    static native void registerStreamHandler(long room, boolean text, byte[] topic);
    static native void unregisterStreamHandler(long room, boolean text, byte[] topic);
    static native void sendDtmf(long handle, int code, byte[] digit);
    static native SentChatMessage sendChatMessage(long handle, byte[] message);
    static native void editChat(long handle, byte[] messageId, long originalTimestamp,
                                byte[] message);
    static native Participant[] remoteParticipants(long handle);
    static native Participant localParticipant(long handle);
    static native ParticipantDetails localParticipantDetails(long handle);
    static native ParticipantDetails[] remoteParticipantDetails(long handle);
    static native void setTrackSubscribed(long handle, byte[] participantSid, byte[] trackSid, boolean subscribed);
    static native void updateRemoteTrackSettings(long handle, byte[] participantSid,
                                                  byte[] trackSid, boolean enabled, int videoQuality,
                                                  int width, int height, int fps, int priority);
    static native void setTrackSubscriptionPermissions(long handle, boolean allowAll,
                                                        ParticipantTrackPermission[] permissions);
    static native RpcResult performRpc(long handle, byte[] destination, byte[] method, byte[] payload, int timeoutMs);
    static native void registerRpc(long handle, byte[] method);
    static native void unregisterRpc(long handle, byte[] method);
    static native boolean e2eeEnabled(long handle);
    static native boolean e2eeConfigured(long handle);
    static native void setE2eeEnabled(long handle, boolean enabled);
    static native void setSharedKey(long handle, byte[] key, int index);
    static native byte[] exportSharedKey(long handle, int index);
    static native void ratchetSharedKey(long handle, int index);
    static native void removeSharedKey(long handle, int index);
    static native void setParticipantKey(long handle, byte[] identity, byte[] key, int index);
    static native byte[] exportParticipantKey(long handle, byte[] identity, int index);
    static native void ratchetParticipantKey(long handle, byte[] identity, int index);
    static native void removeParticipantKey(long handle, byte[] identity, int index);
    static native void removeParticipantKeys(long handle, byte[] identity);
    static native void clearE2eeKeys(long handle);
    static native long dataKeyIndex(long handle);
    static native void setDataKeyIndex(long handle, long index);
    static native void setFrameCryptorEnabled(long handle, byte[] trackId, int direction,
                                              boolean enabled);
    static native void setFrameCryptorKeyIndex(long handle, byte[] trackId, int direction,
                                               long index);
    static native long setParticipantE2eeEnabled(long handle, byte[] identity, boolean enabled);
    static native EncryptionState[] frameCryptors(long handle);
    static native long createAudioSource(int sampleRate, int channels, int queueMs);
    static native long createDeviceAudioSource(int kind, byte[] deviceId, int queueMs,
                                               boolean echoCancellation, boolean autoGainControl,
                                               boolean noiseSuppression);
    static native void controlDeviceAudioSource(long source, int kind, int command, byte[] deviceId);
    static native boolean deviceAudioSourceCapturing(long source, int kind);
    static native byte[] deviceAudioSourceId(long source, int kind);
    static native void setMicrophoneMuted(long source, boolean muted);
    static native boolean microphoneMuted(long source);
    static native void setMicrophoneVolume(long source, float volume);
    static native float microphoneVolume(long source);
    static native void destroyAudioSource(long source);
    static native void captureAudio(long source, short[] samples, int samplesPerChannel);
    static native long createVideoSource(boolean screencast);
    static native long createDeviceVideoSource(int kind, byte[] deviceId, int width, int height,
                                               int framesPerSecond, boolean includeCursor);
    static native void controlDeviceVideoSource(long source, int kind, int command, byte[] deviceId);
    static native boolean deviceVideoSourceCapturing(long source, int kind);
    static native byte[] deviceVideoSourceId(long source, int kind);
    static native void destroyVideoSource(long source);
    static native void captureI420(long source, byte[] data, int width, int height, long timestampUs);
    static native long createTrack(long room, byte[] label, long source, boolean video);
    static native void publishTrack(long room, long track, int source);
    static native void publishScreenShareTrack(long room, long track, boolean video);
    static native void unpublishTrack(long track);
    static native void destroyTrack(long track);
    static native void muteTrack(long track, boolean muted);
    static native byte[] trackStats(long track);
    static native MediaDevice[] mediaDevices();
    static native ScreenSource[] screenSources();
    static native long publishDataTrack(long room, byte[] name, int frameEncoding);
    static native long publishDataTrackWithOptions(long room, byte[] name, int frameEncoding,
                                                   byte[] customFrameEncoding, byte[] schemaName,
                                                   int schemaEncoding, byte[] customSchemaEncoding);
    static native void storeDataTrackSchema(long room, byte[] name, int encoding,
                                            byte[] customEncoding, byte[] definition);
    static native DataTrackSchema getDataTrackSchema(long room, byte[] participantIdentity,
                                                     byte[] name, int encoding,
                                                     byte[] customEncoding);
    static native void pushDataTrack(long track, byte[] data, boolean hasTimestamp, long timestamp);
    static native byte[] dataTrackSid(long track);
    static native void unpublishDataTrack(long track);
    static native void destroyDataTrack(long track);
    static native long subscribeDataTrack(long room, byte[] participantIdentity, byte[] trackSid,
                                          int targetFps, int bufferCapacity, int maxPartialFrames);
    static native void updateDataTrackSubscription(long room, byte[] participantIdentity,
                                                    byte[] trackSid, int targetFps,
                                                    int bufferCapacity, int maxPartialFrames);
    static native boolean dataTrackReaderClosed(long reader);
    static native long dataTrackReaderDroppedFrames(long reader);
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
    static native RemoteTrackPublicationDetails[] remoteTrackPublicationDetails(long room);
    static native long startTrackRecording(long room, byte[] participantIdentity, byte[] trackSid,
                                           byte[] outputPath, int queueCapacity);
    static native TrackRecorderStats trackRecorderStats(long recorder);
    static native byte[] trackRecorderString(long recorder, boolean error);
    static native void stopTrackRecorder(long recorder);
    static native void destroyTrackRecorder(long recorder);
}
