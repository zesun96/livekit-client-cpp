using System;
using System.Reflection;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

internal static partial class Native
{
    private const string Library = "livekitclient";

    static Native()
    {
        NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, Resolve);
    }

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != Library) return IntPtr.Zero;
        var path = Environment.GetEnvironmentVariable("LIVEKIT_CLIENT_CPP_LIBRARY");
        return string.IsNullOrWhiteSpace(path) ? IntPtr.Zero : NativeLibrary.Load(path);
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void RoomEvent(IntPtr userData, IntPtr room);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ParticipantEvent(IntPtr userData, IntPtr room, IntPtr participant);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void DataEvent(IntPtr userData, IntPtr room, IntPtr data);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void TextEvent(IntPtr userData, IntPtr room, IntPtr text);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void FileEvent(IntPtr userData, IntPtr room, IntPtr file);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void MetadataEvent(IntPtr userData, IntPtr room, IntPtr metadata);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ChatEvent(IntPtr userData, IntPtr room, IntPtr chat);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void MediaFrameEvent(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, IntPtr frame);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void DataTrackFrameEvent(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, IntPtr frame);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void TrackEvent(IntPtr userData, IntPtr room, IntPtr track, IntPtr participant);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void StateEvent(IntPtr userData, IntPtr room, int state);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void DtmfEvent(IntPtr userData, IntPtr room, IntPtr dtmf);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void SidEvent(IntPtr userData, IntPtr room, IntPtr previousSid, IntPtr sid);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void SnapshotEvent(IntPtr userData, IntPtr room, IntPtr snapshot);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ParticipantStringEvent(IntPtr userData, IntPtr room,
        IntPtr value, IntPtr participant);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ParticipantAttributesEvent(IntPtr userData, IntPtr room,
        IntPtr changes, nuint count, IntPtr participant);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void VideoFrameMetadataEvent(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, IntPtr frame, IntPtr metadata);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void QualityEvent(IntPtr userData, IntPtr room, int quality, IntPtr participant);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ParticipantListEvent(IntPtr userData, IntPtr room,
        IntPtr participants, nuint count);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void TrackStateEvent(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, int state);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ParticipantPermissionsEvent(IntPtr userData, IntPtr room,
        IntPtr previous, IntPtr current, IntPtr participant);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate nuint StringGetter(IntPtr handle, [Out] byte[]? buffer, nuint size);

    // The full versioned lk_room_callbacks_t; struct_size lets older native builds ignore tail fields.
    [StructLayout(LayoutKind.Sequential)]
    internal struct Callbacks
    {
        internal nuint StructSize;
        internal IntPtr UserData;
        internal IntPtr OnConnected;
        internal IntPtr OnDisconnected;
        internal IntPtr OnParticipantConnected;
        internal IntPtr OnParticipantDisconnected;
        internal IntPtr OnTrackPublished;
        internal IntPtr OnTrackUnpublished;
        internal IntPtr OnTrackMuted;
        internal IntPtr OnTrackUnmuted;
        internal IntPtr OnTrackSubscribed;
        internal IntPtr OnAudioFrame;
        internal IntPtr OnVideoFrame;
        internal IntPtr OnDataReceived;
        internal IntPtr OnFileReceived;
        internal IntPtr OnRoomMetadataChanged;
        internal IntPtr OnConnectionQualityChanged;
        internal IntPtr OnActiveSpeakersChanged;
        internal IntPtr OnLocalTrackPublished;
        internal IntPtr OnLocalTrackUnpublished;
        internal IntPtr OnTextReceived;
        internal IntPtr OnByteReceived;
        internal IntPtr OnReconnecting;
        internal IntPtr OnReconnected;
        internal IntPtr OnDisconnectedWithReason;
        internal IntPtr OnTrackSubscriptionPermissionChanged;
        internal IntPtr OnTrackSubscriptionFailed;
        internal IntPtr OnTrackUnsubscribed;
        internal IntPtr OnTrackStreamStateChanged;
        internal IntPtr OnTrackSubscriptionStatusChanged;
        internal IntPtr OnDataChannelBufferStatusChanged;
        internal IntPtr OnSipDtmfReceived;
        internal IntPtr OnChatMessageReceived;
        internal IntPtr OnTranscriptionReceived;
        internal IntPtr OnRecordingStatusChanged;
        internal IntPtr OnMetricsReceived;
        internal IntPtr OnConnectionStateChanged;
        internal IntPtr OnParticipantPermissionsChanged;
        internal IntPtr OnLocalTrackSubscribed;
        internal IntPtr OnSubscribedQualityUpdate;
        internal IntPtr OnEncryptionStateChanged;
        internal IntPtr OnParticipantMetadataChanged;
        internal IntPtr OnParticipantNameChanged;
        internal IntPtr OnParticipantAttributesChanged;
        internal IntPtr OnDataTrackPublished;
        internal IntPtr OnDataTrackUnpublished;
        internal IntPtr OnLocalDataTrackPublished;
        internal IntPtr OnLocalDataTrackUnpublished;
        internal IntPtr OnDataTrackFrame;
        internal IntPtr OnTokenRefreshed;
        internal IntPtr OnRoomUpdated;
        internal IntPtr OnRoomSidChanged;
        internal IntPtr OnRoomMoved;
        internal IntPtr OnRoomEos;
        internal IntPtr OnParticipantsUpdated;
        internal IntPtr OnVideoFrameWithMetadata;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TrackPublicationInfo
    {
        internal IntPtr Sid;
        internal IntPtr Name;
        internal IntPtr MimeType;
        internal int Kind;
        internal int Source;
        internal uint Width;
        internal uint Height;
        internal int IsMuted;
        internal int IsSimulcasted;
        internal int SubscriptionAllowed;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RoomSnapshot
    {
        internal nuint StructSize;
        internal IntPtr Sid;
        internal IntPtr Name;
        internal IntPtr Metadata;
        internal int IsRecording;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SipDtmf
    {
        internal uint Code;
        internal IntPtr Digit;
        internal IntPtr ParticipantIdentity;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataChannelBufferStatus
    {
        internal int Reliable;
        internal ulong BufferedAmount;
        internal ulong HighWaterMark;
        internal ulong LowWaterMark;
        internal int Backpressured;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ParticipantPermissions
    {
        internal int CanSubscribe;
        internal int CanPublish;
        internal int CanPublishData;
        internal IntPtr CanPublishSources;
        internal nuint CanPublishSourceCount;
        internal int Hidden;
        internal int Recorder;
        internal int CanUpdateMetadata;
        internal int Agent;
        internal int CanSubscribeMetrics;
        internal int CanManageAgentSession;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ParticipantInfo
    {
        internal IntPtr Sid;
        internal IntPtr Identity;
        internal IntPtr Name;
        internal IntPtr Metadata;
        internal float AudioLevel;
        internal int ConnectionQuality;
        internal int IsSpeaking;
        internal int IsLocal;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataReceived
    {
        internal IntPtr Data;
        internal nuint DataSize;
        internal IntPtr Topic;
        internal IntPtr ParticipantIdentity;
        internal int Reliable;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TextReceived
    {
        internal IntPtr StreamId;
        internal IntPtr Text;
        internal IntPtr Topic;
        internal IntPtr ParticipantIdentity;
        internal IntPtr ReplyToStreamId;
        internal long Timestamp;
        internal IntPtr AttachedStreamIds;
        internal nuint AttachedStreamIdCount;
        internal IntPtr Attributes;
        internal nuint AttributeCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct FileReceived
    {
        internal IntPtr Data;
        internal nuint DataSize;
        internal IntPtr StreamId;
        internal IntPtr Name;
        internal IntPtr MimeType;
        internal IntPtr Topic;
        internal IntPtr ParticipantIdentity;
        internal IntPtr Attributes;
        internal nuint AttributeCount;
        internal long Timestamp;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ChatMessage
    {
        internal IntPtr Id;
        internal long Timestamp;
        internal int HasEditTimestamp;
        internal long EditTimestamp;
        internal IntPtr Message;
        internal int Deleted;
        internal int Generated;
        internal IntPtr ParticipantIdentity;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DataPublishOptions
    {
        internal nuint StructSize;
        internal int Reliable;
        internal IntPtr Topic;
        internal IntPtr DestinationIdentities;
        internal nuint DestinationIdentityCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct E2eeOptions
    {
        internal nuint StructSize;
        internal int Enabled;
        internal IntPtr SharedKey;
        internal nuint SharedKeySize;
        internal IntPtr RatchetSalt;
        internal nuint RatchetSaltSize;
        internal IntPtr UnencryptedMagicBytes;
        internal nuint UnencryptedMagicBytesSize;
        internal nuint RatchetWindowSize;
        internal int FailureTolerance;
        internal nuint KeyRingSize;
        internal int KeyDerivation;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_version([Out] byte[]? buffer, nuint buffer_size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr lk_last_error();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create(out IntPtr room);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_room_destroy(IntPtr room);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_set_callbacks(RoomHandle room, ref Callbacks callbacks);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_connect(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string url,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string token);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_e2ee_options_init(ref E2eeOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_connect_e2ee(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string url,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string token, ref E2eeOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_disconnect(RoomHandle room);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_state(RoomHandle room);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_is_connected(RoomHandle room);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_room_sid(IntPtr room, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_room_name(IntPtr room, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_room_metadata(IntPtr room, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_identity(IntPtr room, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_local_participant_sid(IntPtr room, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_create_remote_participant_snapshot(RoomHandle room, out IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_remote_participant_list_destroy(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_list_count(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_participant_list_at(IntPtr snapshot, nuint index, out IntPtr participant);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_sid(IntPtr participant, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_identity(IntPtr participant, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_name(IntPtr participant, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_remote_participant_snapshot_metadata(IntPtr participant, [Out] byte[]? buffer, nuint size);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_participant_set_name(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_participant_set_metadata(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string metadata);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_data_publish_options_init(ref DataPublishOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_publish_data(RoomHandle room, byte[] data, nuint size,
        ref DataPublishOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_is_configured(RoomHandle room);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_is_enabled(RoomHandle room);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_set_enabled(RoomHandle room, int enabled);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_e2ee_set_shared_key(RoomHandle room, byte[] key, nuint size, nuint index);

    internal static void Check(int status)
    {
        if (status == 0) return;
        // The native error is thread-local: copy it before calling any other native function.
        var message = Marshal.PtrToStringUTF8(lk_last_error()) ?? "Native LiveKit operation failed";
        throw new LiveKitException(status, message);
    }

    internal static string ReadString(IntPtr handle, StringGetter getter)
    {
        for (var attempt = 0; attempt < 3; attempt++)
        {
            var size = getter(handle, null, 0);
            if (size == 0) return string.Empty;
            if (size > int.MaxValue) throw new LiveKitException(-1, "Native string is too large");
            var buffer = new byte[(int)size];
            var required = getter(handle, buffer, size);
            if (required <= size)
            {
                var length = Array.IndexOf(buffer, (byte)0);
                return System.Text.Encoding.UTF8.GetString(buffer, 0, length < 0 ? buffer.Length : length);
            }
        }
        throw new LiveKitException(-1, "Native string changed while reading");
    }

    internal static string ReadString(SafeHandle handle, StringGetter getter)
    {
        var added = false;
        handle.DangerousAddRef(ref added);
        try { return ReadString(handle.DangerousGetHandle(), getter); }
        finally { if (added) handle.DangerousRelease(); }
    }
}

internal sealed class RoomHandle : SafeHandle
{
    // Delegates stay alive until the native room has finished destroying its callbacks.
    internal readonly Native.RoomEvent Connected;
    internal readonly Native.RoomEvent Disconnected;
    internal readonly Native.ParticipantEvent ParticipantConnected;
    internal readonly Native.ParticipantEvent ParticipantDisconnected;
    internal readonly Native.DataEvent DataReceived;
    internal readonly Native.FileEvent FileReceived;
    internal readonly Native.FileEvent ByteReceived;
    internal readonly Native.TextEvent TextReceived;
    internal readonly Native.MetadataEvent MetadataChanged;
    internal readonly Native.RoomEvent Reconnecting;
    internal readonly Native.RoomEvent Reconnected;
    internal readonly Native.ChatEvent ChatReceived;
    internal readonly Native.MediaFrameEvent AudioFrame;
    internal readonly Native.MediaFrameEvent VideoFrame;
    internal readonly Native.DataTrackFrameEvent DataTrackFrame;
    private readonly System.Collections.Generic.List<Delegate> _additionalCallbacks = new();

    internal IntPtr Root(Delegate callback)
    {
        _additionalCallbacks.Add(callback);
        return Marshal.GetFunctionPointerForDelegate(callback);
    }

    internal RoomHandle(IntPtr value, Native.RoomEvent connected, Native.RoomEvent disconnected,
        Native.ParticipantEvent participantConnected, Native.ParticipantEvent participantDisconnected,
        Native.DataEvent dataReceived, Native.FileEvent fileReceived,
        Native.FileEvent byteReceived, Native.TextEvent textReceived,
        Native.MetadataEvent metadataChanged, Native.RoomEvent reconnecting,
        Native.RoomEvent reconnected, Native.ChatEvent chatReceived,
        Native.MediaFrameEvent audioFrame, Native.MediaFrameEvent videoFrame,
        Native.DataTrackFrameEvent dataTrackFrame) : base(IntPtr.Zero, true)
    {
        SetHandle(value);
        Connected = connected;
        Disconnected = disconnected;
        ParticipantConnected = participantConnected;
        ParticipantDisconnected = participantDisconnected;
        DataReceived = dataReceived;
        FileReceived = fileReceived;
        ByteReceived = byteReceived;
        TextReceived = textReceived;
        MetadataChanged = metadataChanged;
        Reconnecting = reconnecting;
        Reconnected = reconnected;
        ChatReceived = chatReceived;
        AudioFrame = audioFrame;
        VideoFrame = videoFrame;
        DataTrackFrame = dataTrackFrame;
    }

    public override bool IsInvalid => handle == IntPtr.Zero;

    protected override bool ReleaseHandle()
    {
        Native.lk_room_destroy(handle);
        return true;
    }
}
