using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed record AudioFrame(short[] Samples, uint SampleRate, uint Channels, uint SamplesPerChannel);
public sealed record VideoFrameMetadata(ulong? UserTimestampUs, uint? FrameId, byte[]? UserData);
public sealed record VideoFrame(byte[] Data, uint Width, uint Height, long TimestampUs,
    VideoFrameMetadata? Metadata = null);

public sealed class AudioStream : IDisposable
{
    private readonly AudioStreamHandle _handle;
    internal AudioStream(IntPtr pointer) => _handle = new AudioStreamHandle(pointer);

    public bool IsClosed => _handle.IsClosed || Native.lk_audio_stream_is_closed(_handle) != 0;
    public nuint DroppedFrames => _handle.IsClosed ? 0 : Native.lk_audio_stream_dropped_frames(_handle);

    public AudioFrame? Read(uint timeoutMs = 0)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        var status = Native.lk_audio_stream_read_for(_handle, timeoutMs, out var frame);
        if (status is 1 or 2) return null;
        if (status != 0) throw new LiveKitException(status, "Failed to read audio stream");
        try
        {
            var view = new Native.AudioFrameView();
            Native.Check(Native.lk_owned_audio_frame_data(frame, ref view));
            if (view.SampleCount > int.MaxValue) throw new LiveKitException(-1, "Audio frame is too large");
            var samples = new short[(int)view.SampleCount];
            if (samples.Length != 0) Marshal.Copy(view.Data, samples, 0, samples.Length);
            return new AudioFrame(samples, view.SampleRate, view.NumChannels, view.SamplesPerChannel);
        }
        finally { Native.lk_owned_audio_frame_destroy(frame); }
    }

    public void Close() { if (!_handle.IsClosed) Native.lk_audio_stream_close(_handle); }
    public void Dispose() => _handle.Dispose();
}

public sealed class VideoStream : IDisposable
{
    private readonly VideoStreamHandle _handle;
    internal VideoStream(IntPtr pointer) => _handle = new VideoStreamHandle(pointer);

    public bool IsClosed => _handle.IsClosed || Native.lk_video_stream_is_closed(_handle) != 0;
    public nuint DroppedFrames => _handle.IsClosed ? 0 : Native.lk_video_stream_dropped_frames(_handle);

    public VideoFrame? Read(uint timeoutMs = 0)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        var status = Native.lk_video_stream_read_for(_handle, timeoutMs, out var frame);
        if (status is 1 or 2) return null;
        if (status != 0) throw new LiveKitException(status, "Failed to read video stream");
        try
        {
            var view = new Native.VideoFrameView();
            Native.Check(Native.lk_owned_video_frame_data(frame, ref view));
            if (view.DataSize > int.MaxValue) throw new LiveKitException(-1, "Video frame is too large");
            var data = new byte[(int)view.DataSize];
            if (data.Length != 0) Marshal.Copy(view.Data, data, 0, data.Length);
            var metadata = new Native.VideoFrameMetadata();
            Native.lk_video_frame_metadata_init(ref metadata);
            Native.Check(Native.lk_owned_video_frame_metadata(frame, ref metadata));
            byte[]? userData = null;
            if (metadata.HasUserData != 0)
            {
                if (metadata.UserDataSize > int.MaxValue)
                    throw new LiveKitException(-1, "Video metadata is too large");
                userData = new byte[(int)metadata.UserDataSize];
                if (userData.Length != 0)
                    Marshal.Copy(metadata.UserData, userData, 0, userData.Length);
            }
            var copy = new VideoFrameMetadata(
                metadata.HasUserTimestampUs != 0 ? metadata.UserTimestampUs : null,
                metadata.HasFrameId != 0 ? metadata.FrameId : null, userData);
            return new VideoFrame(data, view.Width, view.Height, view.TimestampUs, copy);
        }
        finally { Native.lk_owned_video_frame_destroy(frame); }
    }

    public void Close() { if (!_handle.IsClosed) Native.lk_video_stream_close(_handle); }
    public void Dispose() => _handle.Dispose();
}

public sealed partial class Room
{
    public AudioStream CreateAudioStream(string participantIdentity, string trackSid,
        nuint capacity = 64)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        return Run(() =>
        {
            var options = new Native.MediaStreamOptions();
            Native.lk_media_stream_options_init(ref options);
            options.Capacity = capacity;
            Native.Check(Native.lk_room_create_audio_stream(_handle, participantIdentity,
                trackSid, ref options, out var pointer));
            return new AudioStream(pointer);
        });
    }

    public VideoStream CreateVideoStream(string participantIdentity, string trackSid,
        nuint capacity = 8)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        return Run(() =>
        {
            var options = new Native.MediaStreamOptions();
            Native.lk_media_stream_options_init(ref options);
            options.Capacity = capacity;
            Native.Check(Native.lk_room_create_video_stream(_handle, participantIdentity,
                trackSid, ref options, out var pointer));
            return new VideoStream(pointer);
        });
    }
}

internal sealed class AudioStreamHandle : SafeHandle
{
    internal AudioStreamHandle(IntPtr value) : base(IntPtr.Zero, true) => SetHandle(value);
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle() { Native.lk_audio_stream_destroy(handle); return true; }
}

internal sealed class VideoStreamHandle : SafeHandle
{
    internal VideoStreamHandle(IntPtr value) : base(IntPtr.Zero, true) => SetHandle(value);
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle() { Native.lk_video_stream_destroy(handle); return true; }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct MediaStreamOptions
    {
        internal nuint StructSize;
        internal nuint Capacity;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AudioFrameView
    {
        internal IntPtr Data;
        internal nuint SampleCount;
        internal uint SampleRate;
        internal uint NumChannels;
        internal uint SamplesPerChannel;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct VideoFrameView
    {
        internal IntPtr Data;
        internal nuint DataSize;
        internal uint Width;
        internal uint Height;
        internal long TimestampUs;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct VideoFrameMetadata
    {
        internal nuint StructSize;
        internal int HasUserTimestampUs;
        internal ulong UserTimestampUs;
        internal int HasFrameId;
        internal uint FrameId;
        internal int HasUserData;
        internal IntPtr UserData;
        internal nuint UserDataSize;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_video_frame_metadata_init(ref VideoFrameMetadata metadata);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_owned_video_frame_metadata(IntPtr frame,
        ref VideoFrameMetadata metadata);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_media_stream_options_init(ref MediaStreamOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create_audio_stream(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackSid,
        ref MediaStreamOptions options, out IntPtr stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create_video_stream(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackSid,
        ref MediaStreamOptions options, out IntPtr stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_audio_stream_destroy(IntPtr stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_audio_stream_close(AudioStreamHandle stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_stream_is_closed(AudioStreamHandle stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_audio_stream_dropped_frames(AudioStreamHandle stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_stream_read_for(AudioStreamHandle stream,
        uint timeoutMs, out IntPtr frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_owned_audio_frame_destroy(IntPtr frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_owned_audio_frame_data(IntPtr frame, ref AudioFrameView view);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_video_stream_destroy(IntPtr stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_video_stream_close(VideoStreamHandle stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_stream_is_closed(VideoStreamHandle stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_video_stream_dropped_frames(VideoStreamHandle stream);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_stream_read_for(VideoStreamHandle stream,
        uint timeoutMs, out IntPtr frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_owned_video_frame_destroy(IntPtr frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_owned_video_frame_data(IntPtr frame, ref VideoFrameView view);
}
