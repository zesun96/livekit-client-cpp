package io.livekit.client;

/** Immutable remote DataTrack snapshot. Use identity and SID to subscribe. */
public record RemoteDataTrack(String participantIdentity, String sid, String name,
                              int frameEncoding, String customFrameEncoding, String schemaName,
                              int schemaEncoding, String customSchemaEncoding,
                              boolean published, int publisherHandle, boolean usesE2ee) {
    public RemoteDataTrack(String participantIdentity, String sid, String name,
                           int frameEncoding, String customFrameEncoding, String schemaName,
                           int schemaEncoding, String customSchemaEncoding, boolean published) {
        this(participantIdentity, sid, name, frameEncoding, customFrameEncoding, schemaName,
            schemaEncoding, customSchemaEncoding, published, 0, false);
    }

    public RemoteDataTrack(String participantIdentity, String sid, String name,
                           int frameEncoding, boolean published) {
        this(participantIdentity, sid, name, frameEncoding, "", "", 0, "", published, 0, false);
    }
}
