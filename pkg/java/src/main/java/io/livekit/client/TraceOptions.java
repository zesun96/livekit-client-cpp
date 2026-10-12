package io.livekit.client;

/** Process-wide trace state and category mask. */
public record TraceOptions(boolean enabled, long categoryMask) {}
