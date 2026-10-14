package io.livekit.client;

/** kind: 0 monitor, 1 window. */
public record ScreenSource(String id, String label, int kind, int x, int y, int width, int height) {}
