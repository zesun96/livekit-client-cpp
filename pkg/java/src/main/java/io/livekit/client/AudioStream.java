package io.livekit.client;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.ForkJoinPool;

/** Bounded remote audio reader owned by its room. */
public final class AudioStream implements AutoCloseable {
    private final Room room;
    private long handle;

    AudioStream(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized AudioFrame readFor(int timeoutMs) {
        if (timeoutMs < 0) throw new IllegalArgumentException("Negative timeout");
        if (handle == 0) throw new IllegalStateException("Audio stream is closed");
        return Native.readAudioStream(handle, timeoutMs);
    }

    public CompletableFuture<AudioFrame> readForAsync(int timeoutMs) {
        return readForAsync(timeoutMs, ForkJoinPool.commonPool());
    }

    public CompletableFuture<AudioFrame> readForAsync(int timeoutMs, Executor executor) {
        return CompletableFuture.supplyAsync(() -> readFor(timeoutMs), executor);
    }

    public synchronized long droppedFrames() {
        if (handle == 0) throw new IllegalStateException("Audio stream is closed");
        return Native.audioStreamDroppedFrames(handle);
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        Native.destroyAudioStream(handle);
        handle = 0;
        room.removeAudioStream(this);
    }
}
