package io.livekit.client;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;

/** Caller-owned video source. Close tracks before closing their source. */
public final class VideoSource implements AutoCloseable {
    private long handle;
    private final int kind;
    private int tracks;

    public VideoSource(boolean screencast) { kind = 0; handle = Native.createVideoSource(screencast); }

    private VideoSource(long handle, int kind) { this.handle = handle; this.kind = kind; }

    /** Opens a camera. Pass null to use the default device. */
    public static VideoSource createCamera(String deviceId, int width, int height, int framesPerSecond) {
        if (width < 1 || height < 1 || framesPerSecond < 1)
            throw new IllegalArgumentException("Invalid camera format");
        return new VideoSource(Native.createDeviceVideoSource(1, Native.utf8(deviceId), width,
            height, framesPerSecond, false), 1);
    }

    public static VideoSource createCamera() { return createCamera(null, 1280, 720, 30); }

    /** Opens a screen or window returned by MediaDevices.listScreenSources(). */
    public static VideoSource createScreen(String sourceId, int framesPerSecond, boolean includeCursor) {
        if (sourceId == null || sourceId.isEmpty() || framesPerSecond < 1 || framesPerSecond > 60)
            throw new IllegalArgumentException("Invalid screen source or frame rate");
        return new VideoSource(Native.createDeviceVideoSource(2, Native.utf8(sourceId), 0, 0,
            framesPerSecond, includeCursor), 2);
    }

    public static VideoSource createScreen(String sourceId) { return createScreen(sourceId, 15, true); }

    private void requireDevice() {
        if (kind == 0) throw new IllegalStateException("Synthetic video source has no device");
    }

    public synchronized void startCapture() {
        requireDevice();
        Native.controlDeviceVideoSource(handle(), kind, 0, null);
    }

    public synchronized void stopCapture() {
        requireDevice();
        Native.controlDeviceVideoSource(handle(), kind, 1, null);
    }

    public synchronized void switchDevice(String id) {
        requireDevice();
        Native.controlDeviceVideoSource(handle(), kind, 2,
            Native.utf8(java.util.Objects.requireNonNull(id)));
    }

    public synchronized boolean isCapturing() {
        requireDevice();
        return Native.deviceVideoSourceCapturing(handle(), kind);
    }

    public synchronized String deviceId() {
        requireDevice();
        return Native.string(Native.deviceVideoSourceId(handle(), kind));
    }

    synchronized long handle() {
        if (handle == 0) throw new IllegalStateException("VideoSource is closed");
        return handle;
    }

    synchronized void retain() { handle(); tracks++; }
    synchronized void release() { tracks--; }

    public synchronized void captureI420(byte[] data, int width, int height, long timestampUs) {
        if (kind != 0) throw new IllegalStateException("Device source captures its own video");
        if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0 ||
                data.length != (long) width * height * 3 / 2) throw new IllegalArgumentException("Invalid I420 frame");
        Native.captureI420(handle(), data, width, height, timestampUs);
    }

    /** Capture any C ABI video format with optional planes and frame metadata. */
    public synchronized void captureFrame(VideoFrameInput frame) {
        if (kind != 0) throw new IllegalStateException("Device source captures its own video");
        Objects.requireNonNull(frame);
        long[] offsets = new long[frame.planes().size()];
        long[] sizes = new long[offsets.length];
        int[] strides = new int[offsets.length];
        for (int i = 0; i < offsets.length; i++) {
            VideoPlane plane = frame.planes().get(i);
            offsets[i] = plane.offset();
            sizes[i] = plane.size();
            strides[i] = plane.stride();
        }
        VideoFrameMetadata metadata = frame.metadata();
        Native.captureVideoFrame(handle(), frame.data(), frame.width(), frame.height(),
            frame.timestampUs(), frame.rotation(), frame.format(), offsets, sizes, strides,
            metadata == null ? null : metadata.userTimestampUs(),
            metadata == null ? null : metadata.frameId(),
            metadata == null ? null : metadata.userData());
    }

    public CompletableFuture<Void> captureFrameAsync(VideoFrameInput frame) {
        return CompletableFuture.runAsync(() -> captureFrame(frame));
    }

    public CompletableFuture<Void> captureI420Async(byte[] data, int width, int height,
                                                    long timestampUs) {
        byte[] frame = Objects.requireNonNull(data).clone();
        return CompletableFuture.runAsync(() -> captureI420(frame, width, height, timestampUs));
    }

    @Override public synchronized void close() {
        if (tracks != 0) throw new IllegalStateException("Close video tracks first");
        if (handle != 0) { Native.destroyVideoSource(handle); handle = 0; }
    }
}
