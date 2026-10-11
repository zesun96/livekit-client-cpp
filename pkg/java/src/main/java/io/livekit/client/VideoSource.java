package io.livekit.client;

/** Caller-owned synthetic I420 video source. Push a frame before publishing its track. */
public final class VideoSource implements AutoCloseable {
    private long handle;
    private int tracks;

    public VideoSource(boolean screencast) { handle = Native.createVideoSource(screencast); }

    synchronized long handle() {
        if (handle == 0) throw new IllegalStateException("VideoSource is closed");
        return handle;
    }

    synchronized void retain() { handle(); tracks++; }
    synchronized void release() { tracks--; }

    public synchronized void captureI420(byte[] data, int width, int height, long timestampUs) {
        if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0 ||
                data.length != (long) width * height * 3 / 2) throw new IllegalArgumentException("Invalid I420 frame");
        Native.captureI420(handle(), data, width, height, timestampUs);
    }

    @Override public synchronized void close() {
        if (tracks != 0) throw new IllegalStateException("Close video tracks first");
        if (handle != 0) { Native.destroyVideoSource(handle); handle = 0; }
    }
}
