using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

internal enum AudioCaptureKind { External, Microphone, SystemAudio }
internal enum VideoCaptureKind { External, Camera, Screen }

public sealed partial class AudioSource
{
    private readonly AudioCaptureKind _captureKind;

    /// <summary>Opens a physical microphone. Run hardware tests outside restricted sandboxes.</summary>
    public static AudioSource CreateMicrophone(string? deviceId = null, uint queueSizeMs = 200,
        bool echoCancellation = true, bool autoGainControl = true, bool noiseSuppression = true)
    {
        var options = new Native.MicrophoneCaptureOptions();
        Native.lk_microphone_capture_options_init(ref options);
        options.DeviceId = deviceId is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(deviceId);
        options.QueueSizeMs = queueSizeMs;
        options.EchoCancellation = echoCancellation ? 1 : 0;
        options.AutoGainControl = autoGainControl ? 1 : 0;
        options.NoiseSuppression = noiseSuppression ? 1 : 0;
        try
        {
            Native.Check(Native.lk_audio_source_create_microphone(ref options, out var pointer));
            return new AudioSource(pointer, 48_000, 1, AudioCaptureKind.Microphone);
        }
        finally { Marshal.FreeCoTaskMem(options.DeviceId); }
    }

    /// <summary>Opens host output-loopback capture. Run hardware tests outside restricted sandboxes.</summary>
    public static AudioSource CreateSystemAudio(string? deviceId = null, uint queueSizeMs = 200)
    {
        var options = new Native.SystemAudioCaptureOptions();
        Native.lk_system_audio_capture_options_init(ref options);
        options.DeviceId = deviceId is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(deviceId);
        options.QueueSizeMs = queueSizeMs;
        try
        {
            Native.Check(Native.lk_audio_source_create_system_audio(ref options, out var pointer));
            return new AudioSource(pointer, 48_000, 2, AudioCaptureKind.SystemAudio);
        }
        finally { Marshal.FreeCoTaskMem(options.DeviceId); }
    }

    public bool IsCapturing
    {
        get
        {
            ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
            return _captureKind switch
            {
                AudioCaptureKind.Microphone => Native.lk_audio_source_microphone_is_capturing(Handle) != 0,
                AudioCaptureKind.SystemAudio => Native.lk_audio_source_system_audio_is_capturing(Handle) != 0,
                _ => false,
            };
        }
    }

    public string DeviceId
    {
        get
        {
            ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
            return _captureKind switch
            {
                AudioCaptureKind.Microphone => Native.ReadString(Handle,
                    Native.lk_audio_source_microphone_device_id),
                AudioCaptureKind.SystemAudio => Native.ReadString(Handle,
                    Native.lk_audio_source_system_audio_device_id),
                _ => string.Empty,
            };
        }
    }

    public void StartCapture()
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(_captureKind switch
        {
            AudioCaptureKind.Microphone => Native.lk_audio_source_microphone_start(Handle),
            AudioCaptureKind.SystemAudio => Native.lk_audio_source_system_audio_start(Handle),
            _ => throw new InvalidOperationException("External audio sources cannot start device capture"),
        });
    }

    public void StopCapture()
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(_captureKind switch
        {
            AudioCaptureKind.Microphone => Native.lk_audio_source_microphone_stop(Handle),
            AudioCaptureKind.SystemAudio => Native.lk_audio_source_system_audio_stop(Handle),
            _ => throw new InvalidOperationException("External audio sources cannot stop device capture"),
        });
    }

    public void SwitchDevice(string deviceId)
    {
        ArgumentNullException.ThrowIfNull(deviceId);
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(_captureKind switch
        {
            AudioCaptureKind.Microphone => Native.lk_audio_source_microphone_switch_device(Handle, deviceId),
            AudioCaptureKind.SystemAudio => Native.lk_audio_source_system_audio_switch_device(Handle, deviceId),
            _ => throw new InvalidOperationException("External audio sources have no device"),
        });
    }

    public void SetMicrophoneMuted(bool muted)
    {
        if (_captureKind != AudioCaptureKind.Microphone)
            throw new InvalidOperationException("Source is not a microphone");
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_audio_source_microphone_set_muted(Handle, muted ? 1 : 0));
    }

    public void SetMicrophoneVolume(float volume)
    {
        if (_captureKind != AudioCaptureKind.Microphone)
            throw new InvalidOperationException("Source is not a microphone");
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_audio_source_microphone_set_volume(Handle, volume));
    }
}

public sealed partial class VideoSource
{
    private readonly VideoCaptureKind _captureKind;

