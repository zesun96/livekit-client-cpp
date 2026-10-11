package io.livekit.client;

import java.util.Objects;

/** Caller-owned DataTrack publisher. The room closes remaining publishers on close. */
public final class LocalDataTrack implements AutoCloseable {
    private final Room room;
    private long handle;

    LocalDataTrack(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized String sid() {
        if (handle == 0) throw new IllegalStateException("DataTrack is closed");
        return Native.string(Native.dataTrackSid(handle));
    }

    public synchronized void push(byte[] data) { push(data, 0, false); }
    public synchronized void push(byte[] data, long userTimestamp) { push(data, userTimestamp, true); }

    public synchronized void unpublish() {
        if (handle == 0) throw new IllegalStateException("DataTrack is closed");
        Native.unpublishDataTrack(handle);
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
