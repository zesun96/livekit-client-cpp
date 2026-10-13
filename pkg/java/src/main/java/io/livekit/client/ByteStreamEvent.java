package io.livekit.client;

import java.util.Map;
import java.util.Objects;

/** Incremental byte stream event: 0 open, 1 chunk, 2 closed, 3 failed. */
public record ByteStreamEvent(int type, String streamId, String topic, String participantIdentity,
                              String name, String mimeType, byte[] content, long chunkIndex,
                              String reason, Long totalSize, Map<String, String> attributes,
                              long timestamp) {
    public ByteStreamEvent {
        content = Objects.requireNonNull(content).clone();
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
    }

    public ByteStreamEvent(int type, String streamId, String topic, String participantIdentity,
                           String name, String mimeType, byte[] content, long chunkIndex,
                           String reason) {
        this(type, streamId, topic, participantIdentity, name, mimeType, content, chunkIndex,
            reason, null, Map.of(), 0);
    }

    @Override public byte[] content() { return content.clone(); }
}
