package io.livekit.client;

import java.util.List;
import java.util.Objects;

/** Copied video input. An empty plane list selects the canonical layout for the format. */
public record VideoFrameInput(byte[] data, int width, int height, long timestampUs,
                              int rotation, int format, List<VideoPlane> planes,
                              VideoFrameMetadata metadata) {
    public VideoFrameInput {
        data = Objects.requireNonNull(data).clone();
        planes = List.copyOf(Objects.requireNonNull(planes));
        if (data.length == 0 || width <= 0 || height <= 0 || format < 0 || format > 10 ||
            (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270))
            throw new IllegalArgumentException("Invalid video frame input");
        for (VideoPlane plane : planes) {
            if (plane.offset() > data.length || plane.size() > data.length - plane.offset())
                throw new IllegalArgumentException("Video plane exceeds frame data");
        }
    }

    @Override public byte[] data() { return data.clone(); }
}
