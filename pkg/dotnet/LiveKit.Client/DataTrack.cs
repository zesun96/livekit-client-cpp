using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public enum DataTrackFrameEncoding
{
    Unspecified = 0, Ros1 = 1, Cdr = 2, Protobuf = 3, FlatBuffer = 4,
    Cbor = 5, MessagePack = 6, Json = 7, Custom = 8,
}

public enum DataTrackSchemaEncoding
{
    Unspecified = 0, Protobuf = 1, FlatBuffer = 2, Ros1Message = 3,
    Ros2Message = 4, Ros2Idl = 5, OmgIdl = 6, JsonSchema = 7, Custom = 8,
}

public sealed class DataTrackException(int code, string message) : Exception(message)
{
    public int Code { get; } = code;
}

public sealed record RemoteDataTrack(string PublisherIdentity, string Sid, string Name,
    string SchemaName, DataTrackFrameEncoding FrameEncoding = DataTrackFrameEncoding.Unspecified,
    string CustomFrameEncoding = "", DataTrackSchemaEncoding SchemaEncoding =
        DataTrackSchemaEncoding.Unspecified, string CustomSchemaEncoding = "");
public sealed record DataTrackFrame(byte[] Data, ulong? UserTimestamp);
public sealed record DataTrackFrameReceived(string TrackSid, string TrackName,
    Participant Participant, DataTrackFrame Frame);
public sealed record DataTrackSchema(string Name, DataTrackSchemaEncoding Encoding,
    string CustomEncoding, byte[] Definition);
public sealed record LocalDataTrackInfo(ushort PublisherHandle, string Sid, string Name,
    bool UsesE2ee, bool IsPublished, DataTrackFrameEncoding FrameEncoding,
    string CustomFrameEncoding, string SchemaName, DataTrackSchemaEncoding SchemaEncoding,
    string CustomSchemaEncoding);

public sealed class LocalDataTrack : IDisposable
{
    private readonly Room _room;
    internal DataTrackHandle Handle { get; }

    internal LocalDataTrack(Room room, IntPtr pointer)
    {
        _room = room;
        Handle = new DataTrackHandle(pointer);
    }

    public string Sid => _room.Run(() => Native.ReadString(Handle,
        Native.lk_local_data_track_sid));
    public string Name => _room.Run(() => Native.ReadString(Handle,
        Native.lk_local_data_track_name));
    public bool IsPublished => _room.Run(() => Native.lk_local_data_track_is_published(Handle) != 0);

    public LocalDataTrackInfo GetInfo() => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        var info = new Native.DataTrackSnapshotInfo();
        Native.lk_data_track_snapshot_info_init(ref info);
        Native.Check(Native.lk_local_data_track_info(Handle, ref info));
        return new LocalDataTrackInfo(info.PublisherHandle,
            Native.ReadString(Handle, Native.lk_local_data_track_sid),
            Native.ReadString(Handle, Native.lk_local_data_track_name),
            info.UsesE2ee != 0, info.IsPublished != 0,
            info.HasFrameEncoding != 0 ? (DataTrackFrameEncoding)info.FrameEncoding :
                DataTrackFrameEncoding.Unspecified,
            Native.ReadString(Handle, Native.lk_local_data_track_custom_frame_encoding),
            Native.ReadString(Handle, Native.lk_local_data_track_schema_name),
            info.HasSchema != 0 ? (DataTrackSchemaEncoding)info.SchemaEncoding :
                DataTrackSchemaEncoding.Unspecified,
            Native.ReadString(Handle, Native.lk_local_data_track_custom_schema_encoding));
    });

    public void Push(byte[] data, ulong? userTimestamp = null)
    {
        ArgumentNullException.ThrowIfNull(data);
        _room.Run(() => Native.CheckDataTrack(Native.lk_local_data_track_try_push(Handle,
            data, (nuint)data.Length, userTimestamp.HasValue ? 1 : 0, userTimestamp ?? 0)));
    }

    public void Unpublish() => _room.Run(() =>
        Native.CheckDataTrack(Native.lk_local_data_track_unpublish(Handle)));

    public void Dispose() => _room.DisposeDataTrack(this);
    internal void DisposeNative() => Handle.Dispose();
}

