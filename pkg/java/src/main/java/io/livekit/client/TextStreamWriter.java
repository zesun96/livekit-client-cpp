package io.livekit.client;

import java.util.Objects;

/** Outgoing incremental text stream. Close completes the stream. */
public final class TextStreamWriter implements AutoCloseable {
    private final Room room;
    private long handle;

    TextStreamWriter(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized String id() {
        if (handle == 0) throw new IllegalStateException("Text stream is closed");
        return Native.string(Native.streamWriterId(handle, true));
    }

    public synchronized void write(String text) {
        if (handle == 0) throw new IllegalStateException("Text stream is closed");
        Native.writeStream(handle, true, Native.utf8(Objects.requireNonNull(text)));
    }

    public synchronized void cancel(String reason) {
        if (handle == 0) return;
        try { Native.cancelStream(handle, true, Native.utf8(reason)); }
        finally { destroy(); }
    }

    private void destroy() {
        Native.destroyStreamWriter(handle, true);
        handle = 0;
        room.removeTextStreamWriter(this);
    }

    synchronized void abortOnRoomClose() {
        if (handle != 0) destroy();
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        try { Native.closeStreamWriter(handle, true); }
        finally { destroy(); }
    }
}
