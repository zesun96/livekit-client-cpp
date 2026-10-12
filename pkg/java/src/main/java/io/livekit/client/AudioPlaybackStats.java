package io.livekit.client;

/** Snapshot of room speaker playout counters and delay estimates. */
public record AudioPlaybackStats(long queuedFrames, long playedFrames, long droppedFrames,
                                 long underrunFrames, int bufferedDurationMs,
                                 int deviceLatencyMs, int estimatedDelayMs) {}