public sealed class DataTrackReader : IDisposable
{
    private readonly DataTrackReaderHandle _handle;

    internal DataTrackReader(IntPtr pointer) => _handle = new DataTrackReaderHandle(pointer);

    public bool IsClosed => _handle.IsClosed || Native.lk_data_track_reader_is_closed(_handle) != 0;
    public nuint DroppedFrames => _handle.IsClosed ? 0 : Native.lk_data_track_reader_dropped_frames(_handle);

    /// <returns>A frame, or null on timeout/closure.</returns>
    public DataTrackFrame? Read(uint timeoutMs = 0)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        var status = Native.lk_data_track_reader_read_for(_handle, timeoutMs, out var frame);
        if (status == 1 || status == 2) return null;
        if (status != 0) throw new DataTrackException(status, "Failed to read DataTrack frame");
        try
        {
            var size = Native.lk_data_track_frame_data(frame, null, 0);
            if (size > int.MaxValue) throw new DataTrackException(-1, "DataTrack frame is too large");
            var data = new byte[(int)size];
            if (size != 0) Native.lk_data_track_frame_data(frame, data, size);
            var timestamp = Native.lk_data_track_frame_has_user_timestamp(frame) != 0
                ? Native.lk_data_track_frame_user_timestamp(frame) : (ulong?)null;
            return new DataTrackFrame(data, timestamp);
        }
        finally { Native.lk_data_track_frame_destroy(frame); }
    }

    public void Close() { if (!_handle.IsClosed) Native.lk_data_track_reader_close(_handle); }
    public void Dispose() => _handle.Dispose();
}

public sealed partial class Room
{
    private readonly HashSet<LocalDataTrack> _dataTracks = new();

    private void OnDataTrackFrame(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, IntPtr frame)
    {
        if (DataTrackFrameReceived is null || track == IntPtr.Zero ||
            participant == IntPtr.Zero || frame == IntPtr.Zero) return;
        if (System.Threading.Interlocked.Increment(ref _queuedDataTrackFrames) > 32)
        {
            System.Threading.Interlocked.Decrement(ref _queuedDataTrackFrames);
            return;
        }
        try
        {
            var info = Marshal.PtrToStructure<Native.DataTrackInfo>(track);
            var view = Marshal.PtrToStructure<Native.DataTrackFrameView>(frame);
            if (view.DataSize > int.MaxValue) throw new InvalidOperationException("DataTrack frame is too large");
            var data = new byte[(int)view.DataSize];
            if (data.Length != 0) Marshal.Copy(view.Data, data, 0, data.Length);
            var value = new DataTrackFrameReceived(
                Marshal.PtrToStringUTF8(info.Sid) ?? string.Empty,
                Marshal.PtrToStringUTF8(info.Name) ?? string.Empty,
                CopyParticipant(participant),
                new DataTrackFrame(data, view.HasUserTimestamp != 0 ? view.UserTimestamp : null));
            QueueFrame(() => DataTrackFrameReceived?.Invoke(value), null);
        }
        catch { System.Threading.Interlocked.Decrement(ref _queuedDataTrackFrames); }
    }

    public void StoreDataTrackSchema(string name, DataTrackSchemaEncoding encoding,
        byte[] definition, string? customEncoding = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(definition);
        if (encoding == DataTrackSchemaEncoding.Custom)
            ArgumentException.ThrowIfNullOrWhiteSpace(customEncoding);
        Run(() =>
        {
            var schema = new Native.DataTrackSchemaId();
            Native.lk_data_track_schema_id_init(ref schema);
            schema.Name = Marshal.StringToCoTaskMemUTF8(name);
            schema.Encoding = (int)encoding;
            schema.CustomEncoding = customEncoding is null ? IntPtr.Zero :
                Marshal.StringToCoTaskMemUTF8(customEncoding);
            try { Native.CheckDataTrack(Native.lk_room_store_data_track_schema(_handle,
                ref schema, definition, (nuint)definition.Length)); }
            finally
            {
                Marshal.FreeCoTaskMem(schema.Name);
                Marshal.FreeCoTaskMem(schema.CustomEncoding);
            }
        });
    }

