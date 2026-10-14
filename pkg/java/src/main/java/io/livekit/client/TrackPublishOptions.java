package io.livekit.client;

import java.util.Objects;

/** Publication controls corresponding to `lk_track_publish_options_t`. */
public record TrackPublishOptions(
    int source, boolean dtx, boolean red, boolean simulcast, String stream, int videoCodec,
    String scalabilityMode, boolean backupVideoCodecEnabled, int backupVideoCodec,
    int backupCodecPolicy, VideoEncoding videoEncoding, VideoEncoding backupVideoEncoding,
    FrameMetadataFeatures frameMetadataFeatures, boolean preconnectBuffer,
    Integer degradationPreference) {
    public TrackPublishOptions {
        if (source < 0 || source > 4 || videoCodec < 0 || videoCodec > 3 ||
            backupVideoCodec < 0 || backupVideoCodec > 3 ||
            backupCodecPolicy < 0 || backupCodecPolicy > 2 ||
            (degradationPreference != null &&
                (degradationPreference < 0 || degradationPreference > 3)))
            throw new IllegalArgumentException("Invalid track publish enum value");
        stream = stream == null ? "" : stream;
        scalabilityMode = Objects.requireNonNull(scalabilityMode);
        videoEncoding = Objects.requireNonNull(videoEncoding);
        backupVideoEncoding = Objects.requireNonNull(backupVideoEncoding);
        frameMetadataFeatures = Objects.requireNonNull(frameMetadataFeatures);
    }

    public TrackPublishOptions(int source) {
        this(source, true, true, true, "", 0, "L3T3_KEY", false, 0, 0,
            new VideoEncoding(0, 0), new VideoEncoding(0, 0),
            new FrameMetadataFeatures(false, false, false), false, null);
    }
}
