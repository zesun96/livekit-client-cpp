using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace LiveKit.Client;

public enum StreamEventType { Open = 0, Chunk = 1, Closed = 2, Failed = 3 }

public sealed record TextStreamEvent(StreamEventType Type, string StreamId, string MimeType,
    string Topic, string ParticipantIdentity, string Content, ulong ChunkIndex,
    ulong? TotalSize, string Reason, long Timestamp, string ReplyToStreamId,
    byte[] ContentBytes);

public sealed record ByteStreamEvent(StreamEventType Type, string StreamId, string Name,
    string MimeType, string Topic, string ParticipantIdentity, byte[] Content,
    ulong ChunkIndex, ulong? TotalSize, string Reason, long Timestamp);

public sealed partial class Room
{
    private readonly Dictionary<string, Native.IncomingTextHandler> _textStreamHandlers = new();
    private readonly Dictionary<string, Native.IncomingByteHandler> _byteStreamHandlers = new();
    private readonly List<Delegate> _retiredStreamHandlers = new();

    public void RegisterTextStreamHandler(string topic, Action<TextStreamEvent> handler)
    {
        ArgumentNullException.ThrowIfNull(topic);
        ArgumentNullException.ThrowIfNull(handler);
        Run(() =>
        {
            if (_textStreamHandlers.ContainsKey(topic))
                throw new InvalidOperationException("Text stream topic is already registered");
            Native.IncomingTextHandler callback = (_, _, pointer) =>
            {
                try
                {
                    var raw = Marshal.PtrToStructure<Native.IncomingTextEvent>(pointer);
                    var content = CopyBytes(raw.Content, raw.ContentSize);
                    var value = new TextStreamEvent((StreamEventType)raw.Type,
                        ReadUtf8(raw.StreamId), ReadUtf8(raw.MimeType), ReadUtf8(raw.Topic),
                        ReadUtf8(raw.ParticipantIdentity), Encoding.UTF8.GetString(content),
                        raw.ChunkIndex, raw.HasTotalSize != 0 ? raw.TotalSize : null,
                        ReadUtf8(raw.Reason), raw.Timestamp, ReadUtf8(raw.ReplyToStreamId),
                        content);
                    Queue(() => handler(value));
                }
                catch (Exception error) { Queue(() => throw error); }
            };
            Native.Check(Native.lk_room_register_text_stream_handler(_handle, topic,
                callback, IntPtr.Zero));
            _textStreamHandlers.Add(topic, callback);
        });
    }

    public void UnregisterTextStreamHandler(string topic)
    {
        ArgumentNullException.ThrowIfNull(topic);
        Run(() =>
        {
            Native.Check(Native.lk_room_unregister_text_stream_handler(_handle, topic));
            if (_textStreamHandlers.Remove(topic, out var callback))
                _retiredStreamHandlers.Add(callback);
        });
    }

    public void RegisterByteStreamHandler(string topic, Action<ByteStreamEvent> handler)
    {
        ArgumentNullException.ThrowIfNull(topic);
        ArgumentNullException.ThrowIfNull(handler);
        Run(() =>
        {
            if (_byteStreamHandlers.ContainsKey(topic))
                throw new InvalidOperationException("Byte stream topic is already registered");
            Native.IncomingByteHandler callback = (_, _, pointer) =>
            {
                try
                {
                    var raw = Marshal.PtrToStructure<Native.IncomingByteEvent>(pointer);
                    var value = new ByteStreamEvent((StreamEventType)raw.Type,
                        ReadUtf8(raw.StreamId), ReadUtf8(raw.Name), ReadUtf8(raw.MimeType),
                        ReadUtf8(raw.Topic), ReadUtf8(raw.ParticipantIdentity),
                        CopyBytes(raw.Content, raw.ContentSize), raw.ChunkIndex,
                        raw.HasTotalSize != 0 ? raw.TotalSize : null,
                        ReadUtf8(raw.Reason), raw.Timestamp);
                    Queue(() => handler(value));
                }
                catch (Exception error) { Queue(() => throw error); }
            };
            Native.Check(Native.lk_room_register_byte_stream_handler(_handle, topic,
                callback, IntPtr.Zero));
            _byteStreamHandlers.Add(topic, callback);
        });
    }

    public void UnregisterByteStreamHandler(string topic)
    {
        ArgumentNullException.ThrowIfNull(topic);
        Run(() =>
        {
            Native.Check(Native.lk_room_unregister_byte_stream_handler(_handle, topic));
            if (_byteStreamHandlers.Remove(topic, out var callback))
                _retiredStreamHandlers.Add(callback);
        });
    }

    private static string ReadUtf8(IntPtr pointer) =>
        Marshal.PtrToStringUTF8(pointer) ?? string.Empty;

    private static byte[] CopyBytes(IntPtr pointer, nuint size)
    {
        if (size > int.MaxValue) throw new InvalidOperationException("Stream chunk is too large");
        var result = new byte[(int)size];
        if (result.Length != 0) Marshal.Copy(pointer, result, 0, result.Length);
        return result;
    }
}

internal static partial class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void IncomingTextHandler(IntPtr userData, IntPtr room, IntPtr streamEvent);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void IncomingByteHandler(IntPtr userData, IntPtr room, IntPtr streamEvent);

    [StructLayout(LayoutKind.Sequential)]
    internal struct IncomingTextEvent
    {
        internal int Type;
        internal IntPtr StreamId, MimeType, Topic, ParticipantIdentity, Content;
        internal nuint ContentSize;
        internal ulong ChunkIndex;
        internal int HasTotalSize;
        internal ulong TotalSize;
        internal IntPtr Reason, Attributes;
        internal nuint AttributeCount;
        internal long Timestamp;
        internal IntPtr ReplyToStreamId, AttachedStreamIds;
        internal nuint AttachedStreamIdCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct IncomingByteEvent
    {
        internal int Type;
        internal IntPtr StreamId, Name, MimeType, Topic, ParticipantIdentity, Content;
        internal nuint ContentSize;
        internal ulong ChunkIndex;
        internal int HasTotalSize;
        internal ulong TotalSize;
        internal IntPtr Reason, Attributes;
        internal nuint AttributeCount;
        internal long Timestamp;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_register_text_stream_handler(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topic, IncomingTextHandler handler,
        IntPtr userData);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_unregister_text_stream_handler(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topic);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_register_byte_stream_handler(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topic, IncomingByteHandler handler,
        IntPtr userData);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_unregister_byte_stream_handler(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topic);
}
