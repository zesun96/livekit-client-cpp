package io.livekit.client;

/** Detached DataTrack publication metadata. */
public record DataTrackPublication(String participantIdentity, int publisherHandle, String sid,
                                   String name, boolean usesE2ee, boolean hasFrameEncoding,
                                   int frameEncoding, String customFrameEncoding,
                                   boolean hasSchema, String schemaName, int schemaEncoding,
                                   String customSchemaEncoding) {}
