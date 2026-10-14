package io.livekit.client;

import java.util.Objects;

/** DataTrack publication name, frame encoding, and optional schema ID. */
public record DataTrackPublishOptions(String name, int frameEncoding, String customFrameEncoding,
                                      String schemaName, int schemaEncoding,
                                      String customSchemaEncoding) {
    public DataTrackPublishOptions {
        if (Objects.requireNonNull(name).isEmpty()) throw new IllegalArgumentException("Empty DataTrack name");
        if (frameEncoding < 0 || frameEncoding > 8 || schemaEncoding < 0 || schemaEncoding > 8)
            throw new IllegalArgumentException("Invalid DataTrack encoding");
        customFrameEncoding = customFrameEncoding == null ? "" : customFrameEncoding;
        customSchemaEncoding = customSchemaEncoding == null ? "" : customSchemaEncoding;
        schemaName = schemaName == null ? "" : schemaName;
        if (frameEncoding == 8 && customFrameEncoding.isEmpty())
            throw new IllegalArgumentException("Custom frame encoding is required");
        if (schemaEncoding == 8 && customSchemaEncoding.isEmpty())
            throw new IllegalArgumentException("Custom schema encoding is required");
        if (schemaName.isEmpty() && schemaEncoding != 0)
            throw new IllegalArgumentException("Schema name is required");
    }

    public DataTrackPublishOptions(String name, int frameEncoding) {
        this(name, frameEncoding, "", "", 0, "");
    }
}
