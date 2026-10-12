package io.livekit.client;

import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.ForkJoinPool;
import java.util.function.Consumer;
import java.util.function.LongConsumer;
import java.util.function.LongFunction;

/** Owns one native LiveKit room. Close after all calls and callbacks have completed. */
public final class Room implements AutoCloseable {
    public interface Listener {
        default void onConnected() {}
        default void onDisconnected(int reason) {}
        default void onReconnecting() {}
        default void onReconnected() {}
        default void onConnectionStateChanged(int state) {}
        default void onParticipantConnected(Participant participant) {}
        default void onParticipantDisconnected(Participant participant) {}
        default void onDataReceived(DataPacket packet) {}
        default void onTextReceived(TextMessage message) {}
        default void onBytesReceived(ByteMessage message) {}
        default void onFileReceived(ByteMessage message) {}
        default void onChatMessageReceived(ChatMessage message) {}
    }

    private final List<Listener> listeners = new CopyOnWriteArrayList<>();
    private final List<LocalTrack> tracks = new CopyOnWriteArrayList<>();
    private final List<LocalDataTrack> dataTracks = new CopyOnWriteArrayList<>();
    private final List<DataTrackReader> dataTrackReaders = new CopyOnWriteArrayList<>();
    private final List<AudioStream> audioStreams = new CopyOnWriteArrayList<>();
    private final List<VideoStream> videoStreams = new CopyOnWriteArrayList<>();
    private final List<TrackRecorder> recorders = new CopyOnWriteArrayList<>();
    private final ConcurrentHashMap<String, RpcHandler> rpcHandlers = new ConcurrentHashMap<>();
    private final ThreadLocal<Boolean> inCallback = ThreadLocal.withInitial(() -> false);
    private long handle;
    private int activeCalls;
    private boolean closing;

    public Room() { handle = Native.create(this); }
    public static String version() { return Native.version(); }
    public void addListener(Listener listener) { listeners.add(Objects.requireNonNull(listener)); }
    public void removeListener(Listener listener) { listeners.remove(listener); }

    private synchronized long beginCall() {
        if (handle == 0 || closing) throw new IllegalStateException("Room is closed");
        activeCalls++;
        return handle;
    }

    private synchronized void endCall() { activeCalls--; notifyAll(); }

    <T> T withHandle(LongFunction<T> operation) {
        long h = beginCall();
        try { return operation.apply(h); }
        finally { endCall(); }
    }

    void useHandle(LongConsumer operation) { withHandle(h -> { operation.accept(h); return null; }); }
    void removeTrack(LocalTrack track) { tracks.remove(track); }
    void removeDataTrack(LocalDataTrack track) { dataTracks.remove(track); }
    void removeDataTrackReader(DataTrackReader reader) { dataTrackReaders.remove(reader); }
    void removeAudioStream(AudioStream stream) { audioStreams.remove(stream); }
    void removeVideoStream(VideoStream stream) { videoStreams.remove(stream); }
    void removeTrackRecorder(TrackRecorder recorder) { recorders.remove(recorder); }

    public TrackRecorder startTrackRecording(String participantIdentity, String trackSid,
                                             String outputPath, int queueCapacity) {
        if (queueCapacity < 1) throw new IllegalArgumentException("Invalid recording queue capacity");
        Objects.requireNonNull(participantIdentity);
        Objects.requireNonNull(trackSid);
        Objects.requireNonNull(outputPath);
        return withHandle(h -> {
            TrackRecorder recorder = new TrackRecorder(this, Native.startTrackRecording(h,
                Native.utf8(participantIdentity), Native.utf8(trackSid),
                Native.utf8(outputPath), queueCapacity));
            recorders.add(recorder);
            return recorder;
        });
    }

    public TrackRecorder startTrackRecording(String participantIdentity, String trackSid,
                                             String outputPath) {
        return startTrackRecording(participantIdentity, trackSid, outputPath, 64);
    }

    public AudioStream createAudioStream(String participantIdentity, String trackSid, int capacity) {
        if (capacity < 1) throw new IllegalArgumentException("Invalid audio stream capacity");
        Objects.requireNonNull(participantIdentity);
        Objects.requireNonNull(trackSid);
        return withHandle(h -> {
            AudioStream stream = new AudioStream(this, Native.createAudioStream(h,
                Native.utf8(participantIdentity), Native.utf8(trackSid), capacity));
            audioStreams.add(stream);
            return stream;
        });
    }

