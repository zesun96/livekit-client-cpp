package io.livekit.client;

import java.util.List;

/** Owned local track. Close it before its source; the room also closes its remaining tracks. */
public final class LocalTrack implements AutoCloseable {
    private final Room room;
    private final Object source;
    private long handle;
    private boolean published;

    LocalTrack(Room room, Object source, long handle) {
        this.room = room;
        this.source = source;
        this.handle = handle;
    }

    public synchronized void publish(int trackSource) {
        if (handle == 0) throw new IllegalStateException("Track is closed");
        room.useHandle(h -> Native.publishTrack(h, handle, trackSource));
        published = true;
    }

    public synchronized void publishScreenShare() {
        if (handle == 0) throw new IllegalStateException("Track is closed");
        room.useHandle(h -> Native.publishScreenShareTrack(h, handle, source instanceof VideoSource));
        published = true;
    }

    public synchronized void unpublish() {
        if (published) { Native.unpublishTrack(handle); published = false; }
    }

    public synchronized void setMuted(boolean muted) {
        if (handle == 0) throw new IllegalStateException("Track is closed");
        Native.muteTrack(handle, muted);
    }

    public synchronized String rtcStatsJson() {
        if (handle == 0) throw new IllegalStateException("Track is closed");
        return Native.string(Native.trackStats(handle));
    }

    public synchronized List<RtcTrackStats> rtcStats() {
        if (handle == 0) throw new IllegalStateException("Track is closed");
        return List.of(Native.localRtcStats(handle));
    }

    @Override public synchronized void close() {
        if (handle == 0) return;
        unpublish();
        Native.destroyTrack(handle);
        handle = 0;
        if (source instanceof AudioSource audio) audio.release();
        else if (source instanceof VideoSource video) video.release();
        room.removeTrack(this);
    }
}
