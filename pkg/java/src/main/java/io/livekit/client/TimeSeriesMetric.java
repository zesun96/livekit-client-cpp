package io.livekit.client;

import java.util.List;
import java.util.Objects;

/** Time series with indexes into the metrics string table. */
public record TimeSeriesMetric(long label, long participantIdentity, long trackSid, long rid,
                               List<MetricSample> samples) {
    public TimeSeriesMetric {
        samples = List.copyOf(Objects.requireNonNull(samples));
    }
}
