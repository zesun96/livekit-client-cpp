using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed partial class AudioSource : IDisposable
{
    internal AudioSourceHandle Handle { get; }
    public uint SampleRate { get; }
    public uint Channels { get; }

    public AudioSource(uint sampleRate = 48_000, uint channels = 1, uint queueSizeMs = 200)
    {
        if (sampleRate == 0 || channels == 0 || queueSizeMs == 0)
            throw new ArgumentOutOfRangeException(nameof(sampleRate), "Audio dimensions must be positive");
        var options = new Native.AudioSourceOptions();
        Native.lk_audio_source_options_init(ref options);
        options.SampleRate = sampleRate;
        options.NumChannels = channels;
        options.QueueSizeMs = queueSizeMs;
        Native.Check(Native.lk_audio_source_create(ref options, out var pointer));
        Handle = new AudioSourceHandle(pointer);
        SampleRate = sampleRate;
        Channels = channels;
    }

    private AudioSource(IntPtr pointer, uint sampleRate, uint channels, AudioCaptureKind kind)
    {
        Handle = new AudioSourceHandle(pointer);
        SampleRate = sampleRate;
        Channels = channels;
        _captureKind = kind;
    }

    /// <summary>Push interleaved signed 16-bit PCM.</summary>
    public void Capture(short[] samples)
    {
        ArgumentNullException.ThrowIfNull(samples);
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        if (samples.Length == 0 || (uint)samples.Length % Channels != 0)
            throw new ArgumentException("PCM length must contain whole frames", nameof(samples));
        Native.Check(Native.lk_audio_source_capture_frame(Handle, samples,
            (uint)(samples.Length / Channels)));
    }

    public uint QueuedDurationMs
    {
        get
        {
            ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
            Native.Check(Native.lk_audio_source_queued_duration_ms(Handle, out var duration));
            return duration;
        }
    }

    public void ClearQueue()
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_audio_source_clear_queue(Handle));
    }

    public void WaitForPlayout(uint timeoutMs)
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_audio_source_wait_for_playout(Handle, timeoutMs));
    }

    public void Dispose() => Handle.Dispose();
}

public sealed partial class VideoSource : IDisposable
{
    internal VideoSourceHandle Handle { get; }

    public VideoSource(bool isScreencast = false)
    {
        var options = new Native.VideoSourceOptions();
        Native.lk_video_source_options_init(ref options);
        options.IsScreencast = isScreencast ? 1 : 0;
        Native.Check(Native.lk_video_source_create(ref options, out var pointer));
        Handle = new VideoSourceHandle(pointer);
    }

    private VideoSource(IntPtr pointer, VideoCaptureKind kind)
    {
        Handle = new VideoSourceHandle(pointer);
        _captureKind = kind;
    }

    /// <summary>Push tightly packed I420 pixels. Width and height must be even.</summary>
    public void CaptureI420(byte[] data, uint width, uint height, long timestampUs = 0)
    {
        ArgumentNullException.ThrowIfNull(data);
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        if (width == 0 || height == 0 || (width & 1) != 0 || (height & 1) != 0)
            throw new ArgumentException("I420 dimensions must be positive and even");
        var required = checked((long)width * height * 3 / 2);
        if (data.LongLength != required) throw new ArgumentException("Incorrect I420 byte count", nameof(data));
        Native.Check(Native.lk_video_source_capture_i420(Handle, data, (nuint)data.Length,
            width, height, timestampUs));
    }

