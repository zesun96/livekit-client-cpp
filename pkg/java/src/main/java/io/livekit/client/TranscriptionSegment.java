package io.livekit.client;

/** A timed piece of a received transcription. */
public record TranscriptionSegment(String id, String text, String language, long startTime,
                                   long endTime, boolean isFinal, long firstReceivedTime,
                                   long lastReceivedTime) {}
