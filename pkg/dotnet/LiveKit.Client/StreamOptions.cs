using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed class TextStreamRequest
{
    public Action<StreamProgress>? OnProgress { get; init; }
    public Action<StreamCompletion>? OnComplete { get; init; }
    public string Topic { get; init; } = "";
    public IReadOnlyList<string> DestinationIdentities { get; init; } = Array.Empty<string>();
    public IReadOnlyDictionary<string, string> Attributes { get; init; } =
        new Dictionary<string, string>();
    public string? ReplyToStreamId { get; init; }
    public IReadOnlyList<string> AttachedStreamIds { get; init; } = Array.Empty<string>();
    public string? StreamId { get; init; }
    public ulong? TotalSize { get; init; }
    public nuint ChunkSize { get; init; } = 15_000;
    public bool Compress { get; init; }
    public bool Update { get; init; }
    public int Version { get; init; }
}

public sealed class ByteStreamRequest
{
    public Action<StreamProgress>? OnProgress { get; init; }
    public Action<StreamCompletion>? OnComplete { get; init; }
    public string Topic { get; init; } = "";
    public string MimeType { get; init; } = "application/octet-stream";
    public string Name { get; init; } = "";
    public IReadOnlyList<string> DestinationIdentities { get; init; } = Array.Empty<string>();
    public IReadOnlyDictionary<string, string> Attributes { get; init; } =
        new Dictionary<string, string>();
    public string? StreamId { get; init; }
    public ulong? TotalSize { get; init; }
    public nuint ChunkSize { get; init; } = 15_000;
    public bool Compress { get; init; }
}

public sealed partial class Room
{
    public TextStreamWriter StreamText(TextStreamRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return Run(() =>
        {
            using var arena = new NativeArena();
            var callbacks = new StreamCallbackRegistration(request.OnProgress,
                request.OnComplete, Queue);
            var options = new Native.AdvancedTextStreamOptions
            {
                StructSize = (nuint)Marshal.SizeOf<Native.AdvancedTextStreamOptions>(),
                Topic = arena.Utf8(request.Topic),
                ReplyToStreamId = request.ReplyToStreamId is null ? IntPtr.Zero :
                    arena.Utf8(request.ReplyToStreamId),
                StreamId = request.StreamId is null ? IntPtr.Zero : arena.Utf8(request.StreamId),
                HasTotalSize = request.TotalSize.HasValue ? 1 : 0,
                TotalSize = request.TotalSize ?? 0,
                ChunkSize = request.ChunkSize,
                Compress = request.Compress ? 1 : 0,
                Update = request.Update ? 1 : 0,
                Version = request.Version,
                OnProgress = callbacks.ProgressPointer,
                OnComplete = callbacks.CompletionPointer,
            };
            (options.DestinationIdentities, options.DestinationIdentityCount) =
                CopyStrings(arena, request.DestinationIdentities);
            (options.AttachedStreamIds, options.AttachedStreamIdCount) =
                CopyStrings(arena, request.AttachedStreamIds);
            (options.Attributes, options.AttributeCount) = CopyAttributes(arena, request.Attributes);
            Native.Check(Native.lk_room_stream_text_advanced(_handle, ref options,
                out var writer));
            return new TextStreamWriter(writer, callbacks);
        });
    }

