package io.livekit.client;

/** Outgoing stream result. Status: 0 completed, 1 cancelled, 2 failed. */
public record StreamCompletion(int status, String streamId, long bytesSent, Long totalSize,
                               String reason, int errorCode) {}
