package io.livekit.client;

import java.nio.charset.StandardCharsets;

/** Incremental text stream event: 0 open, 1 chunk, 2 closed, 3 failed. */
public record TextStreamEvent(int type, String streamId, String topic, String participantIdentity,
                              byte[] contentUtf8, long chunkIndex, String reason) {
    public String content() { return new String(contentUtf8, StandardCharsets.UTF_8); }
}
