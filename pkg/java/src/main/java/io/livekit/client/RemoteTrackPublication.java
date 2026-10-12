package io.livekit.client;

/** Detached remote track publication. Kind is 1 for audio and 2 for video. */
public record RemoteTrackPublication(String participantIdentity, String sid, String name,
                                     String mimeType, int kind, int source, boolean muted,
                                     int width, int height) {}
