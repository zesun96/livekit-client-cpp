package io.livekit.client;

public record ChatMessage(String id, long timestamp, String message, String participantIdentity,
                          boolean deleted, boolean generated) {}
