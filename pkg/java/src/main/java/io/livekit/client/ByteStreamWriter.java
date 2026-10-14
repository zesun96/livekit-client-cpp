package io.livekit.client;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;

/** Outgoing incremental byte stream. Close completes the stream. */
public final class ByteStreamWriter implements AutoCloseable {
    private final Room room;
    private final StreamWriterCallbacks callbacks;
    private long handle;

    ByteStreamWriter(Room room, long handle) { this(room, handle, null, null); }
    ByteStreamWriter(Room room, long handle, StreamWriterListener listener, Long totalSize) {
        this.room = room;
        this.handle = handle;
        this.callbacks = new StreamWriterCallbacks(listener, totalSize);
    }

    public synchronized String id() {
        if (handle == 0) throw new IllegalStateException("Byte stream is closed");
        return Native.string(Native.streamWriterId(handle, false));
    }

    public synchronized boolean isClosed() {
        return handle == 0 || Native.streamWriterClosed(handle, false);
    }

    public synchronized StreamWriterInfo info() {
        if (handle == 0) throw new IllegalStateException("Byte stream is closed");
        return Native.streamWriterInfo(handle, false);
    }

    public synchronized void write(byte[] data) {
        if (handle == 0) throw new IllegalStateException("Byte stream is closed");
        Objects.requireNonNull(data);
        try { Native.writeStream(handle, false, data); }
        catch (LiveKitException failure) {
            String streamId = id();
            destroy();
            callbacks.complete(2, streamId, failure.getMessage(), failure.status());
            throw failure;
        }
        callbacks.wrote(data.length);
    }

    public CompletableFuture<Void> writeAsync(byte[] data) {
        byte[] payload = Objects.requireNonNull(data).clone();
        return CompletableFuture.runAsync(() -> write(payload));
    }

    public synchronized void cancel(String reason) {
        if (handle == 0) return;
        String streamId = id();
        LiveKitException failure = null;
        try {
            Native.cancelStream(handle, false, Native.utf8(reason));
        } catch (LiveKitException error) {
            failure = error;
        } finally { destroy(); }
        if (failure != null) {
            callbacks.complete(2, streamId, failure.getMessage(), failure.status());
            throw failure;
        }
        callbacks.complete(1, streamId, reason == null || reason.isEmpty() ? "cancelled" : reason, 0);
    }

    public CompletableFuture<Void> cancelAsync(String reason) {
        return CompletableFuture.runAsync(() -> cancel(reason));
    }

    private void destroy() {
        Native.destroyStreamWriter(handle, false);
        handle = 0;
        room.removeByteStreamWriter(this);
    }

    synchronized void abortOnRoomClose() {
        if (handle != 0) {
            String streamId = id();
            destroy();
            callbacks.complete(1, streamId, "writer destroyed before close", 0);
        }
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        String streamId = id();
        LiveKitException failure = null;
        try {
            Native.closeStreamWriter(handle, false);
        } catch (LiveKitException error) {
            failure = error;
        } finally { destroy(); }
        if (failure != null) {
            callbacks.complete(2, streamId, failure.getMessage(), failure.status());
            throw failure;
        }
        callbacks.complete(0, streamId, "", 0);
    }

    public CompletableFuture<Void> closeAsync() {
        return CompletableFuture.runAsync(this::close);
    }
}
