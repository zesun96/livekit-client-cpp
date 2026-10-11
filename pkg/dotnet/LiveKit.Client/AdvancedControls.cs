using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public enum DisconnectReason
{
    Unknown = 0, ClientInitiated = 1, DuplicateIdentity = 2, ServerShutdown = 3,
    ParticipantRemoved = 4, RoomDeleted = 5, StateMismatch = 6, JoinFailure = 7,
    Migration = 8, SignalClose = 9, RoomClosed = 10, UserUnavailable = 11,
    UserRejected = 12, SipTrunkFailure = 13, ConnectionTimeout = 14,
    MediaFailure = 15, AgentError = 16,
}

public enum VideoDegradationPreference
{
    MaintainFramerate = 0, MaintainResolution = 1, Balanced = 2, Disabled = 3,
}

public sealed record AudioPlaybackStats(ulong QueuedFrames, ulong PlayedFrames,
    ulong DroppedFrames, ulong UnderrunFrames, uint BufferedDurationMs,
    uint DeviceLatencyMs, uint EstimatedDelayMs);

public sealed record MicrophoneProcessingOptions(bool EchoCancellation,
    bool AutoGainControl, bool NoiseSuppression);

public sealed record MicrophoneProcessingStats(ulong CaptureFramesProcessed,
    ulong RenderFramesProcessed, ulong CaptureProcessingErrors,
    ulong RenderProcessingErrors, ulong FramesDropped, bool EchoCancellationEnabled,
    double? EchoReturnLossDb, double? EchoReturnLossEnhancementDb,
    double? ResidualEchoLikelihood, double? ResidualEchoLikelihoodRecentMax,
    int? DelayMedianMs, int? DelayStandardDeviationMs, int? DelayMs);

public sealed partial class Room
{
    public DisconnectReason LastDisconnectReason =>
        Run(() => (DisconnectReason)Native.lk_room_disconnect_reason(_handle));

    public void RepublishAllTracks() =>
        Run(() => Native.Check(Native.lk_room_republish_all_tracks(_handle)));

    public AudioPlaybackStats GetAudioPlaybackStats() => Run(() =>
    {
        var native = new Native.AudioPlaybackStats
        {
            StructSize = (nuint)Marshal.SizeOf<Native.AudioPlaybackStats>(),
        };
        Native.Check(Native.lk_room_audio_playback_stats(_handle, ref native));
        return new AudioPlaybackStats(native.QueuedFrames, native.PlayedFrames,
            native.DroppedFrames, native.UnderrunFrames, native.BufferedDurationMs,
            native.DeviceLatencyMs, native.EstimatedDelayMs);
    });

    public void RemoveAllE2eeParticipantKeys(string participantIdentity)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        Run(() => Native.Check(Native.lk_room_e2ee_remove_participant_keys(_handle,
            participantIdentity)));
    }
}

public sealed partial class AudioSource
{
    private void RequireMicrophone()
    {
        if (_captureKind != AudioCaptureKind.Microphone)
            throw new InvalidOperationException("Source is not a microphone");
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
    }

    public bool IsMicrophoneMuted
    {
        get { RequireMicrophone(); return Native.lk_audio_source_microphone_is_muted(Handle) != 0; }
    }

    public float MicrophoneVolume
    {
        get { RequireMicrophone(); return Native.lk_audio_source_microphone_volume(Handle); }
    }

    public MicrophoneProcessingOptions GetMicrophoneProcessingOptions()
    {
        RequireMicrophone();
        var options = new Native.AudioSourceOptions();
        Native.lk_audio_source_options_init(ref options);
        Native.Check(Native.lk_audio_source_microphone_processing_options(Handle, ref options));
        return new MicrophoneProcessingOptions(options.EchoCancellation != 0,
            options.AutoGainControl != 0, options.NoiseSuppression != 0);
    }

    public void SetMicrophoneProcessingOptions(MicrophoneProcessingOptions value)
    {
        ArgumentNullException.ThrowIfNull(value);
        RequireMicrophone();
        var options = new Native.AudioSourceOptions();
        Native.lk_audio_source_options_init(ref options);
        options.EchoCancellation = value.EchoCancellation ? 1 : 0;
        options.AutoGainControl = value.AutoGainControl ? 1 : 0;
        options.NoiseSuppression = value.NoiseSuppression ? 1 : 0;
        Native.Check(Native.lk_audio_source_microphone_set_processing_options(Handle,
            ref options));
    }

