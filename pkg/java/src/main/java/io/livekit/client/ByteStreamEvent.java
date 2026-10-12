package io.livekit.client;

/** Incremental byte stream event: 0 open, 1 chunk, 2 closed, 3 failed. */
public record ByteStreamEvent(int type, String streamId, String topic, String participantIdentity,
                              String name, String mimeType, byte[] content, long chunkIndex,
                              String reason) {}
