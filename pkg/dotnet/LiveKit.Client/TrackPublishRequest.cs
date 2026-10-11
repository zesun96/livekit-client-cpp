using System;

namespace LiveKit.Client;

public enum TrackSource { Unknown = 0, Camera = 1, Microphone = 2,
    ScreenShare = 3, ScreenShareAudio = 4 }
public enum VideoCodec { Vp8 = 0, H264 = 1, Vp9 = 2, Av1 = 3 }
public enum BackupCodecPolicy { PreferRegression = 0, Simulcast = 1, Regression = 2 }
public sealed record VideoEncodingOptions(ulong MaxBitrate = 0, float MaxFramerate = 0);

public sealed record TrackPublishRequest
{
    public TrackSource Source { get; init; } = TrackSource.Unknown;
    public bool Dtx { get; init; } = true;
    public bool Red { get; init; } = true;
    public bool Simulcast { get; init; } = true;
    public string? Stream { get; init; }
    public VideoCodec VideoCodec { get; init; } = VideoCodec.Vp8;
    public string? ScalabilityMode { get; init; }
    public bool BackupVideoCodecEnabled { get; init; }
    public VideoCodec BackupVideoCodec { get; init; } = VideoCodec.Vp8;
    public BackupCodecPolicy BackupCodecPolicy { get; init; } = BackupCodecPolicy.PreferRegression;
    public VideoEncodingOptions? VideoEncoding { get; init; }
    public VideoEncodingOptions? BackupVideoEncoding { get; init; }
    public bool UserTimestampMetadata { get; init; }
    public bool FrameIdMetadata { get; init; }
    public bool UserDataMetadata { get; init; }
    public bool PreconnectBuffer { get; init; }
    public VideoDegradationPreference? DegradationPreference { get; init; }
}

public sealed partial class LocalTrack
{
    private static Native.TrackPublishOptions MakePublishOptions(TrackPublishRequest request,
        NativeArena arena)
    {
        ArgumentNullException.ThrowIfNull(request);
        var options = new Native.TrackPublishOptions();
        Native.lk_track_publish_options_init(ref options);
        options.Source = (int)request.Source;
        options.Dtx = request.Dtx ? 1 : 0;
        options.Red = request.Red ? 1 : 0;
        options.Simulcast = request.Simulcast ? 1 : 0;
        if (request.Stream is not null) options.Stream = arena.Utf8(request.Stream);
        options.VideoCodec = (int)request.VideoCodec;
        if (request.ScalabilityMode is not null)
            options.ScalabilityMode = arena.Utf8(request.ScalabilityMode);
        options.BackupVideoCodecEnabled = request.BackupVideoCodecEnabled ? 1 : 0;
        options.BackupVideoCodec = (int)request.BackupVideoCodec;
        options.BackupCodecPolicy = (int)request.BackupCodecPolicy;
        if (request.VideoEncoding is not null)
        {
            options.VideoEncoding.MaxBitrate = request.VideoEncoding.MaxBitrate;
            options.VideoEncoding.MaxFramerate = request.VideoEncoding.MaxFramerate;
        }
        if (request.BackupVideoEncoding is not null)
        {
            options.BackupVideoEncoding.MaxBitrate = request.BackupVideoEncoding.MaxBitrate;
            options.BackupVideoEncoding.MaxFramerate = request.BackupVideoEncoding.MaxFramerate;
        }
        options.FrameMetadataFeatures.UserTimestamp = request.UserTimestampMetadata ? 1 : 0;
        options.FrameMetadataFeatures.FrameId = request.FrameIdMetadata ? 1 : 0;
        options.FrameMetadataFeatures.UserData = request.UserDataMetadata ? 1 : 0;
        options.PreconnectBuffer = request.PreconnectBuffer ? 1 : 0;
        options.HasDegradationPreference = request.DegradationPreference.HasValue ? 1 : 0;
        if (request.DegradationPreference.HasValue)
            options.DegradationPreference = (int)request.DegradationPreference.Value;
        return options;
    }

    public void Publish(TrackPublishRequest request) => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        using var arena = new NativeArena();
        var options = MakePublishOptions(request, arena);
        Native.Check(Native.lk_local_track_publish_with_options(_room.NativeHandle,
            Handle, ref options));
    });

    public void PublishScreenShareVideo(TrackPublishRequest request) => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        using var arena = new NativeArena();
        var options = MakePublishOptions(request, arena);
        Native.Check(Native.lk_local_track_publish_screen_share_video_with_options(
            _room.NativeHandle, Handle, ref options));
    });

    public void PublishScreenShareAudio(TrackPublishRequest request) => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        using var arena = new NativeArena();
        var options = MakePublishOptions(request, arena);
        Native.Check(Native.lk_local_track_publish_screen_share_audio_with_options(
            _room.NativeHandle, Handle, ref options));
    });
}

internal static partial class Native
{
    [System.Runtime.InteropServices.DllImport(Library,
        CallingConvention = System.Runtime.InteropServices.CallingConvention.Cdecl,
        EntryPoint = "lk_local_track_publish_screen_share_video")]
    internal static extern int lk_local_track_publish_screen_share_video_with_options(
        RoomHandle room, TrackHandle track, ref TrackPublishOptions options);

    [System.Runtime.InteropServices.DllImport(Library,
        CallingConvention = System.Runtime.InteropServices.CallingConvention.Cdecl,
        EntryPoint = "lk_local_track_publish_screen_share_audio")]
    internal static extern int lk_local_track_publish_screen_share_audio_with_options(
        RoomHandle room, TrackHandle track, ref TrackPublishOptions options);
}
