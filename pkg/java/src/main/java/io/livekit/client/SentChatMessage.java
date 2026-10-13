package io.livekit.client;

/** ID and original timestamp required to edit a sent chat message. */
public record SentChatMessage(String id, long timestamp) {}