    /// <summary>Push tightly packed I420 pixels with optional user metadata.</summary>
    public void CaptureI420(byte[] data, uint width, uint height, VideoFrameMetadata metadata,
        long timestampUs = 0)
    {
        ArgumentNullException.ThrowIfNull(data);
        ArgumentNullException.ThrowIfNull(metadata);
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        if (width == 0 || height == 0 || (width & 1) != 0 || (height & 1) != 0)
            throw new ArgumentException("I420 dimensions must be positive and even");
        var required = checked((long)width * height * 3 / 2);
        if (data.LongLength != required) throw new ArgumentException("Incorrect I420 byte count", nameof(data));
        var rawMetadata = new Native.VideoFrameMetadata();
        Native.lk_video_frame_metadata_init(ref rawMetadata);
        rawMetadata.HasUserTimestampUs = metadata.UserTimestampUs.HasValue ? 1 : 0;
        rawMetadata.UserTimestampUs = metadata.UserTimestampUs ?? 0;
        rawMetadata.HasFrameId = metadata.FrameId.HasValue ? 1 : 0;
        rawMetadata.FrameId = metadata.FrameId ?? 0;
        rawMetadata.HasUserData = metadata.UserData is null ? 0 : 1;
        var dataPin = GCHandle.Alloc(data, GCHandleType.Pinned);
        GCHandle userPin = default;
        try
        {
            if (metadata.UserData is not null)
            {
                userPin = GCHandle.Alloc(metadata.UserData, GCHandleType.Pinned);
                rawMetadata.UserData = userPin.AddrOfPinnedObject();
                rawMetadata.UserDataSize = (nuint)metadata.UserData.Length;
            }
            var frame = new Native.VideoFrameInput
            {
                StructSize = (nuint)Marshal.SizeOf<Native.VideoFrameInput>(),
                Data = dataPin.AddrOfPinnedObject(),
                DataSize = (nuint)data.Length,
                Width = width,
                Height = height,
                TimestampUs = timestampUs,
                Format = 5, // LK_VIDEO_BUFFER_I420
            };
            var metadataPointer = Marshal.AllocHGlobal(Marshal.SizeOf<Native.VideoFrameMetadata>());
            try
            {
                Marshal.StructureToPtr(rawMetadata, metadataPointer, false);
                frame.Metadata = metadataPointer;
                Native.Check(Native.lk_video_source_capture_frame(Handle, ref frame));
            }
            finally { Marshal.FreeHGlobal(metadataPointer); }
        }
        finally
        {
            if (userPin.IsAllocated) userPin.Free();
            dataPin.Free();
        }
    }

    public void Dispose() => Handle.Dispose();
}

public sealed partial class LocalTrack : IDisposable
{
    private readonly Room _room;
    internal TrackHandle Handle { get; }

    internal LocalTrack(Room room, TrackHandle handle)
    {
        _room = room;
        Handle = handle;
    }

    public string RtcStatsJson => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        return Native.ReadString(Handle, Native.lk_local_track_rtc_stats);
    });

    public void Publish() => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_local_track_publish(_room.NativeHandle, Handle, IntPtr.Zero));
    });

    /// <summary>Publishes a video track with transport of user timestamp, frame ID and user data.</summary>
    public void PublishWithFrameMetadata() => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        var options = new Native.TrackPublishOptions();
        Native.lk_track_publish_options_init(ref options);
        options.FrameMetadataFeatures.UserTimestamp = 1;
        options.FrameMetadataFeatures.FrameId = 1;
        options.FrameMetadataFeatures.UserData = 1;
        Native.Check(Native.lk_local_track_publish_with_options(_room.NativeHandle,
            Handle, ref options));
    });

    public void Unpublish(bool stopOnUnpublish = false) => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_local_track_unpublish(Handle, stopOnUnpublish ? 1 : 0));
    });

    public void SetMuted(bool muted) => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_local_track_set_muted(Handle, muted ? 1 : 0));
    });

    public void Dispose() => _room.DisposeTrack(this);
    internal void DisposeNative() => Handle.Dispose();
}

public sealed partial class Room
{
    private readonly HashSet<LocalTrack> _tracks = new();
    internal RoomHandle NativeHandle => _handle;

    public LocalTrack CreateAudioTrack(string label, AudioSource source)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        ArgumentNullException.ThrowIfNull(source);
        return Run(() =>
        {
            ObjectDisposedException.ThrowIf(source.Handle.IsClosed, source);
            Native.Check(Native.lk_room_create_audio_track(_handle, label, source.Handle, out var pointer));
            var track = new LocalTrack(this, new TrackHandle(pointer, source.Handle));
            _tracks.Add(track);
            return track;
        });
    }

    public LocalTrack CreateVideoTrack(string label, VideoSource source)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        ArgumentNullException.ThrowIfNull(source);
        return Run(() =>
        {
            ObjectDisposedException.ThrowIf(source.Handle.IsClosed, source);
            Native.Check(Native.lk_room_create_video_track(_handle, label, source.Handle, out var pointer));
            var track = new LocalTrack(this, new TrackHandle(pointer, source.Handle));
            _tracks.Add(track);
            return track;
        });
    }

    internal void DisposeTrack(LocalTrack track)
    {
        lock (_sync)
        {
            if (_tracks.Remove(track)) track.DisposeNative();
        }
    }
}

