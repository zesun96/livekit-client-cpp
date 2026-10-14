package io.livekit.client;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;

/** Caller-owned DataTrack publisher. The room closes remaining publishers on close. */
public final class LocalDataTrack implements AutoCloseable {
    private final Room room;
    private long handle;

    LocalDataTrack(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized String sid() {
        if (handle == 0) throw new IllegalStateException("DataTrack is closed");
        return Native.string(Native.dataTrackSid(handle));
    }

    public synchronized LocalDataTrackInfo info() {
        if (handle == 0) throw new IllegalStateException("DataTrack is closed");
        return Native.localDataTrackInfo(handle);
    }

    public synchronized boolean isPublished() {
        if (handle == 0) throw new IllegalStateException("DataTrack is closed");
        return Native.localDataTrackPublished(handle);
    }

    public synchronized void push(byte[] data) { push(data, 0, false); }
    public synchronized void push(byte[] data, long userTimestamp) { push(data, userTimestamp, true); }

    public CompletableFuture<Void> pushAsync(byte[] data) {
        byte[] payload = Objects.requireNonNull(data).clone();
        return CompletableFuture.runAsync(() -> push(payload));
    }

    public CompletableFuture<Void> pushAsync(byte[] data, long userTimestamp) {
        byte[] payload = Objects.requireNonNull(data).clone();
        return CompletableFuture.runAsync(() -> push(payload, userTimestamp));
    }

    public synchronized void unpublish() {
        if (handle == 0) throw new IllegalStateException("DataTrack is closed");
        Native.unpublishDataTrack(handle);
    }

    public CompletableFuture<Void> unpublishAsync() {
        return CompletableFuture.runAsync(this::unpublish);
    }

    private void push(byte[] data, long timestamp, boolean hasTimestamp) {
        if (handle == 0) throw new IllegalStateException("DataTrack is closed");
        Native.pushDataTrack(handle, Objects.requireNonNull(data), hasTimestamp, timestamp);
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        Native.destroyDataTrack(handle);
        handle = 0;
        room.removeDataTrack(this);
    }
}
