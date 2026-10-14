package io.livekit.client;

import java.util.Objects;

/** DataTrack schema value. Encoding follows lk_data_track_schema_encoding_t. */
public record DataTrackSchema(String name, int encoding, String customEncoding, byte[] definition) {
    public DataTrackSchema {
        if (Objects.requireNonNull(name).isEmpty()) throw new IllegalArgumentException("Empty schema name");
        if (encoding < 0 || encoding > 8) throw new IllegalArgumentException("Invalid schema encoding");
        customEncoding = customEncoding == null ? "" : customEncoding;
        if (encoding == 8 && customEncoding.isEmpty())
            throw new IllegalArgumentException("Custom schema encoding is required");
        definition = Objects.requireNonNull(definition).clone();
    }

    @Override public byte[] definition() { return definition.clone(); }
}
