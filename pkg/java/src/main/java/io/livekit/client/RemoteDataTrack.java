package io.livekit.client;

/** Immutable remote DataTrack snapshot. Use identity and SID to subscribe. */
public record RemoteDataTrack(String participantIdentity, String sid, String name,
                              int frameEncoding, boolean published) {}
