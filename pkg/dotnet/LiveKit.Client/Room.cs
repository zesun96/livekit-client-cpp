using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace LiveKit.Client;

public sealed class LiveKitException : Exception
{
    public int Status { get; }

    public LiveKitException(int status, string message) : base(message) => Status = status;
}

public enum ConnectionState
{
    Connecting = 0,
    Connected = 1,
    Disconnecting = 2,
    Disconnected = 3,
    Failed = 4,
    Reconnecting = 5,
}

public sealed record Participant(string Sid, string Identity, string Name, string Metadata,
    float AudioLevel = 0, int ConnectionQuality = 0, bool IsSpeaking = false, bool IsLocal = false,
    IReadOnlyDictionary<string, string>? Attributes = null,
    IReadOnlyList<RemoteTrackPublication>? TrackPublications = null)
{
    public ParticipantPermissions? Permissions { get; init; }
}

public sealed record DataPacket(byte[] Data, string Topic, string ParticipantIdentity, bool Reliable);
public sealed record DataPublishRequest
{
    public bool Reliable { get; init; } = true;
    public string Topic { get; init; } = "";
    public IReadOnlyList<string> DestinationIdentities { get; init; } = Array.Empty<string>();
}
public sealed record TextMessage(string StreamId, string Text, string Topic,
    string ParticipantIdentity, string ReplyToStreamId, long Timestamp)
{
    public IReadOnlyList<string> AttachedStreamIds { get; init; } = Array.Empty<string>();
    public IReadOnlyDictionary<string, string> Attributes { get; init; } =
        new Dictionary<string, string>();
}
public sealed record FileMessage(byte[] Data, string StreamId, string Name, string MimeType,
    string Topic, string ParticipantIdentity, long Timestamp)
{
    public IReadOnlyDictionary<string, string> Attributes { get; init; } =
        new Dictionary<string, string>();
}
public sealed record ChatMessage(string Id, long Timestamp, long? EditTimestamp,
    string Message, bool Deleted, bool Generated, string ParticipantIdentity);
public sealed record AudioFrameEvent(string TrackSid, Participant Participant, AudioFrame Frame);
public sealed record VideoFrameEvent(string TrackSid, Participant Participant, VideoFrame Frame);

/// <summary>An owned LiveKit room backed by the livekitclient C ABI.</summary>
public sealed partial class Room : IDisposable
{
    private readonly object _sync = new();
    private readonly RoomHandle _handle;
    private volatile bool _disposed;
    private int _queuedAudioFrames;
    private int _queuedVideoFrames;
    private int _queuedDataTrackFrames;

    public event Action? Connected;
    public event Action? Disconnected;
    public event Action<Participant>? ParticipantConnected;
    public event Action<Participant>? ParticipantDisconnected;
    public event Action<DataPacket>? DataReceived;
    public event Action<FileMessage>? FileReceived;
    public event Action<FileMessage>? BytesReceived;
    public event Action<TextMessage>? TextReceived;
    public event Action<ChatMessage>? ChatReceived;
    public event Action<string>? MetadataChanged;
    public event Action? Reconnecting;
    public event Action? Reconnected;
    public event Action<AudioFrameEvent>? AudioFrameReceived;
    public event Action<VideoFrameEvent>? VideoFrameReceived;
    public event Action<DataTrackFrameReceived>? DataTrackFrameReceived;
    /// <summary>Reports exceptions raised by event subscribers on the thread pool.</summary>
    public event Action<Exception>? CallbackError;

