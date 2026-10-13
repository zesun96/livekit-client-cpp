package io.livekit.client;

/** One sample in a metric time series. */
public record MetricSample(long timestampMs, MetricTimestamp normalizedTimestamp,
                           float value) {}
