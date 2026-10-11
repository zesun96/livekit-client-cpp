package io.livekit.client;

/** Bounded pull reader. A null result means timeout or a closed track. */
public final class DataTrackReader implements AutoCloseable {
    private final Room room;
    private long handle;

    DataTrackReader(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized DataTrackFrame readFor(int timeoutMs) {
        if (timeoutMs < 0) throw new IllegalArgumentException("Negative timeout");
        if (handle == 0) throw new IllegalStateException("DataTrack reader is closed");
        return Native.readDataTrack(handle, timeoutMs);
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        Native.destroyDataTrackReader(handle);
        handle = 0;
        room.removeDataTrackReader(this);
    }
}
