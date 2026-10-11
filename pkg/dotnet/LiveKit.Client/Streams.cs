using System;
using System.Runtime.InteropServices;
using System.Text;

namespace LiveKit.Client;

public sealed partial class TextStreamWriter : IDisposable
{
    private readonly TextStreamWriterHandle _handle;
    internal TextStreamWriter(IntPtr pointer, StreamCallbackRegistration? callbacks = null) =>
        _handle = new TextStreamWriterHandle(pointer, callbacks);

    public string Id
    {
        get
        {
            ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
            return Native.ReadString(_handle, Native.lk_text_stream_writer_id);
        }
    }

    public bool IsClosed => _handle.IsClosed || Native.lk_text_stream_writer_is_closed(_handle) != 0;

    public void Write(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        var bytes = Encoding.UTF8.GetBytes(text);
        Native.Check(Native.lk_text_stream_writer_write(_handle, bytes, (nuint)bytes.Length));
    }

    public void Close()
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        Native.Check(Native.lk_text_stream_writer_close(_handle));
    }

    public void Cancel(string reason)
    {
        ArgumentNullException.ThrowIfNull(reason);
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        Native.Check(Native.lk_text_stream_writer_cancel(_handle, reason));
    }

    public void Dispose() => _handle.Dispose();
}

public sealed partial class ByteStreamWriter : IDisposable
{
    private readonly ByteStreamWriterHandle _handle;
    internal ByteStreamWriter(IntPtr pointer, StreamCallbackRegistration? callbacks = null) =>
        _handle = new ByteStreamWriterHandle(pointer, callbacks);

    public string Id
    {
        get
        {
            ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
            return Native.ReadString(_handle, Native.lk_byte_stream_writer_id);
        }
    }

    public bool IsClosed => _handle.IsClosed || Native.lk_byte_stream_writer_is_closed(_handle) != 0;

    public void Write(byte[] data)
    {
        ArgumentNullException.ThrowIfNull(data);
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        Native.Check(Native.lk_byte_stream_writer_write(_handle, data, (nuint)data.Length));
    }

    public void Close()
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        Native.Check(Native.lk_byte_stream_writer_close(_handle));
    }

    public void Cancel(string reason)
    {
        ArgumentNullException.ThrowIfNull(reason);
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        Native.Check(Native.lk_byte_stream_writer_cancel(_handle, reason));
    }

    public void Dispose() => _handle.Dispose();
}

public sealed partial class Room
{
    public TextStreamWriter StreamText(string topic = "")
    {
        ArgumentNullException.ThrowIfNull(topic);
        return Run(() =>
        {
            var options = new Native.TextStreamOptions
            {
                StructSize = (nuint)Marshal.SizeOf<Native.TextStreamOptions>(),
                Topic = Marshal.StringToCoTaskMemUTF8(topic),
            };
            try
            {
                Native.Check(Native.lk_room_stream_text(_handle, ref options, out var writer));
                return new TextStreamWriter(writer);
            }
            finally { Marshal.FreeCoTaskMem(options.Topic); }
        });
    }

    public ByteStreamWriter StreamBytes(string topic = "", string mimeType = "application/octet-stream",
        string name = "")
    {
        ArgumentNullException.ThrowIfNull(topic);
        ArgumentNullException.ThrowIfNull(mimeType);
        ArgumentNullException.ThrowIfNull(name);
        return Run(() =>
        {
            var options = new Native.ByteStreamOptions
            {
                StructSize = (nuint)Marshal.SizeOf<Native.ByteStreamOptions>(),
                Topic = Marshal.StringToCoTaskMemUTF8(topic),
                MimeType = Marshal.StringToCoTaskMemUTF8(mimeType),
                Name = Marshal.StringToCoTaskMemUTF8(name),
            };
            try
            {
                Native.Check(Native.lk_room_stream_bytes(_handle, ref options, out var writer));
                return new ByteStreamWriter(writer);
            }
            finally
            {
                Marshal.FreeCoTaskMem(options.Topic);
                Marshal.FreeCoTaskMem(options.MimeType);
                Marshal.FreeCoTaskMem(options.Name);
            }
        });
    }
}

internal sealed class TextStreamWriterHandle : SafeHandle
{
    private readonly StreamCallbackRegistration? _callbacks;
    internal TextStreamWriterHandle(IntPtr value, StreamCallbackRegistration? callbacks)
        : base(IntPtr.Zero, true)
    {
        SetHandle(value);
        _callbacks = callbacks;
    }
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle()
    {
        Native.lk_text_stream_writer_destroy(handle);
        GC.KeepAlive(_callbacks);
        return true;
    }
}

internal sealed class ByteStreamWriterHandle : SafeHandle
{
    private readonly StreamCallbackRegistration? _callbacks;
    internal ByteStreamWriterHandle(IntPtr value, StreamCallbackRegistration? callbacks)
        : base(IntPtr.Zero, true)
    {
        SetHandle(value);
        _callbacks = callbacks;
    }
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle()
    {
        Native.lk_byte_stream_writer_destroy(handle);
        GC.KeepAlive(_callbacks);
        return true;
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct TextStreamOptions
    {
        internal nuint StructSize;
        internal IntPtr Topic;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ByteStreamOptions
    {
        internal nuint StructSize;
        internal IntPtr Topic;
        internal IntPtr MimeType;
        internal IntPtr Name;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_stream_text(RoomHandle room, ref TextStreamOptions options,
        out IntPtr writer);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_text_stream_writer_write(TextStreamWriterHandle writer, byte[] text, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_text_stream_writer_close(TextStreamWriterHandle writer);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_text_stream_writer_cancel(TextStreamWriterHandle writer,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string reason);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_text_stream_writer_id(IntPtr writer, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_text_stream_writer_is_closed(TextStreamWriterHandle writer);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_text_stream_writer_destroy(IntPtr writer);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_stream_bytes(RoomHandle room, ref ByteStreamOptions options,
        out IntPtr writer);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_byte_stream_writer_write(ByteStreamWriterHandle writer, byte[] data, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_byte_stream_writer_close(ByteStreamWriterHandle writer);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_byte_stream_writer_cancel(ByteStreamWriterHandle writer,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string reason);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_byte_stream_writer_id(IntPtr writer, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_byte_stream_writer_is_closed(ByteStreamWriterHandle writer);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_byte_stream_writer_destroy(IntPtr writer);
}