    public MicrophoneProcessingStats GetMicrophoneProcessingStats()
    {
        RequireMicrophone();
        var stats = new Native.MicrophoneProcessingStats
        {
            StructSize = (nuint)Marshal.SizeOf<Native.MicrophoneProcessingStats>(),
        };
        Native.Check(Native.lk_audio_source_microphone_processing_stats(Handle, ref stats));
        return new MicrophoneProcessingStats(stats.CaptureFramesProcessed,
            stats.RenderFramesProcessed, stats.CaptureProcessingErrors,
            stats.RenderProcessingErrors, stats.FramesDropped,
            stats.EchoCancellationEnabled != 0,
            stats.EchoReturnLossAvailable != 0 ? stats.EchoReturnLossDb : null,
            stats.EchoReturnLossEnhancementAvailable != 0 ? stats.EchoReturnLossEnhancementDb : null,
            stats.ResidualEchoLikelihoodAvailable != 0 ? stats.ResidualEchoLikelihood : null,
            stats.ResidualEchoLikelihoodRecentMaxAvailable != 0 ?
                stats.ResidualEchoLikelihoodRecentMax : null,
            stats.DelayMedianAvailable != 0 ? stats.DelayMedianMs : null,
            stats.DelayStandardDeviationAvailable != 0 ? stats.DelayStandardDeviationMs : null,
            stats.DelayAvailable != 0 ? stats.DelayMs : null);
    }
}

public sealed partial class VideoSource
{
    public (uint Width, uint Height) Dimensions
    {
        get
        {
            ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
            Native.Check(Native.lk_video_source_dimensions(Handle, out var width, out var height));
            return (width, height);
        }
    }

    public string CaptureSourceId
    {
        get
        {
            ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
            return _captureKind switch
            {
                VideoCaptureKind.Camera => Native.ReadString(Handle,
                    Native.lk_video_source_camera_device_id),
                VideoCaptureKind.Screen => Native.ReadString(Handle,
                    Native.lk_video_source_screen_source_id),
                _ => string.Empty,
            };
        }
    }
}

public sealed partial class LocalTrack
{
    public void PublishScreenShareVideo() => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_local_track_publish_screen_share_video(_room.NativeHandle,
            Handle, IntPtr.Zero));
    });

    public void PublishScreenShareAudio() => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_local_track_publish_screen_share_audio(_room.NativeHandle,
            Handle, IntPtr.Zero));
    });

    public void UpdateVideoEncoding(ulong maxBitrate, float maxFramerate,
        bool backupCodec = false) => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        var encoding = new Native.VideoEncoding
        {
            StructSize = (nuint)Marshal.SizeOf<Native.VideoEncoding>(),
            MaxBitrate = maxBitrate,
            MaxFramerate = maxFramerate,
        };
        Native.Check(Native.lk_local_video_track_update_encoding(Handle, ref encoding,
            backupCodec ? 1 : 0));
    });

    public void UpdateDegradationPreference(VideoDegradationPreference preference) =>
        _room.Run(() =>
        {
            ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
            Native.Check(Native.lk_local_video_track_update_degradation_preference(Handle,
                (int)preference));
        });
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct AudioPlaybackStats
    {
        internal nuint StructSize;
        internal ulong QueuedFrames;
        internal ulong PlayedFrames;
        internal ulong DroppedFrames;
        internal ulong UnderrunFrames;
        internal uint BufferedDurationMs;
        internal uint DeviceLatencyMs;
        internal uint EstimatedDelayMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct MicrophoneProcessingStats
    {
        internal nuint StructSize;
        internal ulong CaptureFramesProcessed;
        internal ulong RenderFramesProcessed;
        internal ulong CaptureProcessingErrors;
        internal ulong RenderProcessingErrors;
        internal ulong FramesDropped;
        internal int EchoCancellationEnabled;
        internal int EchoReturnLossAvailable;
        internal double EchoReturnLossDb;
        internal int EchoReturnLossEnhancementAvailable;
        internal double EchoReturnLossEnhancementDb;
        internal int ResidualEchoLikelihoodAvailable;
        internal double ResidualEchoLikelihood;
        internal int ResidualEchoLikelihoodRecentMaxAvailable;
        internal double ResidualEchoLikelihoodRecentMax;
        internal int DelayMedianAvailable;
        internal int DelayMedianMs;
        internal int DelayStandardDeviationAvailable;
        internal int DelayStandardDeviationMs;
        internal int DelayAvailable;
        internal int DelayMs;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_disconnect_reason(RoomHandle room);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_republish_all_tracks(RoomHandle room);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_audio_playback_stats(RoomHandle room,
        ref AudioPlaybackStats stats);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_remove_participant_keys(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_is_muted(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern float lk_audio_source_microphone_volume(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_set_processing_options(
        AudioSourceHandle source, ref AudioSourceOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_processing_options(
        AudioSourceHandle source, ref AudioSourceOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_processing_stats(
        AudioSourceHandle source, ref MicrophoneProcessingStats stats);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_dimensions(VideoSourceHandle source,
        out uint width, out uint height);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_video_source_camera_device_id(IntPtr source,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_video_source_screen_source_id(IntPtr source,
        [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_publish_screen_share_video(RoomHandle room,
        TrackHandle track, IntPtr options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_publish_screen_share_audio(RoomHandle room,
        TrackHandle track, IntPtr options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_video_track_update_encoding(TrackHandle track,
        ref VideoEncoding encoding, int backupCodec);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_video_track_update_degradation_preference(
        TrackHandle track, int preference);
}
