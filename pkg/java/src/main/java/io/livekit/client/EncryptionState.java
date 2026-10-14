package io.livekit.client;

/** Detached E2EE frame cryptor state; enum values match the C ABI. */
public record EncryptionState(String trackId, String participantIdentity, int kind,
                              int direction, boolean enabled, long keyIndex, int state) {}
