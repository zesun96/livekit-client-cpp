package io.livekit.client;

/** Detached state of a subscribed remote track. Enum values match the C ABI. */
public record RemoteTrackState(String sid, String name, int kind, int source, int streamState,
                               int width, int height, boolean enabled) {}
