package io.livekit.client;

/** Copied SDK log record. Level is 0 trace through 5 off; source is 0 LiveKit, 1 WebRTC, 2 WebSocket. */
public record LogRecord(int level, int source, String message, String file, int line) {}
