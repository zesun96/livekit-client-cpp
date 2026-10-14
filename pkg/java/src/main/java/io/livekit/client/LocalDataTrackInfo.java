package io.livekit.client;

/** Detached local DataTrack publication metadata. */
public record LocalDataTrackInfo(String sid, String name, int publisherHandle, boolean usesE2ee,
                                 int frameEncoding, String customFrameEncoding, String schemaName,
                                 int schemaEncoding, String customSchemaEncoding,
                                 boolean published) {}
