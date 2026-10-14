package io.livekit.client;

/** Detached local track publication. Kind is 1 for audio and 2 for video. */
public record LocalTrackPublication(String participantIdentity, String sid, String name,
                                    String mimeType, int kind, int source, boolean muted,
                                    int width, int height) {
    static LocalTrackPublication from(RemoteTrackPublication track) {
        return new LocalTrackPublication(track.participantIdentity(), track.sid(), track.name(),
            track.mimeType(), track.kind(), track.source(), track.muted(), track.width(),
            track.height());
    }
}