    public DataTrackSchema GetDataTrackSchema(string participantIdentity, string name,
        DataTrackSchemaEncoding encoding, string? customEncoding = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (encoding == DataTrackSchemaEncoding.Custom)
            ArgumentException.ThrowIfNullOrWhiteSpace(customEncoding);
        return Run(() =>
        {
            var id = new Native.DataTrackSchemaId();
            Native.lk_data_track_schema_id_init(ref id);
            id.Name = Marshal.StringToCoTaskMemUTF8(name);
            id.Encoding = (int)encoding;
            id.CustomEncoding = customEncoding is null ? IntPtr.Zero :
                Marshal.StringToCoTaskMemUTF8(customEncoding);
            try
            {
                Native.CheckDataTrack(Native.lk_room_get_data_track_schema(_handle,
                    participantIdentity, ref id, out var schema));
                try
                {
                    var size = Native.lk_data_track_schema_definition(schema, null, 0);
                    if (size > int.MaxValue) throw new DataTrackException(-1, "Schema is too large");
                    var definition = new byte[(int)size];
                    if (size != 0) Native.lk_data_track_schema_definition(schema, definition, size);
                    return new DataTrackSchema(
                        Native.ReadString(schema, Native.lk_data_track_schema_name),
                        (DataTrackSchemaEncoding)Native.lk_data_track_schema_encoding(schema),
                        Native.ReadString(schema, Native.lk_data_track_schema_custom_encoding),
                        definition);
                }
                finally { Native.lk_data_track_schema_destroy(schema); }
            }
            finally
            {
                Marshal.FreeCoTaskMem(id.Name);
                Marshal.FreeCoTaskMem(id.CustomEncoding);
            }
        });
    }

    public LocalDataTrack PublishDataTrack(string name, DataTrackFrameEncoding frameEncoding =
        DataTrackFrameEncoding.Unspecified, string? schemaName = null,
        DataTrackSchemaEncoding schemaEncoding = DataTrackSchemaEncoding.Unspecified,
        string? customFrameEncoding = null, string? customSchemaEncoding = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (frameEncoding == DataTrackFrameEncoding.Custom)
            ArgumentException.ThrowIfNullOrWhiteSpace(customFrameEncoding);
        if (schemaEncoding == DataTrackSchemaEncoding.Custom)
            ArgumentException.ThrowIfNullOrWhiteSpace(customSchemaEncoding);
        return Run(() =>
        {
            var options = new Native.DataTrackPublishOptions();
            Native.lk_data_track_publish_options_init(ref options);
            options.Name = Marshal.StringToCoTaskMemUTF8(name);
            options.HasFrameEncoding = frameEncoding == DataTrackFrameEncoding.Unspecified ? 0 : 1;
            options.FrameEncoding = (int)frameEncoding;
            options.CustomFrameEncoding = customFrameEncoding is null ? IntPtr.Zero :
                Marshal.StringToCoTaskMemUTF8(customFrameEncoding);
            if (schemaName is not null)
            {
                options.HasSchema = 1;
                options.Schema.Name = Marshal.StringToCoTaskMemUTF8(schemaName);
                options.Schema.Encoding = (int)schemaEncoding;
                options.Schema.CustomEncoding = customSchemaEncoding is null ? IntPtr.Zero :
                    Marshal.StringToCoTaskMemUTF8(customSchemaEncoding);
            }
            try
            {
                Native.CheckDataTrack(Native.lk_room_publish_data_track(_handle, ref options,
                    out var pointer));
                var track = new LocalDataTrack(this, pointer);
                _dataTracks.Add(track);
                return track;
            }
            finally
            {
                Marshal.FreeCoTaskMem(options.Name);
                Marshal.FreeCoTaskMem(options.CustomFrameEncoding);
                Marshal.FreeCoTaskMem(options.Schema.Name);
                Marshal.FreeCoTaskMem(options.Schema.CustomEncoding);
            }
        });
    }

