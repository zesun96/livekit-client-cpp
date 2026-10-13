package io.livekit.client;

/** Context supplied to a custom reconnect policy on the recovery thread. */
public record ReconnectContext(long retryCount, long elapsedMs, int reason, String serverUrl) {}