    public AudioStream createAudioStream(String participantIdentity, String trackSid) {
        return createAudioStream(participantIdentity, trackSid, 64);
    }

    public VideoStream createVideoStream(String participantIdentity, String trackSid, int capacity) {
        if (capacity < 1) throw new IllegalArgumentException("Invalid video stream capacity");
        Objects.requireNonNull(participantIdentity);
        Objects.requireNonNull(trackSid);
        return withHandle(h -> {
            VideoStream stream = new VideoStream(this, Native.createVideoStream(h,
                Native.utf8(participantIdentity), Native.utf8(trackSid), capacity));
            videoStreams.add(stream);
            return stream;
        });
    }

    public VideoStream createVideoStream(String participantIdentity, String trackSid) {
        return createVideoStream(participantIdentity, trackSid, 8);
    }

    public List<RemoteTrackPublication> remoteTrackPublications() {
        return withHandle(h -> List.of(Native.remoteTrackPublications(h)));
    }

    public LocalDataTrack publishDataTrack(String name, int frameEncoding) {
        Objects.requireNonNull(name);
        if (name.isEmpty()) throw new IllegalArgumentException("DataTrack name is empty");
        if (frameEncoding < 0 || frameEncoding > 7)
            throw new IllegalArgumentException("Unsupported DataTrack frame encoding");
        return withHandle(h -> {
            LocalDataTrack track = new LocalDataTrack(this,
                Native.publishDataTrack(h, Native.utf8(name), frameEncoding));
            dataTracks.add(track);
            return track;
        });
    }

    public DataTrackReader subscribeDataTrack(String participantIdentity, String trackSid,
                                               int bufferCapacity) {
        if (bufferCapacity < 1) throw new IllegalArgumentException("Invalid buffer capacity");
        return withHandle(h -> {
            DataTrackReader reader = new DataTrackReader(this, Native.subscribeDataTrack(h,
                Native.utf8(Objects.requireNonNull(participantIdentity)),
                Native.utf8(Objects.requireNonNull(trackSid)), bufferCapacity));
            dataTrackReaders.add(reader);
            return reader;
        });
    }

    public List<RemoteDataTrack> remoteDataTracks() {
        return withHandle(h -> List.of(Native.remoteDataTracks(h)));
    }

    public LocalTrack createAudioTrack(String label, AudioSource source) {
        Objects.requireNonNull(source).retain();
        try {
            return withHandle(h -> {
                LocalTrack track = new LocalTrack(this, source,
                    Native.createTrack(h, Native.utf8(label), source.handle(), false));
                tracks.add(track);
                return track;
            });
        } catch (RuntimeException failure) { source.release(); throw failure; }
    }

    public LocalTrack createVideoTrack(String label, VideoSource source) {
        Objects.requireNonNull(source).retain();
        try {
            return withHandle(h -> {
                LocalTrack track = new LocalTrack(this, source,
                    Native.createTrack(h, Native.utf8(label), source.handle(), true));
                tracks.add(track);
                return track;
            });
        } catch (RuntimeException failure) { source.release(); throw failure; }
    }

    public void connect(String url, String token) {
        useHandle(h -> Native.connect(h, Native.utf8(Objects.requireNonNull(url)), Native.utf8(Objects.requireNonNull(token))));
    }

    public void connect(String url, String token, ConnectionOptions options) {
        Objects.requireNonNull(options);
        useHandle(h -> Native.connectWithOptions(h, Native.utf8(Objects.requireNonNull(url)),
            Native.utf8(Objects.requireNonNull(token)), options));
    }

    public CompletableFuture<Void> connectAsync(String url, String token, ConnectionOptions options) {
        return CompletableFuture.runAsync(() -> connect(url, token, options));
    }

    public CompletableFuture<Void> connectAsync(String url, String token) {
        return connectAsync(url, token, ForkJoinPool.commonPool());
    }

    public CompletableFuture<Void> connectAsync(String url, String token, Executor executor) {
        return CompletableFuture.runAsync(() -> connect(url, token), executor);
    }

    public void disconnect() { useHandle(Native::disconnect); }
    public CompletableFuture<Void> disconnectAsync() {
        return CompletableFuture.runAsync(this::disconnect);
    }

