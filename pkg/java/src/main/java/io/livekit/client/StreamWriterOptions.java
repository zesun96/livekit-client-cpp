package io.livekit.client;

import java.util.List;
import java.util.Map;
import java.util.Objects;

/** Options shared by text and byte stream writers. Text writers use reply, attachment, update and version. */
public record StreamWriterOptions(String topic, String mimeType, String name,
                                  List<String> destinations, Map<String, String> attributes,
                                  String replyToStreamId, List<String> attachedStreamIds,
                                  String streamId, Long totalSize, int chunkSize,
                                  boolean compress, boolean update, int version) {
    public StreamWriterOptions {
        destinations = List.copyOf(Objects.requireNonNull(destinations));
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
        attachedStreamIds = List.copyOf(Objects.requireNonNull(attachedStreamIds));
        if (totalSize != null && totalSize < 0) throw new IllegalArgumentException("Negative total size");
        if (chunkSize < 1) throw new IllegalArgumentException("Invalid chunk size");
    }

    public StreamWriterOptions(String topic) {
        this(topic, null, null, List.of(), Map.of(), null, List.of(), null, null, 15000,
            false, false, 0);
    }
}
