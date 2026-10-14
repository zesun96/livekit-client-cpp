package io.livekit.client;

/** Detached room state supplied by update and move callbacks. */
public record RoomSnapshot(String sid, String name, String metadata, boolean recording) {}
