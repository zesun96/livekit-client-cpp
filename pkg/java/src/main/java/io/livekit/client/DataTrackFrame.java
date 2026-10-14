package io.livekit.client;

/** Owned frame bytes returned by a bounded DataTrack reader. */
public record DataTrackFrame(byte[] data, long userTimestamp, boolean hasUserTimestamp) {}