    public ByteStreamWriter StreamBytes(ByteStreamRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return Run(() =>
        {
            using var arena = new NativeArena();
            var callbacks = new StreamCallbackRegistration(request.OnProgress,
                request.OnComplete, Queue);
            var options = new Native.AdvancedByteStreamOptions
            {
                StructSize = (nuint)Marshal.SizeOf<Native.AdvancedByteStreamOptions>(),
                Topic = arena.Utf8(request.Topic),
                MimeType = arena.Utf8(request.MimeType),
                Name = arena.Utf8(request.Name),
                StreamId = request.StreamId is null ? IntPtr.Zero : arena.Utf8(request.StreamId),
                HasTotalSize = request.TotalSize.HasValue ? 1 : 0,
                TotalSize = request.TotalSize ?? 0,
                ChunkSize = request.ChunkSize,
                Compress = request.Compress ? 1 : 0,
                OnProgress = callbacks.ProgressPointer,
                OnComplete = callbacks.CompletionPointer,
            };
            (options.DestinationIdentities, options.DestinationIdentityCount) =
                CopyStrings(arena, request.DestinationIdentities);
            (options.Attributes, options.AttributeCount) = CopyAttributes(arena, request.Attributes);
            Native.Check(Native.lk_room_stream_bytes_advanced(_handle, ref options,
                out var writer));
            return new ByteStreamWriter(writer, callbacks);
        });
    }

    private static (IntPtr, nuint) CopyStrings(NativeArena arena,
        IReadOnlyList<string> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        if (values.Count == 0) return (IntPtr.Zero, 0);
        var pointers = new IntPtr[values.Count];
        for (var i = 0; i < pointers.Length; i++) pointers[i] = arena.Utf8(values[i]);
        return (arena.PointerArray(pointers), (nuint)pointers.Length);
    }

    private static (IntPtr, nuint) CopyAttributes(NativeArena arena,
        IReadOnlyDictionary<string, string> attributes)
    {
        ArgumentNullException.ThrowIfNull(attributes);
        if (attributes.Count == 0) return (IntPtr.Zero, 0);
        var values = new Native.Attribute[attributes.Count];
        var i = 0;
        foreach (var pair in attributes)
            values[i++] = new Native.Attribute
            {
                Key = arena.Utf8(pair.Key), Value = arena.Utf8(pair.Value),
            };
        return (arena.StructArray(values), (nuint)values.Length);
    }
}

internal static partial class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void StreamProgressCallback(IntPtr userData, ulong bytesSent,
        int hasTotalSize, ulong totalSize);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void StreamCompletionCallback(IntPtr userData, IntPtr completion);

    [StructLayout(LayoutKind.Sequential)]
    internal struct StreamCompletionView
    {
        internal int Status;
        internal IntPtr StreamId;
        internal ulong BytesSent;
        internal int HasTotalSize;
        internal ulong TotalSize;
        internal IntPtr Reason;
        internal int ErrorDomain;
        internal int ErrorCode;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AdvancedTextStreamOptions
    {
        internal nuint StructSize;
        internal IntPtr Topic, DestinationIdentities;
        internal nuint DestinationIdentityCount;
        internal IntPtr Attributes;
        internal nuint AttributeCount;
        internal IntPtr ReplyToStreamId, AttachedStreamIds;
        internal nuint AttachedStreamIdCount;
        internal IntPtr StreamId;
        internal int HasTotalSize;
        internal ulong TotalSize;
        internal nuint ChunkSize;
        internal int Update;
        internal int Version;
        internal IntPtr OnProgress, ProgressUserData;
        internal int Compress;
        internal IntPtr OnComplete, CompletionUserData;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AdvancedByteStreamOptions
    {
        internal nuint StructSize;
        internal IntPtr Topic, MimeType, Name, DestinationIdentities;
        internal nuint DestinationIdentityCount;
        internal IntPtr Attributes;
        internal nuint AttributeCount;
        internal IntPtr StreamId;
        internal int HasTotalSize;
        internal ulong TotalSize;
        internal nuint ChunkSize;
        internal IntPtr OnProgress, ProgressUserData;
        internal int Compress;
        internal IntPtr OnComplete, CompletionUserData;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "lk_room_stream_text")]
    internal static extern int lk_room_stream_text_advanced(RoomHandle room,
        ref AdvancedTextStreamOptions options, out IntPtr writer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "lk_room_stream_bytes")]
    internal static extern int lk_room_stream_bytes_advanced(RoomHandle room,
        ref AdvancedByteStreamOptions options, out IntPtr writer);
}
