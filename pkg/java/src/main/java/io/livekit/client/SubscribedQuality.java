package io.livekit.client;

/** Whether a video quality is currently subscribed. */
public record SubscribedQuality(int quality, boolean enabled) {}
