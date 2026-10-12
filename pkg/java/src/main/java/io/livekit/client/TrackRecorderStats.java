package io.livekit.client;

/** State: 0 recording, 1 stopped, 2 failed. Counters are unsigned 64-bit values. */
public record TrackRecorderStats(int state, long framesWritten, long bytesWritten,
                                 long framesDropped) {}
