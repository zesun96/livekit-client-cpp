package io.livekit.client;

import java.util.Objects;

/** Outgoing incremental byte stream. Close completes the stream. */
public final class ByteStreamWriter implements AutoCloseable {
    private final Room room;
    private long handle;

    ByteStreamWriter(Room room, long handle) { this.room = room; this.handle = handle; }

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
        Native.writeStream(handle, false, Objects.requireNonNull(data));
    }

    public synchronized void cancel(String reason) {
        if (handle == 0) return;
        try { Native.cancelStream(handle, false, Native.utf8(reason)); }
        finally { destroy(); }
    }

    private void destroy() {
        Native.destroyStreamWriter(handle, false);
        handle = 0;
        room.removeByteStreamWriter(this);
    }

    synchronized void abortOnRoomClose() {
        if (handle != 0) destroy();
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        try { Native.closeStreamWriter(handle, false); }
        finally { destroy(); }
    }
}