internal sealed class AudioSourceHandle : SafeHandle
{
    internal AudioSourceHandle(IntPtr value) : base(IntPtr.Zero, true) => SetHandle(value);
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle() => Native.lk_audio_source_destroy(handle) == 0;
}

internal sealed class VideoSourceHandle : SafeHandle
{
    internal VideoSourceHandle(IntPtr value) : base(IntPtr.Zero, true) => SetHandle(value);
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle() => Native.lk_video_source_destroy(handle) == 0;
}

internal sealed class TrackHandle : SafeHandle
{
    private readonly SafeHandle _source;
    private readonly bool _sourceRefAdded;

    internal TrackHandle(IntPtr value, SafeHandle source) : base(IntPtr.Zero, true)
    {
        _source = source;
        var added = false;
        source.DangerousAddRef(ref added);
        _sourceRefAdded = added;
        SetHandle(value);
    }

    public override bool IsInvalid => handle == IntPtr.Zero;

    protected override bool ReleaseHandle()
    {
        try
        {
            Native.lk_local_track_unpublish(handle, 0);
            return Native.lk_local_track_destroy(handle) == 0;
        }
        finally
        {
            if (_sourceRefAdded) _source.DangerousRelease();
        }
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct AudioSourceOptions
    {
        internal nuint StructSize;
        internal uint SampleRate;
        internal uint NumChannels;
        internal uint QueueSizeMs;
        internal int EchoCancellation;
        internal int AutoGainControl;
        internal int NoiseSuppression;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct VideoSourceOptions
    {
        internal nuint StructSize;
        internal int IsScreencast;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct VideoFrameInput
    {
        internal nuint StructSize;
        internal IntPtr Data;
        internal nuint DataSize;
        internal uint Width;
        internal uint Height;
        internal long TimestampUs;
        internal int Rotation;
        internal int Format;
        internal IntPtr Planes;
        internal nuint PlaneCount;
        internal IntPtr Metadata;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct VideoEncoding
    {
        internal nuint StructSize;
        internal ulong MaxBitrate;
        internal float MaxFramerate;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct FrameMetadataFeatures
    {
        internal nuint StructSize;
        internal int UserTimestamp;
        internal int FrameId;
        internal int UserData;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TrackPublishOptions
    {
        internal nuint StructSize;
        internal int Source;
        internal int Dtx;
        internal int Red;
        internal int Simulcast;
        internal IntPtr Stream;
        internal int VideoCodec;
        internal IntPtr ScalabilityMode;
        internal int BackupVideoCodecEnabled;
        internal int BackupVideoCodec;
        internal int BackupCodecPolicy;
        internal VideoEncoding VideoEncoding;
        internal VideoEncoding BackupVideoEncoding;
        internal FrameMetadataFeatures FrameMetadataFeatures;
        internal int PreconnectBuffer;
        internal int HasDegradationPreference;
        internal int DegradationPreference;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_audio_source_options_init(ref AudioSourceOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_create(ref AudioSourceOptions options, out IntPtr source);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_destroy(IntPtr source);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_capture_frame(AudioSourceHandle source, short[] samples,
        uint samplesPerChannel);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_queued_duration_ms(AudioSourceHandle source, out uint durationMs);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_clear_queue(AudioSourceHandle source);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_wait_for_playout(AudioSourceHandle source, uint timeoutMs);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_video_source_options_init(ref VideoSourceOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_create(ref VideoSourceOptions options, out IntPtr source);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_destroy(IntPtr source);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_capture_i420(VideoSourceHandle source, byte[] data,
        nuint size, uint width, uint height, long timestampUs);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_capture_frame(VideoSourceHandle source,
        ref VideoFrameInput frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create_audio_track(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string label, AudioSourceHandle source, out IntPtr track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create_video_track(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string label, VideoSourceHandle source, out IntPtr track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_publish(RoomHandle room, TrackHandle track, IntPtr options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "lk_local_track_publish")]
    internal static extern int lk_local_track_publish_with_options(RoomHandle room,
        TrackHandle track, ref TrackPublishOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_track_publish_options_init(ref TrackPublishOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_unpublish(TrackHandle track, int stopOnUnpublish);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_unpublish(IntPtr track, int stopOnUnpublish);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_set_muted(TrackHandle track, int muted);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_destroy(IntPtr track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_track_rtc_stats(IntPtr track, [Out] byte[]? buffer, nuint size);
}
