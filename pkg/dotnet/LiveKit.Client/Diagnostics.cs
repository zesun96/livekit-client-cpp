using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public enum LogLevel { Trace = 0, Debug = 1, Info = 2, Warning = 3, Error = 4, Off = 5 }
public sealed record LogOptions(LogLevel LiveKit, LogLevel WebRtc, LogLevel WebSocket);

public static partial class Diagnostics
{
    public static LogOptions GetLogOptions()
    {
        var options = new Native.LogOptions();
        Native.lk_log_options_init(ref options);
        Native.Check(Native.lk_log_get_options(ref options));
        return new LogOptions((LogLevel)options.LiveKitLevel,
            (LogLevel)options.WebRtcLevel, (LogLevel)options.WebSocketLevel);
    }

    public static void SetLogOptions(LogOptions value)
    {
        ArgumentNullException.ThrowIfNull(value);
        var options = new Native.LogOptions();
        Native.lk_log_options_init(ref options);
        options.LiveKitLevel = (int)value.LiveKit;
        options.WebRtcLevel = (int)value.WebRtc;
        options.WebSocketLevel = (int)value.WebSocket;
        Native.Check(Native.lk_log_set_options(ref options));
    }

    public static void StartTraceJsonFile(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Native.Check(Native.lk_trace_start_json_file(path));
    }

    public static void StopTrace() => Native.Check(Native.lk_trace_stop());
}

public sealed partial class Room
{
    public bool IsRecording => Run(() => Native.lk_room_is_recording(_handle) != 0);
    public string AudioOutputDeviceId => GetString(Native.lk_room_audio_output_device);
    public float SpeakerVolume => Run(() => Native.lk_room_speaker_volume(_handle));
    public bool IsSpeakerMuted => Run(() => Native.lk_room_speaker_is_muted(_handle) != 0);

    public void SetAudioOutputDevice(string deviceId)
    {
        ArgumentNullException.ThrowIfNull(deviceId);
        Run(() => Native.Check(Native.lk_room_set_audio_output_device(_handle, deviceId)));
    }

    public void SetSpeakerVolume(float volume) =>
        Run(() => Native.Check(Native.lk_room_set_speaker_volume(_handle, volume)));

    public void SetSpeakerMuted(bool muted) =>
        Run(() => Native.Check(Native.lk_room_set_speaker_muted(_handle, muted ? 1 : 0)));
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct LogOptions
    {
        internal nuint StructSize;
        internal int LiveKitLevel;
        internal int WebRtcLevel;
        internal int WebSocketLevel;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_log_options_init(ref LogOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_log_set_options(ref LogOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_log_get_options(ref LogOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_trace_start_json_file(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_trace_stop();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_is_recording(RoomHandle room);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_room_audio_output_device(IntPtr room,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_set_audio_output_device(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string deviceId);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern float lk_room_speaker_volume(RoomHandle room);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_speaker_is_muted(RoomHandle room);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_set_speaker_volume(RoomHandle room, float volume);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_set_speaker_muted(RoomHandle room, int muted);
}