    public int state() { return withHandle(Native::state); }
    public int disconnectReason() { return withHandle(Native::disconnectReason); }
    public boolean isConnected() { return withHandle(Native::connected); }
    public AudioPlaybackStats audioPlaybackStats() { return withHandle(Native::audioPlaybackStats); }
    public String audioOutputDevice() { return withHandle(h -> Native.string(Native.audioOutputDevice(h))); }
    public void setAudioOutputDevice(String deviceId) {
        useHandle(h -> Native.setAudioOutputDevice(h, Native.utf8(Objects.requireNonNull(deviceId))));
    }
    public float speakerVolume() { return withHandle(Native::speakerVolume); }
    public void setSpeakerVolume(float volume) {
        if (!Float.isFinite(volume) || volume < 0 || volume > 1)
            throw new IllegalArgumentException("Volume must be between 0 and 1");
        useHandle(h -> Native.setSpeakerVolume(h, volume));
    }
    public boolean speakerMuted() { return withHandle(Native::speakerMuted); }
    public void setSpeakerMuted(boolean muted) { useHandle(h -> Native.setSpeakerMuted(h, muted)); }
    public void republishAllTracks() { useHandle(Native::republishAllTracks); }
    public boolean isRecording() { return withHandle(Native::isRecording); }
    public String sid() { return withHandle(h -> Native.string(Native.roomString(h, 0))); }
    public String name() { return withHandle(h -> Native.string(Native.roomString(h, 1))); }
    public String metadata() { return withHandle(h -> Native.string(Native.roomString(h, 2))); }
    public String localSid() { return withHandle(h -> Native.string(Native.localString(h, 0))); }
    public String localIdentity() { return withHandle(h -> Native.string(Native.localString(h, 1))); }
    public String localName() { return withHandle(h -> Native.string(Native.localString(h, 2))); }
    public String localMetadata() { return withHandle(h -> Native.string(Native.localString(h, 3))); }
    public void setLocalName(String name) { useHandle(h -> Native.setLocalString(h, 0, Native.utf8(name))); }
    public void setLocalMetadata(String metadata) { useHandle(h -> Native.setLocalString(h, 1, Native.utf8(metadata))); }
    public void setLocalAttributes(Map<String, String> attributes) {
        Map<String, String> snapshot = Map.copyOf(Objects.requireNonNull(attributes));
        byte[][] keys = new byte[snapshot.size()][];
        byte[][] values = new byte[snapshot.size()][];
        int index = 0;
        for (var entry : snapshot.entrySet()) {
            keys[index] = Native.utf8(entry.getKey());
            values[index++] = Native.utf8(entry.getValue());
        }
        useHandle(h -> Native.setLocalAttributes(h, keys, values));
    }

    public void publishData(byte[] data, String topic, boolean reliable, String... destinations) {
        byte[][] encoded = new byte[destinations.length][];
        for (int i = 0; i < destinations.length; i++) encoded[i] = Native.utf8(Objects.requireNonNull(destinations[i]));
        useHandle(h -> Native.publishData(h, Objects.requireNonNull(data), Native.utf8(topic), reliable, encoded));
    }

    public CompletableFuture<Void> publishDataAsync(byte[] data, String topic, boolean reliable, String... destinations) {
        return CompletableFuture.runAsync(() -> publishData(data, topic, reliable, destinations));
    }

    public void sendText(String text) { useHandle(h -> Native.sendText(h, Native.utf8(Objects.requireNonNull(text)))); }
    public void sendBytes(byte[] data) { useHandle(h -> Native.sendBytes(h, Objects.requireNonNull(data))); }
    public void sendFile(String path) { useHandle(h -> Native.sendFile(h, Native.utf8(Objects.requireNonNull(path)))); }
    public void publishDtmf(int code, String digit) { useHandle(h -> Native.sendDtmf(h, code, Native.utf8(digit))); }
    public String sendChat(String message) { return withHandle(h -> Native.sendChat(h, Native.utf8(Objects.requireNonNull(message)))); }
    public List<Participant> remoteParticipants() { return withHandle(h -> List.of(Native.remoteParticipants(h))); }

    public void setTrackSubscribed(String participantSid, String trackSid, boolean subscribed) {
        useHandle(h -> Native.setTrackSubscribed(h, Native.utf8(participantSid), Native.utf8(trackSid), subscribed));
    }

    public RpcResult performRpc(String destination, String method, String payload, int timeoutMs) {
        return withHandle(h -> Native.performRpc(h, Native.utf8(destination), Native.utf8(method), Native.utf8(payload), timeoutMs));
    }

    public CompletableFuture<RpcResult> performRpcAsync(String destination, String method, String payload, int timeoutMs) {
        return CompletableFuture.supplyAsync(() -> performRpc(destination, method, payload, timeoutMs));
    }

