package io.livekit.client;

/** Video frame metadata fields enabled for a published track. */
public record FrameMetadataFeatures(boolean userTimestamp, boolean frameId, boolean userData) {}
