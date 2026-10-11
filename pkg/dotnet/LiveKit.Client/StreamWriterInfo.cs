using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace LiveKit.Client;

public enum StreamWriterKind { Text = 0, Bytes = 1 }
public sealed record StreamWriterInfo(StreamWriterKind Kind, string StreamId,
    string MimeType, string Topic, string ParticipantIdentity, string Name,
    string ReplyToStreamId, ulong? TotalSize, long Timestamp,
    IReadOnlyDictionary<string, string> Attributes,
    IReadOnlyList<string> AttachedStreamIds);

public sealed partial class TextStreamWriter
{
    public StreamWriterInfo GetInfo()
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        Native.Check(Native.lk_text_stream_writer_create_info_snapshot(_handle,
            out var snapshot));
        try { return StreamWriterInfoReader.Read(snapshot); }
        finally { Native.lk_data_stream_writer_info_snapshot_destroy(snapshot); }
    }
}

public sealed partial class ByteStreamWriter
{
    public StreamWriterInfo GetInfo()
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        Native.Check(Native.lk_byte_stream_writer_create_info_snapshot(_handle,
            out var snapshot));
        try { return StreamWriterInfoReader.Read(snapshot); }
        finally { Native.lk_data_stream_writer_info_snapshot_destroy(snapshot); }
    }
}

internal static class StreamWriterInfoReader
{
    internal static StreamWriterInfo Read(IntPtr snapshot)
    {
        var info = new Native.StreamWriterInfoView();
        Native.lk_data_stream_writer_info_init(ref info);
        Native.Check(Native.lk_data_stream_writer_info_snapshot_info(snapshot, ref info));
        if (info.AttributeCount > int.MaxValue || info.AttachedStreamIdCount > int.MaxValue)
            throw new LiveKitException(-1, "Stream writer metadata is too large");
        var attributes = new Dictionary<string, string>((int)info.AttributeCount);
        for (nuint i = 0; i < info.AttributeCount; i++)
            attributes[ReadIndexed(snapshot, i,
                Native.lk_data_stream_writer_info_snapshot_attribute_key)] =
                ReadIndexed(snapshot, i,
                    Native.lk_data_stream_writer_info_snapshot_attribute_value);
        var attached = new List<string>((int)info.AttachedStreamIdCount);
        for (nuint i = 0; i < info.AttachedStreamIdCount; i++)
            attached.Add(ReadIndexed(snapshot, i,
                Native.lk_data_stream_writer_info_snapshot_attached_stream_id));
        return new StreamWriterInfo((StreamWriterKind)info.Kind,
            Native.ReadString(snapshot, Native.lk_data_stream_writer_info_snapshot_stream_id),
            Native.ReadString(snapshot, Native.lk_data_stream_writer_info_snapshot_mime_type),
            Native.ReadString(snapshot, Native.lk_data_stream_writer_info_snapshot_topic),
            Native.ReadString(snapshot,
                Native.lk_data_stream_writer_info_snapshot_participant_identity),
            Native.ReadString(snapshot, Native.lk_data_stream_writer_info_snapshot_name),
            Native.ReadString(snapshot, Native.lk_data_stream_writer_info_snapshot_reply_to_stream_id),
            info.HasTotalSize != 0 ? info.TotalSize : null,
            info.Timestamp, attributes, attached);
    }

    private static string ReadIndexed(IntPtr snapshot, nuint index,
        Native.IndexedStringGetter getter)
    {
        var size = getter(snapshot, index, null, 0);
        if (size == 0) return string.Empty;
        if (size > int.MaxValue) throw new LiveKitException(-1, "Stream writer string is too large");
        var bytes = new byte[(int)size];
        getter(snapshot, index, bytes, size);
        var length = Array.IndexOf(bytes, (byte)0);
        return Encoding.UTF8.GetString(bytes, 0, length < 0 ? bytes.Length : length);
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct StreamWriterInfoView
    {
        internal nuint StructSize;
        internal int Kind;
        internal int HasTotalSize;
        internal ulong TotalSize;
        internal long Timestamp;
        internal nuint AttributeCount;
        internal nuint AttachedStreamIdCount;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_text_stream_writer_create_info_snapshot(
        TextStreamWriterHandle writer, out IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_byte_stream_writer_create_info_snapshot(
        ByteStreamWriterHandle writer, out IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_stream_writer_info_snapshot_destroy(IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_stream_writer_info_init(ref StreamWriterInfoView info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_data_stream_writer_info_snapshot_info(IntPtr snapshot,
        ref StreamWriterInfoView info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_stream_id(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_mime_type(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_topic(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_participant_identity(
        IntPtr snapshot, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_name(IntPtr snapshot,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_reply_to_stream_id(
        IntPtr snapshot, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_attribute_key(
        IntPtr snapshot, nuint index, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_attribute_value(
        IntPtr snapshot, nuint index, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_stream_writer_info_snapshot_attached_stream_id(
        IntPtr snapshot, nuint index, [Out] byte[]? buffer, nuint size);
}
