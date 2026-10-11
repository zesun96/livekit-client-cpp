using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct TextSendOptions
    {
        internal nuint StructSize;
        internal IntPtr Topic;
        internal IntPtr DestinationIdentities;
        internal nuint DestinationIdentityCount;
        internal IntPtr Attributes;
        internal nuint AttributeCount;
        internal IntPtr ReplyToStreamId;
        internal IntPtr AttachedStreamIds;
        internal nuint AttachedStreamIdCount;
        internal nuint ChunkSize;
        internal int Compress;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ByteSendOptions
    {
        internal nuint StructSize;
        internal IntPtr Topic;
        internal IntPtr MimeType;
        internal IntPtr Name;
        internal IntPtr DestinationIdentities;
        internal nuint DestinationIdentityCount;
        internal IntPtr Attributes;
        internal nuint AttributeCount;
        internal nuint ChunkSize;
        internal int Compress;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct FileSendOptions
    {
        internal nuint StructSize;
        internal IntPtr Topic;
        internal IntPtr MimeType;
        internal IntPtr DestinationIdentities;
        internal nuint DestinationIdentityCount;
        internal nuint ChunkSize;
        internal IntPtr Attributes;
        internal nuint AttributeCount;
        internal int Compress;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RpcPerformOptions
    {
        internal nuint StructSize;
        internal IntPtr DestinationIdentity;
        internal IntPtr Method;
        internal IntPtr Payload;
        internal uint ResponseTimeoutMs;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_send_text(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string text, ref TextSendOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_send_bytes(RoomHandle room, byte[] data, nuint size,
        ref ByteSendOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_send_file(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path, ref FileSendOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_publish_dtmf(RoomHandle room, uint code,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string digit);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_send_chat_message(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string message, [Out] byte[] id,
        nuint idSize, out long timestamp);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_edit_chat_message(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string id, long originalTimestamp,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string message);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_rpc_perform_options_init(ref RpcPerformOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_perform_rpc(RoomHandle room, ref RpcPerformOptions options,
        out IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_rpc_result_destroy(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_rpc_result_ok(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rpc_result_payload(IntPtr result, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint lk_rpc_result_error_code(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rpc_result_error_message(IntPtr result, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rpc_result_error_data(IntPtr result, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_set_remote_track_subscribed(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantSid,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackSid, int subscribed);

    [StructLayout(LayoutKind.Sequential)]
    internal struct RemoteTrackSettings
    {
        internal nuint StructSize;
        internal int Enabled;
        internal int HasVideoQuality;
        internal int VideoQuality;
        internal uint VideoWidth;
        internal uint VideoHeight;
        internal uint VideoFps;
        internal uint Priority;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_remote_track_settings_init(ref RemoteTrackSettings settings);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_update_remote_track_settings(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string participantSid,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string trackSid, ref RemoteTrackSettings settings);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_set_track_subscription_permissions(RoomHandle room,
        int allowAll, IntPtr permissions, nuint count);

    [StructLayout(LayoutKind.Sequential)]
    internal struct ParticipantTrackPermission
    {
        internal nuint StructSize;
        internal IntPtr ParticipantSid;
        internal IntPtr ParticipantIdentity;
        internal int AllowAll;
        internal IntPtr AllowedTrackSids;
        internal nuint AllowedTrackSidCount;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_participant_track_permission_init(ref ParticipantTrackPermission permission);
}
