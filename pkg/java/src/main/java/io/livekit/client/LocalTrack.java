package io.livekit.client;

import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

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

    public synchronized void publish(TrackPublishOptions options) {
        publishWithOptions(Objects.requireNonNull(options), 0);
    }

    public CompletableFuture<Void> publishAsync(TrackPublishOptions options) {
        return CompletableFuture.runAsync(() -> publish(options));
    }

    public CompletableFuture<Void> publishAsync(int trackSource) {
        return CompletableFuture.runAsync(() -> publish(trackSource));
    }

    public synchronized void publishScreenShare() {
        if (handle == 0) throw new IllegalStateException("Track is closed");
        room.useHandle(h -> Native.publishScreenShareTrack(h, handle, source instanceof VideoSource));
        published = true;
    }

    public synchronized void publishScreenShare(TrackPublishOptions options) {
        publishWithOptions(Objects.requireNonNull(options), source instanceof VideoSource ? 1 : 2);
    }

    public CompletableFuture<Void> publishScreenShareAsync(TrackPublishOptions options) {
        return CompletableFuture.runAsync(() -> publishScreenShare(options));
    }

    public CompletableFuture<Void> publishScreenShareAsync() {
        return CompletableFuture.runAsync(this::publishScreenShare);
    }

    private void publishWithOptions(TrackPublishOptions options, int mode) {
        if (handle == 0) throw new IllegalStateException("Track is closed");
        VideoEncoding video = options.videoEncoding();
        VideoEncoding backup = options.backupVideoEncoding();
        FrameMetadataFeatures metadata = options.frameMetadataFeatures();
        long[] integers = {options.source(), options.videoCodec(), options.backupVideoCodec(),
            options.backupCodecPolicy(), video.maxBitrate(), backup.maxBitrate(),
            options.degradationPreference() == null ? -1 : options.degradationPreference()};
        float[] frameRates = {video.maxFramerate(), backup.maxFramerate()};
        boolean[] flags = {options.dtx(), options.red(), options.simulcast(),
            options.backupVideoCodecEnabled(), metadata.userTimestamp(), metadata.frameId(),
            metadata.userData(), options.preconnectBuffer()};
        byte[][] strings = {Native.utf8(options.stream()),
            Native.utf8(options.scalabilityMode())};
        room.useHandle(h -> Native.publishTrackWithOptions(h, handle, mode, integers,
            frameRates, flags, strings));
        published = true;
    }

    public synchronized void unpublish() {
        if (published) { Native.unpublishTrack(handle); published = false; }
    }

    public CompletableFuture<Void> unpublishAsync() {
        return CompletableFuture.runAsync(this::unpublish);
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
