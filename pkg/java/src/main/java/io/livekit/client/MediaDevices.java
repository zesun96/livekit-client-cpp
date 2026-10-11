package io.livekit.client;

import java.util.List;

/** Enumerates media devices and shareable screens without opening capture devices. */
public final class MediaDevices {
    private MediaDevices() {}
    public static List<MediaDevice> listDevices() { return List.of(Native.mediaDevices()); }
    public static List<ScreenSource> listScreenSources() { return List.of(Native.screenSources()); }
}
