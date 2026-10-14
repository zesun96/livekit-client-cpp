package io.livekit.client;

import java.util.List;
import java.util.Map;
import java.util.Objects;

/** Detached metadata for an outgoing incremental stream. Kind matches the C ABI. */
public record StreamWriterInfo(int kind, String streamId, String mimeType, String topic,
                               String participantIdentity, String name, String replyToStreamId,
                               Long totalSize, long timestamp, Map<String, String> attributes,
                               List<String> attachedStreamIds) {
    public StreamWriterInfo {
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
        attachedStreamIds = List.copyOf(Objects.requireNonNull(attachedStreamIds));
    }
}
