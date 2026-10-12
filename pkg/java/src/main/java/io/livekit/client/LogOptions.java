package io.livekit.client;

/** Process-wide log levels: 0 trace through 5 off. */
public record LogOptions(int liveKitLevel, int webRtcLevel, int webSocketLevel) {
    public LogOptions {
        if (liveKitLevel < 0 || liveKitLevel > 5 || webRtcLevel < 0 || webRtcLevel > 5 ||
            webSocketLevel < 0 || webSocketLevel > 5)
            throw new IllegalArgumentException("Invalid log level");
    }
}
