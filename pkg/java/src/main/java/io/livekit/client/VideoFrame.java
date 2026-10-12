package io.livekit.client;

/** I420 bytes and metadata copied from one remote video frame. */
public record VideoFrame(byte[] data, int width, int height, long timestampUs,
                         Long userTimestampUs, Integer frameId, byte[] userData) {}
