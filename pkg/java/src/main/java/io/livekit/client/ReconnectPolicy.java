package io.livekit.client;

/** Return a delay in milliseconds, or null to stop reconnecting. */
@FunctionalInterface
public interface ReconnectPolicy {
    Long nextDelayMs(ReconnectContext context);
}
