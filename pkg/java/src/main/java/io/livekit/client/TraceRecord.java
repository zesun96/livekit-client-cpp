package io.livekit.client;

/** Copied SDK trace record. Phase and category values match the C ABI. */
public record TraceRecord(int phase, long category, String name, long timestampUs,
                          long threadId, long correlationId) {}
