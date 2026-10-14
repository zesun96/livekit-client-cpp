package io.livekit.client;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.Objects;

/** Incremental text stream event: 0 open, 1 chunk, 2 closed, 3 failed. */
public record TextStreamEvent(int type, String streamId, String topic, String participantIdentity,
                              byte[] contentUtf8, long chunkIndex, String reason,
                              String mimeType, Long totalSize, Map<String, String> attributes,
                              long timestamp, String replyToStreamId,
                              List<String> attachedStreamIds) {
    public TextStreamEvent {
        contentUtf8 = Objects.requireNonNull(contentUtf8).clone();
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
        attachedStreamIds = List.copyOf(Objects.requireNonNull(attachedStreamIds));
    }

    public TextStreamEvent(int type, String streamId, String topic, String participantIdentity,
                           byte[] contentUtf8, long chunkIndex, String reason) {
        this(type, streamId, topic, participantIdentity, contentUtf8, chunkIndex, reason,
            "", null, Map.of(), 0, "", List.of());
    }

    @Override public byte[] contentUtf8() { return contentUtf8.clone(); }
    public String content() { return new String(contentUtf8, StandardCharsets.UTF_8); }
}
