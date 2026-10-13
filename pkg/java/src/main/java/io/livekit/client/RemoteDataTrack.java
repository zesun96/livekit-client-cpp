package io.livekit.client;

/** Immutable remote DataTrack snapshot. Use identity and SID to subscribe. */
public record RemoteDataTrack(String participantIdentity, String sid, String name,
                              int frameEncoding, String customFrameEncoding, String schemaName,
                              int schemaEncoding, String customSchemaEncoding,
                              boolean published) {
    public RemoteDataTrack(String participantIdentity, String sid, String name,
                           int frameEncoding, boolean published) {
        this(participantIdentity, sid, name, frameEncoding, "", "", 0, "", published);
    }
}
