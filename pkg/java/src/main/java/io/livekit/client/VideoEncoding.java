package io.livekit.client;

/** Optional video bitrate and frame-rate limits. Zero leaves a limit unset. */
public record VideoEncoding(long maxBitrate, float maxFramerate) {
    public VideoEncoding {
        if (maxBitrate < 0 || !Float.isFinite(maxFramerate) || maxFramerate < 0)
            throw new IllegalArgumentException("Invalid video encoding");
    }
}
