package io.livekit.client;

import java.util.Map;
import java.util.Objects;

public record ByteMessage(String streamId, byte[] data, String name, String mimeType,
                          String topic, String participantIdentity, long timestamp,
                          Map<String, String> attributes) {
    public ByteMessage {
        data = Objects.requireNonNull(data).clone();
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
    }

    public ByteMessage(String streamId, byte[] data, String name, String mimeType,
                       String topic, String participantIdentity, long timestamp) {
        this(streamId, data, name, mimeType, topic, participantIdentity, timestamp, Map.of());
    }

    @Override public byte[] data() { return data.clone(); }
}
