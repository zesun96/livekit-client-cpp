package io.livekit.client;

public record ChatMessage(String id, long timestamp, Long editTimestamp, String message,
                          String participantIdentity, boolean deleted, boolean generated) {
    public ChatMessage(String id, long timestamp, String message, String participantIdentity,
                       boolean deleted, boolean generated) {
        this(id, timestamp, null, message, participantIdentity, deleted, generated);
    }
}
