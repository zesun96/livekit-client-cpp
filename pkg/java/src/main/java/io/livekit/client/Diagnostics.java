package io.livekit.client;

import java.util.Objects;

/** Process-wide SDK logging and JSON tracing controls. */
public final class Diagnostics {
    private Diagnostics() {}

    public static LogOptions logOptions() { return Native.logOptions(); }
    public static void setLogOptions(LogOptions options) {
        Objects.requireNonNull(options);
        Native.setLogOptions(options.liveKitLevel(), options.webRtcLevel(),
                             options.webSocketLevel());
    }

    /** Set enabled categories before starting a JSON trace; mask 127 selects all categories. */
    public static void setTraceOptions(boolean enabled, long categoryMask) {
        if (categoryMask < 0 || (categoryMask & ~127L) != 0)
            throw new IllegalArgumentException("Invalid trace category mask");
        Native.setTraceOptions(enabled, categoryMask);
    }
    public static TraceOptions traceOptions() { return Native.traceOptions(); }

    public static void startTraceJson(String path) {
        if (path == null || path.isEmpty()) throw new IllegalArgumentException("Trace path is required");
        Native.startTraceJson(Native.utf8(path));
    }

    public static void stopTrace() { Native.stopTrace(); }
}
