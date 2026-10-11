using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public enum TrackRecorderState { Recording = 0, Stopped = 1, Failed = 2 }
public sealed record TrackRecorderStats(TrackRecorderState State, ulong FramesWritten,
    ulong BytesWritten, ulong FramesDropped);

public sealed class TrackRecorder : IDisposable
{
    private readonly TrackRecorderHandle _handle;
    internal TrackRecorder(IntPtr pointer) => _handle = new TrackRecorderHandle(pointer);

    public string OutputPath
    {
        get
        {
            ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
            return Native.ReadString(_handle, Native.lk_track_recorder_output_path);
        }
    }

    public string Error
    {
        get
        {
            ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
            return Native.ReadString(_handle, Native.lk_track_recorder_error);
        }
    }

    public TrackRecorderStats Stats
    {
        get
        {
            ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
            var stats = new Native.TrackRecorderStats();
            Native.lk_track_recorder_stats_init(ref stats);
            Native.Check(Native.lk_track_recorder_get_stats(_handle, ref stats));
            return new TrackRecorderStats((TrackRecorderState)stats.State,
                stats.FramesWritten, stats.BytesWritten, stats.FramesDropped);
        }
    }

    public void Stop() { if (!_handle.IsClosed) Native.lk_track_recorder_stop(_handle); }
    public void Dispose() => _handle.Dispose();
}

public sealed partial class Room
{
    public TrackRecorder StartTrackRecording(string participantIdentity, string trackSid,
        string outputPath, nuint queueCapacity = 64)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        ArgumentException.ThrowIfNullOrWhiteSpace(outputPath);
        return Run(() =>
        {
            var options = new Native.TrackRecorderOptions();
            Native.lk_track_recorder_options_init(ref options);
            options.OutputPath = Marshal.StringToCoTaskMemUTF8(outputPath);
            options.QueueCapacity = queueCapacity;
            try
            {
                Native.Check(Native.lk_room_start_track_recording(_handle, participantIdentity,
                    trackSid, ref options, out var pointer));
                return new TrackRecorder(pointer);
            }
            finally { Marshal.FreeCoTaskMem(options.OutputPath); }
        });
    }
}

internal sealed class TrackRecorderHandle : SafeHandle
{
    internal TrackRecorderHandle(IntPtr value) : base(IntPtr.Zero, true) => SetHandle(value);
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle() { Native.lk_track_recorder_destroy(handle); return true; }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct TrackRecorderOptions
    {
        internal nuint StructSize;
        internal IntPtr OutputPath;
        internal nuint QueueCapacity;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TrackRecorderStats
    {
        internal nuint StructSize;
        internal int State;
        internal ulong FramesWritten;
        internal ulong BytesWritten;
        internal ulong FramesDropped;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_track_recorder_options_init(ref TrackRecorderOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_track_recorder_stats_init(ref TrackRecorderStats stats);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_start_track_recording(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackSid,
        ref TrackRecorderOptions options, out IntPtr recorder);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_track_recorder_destroy(IntPtr recorder);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_track_recorder_stop(TrackRecorderHandle recorder);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_track_recorder_get_stats(TrackRecorderHandle recorder,
        ref TrackRecorderStats stats);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_track_recorder_output_path(IntPtr recorder,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_track_recorder_error(IntPtr recorder,
        [Out] byte[]? buffer, nuint size);
}
