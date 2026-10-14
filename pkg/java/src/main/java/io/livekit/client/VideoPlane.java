package io.livekit.client;

/** Byte range and row stride for one input video plane. */
public record VideoPlane(long offset, long size, int stride) {
    public VideoPlane {
        if (offset < 0 || size < 0 || stride < 0)
            throw new IllegalArgumentException("Invalid video plane");
    }
}
