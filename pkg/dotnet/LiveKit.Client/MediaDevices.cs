using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace LiveKit.Client;

public enum MediaDeviceKind { AudioInput = 0, AudioOutput = 1, VideoInput = 2 }
public enum ScreenSourceKind { Monitor = 0, Window = 1 }

public sealed record MediaDevice(string Id, string Label, MediaDeviceKind Kind, bool IsDefault);
public sealed record ScreenSource(string Id, string Label, ScreenSourceKind Kind,
    int X, int Y, uint Width, uint Height);

/// <summary>Read-only host device and screen-source enumeration.</summary>
public static class MediaDevices
{
    public static IReadOnlyList<MediaDevice> ListDevices()
    {
        Native.Check(Native.lk_media_device_list_create(out var list));
        try
        {
            var count = Native.lk_media_device_list_count(list);
            if (count > int.MaxValue) throw new LiveKitException(-1, "Too many media devices");
            var devices = new List<MediaDevice>((int)count);
            for (nuint i = 0; i < count; i++)
            {
                var info = new Native.MediaDeviceInfo
                {
                    StructSize = (nuint)Marshal.SizeOf<Native.MediaDeviceInfo>(),
                };
                Native.Check(Native.lk_media_device_list_info(list, i, ref info));
                devices.Add(new MediaDevice(
                    ReadIndexedString(list, i, Native.lk_media_device_list_id),
                    ReadIndexedString(list, i, Native.lk_media_device_list_label),
                    (MediaDeviceKind)info.Kind, info.IsDefault != 0));
            }
            return devices;
        }
        finally { Native.lk_media_device_list_destroy(list); }
    }

    public static IReadOnlyList<ScreenSource> ListScreenSources()
    {
        Native.Check(Native.lk_screen_source_list_create(out var list));
        try
        {
            var count = Native.lk_screen_source_list_count(list);
            if (count > int.MaxValue) throw new LiveKitException(-1, "Too many screen sources");
            var sources = new List<ScreenSource>((int)count);
            for (nuint i = 0; i < count; i++)
            {
                var info = new Native.ScreenSourceInfo
                {
                    StructSize = (nuint)Marshal.SizeOf<Native.ScreenSourceInfo>(),
                };
                Native.Check(Native.lk_screen_source_list_info(list, i, ref info));
                sources.Add(new ScreenSource(
                    ReadIndexedString(list, i, Native.lk_screen_source_list_id),
                    ReadIndexedString(list, i, Native.lk_screen_source_list_label),
                    (ScreenSourceKind)info.Kind, info.X, info.Y, info.Width, info.Height));
            }
            return sources;
        }
        finally { Native.lk_screen_source_list_destroy(list); }
    }

    private static string ReadIndexedString(IntPtr list, nuint index, Native.IndexedStringGetter getter)
    {
        for (var attempt = 0; attempt < 3; attempt++)
        {
            var size = getter(list, index, null, 0);
            if (size == 0) return string.Empty;
            if (size > int.MaxValue) throw new LiveKitException(-1, "Native string is too large");
            var buffer = new byte[(int)size];
            if (getter(list, index, buffer, size) <= size)
            {
                var length = Array.IndexOf(buffer, (byte)0);
                return Encoding.UTF8.GetString(buffer, 0, length < 0 ? buffer.Length : length);
            }
        }
        throw new LiveKitException(-1, "Native string changed while reading");
    }
}

internal static partial class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate nuint IndexedStringGetter(IntPtr list, nuint index, [Out] byte[]? buffer, nuint size);

    [StructLayout(LayoutKind.Sequential)]
    internal struct MediaDeviceInfo
    {
        internal nuint StructSize;
        internal int Kind;
        internal int IsDefault;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ScreenSourceInfo
    {
        internal nuint StructSize;
        internal int Kind;
        internal int X;
        internal int Y;
        internal uint Width;
        internal uint Height;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_media_device_list_create(out IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_media_device_list_destroy(IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_media_device_list_count(IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_media_device_list_info(IntPtr list, nuint index, ref MediaDeviceInfo info);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_media_device_list_id(IntPtr list, nuint index, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_media_device_list_label(IntPtr list, nuint index, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_screen_source_list_create(out IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_screen_source_list_destroy(IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_screen_source_list_count(IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_screen_source_list_info(IntPtr list, nuint index, ref ScreenSourceInfo info);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_screen_source_list_id(IntPtr list, nuint index, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_screen_source_list_label(IntPtr list, nuint index, [Out] byte[]? buffer, nuint size);
}
