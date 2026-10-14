package io.livekit.client;

/** kind: 0 audio input, 1 audio output, 2 video input. */
public record MediaDevice(String id, String label, int kind, boolean isDefault) {}
