package io.livekit.client;

import java.util.List;
import java.util.Objects;

/** Subscribed qualities for one codec. */
public record SubscribedCodec(String codec, List<SubscribedQuality> qualities) {
    public SubscribedCodec {
        qualities = List.copyOf(Objects.requireNonNull(qualities));
    }
}
