package io.livekit.client;

import java.util.List;
import java.util.Objects;

/** Snapshot of a received metrics packet. */
public record MetricsMessage(long timestampMs, MetricTimestamp normalizedTimestamp,
                             String participantIdentity, List<String> stringData,
                             List<TimeSeriesMetric> timeSeries, List<EventMetric> events) {
    public MetricsMessage {
        stringData = List.copyOf(Objects.requireNonNull(stringData));
        timeSeries = List.copyOf(Objects.requireNonNull(timeSeries));
        events = List.copyOf(Objects.requireNonNull(events));
    }
}
