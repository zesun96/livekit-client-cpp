package io.livekit.client;

/** The payload is copied while the native callback is active. */
public record DataPacket(byte[] data, String topic, String participantIdentity, boolean reliable) {}
