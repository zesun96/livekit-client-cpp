package io.livekit.client;

/** Caller-owned synthetic PCM source. Close tracks before closing their source. */
public final class AudioSource implements AutoCloseable {
    private long handle;
    private final int channels;
    private int tracks;

    public AudioSource(int sampleRate, int channels, int queueMs) {
        if (sampleRate <= 0 || channels <= 0 || queueMs <= 0) throw new IllegalArgumentException("Invalid audio format");
        this.channels = channels;
        handle = Native.createAudioSource(sampleRate, channels, queueMs);
    }

    synchronized long handle() {
        if (handle == 0) throw new IllegalStateException("AudioSource is closed");
        return handle;
    }

    synchronized void retain() { handle(); tracks++; }
    synchronized void release() { tracks--; }

    public synchronized void capture(short[] interleavedPcm) {
        if (interleavedPcm.length % channels != 0) throw new IllegalArgumentException("Incomplete audio frame");
        Native.captureAudio(handle(), interleavedPcm, interleavedPcm.length / channels);
    }

    @Override public synchronized void close() {
        if (tracks != 0) throw new IllegalStateException("Close audio tracks first");
        if (handle != 0) { Native.destroyAudioSource(handle); handle = 0; }
    }
}
