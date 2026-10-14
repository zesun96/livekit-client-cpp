package io.livekit.client;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.ForkJoinPool;

/** Bounded remote video reader owned by its room. */
public final class VideoStream implements AutoCloseable {
    private final Room room;
    private long handle;

    VideoStream(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized VideoFrame readFor(int timeoutMs) {
        if (timeoutMs < 0) throw new IllegalArgumentException("Negative timeout");
        if (handle == 0) throw new IllegalStateException("Video stream is closed");
        return Native.readVideoStream(handle, timeoutMs);
    }

    public CompletableFuture<VideoFrame> readForAsync(int timeoutMs) {
        return readForAsync(timeoutMs, ForkJoinPool.commonPool());
    }

    public CompletableFuture<VideoFrame> readForAsync(int timeoutMs, Executor executor) {
        return CompletableFuture.supplyAsync(() -> readFor(timeoutMs), executor);
    }

    public synchronized long droppedFrames() {
        if (handle == 0) throw new IllegalStateException("Video stream is closed");
        return Native.videoStreamDroppedFrames(handle);
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        Native.destroyVideoStream(handle);
        handle = 0;
        room.removeVideoStream(this);
    }
}
