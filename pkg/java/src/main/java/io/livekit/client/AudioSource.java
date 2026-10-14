package io.livekit.client;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;

/** Caller-owned audio source. Close tracks before closing their source. */
public final class AudioSource implements AutoCloseable {
    private long handle;
    private final int channels;
    private final int kind;
    private int tracks;

    public AudioSource(int sampleRate, int channels, int queueMs) {
        if (sampleRate <= 0 || channels <= 0 || queueMs <= 0) throw new IllegalArgumentException("Invalid audio format");
        this.channels = channels;
        this.kind = 0;
        handle = Native.createAudioSource(sampleRate, channels, queueMs);
    }

    private AudioSource(long handle, int channels, int kind) {
        this.handle = handle;
        this.channels = channels;
        this.kind = kind;
    }

    /** Opens a microphone. Pass null to use the default input device. */
    public static AudioSource createMicrophone(String deviceId, int queueMs,
                                                boolean echoCancellation, boolean autoGainControl,
                                                boolean noiseSuppression) {
        if (queueMs < 10 || queueMs % 10 != 0) throw new IllegalArgumentException("Invalid queue size");
        return new AudioSource(Native.createDeviceAudioSource(1, Native.utf8(deviceId), queueMs,
            echoCancellation, autoGainControl, noiseSuppression), 1, 1);
    }

    public static AudioSource createMicrophone() {
        return createMicrophone(null, 200, true, true, true);
    }

    /** Opens output loopback capture. Pass null to use the default output device. */
    public static AudioSource createSystemAudio(String deviceId, int queueMs) {
        if (queueMs < 10 || queueMs % 10 != 0) throw new IllegalArgumentException("Invalid queue size");
        return new AudioSource(Native.createDeviceAudioSource(2, Native.utf8(deviceId), queueMs,
            false, false, false), 2, 2);
    }

    public static AudioSource createSystemAudio() { return createSystemAudio(null, 200); }

    private void requireDevice() {
        if (kind == 0) throw new IllegalStateException("Synthetic audio source has no device");
    }

    public synchronized void startCapture() {
        requireDevice();
        Native.controlDeviceAudioSource(handle(), kind, 0, null);
    }

    public synchronized void stopCapture() {
        requireDevice();
        Native.controlDeviceAudioSource(handle(), kind, 1, null);
    }

    public synchronized void switchDevice(String deviceId) {
        requireDevice();
        Native.controlDeviceAudioSource(handle(), kind, 2,
            Native.utf8(java.util.Objects.requireNonNull(deviceId)));
    }

    public synchronized boolean isCapturing() {
        requireDevice();
        return Native.deviceAudioSourceCapturing(handle(), kind);
    }

    public synchronized String deviceId() {
        requireDevice();
        return Native.string(Native.deviceAudioSourceId(handle(), kind));
    }

    private void requireMicrophone() {
        if (kind != 1) throw new IllegalStateException("Audio source is not a microphone");
    }

    public synchronized void setMicrophoneMuted(boolean muted) {
        requireMicrophone();
        Native.setMicrophoneMuted(handle(), muted);
    }

    public synchronized boolean microphoneMuted() {
        requireMicrophone();
        return Native.microphoneMuted(handle());
    }

    public synchronized void setMicrophoneVolume(float volume) {
        requireMicrophone();
        if (!Float.isFinite(volume) || volume < 0 || volume > 1)
            throw new IllegalArgumentException("Volume must be between 0 and 1");
        Native.setMicrophoneVolume(handle(), volume);
    }

    public synchronized float microphoneVolume() {
        requireMicrophone();
        return Native.microphoneVolume(handle());
    }

    public synchronized MicrophoneProcessingOptions microphoneProcessingOptions() {
        requireMicrophone();
        return Native.microphoneProcessingOptions(handle());
    }

    public synchronized void setMicrophoneProcessingOptions(MicrophoneProcessingOptions options) {
        requireMicrophone();
        Objects.requireNonNull(options);
        Native.setMicrophoneProcessingOptions(handle(), options.echoCancellation(),
            options.autoGainControl(), options.noiseSuppression());
    }

    public synchronized MicrophoneProcessingStats microphoneProcessingStats() {
        requireMicrophone();
        return Native.microphoneProcessingStats(handle());
    }

    synchronized long handle() {
        if (handle == 0) throw new IllegalStateException("AudioSource is closed");
        return handle;
    }

    synchronized void retain() { handle(); tracks++; }
    synchronized void release() { tracks--; }

    public synchronized void capture(short[] interleavedPcm) {
        if (kind != 0) throw new IllegalStateException("Device source captures its own audio");
        if (interleavedPcm.length % channels != 0) throw new IllegalArgumentException("Incomplete audio frame");
        Native.captureAudio(handle(), interleavedPcm, interleavedPcm.length / channels);
    }

    public CompletableFuture<Void> captureAsync(short[] interleavedPcm) {
        short[] samples = Objects.requireNonNull(interleavedPcm).clone();
        return CompletableFuture.runAsync(() -> capture(samples));
    }

    public synchronized long queuedDurationMs() {
        return Native.audioSourceQueuedDurationMs(handle());
    }

    public synchronized void clearQueue() {
        Native.clearAudioSourceQueue(handle());
    }

    public synchronized void waitForPlayout(int timeoutMs) {
        if (timeoutMs < 0) throw new IllegalArgumentException("Negative playout timeout");
        Native.waitForAudioSourcePlayout(handle(), timeoutMs);
    }

    public CompletableFuture<Void> waitForPlayoutAsync(int timeoutMs) {
        return CompletableFuture.runAsync(() -> waitForPlayout(timeoutMs));
    }

    @Override public synchronized void close() {
        if (tracks != 0) throw new IllegalStateException("Close audio tracks first");
        if (handle != 0) { Native.destroyAudioSource(handle); handle = 0; }
    }
}
