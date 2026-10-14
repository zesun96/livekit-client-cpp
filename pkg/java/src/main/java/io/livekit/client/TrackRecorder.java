package io.livekit.client;

/** Caller-owned remote track recorder. Stop before using its output file. */
public final class TrackRecorder implements AutoCloseable {
    private final Room room;
    private long handle;

    TrackRecorder(Room room, long handle) { this.room = room; this.handle = handle; }

    public synchronized TrackRecorderStats stats() {
        if (handle == 0) throw new IllegalStateException("Track recorder is closed");
        return Native.trackRecorderStats(handle);
    }

    public synchronized String outputPath() {
        if (handle == 0) throw new IllegalStateException("Track recorder is closed");
        return Native.string(Native.trackRecorderString(handle, false));
    }

    public synchronized String error() {
        if (handle == 0) throw new IllegalStateException("Track recorder is closed");
        return Native.string(Native.trackRecorderString(handle, true));
    }

    public synchronized void stop() {
        if (handle == 0) throw new IllegalStateException("Track recorder is closed");
        Native.stopTrackRecorder(handle);
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        Native.stopTrackRecorder(handle);
        Native.destroyTrackRecorder(handle);
        handle = 0;
        room.removeTrackRecorder(this);
    }
}
