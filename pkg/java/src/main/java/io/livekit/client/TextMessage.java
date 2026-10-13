package io.livekit.client;

import java.util.List;
import java.util.Map;
import java.util.Objects;

public record TextMessage(String streamId, String text, String topic, String participantIdentity,
                          String replyToStreamId, long timestamp, List<String> attachedStreamIds,
                          Map<String, String> attributes) {
    public TextMessage {
        attachedStreamIds = List.copyOf(Objects.requireNonNull(attachedStreamIds));
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
    }

    public TextMessage(String streamId, String text, String topic, String participantIdentity,
                       long timestamp) {
        this(streamId, text, topic, participantIdentity, "", timestamp, List.of(), Map.of());
    }
}
