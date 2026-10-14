package io.livekit.client;

/** Target FPS is optional; capacities match the C ABI defaults. */
public record DataTrackSubscriptionOptions(Integer targetFps, int bufferCapacity,
                                           int maxPartialFrames) {
    public DataTrackSubscriptionOptions {
        if (targetFps != null && targetFps < 1)
            throw new IllegalArgumentException("Target FPS must be positive");
        if (bufferCapacity < 1 || maxPartialFrames < 1)
            throw new IllegalArgumentException("Reader capacities must be positive");
    }

    public DataTrackSubscriptionOptions() { this(null, 16, 1); }
}
