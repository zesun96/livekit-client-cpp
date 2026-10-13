package io.livekit.client;

/** Detached participant value; safe to retain after a room callback or room close. */
public record Participant(String sid, String identity, String name, String metadata,
                          float audioLevel, int connectionQuality, boolean speaking,
                          boolean local) {
    public Participant(String sid, String identity, String name, String metadata) {
        this(sid, identity, name, metadata, 0, 0, false, false);
    }
}