    @FunctionalInterface public interface RpcHandler { String handle(RpcInvocation invocation); }

    public void registerRpcMethod(String method, RpcHandler handler) {
        Objects.requireNonNull(method);
        Objects.requireNonNull(handler);
        if (rpcHandlers.putIfAbsent(method, handler) != null)
            throw new IllegalStateException("RPC method already registered: " + method);
        try { useHandle(h -> Native.registerRpc(h, Native.utf8(method))); }
        catch (RuntimeException failure) { rpcHandlers.remove(method, handler); throw failure; }
    }

    public void unregisterRpcMethod(String method) {
        useHandle(h -> Native.unregisterRpc(h, Native.utf8(method)));
        rpcHandlers.remove(method);
    }

    private String onNativeRpc(String method, String requestId, String caller, String payload, int timeoutMs) {
        RpcHandler handler = rpcHandlers.get(method);
        if (handler == null) throw new IllegalStateException("RPC method is unregistered: " + method);
        boolean previous = inCallback.get();
        inCallback.set(true);
        try { return handler.handle(new RpcInvocation(requestId, caller, payload, timeoutMs)); }
        finally { inCallback.set(previous); }
    }

    public boolean e2eeEnabled() { return withHandle(Native::e2eeEnabled); }
    public void setE2eeEnabled(boolean enabled) { useHandle(h -> Native.setE2eeEnabled(h, enabled)); }
    public void setSharedKey(byte[] key, int index) { useHandle(h -> Native.setSharedKey(h, key, index)); }
    public byte[] exportSharedKey(int index) { return withHandle(h -> Native.exportSharedKey(h, index)); }
    public void ratchetSharedKey(int index) { useHandle(h -> Native.ratchetSharedKey(h, index)); }

    private void onNativeEvent(int event, int value, Participant participant, DataPacket packet) {
        dispatch(listener -> {
            switch (event) {
                case 0 -> listener.onConnected();
                case 1 -> listener.onDisconnected(value);
                case 2 -> listener.onReconnecting();
                case 3 -> listener.onReconnected();
                case 4 -> listener.onConnectionStateChanged(value);
                case 5 -> listener.onParticipantConnected(participant);
                case 6 -> listener.onParticipantDisconnected(participant);
                case 7 -> listener.onDataReceived(packet);
                default -> throw new AssertionError(event);
            }
        });
    }

    private void dispatch(Consumer<Listener> delivery) {
        boolean previous = inCallback.get();
        inCallback.set(true);
        try {
        for (Listener listener : listeners) {
            try {
                delivery.accept(listener);
            } catch (Throwable failure) {
                Thread thread = Thread.currentThread();
                Thread.UncaughtExceptionHandler handler = thread.getUncaughtExceptionHandler();
                if (handler != null) handler.uncaughtException(thread, failure);
            }
        }
        } finally { inCallback.set(previous); }
    }

    private void onNativeText(TextMessage message) {
        dispatch(listener -> listener.onTextReceived(message));
    }

    private void onNativeBytes(ByteMessage message, boolean file) {
        dispatch(listener -> {
            if (file) listener.onFileReceived(message);
            else listener.onBytesReceived(message);
        });
    }

    private void onNativeChat(ChatMessage message) {
        dispatch(listener -> listener.onChatMessageReceived(message));
    }

    @Override public void close() {
        if (inCallback.get()) throw new IllegalStateException("Close the room after its callback returns");
        synchronized (this) {
            boolean interrupted = false;
            while (closing) {
                try { wait(); }
                catch (InterruptedException ignored) { interrupted = true; }
            }
            if (handle == 0) {
                if (interrupted) Thread.currentThread().interrupt();
                return;
            }
            closing = true;
            while (activeCalls != 0) {
                try { wait(); }
                catch (InterruptedException ignored) { interrupted = true; }
            }
            if (interrupted) Thread.currentThread().interrupt();
        }
        try {
            for (TrackRecorder recorder : recorders) recorder.close();
            for (AudioStream stream : audioStreams) stream.close();
            for (VideoStream stream : videoStreams) stream.close();
            for (DataTrackReader reader : dataTrackReaders) reader.close();
            for (LocalDataTrack track : dataTracks) track.close();
            for (LocalTrack track : tracks) track.close();
            long old = handle;
            handle = 0;
            Native.destroy(old);
            rpcHandlers.clear();
        } finally {
            synchronized (this) { closing = false; notifyAll(); }
        }
    }
}
