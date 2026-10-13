package io.livekit.client;

import java.util.List;
import java.util.Map;
import java.util.Objects;

/** Options for a complete text, byte, or file message. */
public record DataSendOptions(String topic, String mimeType, String name,
                              List<String> destinations, Map<String, String> attributes,
                              String replyToStreamId, List<String> attachedStreamIds,
                              int chunkSize, boolean compress) {
    public DataSendOptions {
        destinations = List.copyOf(Objects.requireNonNull(destinations));
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
        attachedStreamIds = List.copyOf(Objects.requireNonNull(attachedStreamIds));
        if (chunkSize < 1) throw new IllegalArgumentException("Invalid chunk size");
    }

    public DataSendOptions(String topic) {
        this(topic, null, null, List.of(), Map.of(), null, List.of(), 15000, false);
    }
}