    public Room()
    {
        Native.Check(Native.lk_room_create(out var pointer));
        _handle = new RoomHandle(pointer, OnConnected, OnDisconnected,
            OnParticipantConnected, OnParticipantDisconnected, OnDataReceived,
            OnFileReceived, OnByteReceived, OnTextReceived, OnMetadataChanged,
            OnReconnecting, OnReconnected, OnChatReceived, OnAudioFrame, OnVideoFrame,
            OnDataTrackFrame);
        var callbacks = new Native.Callbacks
        {
            StructSize = (nuint)Marshal.SizeOf<Native.Callbacks>(),
            OnConnected = Marshal.GetFunctionPointerForDelegate(_handle.Connected),
            OnDisconnected = Marshal.GetFunctionPointerForDelegate(_handle.Disconnected),
            OnParticipantConnected = Marshal.GetFunctionPointerForDelegate(_handle.ParticipantConnected),
            OnParticipantDisconnected = Marshal.GetFunctionPointerForDelegate(_handle.ParticipantDisconnected),
            OnDataReceived = Marshal.GetFunctionPointerForDelegate(_handle.DataReceived),
            OnFileReceived = Marshal.GetFunctionPointerForDelegate(_handle.FileReceived),
            OnByteReceived = Marshal.GetFunctionPointerForDelegate(_handle.ByteReceived),
            OnTextReceived = Marshal.GetFunctionPointerForDelegate(_handle.TextReceived),
            OnRoomMetadataChanged = Marshal.GetFunctionPointerForDelegate(_handle.MetadataChanged),
            OnReconnecting = Marshal.GetFunctionPointerForDelegate(_handle.Reconnecting),
            OnReconnected = Marshal.GetFunctionPointerForDelegate(_handle.Reconnected),
            OnChatMessageReceived = Marshal.GetFunctionPointerForDelegate(_handle.ChatReceived),
            OnAudioFrame = Marshal.GetFunctionPointerForDelegate(_handle.AudioFrame),
            OnVideoFrame = Marshal.GetFunctionPointerForDelegate(_handle.VideoFrame),
            OnDataTrackFrame = Marshal.GetFunctionPointerForDelegate(_handle.DataTrackFrame),
            OnTrackPublished = _handle.Root((Native.TrackEvent)OnTrackPublished),
            OnTrackUnpublished = _handle.Root((Native.TrackEvent)OnTrackUnpublished),
            OnTrackMuted = _handle.Root((Native.TrackEvent)OnTrackMuted),
            OnTrackUnmuted = _handle.Root((Native.TrackEvent)OnTrackUnmuted),
            OnTrackSubscribed = _handle.Root((Native.TrackEvent)OnTrackSubscribed),
            OnTrackUnsubscribed = _handle.Root((Native.TrackEvent)OnTrackUnsubscribed),
            OnLocalTrackPublished = _handle.Root((Native.TrackEvent)OnLocalTrackPublished),
            OnLocalTrackUnpublished = _handle.Root((Native.TrackEvent)OnLocalTrackUnpublished),
            OnConnectionStateChanged = _handle.Root((Native.StateEvent)OnConnectionStateChanged),
            OnDisconnectedWithReason = _handle.Root((Native.StateEvent)OnDisconnectedWithReason),
            OnSipDtmfReceived = _handle.Root((Native.DtmfEvent)OnSipDtmfReceived),
            OnRecordingStatusChanged = _handle.Root((Native.StateEvent)OnRecordingStatusChanged),
            OnTokenRefreshed = _handle.Root((Native.RoomEvent)OnTokenRefreshed),
            OnRoomUpdated = _handle.Root((Native.SnapshotEvent)OnRoomUpdated),
            OnRoomMoved = _handle.Root((Native.SnapshotEvent)OnRoomMoved),
            OnRoomSidChanged = _handle.Root((Native.SidEvent)OnRoomSidChanged),
            OnRoomEos = _handle.Root((Native.RoomEvent)OnRoomEos),
            OnParticipantMetadataChanged = _handle.Root((Native.ParticipantStringEvent)OnParticipantMetadataChanged),
            OnParticipantNameChanged = _handle.Root((Native.ParticipantStringEvent)OnParticipantNameChanged),
            OnParticipantAttributesChanged = _handle.Root((Native.ParticipantAttributesEvent)OnParticipantAttributesChanged),
            OnDataTrackPublished = _handle.Root((Native.TrackEvent)OnDataTrackPublished),
            OnDataTrackUnpublished = _handle.Root((Native.TrackEvent)OnDataTrackUnpublished),
            OnLocalDataTrackPublished = _handle.Root((Native.TrackEvent)OnLocalDataTrackPublished),
            OnLocalDataTrackUnpublished = _handle.Root((Native.TrackEvent)OnLocalDataTrackUnpublished),
            OnVideoFrameWithMetadata = _handle.Root((Native.VideoFrameMetadataEvent)OnVideoFrameWithMetadata),
            OnConnectionQualityChanged = _handle.Root((Native.QualityEvent)OnConnectionQualityChanged),
            OnActiveSpeakersChanged = _handle.Root((Native.ParticipantListEvent)OnActiveSpeakersChanged),
            OnParticipantsUpdated = _handle.Root((Native.ParticipantListEvent)OnParticipantsUpdated),
            OnTrackSubscriptionPermissionChanged = _handle.Root((Native.TrackStateEvent)OnTrackSubscriptionPermissionChanged),
            OnTrackSubscriptionFailed = _handle.Root((Native.TrackStateEvent)OnTrackSubscriptionFailed),
            OnTrackStreamStateChanged = _handle.Root((Native.TrackStateEvent)OnTrackStreamStateChanged),
            OnTrackSubscriptionStatusChanged = _handle.Root((Native.TrackStateEvent)OnTrackSubscriptionStatusChanged),
            OnDataChannelBufferStatusChanged = _handle.Root((Native.DataEvent)OnDataChannelBufferStatusChanged),
            OnParticipantPermissionsChanged = _handle.Root((Native.ParticipantPermissionsEvent)OnParticipantPermissionsChanged),
            OnLocalTrackSubscribed = _handle.Root((Native.TrackEvent)OnLocalTrackSubscribed),
            OnSubscribedQualityUpdate = _handle.Root((Native.TrackQualityUpdateEvent)OnSubscribedQualityUpdate),
            OnEncryptionStateChanged = _handle.Root((Native.DataEvent)OnEncryptionStateChanged),
            OnTranscriptionReceived = _handle.Root((Native.DataEvent)OnTranscriptionReceived),
            OnMetricsReceived = _handle.Root((Native.DataEvent)OnMetricsReceived),
        };
        try { Native.Check(Native.lk_room_set_callbacks(_handle, ref callbacks)); }
        catch { _handle.Dispose(); throw; }
    }