    /// <summary>Opens a physical camera. Run hardware tests outside restricted sandboxes.</summary>
    public static VideoSource CreateCamera(string? deviceId = null, uint width = 1280,
        uint height = 720, uint framesPerSecond = 30)
    {
        var options = new Native.CameraCaptureOptions();
        Native.lk_camera_capture_options_init(ref options);
        options.DeviceId = deviceId is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(deviceId);
        options.Width = width;
        options.Height = height;
        options.FramesPerSecond = framesPerSecond;
        try
        {
            Native.Check(Native.lk_video_source_create_camera(ref options, out var pointer));
            return new VideoSource(pointer, VideoCaptureKind.Camera);
        }
        finally { Marshal.FreeCoTaskMem(options.DeviceId); }
    }

    /// <summary>Opens a screen or window capture source.</summary>
    public static VideoSource CreateScreen(string sourceId, uint framesPerSecond = 15,
        bool includeCursor = true)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceId);
        var options = new Native.ScreenCaptureOptions();
        Native.lk_screen_capture_options_init(ref options);
        options.SourceId = Marshal.StringToCoTaskMemUTF8(sourceId);
        options.FramesPerSecond = framesPerSecond;
        options.IncludeCursor = includeCursor ? 1 : 0;
        try
        {
            Native.Check(Native.lk_video_source_create_screen(ref options, out var pointer));
            return new VideoSource(pointer, VideoCaptureKind.Screen);
        }
        finally { Marshal.FreeCoTaskMem(options.SourceId); }
    }

    public bool IsCapturing
    {
        get
        {
            ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
            return _captureKind switch
            {
                VideoCaptureKind.Camera => Native.lk_video_source_camera_is_capturing(Handle) != 0,
                VideoCaptureKind.Screen => Native.lk_video_source_screen_is_capturing(Handle) != 0,
                _ => false,
            };
        }
    }

    public void StartCapture()
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(_captureKind switch
        {
            VideoCaptureKind.Camera => Native.lk_video_source_camera_start(Handle),
            VideoCaptureKind.Screen => Native.lk_video_source_screen_start(Handle),
            _ => throw new InvalidOperationException("External video sources cannot start device capture"),
        });
    }

    public void StopCapture()
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(_captureKind switch
        {
            VideoCaptureKind.Camera => Native.lk_video_source_camera_stop(Handle),
            VideoCaptureKind.Screen => Native.lk_video_source_screen_stop(Handle),
            _ => throw new InvalidOperationException("External video sources cannot stop device capture"),
        });
    }

    public void SwitchDevice(string id)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(_captureKind switch
        {
            VideoCaptureKind.Camera => Native.lk_video_source_camera_switch_device(Handle, id),
            VideoCaptureKind.Screen => Native.lk_video_source_screen_switch_source(Handle, id),
            _ => throw new InvalidOperationException("External video sources have no device"),
        });
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct MicrophoneCaptureOptions
    {
        internal nuint StructSize;
        internal IntPtr DeviceId;
        internal uint QueueSizeMs;
        internal int EchoCancellation;
        internal int AutoGainControl;
        internal int NoiseSuppression;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SystemAudioCaptureOptions
    {
        internal nuint StructSize;
        internal IntPtr DeviceId;
        internal uint QueueSizeMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct CameraCaptureOptions
    {
        internal nuint StructSize;
        internal IntPtr DeviceId;
        internal uint Width;
        internal uint Height;
        internal uint FramesPerSecond;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ScreenCaptureOptions
    {
        internal nuint StructSize;
        internal IntPtr SourceId;
        internal uint FramesPerSecond;
        internal int IncludeCursor;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_microphone_capture_options_init(ref MicrophoneCaptureOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_system_audio_capture_options_init(ref SystemAudioCaptureOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_camera_capture_options_init(ref CameraCaptureOptions options);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_screen_capture_options_init(ref ScreenCaptureOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_create_microphone(ref MicrophoneCaptureOptions options,
        out IntPtr source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_create_system_audio(ref SystemAudioCaptureOptions options,
        out IntPtr source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_start(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_stop(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_is_capturing(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_audio_source_microphone_device_id(IntPtr source,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_switch_device(AudioSourceHandle source,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string deviceId);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_set_muted(AudioSourceHandle source, int muted);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_microphone_set_volume(AudioSourceHandle source, float volume);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_system_audio_start(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_system_audio_stop(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_system_audio_is_capturing(AudioSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_audio_source_system_audio_device_id(IntPtr source,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_audio_source_system_audio_switch_device(AudioSourceHandle source,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string deviceId);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_create_camera(ref CameraCaptureOptions options,
        out IntPtr source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_create_screen(ref ScreenCaptureOptions options,
        out IntPtr source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_camera_start(VideoSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_camera_stop(VideoSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_camera_is_capturing(VideoSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_camera_switch_device(VideoSourceHandle source,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string deviceId);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_screen_start(VideoSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_screen_stop(VideoSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_screen_is_capturing(VideoSourceHandle source);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_video_source_screen_switch_source(VideoSourceHandle source,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceId);
}
