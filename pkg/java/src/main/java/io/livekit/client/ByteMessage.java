package io.livekit.client;

public record ByteMessage(String streamId, byte[] data, String name, String mimeType,
                          String topic, String participantIdentity, long timestamp) {}