    public static string Version
    {
        get
        {
            var size = Native.lk_version(null, 0);
            if (size == 0) return string.Empty;
            if (size > int.MaxValue) throw new LiveKitException(-1, "Native version is too large");
            var bytes = new byte[(int)size];
            Native.lk_version(bytes, size);
            var length = Array.IndexOf(bytes, (byte)0);
            return System.Text.Encoding.UTF8.GetString(bytes, 0, length < 0 ? bytes.Length : length);
        }
    }

    public ConnectionState State => Run(() => (ConnectionState)Native.lk_room_state(_handle));
    public bool IsConnected => Run(() => Native.lk_room_is_connected(_handle) != 0);
    public string Sid => GetString(Native.lk_room_sid);
    public string Name => GetString(Native.lk_room_name);
    public string Metadata => GetString(Native.lk_room_metadata);
    public string LocalIdentity => GetString(Native.lk_local_participant_identity);
    public string LocalSid => GetString(Native.lk_local_participant_sid);

    public void Connect(string url, string token)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(url);
        ArgumentException.ThrowIfNullOrWhiteSpace(token);
        Run(() => Native.Check(Native.lk_room_connect(_handle, url, token)));
    }

    public void ConnectE2ee(string url, string token, byte[] sharedKey)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(url);
        ArgumentException.ThrowIfNullOrWhiteSpace(token);
        ArgumentNullException.ThrowIfNull(sharedKey);
        if (sharedKey.Length == 0) throw new ArgumentException("Key cannot be empty", nameof(sharedKey));
        Run(() =>
        {
            var options = new Native.E2eeOptions();
            Native.lk_e2ee_options_init(ref options);
            options.Enabled = 1;
            options.SharedKey = Marshal.AllocHGlobal(sharedKey.Length);
            options.SharedKeySize = (nuint)sharedKey.Length;
            try
            {
                Marshal.Copy(sharedKey, 0, options.SharedKey, sharedKey.Length);
                Native.Check(Native.lk_room_connect_e2ee(_handle, url, token, ref options));
            }
            finally
            {
                Marshal.Copy(new byte[sharedKey.Length], 0, options.SharedKey, sharedKey.Length);
                Marshal.FreeHGlobal(options.SharedKey);
            }
        });
    }

    public void Disconnect() => Run(() => Native.Check(Native.lk_room_disconnect(_handle)));

    public void SetLocalName(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        Run(() => Native.Check(Native.lk_local_participant_set_name(_handle, name)));
    }

    public void SetLocalMetadata(string metadata)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        Run(() => Native.Check(Native.lk_local_participant_set_metadata(_handle, metadata)));
    }

    public void PublishData(byte[] data, bool reliable = true, string topic = "")
    {
        PublishData(data, new DataPublishRequest { Reliable = reliable, Topic = topic });
    }

    public void PublishData(byte[] data, DataPublishRequest request)
    {
        ArgumentNullException.ThrowIfNull(data);
        ArgumentNullException.ThrowIfNull(request);
        Run(() =>
        {
            using var arena = new NativeArena();
            var options = new Native.DataPublishOptions();
            Native.lk_data_publish_options_init(ref options);
            options.Reliable = request.Reliable ? 1 : 0;
            options.Topic = arena.Utf8(request.Topic);
            (options.DestinationIdentities, options.DestinationIdentityCount) =
                CopyStrings(arena, request.DestinationIdentities);
            Native.Check(Native.lk_room_publish_data(_handle, data, (nuint)data.Length, ref options));
        });
    }

    /// <summary>Returns detached values; the native snapshot is released before return.</summary>
    public IReadOnlyList<Participant> GetRemoteParticipants() => Run(() =>
    {
        Native.Check(Native.lk_room_create_remote_participant_snapshot(_handle, out var snapshot));
        try
        {
            var count = Native.lk_remote_participant_list_count(snapshot);
            if (count > int.MaxValue) throw new LiveKitException(-1, "Too many participants");
            var participants = new List<Participant>((int)count);
            for (nuint i = 0; i < count; i++)
            {
                Native.Check(Native.lk_remote_participant_list_at(snapshot, i, out var value));
                participants.Add(CopyRemoteParticipant(value));
            }
            return participants;
        }
        finally { Native.lk_remote_participant_list_destroy(snapshot); }
    });

    public bool IsE2eeConfigured => Run(() => Native.lk_room_e2ee_is_configured(_handle) != 0);
    public bool IsE2eeEnabled => Run(() => Native.lk_room_e2ee_is_enabled(_handle) != 0);

    public void SetE2eeEnabled(bool enabled) =>
        Run(() => Native.Check(Native.lk_room_e2ee_set_enabled(_handle, enabled ? 1 : 0)));

    public void SetE2eeSharedKey(byte[] key, nuint index = 0)
    {
        ArgumentNullException.ThrowIfNull(key);
        Run(() => Native.Check(Native.lk_room_e2ee_set_shared_key(_handle, key, (nuint)key.Length, index)));
    }

    private string GetString(Native.StringGetter getter) =>
        Run(() => Native.ReadString(_handle, getter));

    internal T Run<T>(Func<T> operation)
    {
        lock (_sync)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            return operation();
        }
    }

    internal void Run(Action operation) => Run(() => { operation(); return 0; });

    private void Queue(Action action)
    {
        ThreadPool.QueueUserWorkItem(_ =>
        {
            if (_disposed) return;
            try { action(); }
            catch (Exception error)
            {
                try { CallbackError?.Invoke(error); }
                catch { /* Subscriber failures must not escape a callback thread. */ }
            }
        });
    }

    private void QueueFrame(Action action, bool? audio)
    {
        ThreadPool.QueueUserWorkItem(_ =>
        {
            try
            {
                if (!_disposed) action();
            }
            catch (Exception error)
            {
                try { CallbackError?.Invoke(error); }
                catch { }
            }
            finally
            {
                if (audio == true) Interlocked.Decrement(ref _queuedAudioFrames);
                else if (audio == false) Interlocked.Decrement(ref _queuedVideoFrames);
                else Interlocked.Decrement(ref _queuedDataTrackFrames);
            }
        });
    }

    private void OnConnected(IntPtr userData, IntPtr room) => Queue(() => Connected?.Invoke());
    private void OnDisconnected(IntPtr userData, IntPtr room) => Queue(() => Disconnected?.Invoke());
    private void OnReconnecting(IntPtr userData, IntPtr room) => Queue(() => Reconnecting?.Invoke());
    private void OnReconnected(IntPtr userData, IntPtr room) => Queue(() => Reconnected?.Invoke());
    private void OnMetadataChanged(IntPtr userData, IntPtr room, IntPtr metadata)
    {
        var copy = Marshal.PtrToStringUTF8(metadata) ?? string.Empty;
        Queue(() => MetadataChanged?.Invoke(copy));
    }

    private static Participant CopyParticipant(IntPtr pointer)
    {
        var value = Marshal.PtrToStructure<Native.ParticipantInfo>(pointer);
        return new Participant(
            Marshal.PtrToStringUTF8(value.Sid) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Identity) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Name) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Metadata) ?? string.Empty,
            value.AudioLevel, value.ConnectionQuality, value.IsSpeaking != 0, value.IsLocal != 0);
    }

    private void OnParticipantConnected(IntPtr userData, IntPtr room, IntPtr participant)
    {
        if (participant != IntPtr.Zero)
        {
            var copy = CopyParticipant(participant);
            Queue(() => ParticipantConnected?.Invoke(copy));
        }
    }

    private void OnParticipantDisconnected(IntPtr userData, IntPtr room, IntPtr participant)
    {
        if (participant != IntPtr.Zero)
        {
            var copy = CopyParticipant(participant);
            Queue(() => ParticipantDisconnected?.Invoke(copy));
        }
    }

    private void OnDataReceived(IntPtr userData, IntPtr room, IntPtr data)
    {
        if (data == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.DataReceived>(data);
        if (value.DataSize > int.MaxValue) return;
        var bytes = new byte[(int)value.DataSize];
        if (bytes.Length != 0) Marshal.Copy(value.Data, bytes, 0, bytes.Length);
        var packet = new DataPacket(bytes, Marshal.PtrToStringUTF8(value.Topic) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty, value.Reliable != 0);
        Queue(() => DataReceived?.Invoke(packet));
    }

    private void OnTextReceived(IntPtr userData, IntPtr room, IntPtr text)
    {
        if (text == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.TextReceived>(text);
        var message = new TextMessage(
            Marshal.PtrToStringUTF8(value.StreamId) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Text) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Topic) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.ReplyToStreamId) ?? string.Empty,
            value.Timestamp)
        {
            AttachedStreamIds = ReadStringArray(value.AttachedStreamIds, value.AttachedStreamIdCount),
            Attributes = ReadAttributes(value.Attributes, value.AttributeCount),
        };
        Queue(() => TextReceived?.Invoke(message));
    }

    private FileMessage? CopyFile(IntPtr file)
    {
        if (file == IntPtr.Zero) return null;
        var value = Marshal.PtrToStructure<Native.FileReceived>(file);
        if (value.DataSize > int.MaxValue) return null;
        var data = new byte[(int)value.DataSize];
        if (data.Length != 0) Marshal.Copy(value.Data, data, 0, data.Length);
        return new FileMessage(data,
            Marshal.PtrToStringUTF8(value.StreamId) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Name) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.MimeType) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Topic) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty,
            value.Timestamp)
        {
            Attributes = ReadAttributes(value.Attributes, value.AttributeCount),
        };
    }

    private static IReadOnlyList<string> ReadStringArray(IntPtr pointer, nuint count)
    {
        if (pointer == IntPtr.Zero || count == 0 || count > int.MaxValue) return Array.Empty<string>();
        var values = new string[(int)count];
        for (var i = 0; i < values.Length; i++)
            values[i] = Marshal.PtrToStringUTF8(Marshal.ReadIntPtr(pointer, i * IntPtr.Size)) ?? string.Empty;
        return values;
    }

    private static IReadOnlyDictionary<string, string> ReadAttributes(IntPtr pointer, nuint count)
    {
        var values = new Dictionary<string, string>();
        if (pointer == IntPtr.Zero || count == 0 || count > int.MaxValue) return values;
        var size = Marshal.SizeOf<Native.Attribute>();
        for (var i = 0; i < (int)count; i++)
        {
            var entry = Marshal.PtrToStructure<Native.Attribute>(IntPtr.Add(pointer, i * size));
            values[Marshal.PtrToStringUTF8(entry.Key) ?? string.Empty] =
                Marshal.PtrToStringUTF8(entry.Value) ?? string.Empty;
        }
        return values;
    }

    private void OnFileReceived(IntPtr userData, IntPtr room, IntPtr file)
    {
        var copy = CopyFile(file);
        if (copy is not null) Queue(() => FileReceived?.Invoke(copy));
    }

    private void OnByteReceived(IntPtr userData, IntPtr room, IntPtr file)
    {
        var copy = CopyFile(file);
        if (copy is not null) Queue(() => BytesReceived?.Invoke(copy));
    }

    private void OnChatReceived(IntPtr userData, IntPtr room, IntPtr chat)
    {
        if (chat == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.ChatMessage>(chat);
        var copy = new ChatMessage(
            Marshal.PtrToStringUTF8(value.Id) ?? string.Empty,
            value.Timestamp,
            value.HasEditTimestamp != 0 ? value.EditTimestamp : null,
            Marshal.PtrToStringUTF8(value.Message) ?? string.Empty,
            value.Deleted != 0, value.Generated != 0,
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty);
        Queue(() => ChatReceived?.Invoke(copy));
    }

    private void OnAudioFrame(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, IntPtr frame)
    {
        if (AudioFrameReceived is null || track == IntPtr.Zero || participant == IntPtr.Zero ||
            frame == IntPtr.Zero) return;
        if (Interlocked.Increment(ref _queuedAudioFrames) > 32)
        {
            Interlocked.Decrement(ref _queuedAudioFrames);
            return;
        }
        try
        {
            var view = Marshal.PtrToStructure<Native.AudioFrameView>(frame);
            if (view.SampleCount > int.MaxValue) throw new InvalidOperationException("Audio frame too large");
            var samples = new short[(int)view.SampleCount];
            if (samples.Length != 0) Marshal.Copy(view.Data, samples, 0, samples.Length);
            var value = new AudioFrameEvent(
                Marshal.PtrToStringUTF8(Marshal.ReadIntPtr(track)) ?? string.Empty,
                CopyParticipant(participant),
                new AudioFrame(samples, view.SampleRate, view.NumChannels, view.SamplesPerChannel));
            QueueFrame(() => AudioFrameReceived?.Invoke(value), true);
        }
        catch { Interlocked.Decrement(ref _queuedAudioFrames); }
    }

    private void OnVideoFrame(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, IntPtr frame)
    {
        if (VideoFrameReceived is null || track == IntPtr.Zero || participant == IntPtr.Zero ||
            frame == IntPtr.Zero) return;
        if (Interlocked.Increment(ref _queuedVideoFrames) > 8)
        {
            Interlocked.Decrement(ref _queuedVideoFrames);
            return;
        }
        try
        {
            var view = Marshal.PtrToStructure<Native.VideoFrameView>(frame);
            if (view.DataSize > int.MaxValue) throw new InvalidOperationException("Video frame too large");
            var data = new byte[(int)view.DataSize];
            if (data.Length != 0) Marshal.Copy(view.Data, data, 0, data.Length);
            var value = new VideoFrameEvent(
                Marshal.PtrToStringUTF8(Marshal.ReadIntPtr(track)) ?? string.Empty,
                CopyParticipant(participant),
                new VideoFrame(data, view.Width, view.Height, view.TimestampUs));
            QueueFrame(() => VideoFrameReceived?.Invoke(value), false);
        }
        catch { Interlocked.Decrement(ref _queuedVideoFrames); }
    }

    public void Dispose()
    {
        lock (_sync)
        {
            if (_disposed) return;
            _disposed = true;
            _handle.Dispose();
            CompletePendingRpcOnDispose();
            _textStreamHandlers.Clear();
            _byteStreamHandlers.Clear();
            _retiredStreamHandlers.Clear();
            foreach (var track in _tracks) track.DisposeNative();
            _tracks.Clear();
            foreach (var track in _dataTracks) track.DisposeNative();
            _dataTracks.Clear();
            foreach (var registration in _rpcHandlers.Values) registration.Dispose();
            _rpcHandlers.Clear();
            foreach (var registration in _retiredRpcHandlers) registration.Dispose();
            _retiredRpcHandlers.Clear();
            foreach (var root in _connectionCallbackRoots)
                if (root is IDisposable disposable) disposable.Dispose();
            _connectionCallbackRoots.Clear();
            _tokenSource = null;
            _reconnectPolicy = null;
        }
    }
}
