package io.livekit.client;

import java.util.List;
import java.util.Objects;

/** Subscription update for a published video track. */
public record SubscribedQualityChange(LocalTrackPublication track, Participant participant,
                                      String trackSid,
                                      List<SubscribedQuality> qualities,
                                      List<SubscribedCodec> codecs) {
    public SubscribedQualityChange {
        qualities = List.copyOf(Objects.requireNonNull(qualities));
        codecs = List.copyOf(Objects.requireNonNull(codecs));
    }
}
