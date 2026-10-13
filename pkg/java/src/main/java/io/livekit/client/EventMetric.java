package io.livekit.client;

/** Timed event with indexes into the metrics string table. */
public record EventMetric(long label, long participantIdentity, long trackSid, long rid,
                          long startTimestampMs, Long endTimestampMs,
                          MetricTimestamp normalizedStartTimestamp,
                          MetricTimestamp normalizedEndTimestamp, String metadata) {}