    public IReadOnlyList<RemoteDataTrack> GetRemoteDataTracks() => Run(() =>
    {
        Native.Check(Native.lk_room_create_remote_data_track_snapshot(_handle, out var list));
        try
        {
            var count = Native.lk_remote_data_track_list_count(list);
            if (count > int.MaxValue) throw new LiveKitException(-1, "Too many remote DataTracks");
            var tracks = new List<RemoteDataTrack>((int)count);
            for (nuint i = 0; i < count; i++)
            {
                Native.Check(Native.lk_remote_data_track_list_at(list, i, out var item));
                var info = new Native.DataTrackSnapshotInfo();
                Native.lk_data_track_snapshot_info_init(ref info);
                Native.Check(Native.lk_remote_data_track_snapshot_info(item, ref info));
                tracks.Add(new RemoteDataTrack(
                    Native.ReadString(item, Native.lk_remote_data_track_snapshot_publisher_identity),
                    Native.ReadString(item, Native.lk_remote_data_track_snapshot_sid),
                    Native.ReadString(item, Native.lk_remote_data_track_snapshot_name),
                    Native.ReadString(item, Native.lk_remote_data_track_snapshot_schema_name),
                    info.HasFrameEncoding != 0 ? (DataTrackFrameEncoding)info.FrameEncoding :
                        DataTrackFrameEncoding.Unspecified,
                    Native.ReadString(item,
                        Native.lk_remote_data_track_snapshot_custom_frame_encoding),
                    info.HasSchema != 0 ? (DataTrackSchemaEncoding)info.SchemaEncoding :
                        DataTrackSchemaEncoding.Unspecified,
                    Native.ReadString(item,
                        Native.lk_remote_data_track_snapshot_custom_schema_encoding)));
            }
            return tracks;
        }
        finally { Native.lk_remote_data_track_list_destroy(list); }
    });

    public DataTrackReader SubscribeDataTrack(string participantIdentity, string trackSid)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        return Run(() =>
        {
            Native.CheckDataTrack(Native.lk_room_subscribe_data_track(_handle,
                participantIdentity, trackSid, IntPtr.Zero, out var reader));
            return new DataTrackReader(reader);
        });
    }

    public void UpdateDataTrackSubscriptionOptions(string participantIdentity, string trackSid,
        uint? targetFps = null, nuint bufferCapacity = 16)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        Run(() =>
        {
            var options = new Native.DataTrackSubscriptionOptions();
            Native.lk_data_track_subscription_options_init(ref options);
            options.HasTargetFps = targetFps.HasValue ? 1 : 0;
            options.TargetFps = targetFps ?? 0;
            options.BufferCapacity = bufferCapacity;
            Native.CheckDataTrack(Native.lk_room_update_data_track_subscription_options(_handle,
                participantIdentity, trackSid, ref options));
        });
    }

    internal void DisposeDataTrack(LocalDataTrack track)
    {
        lock (_sync)
        {
            if (_dataTracks.Remove(track)) track.DisposeNative();
        }
    }
}

internal sealed class DataTrackHandle : SafeHandle
{
    internal DataTrackHandle(IntPtr value) : base(IntPtr.Zero, true) => SetHandle(value);
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle()
    {
        Native.lk_local_data_track_unpublish(handle);
        return Native.lk_local_data_track_destroy(handle) == 0;
    }
}

