package io.livekit.client;

/** Remote track preferences. Quality is -1 for unset or 0 low, 1 medium, 2 high, 3 off. */
public record RemoteTrackSettings(boolean enabled, int videoQuality, int width, int height,
                                  int fps, int priority) {
    public RemoteTrackSettings {
        if (videoQuality < -1 || videoQuality > 3 || width < 0 || height < 0 ||
            fps < 0 || priority < 0)
            throw new IllegalArgumentException("Invalid remote track settings");
        if ((width == 0) != (height == 0))
            throw new IllegalArgumentException("Width and height must both be set");
        if (videoQuality >= 0 && width != 0)
            throw new IllegalArgumentException("Quality and dimensions cannot both be set");
    }

    public RemoteTrackSettings() { this(true, -1, 0, 0, 0, 0); }
}
