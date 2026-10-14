package io.livekit.client;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.ForkJoinPool;

/** Bounded pull reader. A null result means timeout or a closed track. */
public final class DataTrackReader implements AutoCloseable {
    private final Room room;
    private long handle;

    DataTrackReader(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized boolean isClosed() {
        return handle == 0 || Native.dataTrackReaderClosed(handle);
    }

    public synchronized long droppedFrames() {
        if (handle == 0) throw new IllegalStateException("DataTrack reader is closed");
        return Native.dataTrackReaderDroppedFrames(handle);
    }

    public synchronized DataTrackFrame readFor(int timeoutMs) {
        if (timeoutMs < 0) throw new IllegalArgumentException("Negative timeout");
        if (handle == 0) throw new IllegalStateException("DataTrack reader is closed");
        return Native.readDataTrack(handle, timeoutMs);
    }

    public CompletableFuture<DataTrackFrame> readForAsync(int timeoutMs) {
        return readForAsync(timeoutMs, ForkJoinPool.commonPool());
    }

    public CompletableFuture<DataTrackFrame> readForAsync(int timeoutMs, Executor executor) {
        return CompletableFuture.supplyAsync(() -> readFor(timeoutMs), executor);
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        Native.destroyDataTrackReader(handle);
        handle = 0;
        room.removeDataTrackReader(this);
    }
}