internal sealed class DataTrackReaderHandle : SafeHandle
{
    internal DataTrackReaderHandle(IntPtr value) : base(IntPtr.Zero, true) => SetHandle(value);
    public override bool IsInvalid => handle == IntPtr.Zero;
    protected override bool ReleaseHandle() { Native.lk_data_track_reader_destroy(handle); return true; }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct DataTrackSnapshotInfo
    {
        internal nuint StructSize;
        internal ushort PublisherHandle;
        internal int UsesE2ee;
        internal int HasFrameEncoding;
        internal int FrameEncoding;
        internal int HasSchema;
        internal int SchemaEncoding;
        internal int IsPublished;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_snapshot_info_init(ref DataTrackSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_data_track_snapshot_info(IntPtr item,
        ref DataTrackSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_data_track_info(DataTrackHandle track,
        ref DataTrackSnapshotInfo info);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_data_track_custom_frame_encoding(IntPtr track,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_data_track_schema_name(IntPtr track,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_data_track_custom_schema_encoding(IntPtr track,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_data_track_snapshot_custom_frame_encoding(
        IntPtr item, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_data_track_snapshot_custom_schema_encoding(
        IntPtr item, [Out] byte[]? buffer, nuint size);

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataTrackInfo
    {
        internal ushort PublisherHandle;
        internal IntPtr Sid;
        internal IntPtr Name;
        internal int UsesE2ee;
        internal int HasFrameEncoding;
        internal int FrameEncoding;
        internal IntPtr CustomFrameEncoding;
        internal int HasSchema;
        internal IntPtr SchemaName;
        internal int SchemaEncoding;
        internal IntPtr CustomSchemaEncoding;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataTrackFrameView
    {
        internal IntPtr Data;
        internal nuint DataSize;
        internal int HasUserTimestamp;
        internal ulong UserTimestamp;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataTrackSchemaId
    {
        internal nuint StructSize;
        internal IntPtr Name;
        internal int Encoding;
        internal IntPtr CustomEncoding;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataTrackPublishOptions
    {
        internal nuint StructSize;
        internal IntPtr Name;
        internal int HasFrameEncoding;
        internal int FrameEncoding;
        internal IntPtr CustomFrameEncoding;
        internal int HasSchema;
        internal DataTrackSchemaId Schema;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataTrackSubscriptionOptions
    {
        internal nuint StructSize;
        internal int HasTargetFps;
        internal uint TargetFps;
        internal nuint BufferCapacity;
        internal nuint MaxPartialFrames;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_schema_id_init(ref DataTrackSchemaId schema);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_publish_options_init(ref DataTrackPublishOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_subscription_options_init(
        ref DataTrackSubscriptionOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_store_data_track_schema(RoomHandle room,
        ref DataTrackSchemaId schema, byte[] definition, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_get_data_track_schema(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity,
        ref DataTrackSchemaId id, out IntPtr schema);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_schema_destroy(IntPtr schema);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_track_schema_name(IntPtr schema, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_data_track_schema_encoding(IntPtr schema);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_track_schema_custom_encoding(IntPtr schema,
        [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_track_schema_definition(IntPtr schema,
        [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_publish_data_track(RoomHandle room,
        ref DataTrackPublishOptions options, out IntPtr track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_data_track_try_push(DataTrackHandle track, byte[] data,
        nuint size, int hasTimestamp, ulong timestamp);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_data_track_unpublish(DataTrackHandle track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_data_track_unpublish(IntPtr track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_data_track_destroy(IntPtr track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_data_track_is_published(DataTrackHandle track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_data_track_sid(IntPtr track, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_data_track_name(IntPtr track, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create_remote_data_track_snapshot(RoomHandle room, out IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_remote_data_track_list_destroy(IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_data_track_list_count(IntPtr list);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_data_track_list_at(IntPtr list, nuint index, out IntPtr track);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_data_track_snapshot_publisher_identity(IntPtr track,
        [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_data_track_snapshot_sid(IntPtr track,
        [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_data_track_snapshot_name(IntPtr track,
        [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_data_track_snapshot_schema_name(IntPtr track,
        [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_subscribe_data_track(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackSid, IntPtr options, out IntPtr reader);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_update_data_track_subscription_options(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantIdentity,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackSid,
        ref DataTrackSubscriptionOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_reader_destroy(IntPtr reader);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_reader_close(DataTrackReaderHandle reader);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_data_track_reader_is_closed(DataTrackReaderHandle reader);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_track_reader_dropped_frames(DataTrackReaderHandle reader);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_data_track_reader_read_for(DataTrackReaderHandle reader,
        uint timeoutMs, out IntPtr frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_track_frame_destroy(IntPtr frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_data_track_frame_data(IntPtr frame, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_data_track_frame_has_user_timestamp(IntPtr frame);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong lk_data_track_frame_user_timestamp(IntPtr frame);

    internal static void CheckDataTrack(int code)
    {
        if (code == 0) return;
        throw new DataTrackException(code,
            Marshal.PtrToStringUTF8(lk_last_error()) ?? "DataTrack operation failed");
    }
}
