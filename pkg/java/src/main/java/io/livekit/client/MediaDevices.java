package io.livekit.client;

import java.util.List;

/** Enumerates shareable screens and windows without opening a capture device. */
public final class MediaDevices {
    private MediaDevices() {}
    public static List<ScreenSource> listScreenSources() { return List.of(Native.screenSources()); }
}
