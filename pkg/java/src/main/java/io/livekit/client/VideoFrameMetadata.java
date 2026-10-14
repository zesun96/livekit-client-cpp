package io.livekit.client;

/** Optional metadata for a captured video frame. */
public record VideoFrameMetadata(Long userTimestampUs, Long frameId, byte[] userData) {
    public VideoFrameMetadata {
        if (frameId != null && (frameId < 0 || frameId > 0xffff_ffffL))
            throw new IllegalArgumentException("Frame ID is outside uint32 range");
        userData = userData == null ? null : userData.clone();
    }

    @Override public byte[] userData() { return userData == null ? null : userData.clone(); }
}
