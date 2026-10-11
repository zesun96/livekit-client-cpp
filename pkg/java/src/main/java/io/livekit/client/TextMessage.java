package io.livekit.client;

public record TextMessage(String streamId, String text, String topic, String participantIdentity,
                          long timestamp) {}
