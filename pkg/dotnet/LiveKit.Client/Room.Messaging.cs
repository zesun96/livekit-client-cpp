using System;
using System.Runtime.InteropServices;
using System.Text;

namespace LiveKit.Client;

public sealed record ChatMessageReceipt(string Id, long Timestamp);
public sealed record RpcResponse(bool IsSuccess, string Payload, uint ErrorCode,
    string ErrorMessage, string ErrorData);
public enum VideoQuality { Low = 0, Medium = 1, High = 2, Off = 3 }
public sealed record ParticipantTrackPermission(string ParticipantSid, string ParticipantIdentity,
    bool AllowAll, IReadOnlyList<string> AllowedTrackSids);
public sealed record TextSendRequest
{
    public string Topic { get; init; } = "";
    public IReadOnlyList<string> DestinationIdentities { get; init; } = Array.Empty<string>();
    public IReadOnlyDictionary<string, string> Attributes { get; init; } = new Dictionary<string, string>();
    public string ReplyToStreamId { get; init; } = "";
    public IReadOnlyList<string> AttachedStreamIds { get; init; } = Array.Empty<string>();
    public nuint ChunkSize { get; init; }
    public bool Compress { get; init; }
}
public sealed record ByteSendRequest
{
    public string Topic { get; init; } = "";
    public string MimeType { get; init; } = "application/octet-stream";
    public string Name { get; init; } = "";
    public IReadOnlyList<string> DestinationIdentities { get; init; } = Array.Empty<string>();
    public IReadOnlyDictionary<string, string> Attributes { get; init; } = new Dictionary<string, string>();
    public nuint ChunkSize { get; init; }
    public bool Compress { get; init; }
}
public sealed record FileSendRequest
{
    public string Topic { get; init; } = "";
    public string MimeType { get; init; } = "application/octet-stream";
    public IReadOnlyList<string> DestinationIdentities { get; init; } = Array.Empty<string>();
    public IReadOnlyDictionary<string, string> Attributes { get; init; } = new Dictionary<string, string>();
    public nuint ChunkSize { get; init; }
    public bool Compress { get; init; }
}

public sealed partial class Room
{
    public void SendText(string text, string topic = "")
    {
        SendText(text, new TextSendRequest { Topic = topic });
    }

    public void SendText(string text, TextSendRequest request)
    {
        ArgumentNullException.ThrowIfNull(text);
        ArgumentNullException.ThrowIfNull(request);
        Run(() =>
        {
            using var arena = new NativeArena();
            var options = new Native.TextSendOptions
            {
                StructSize = (nuint)Marshal.SizeOf<Native.TextSendOptions>(),
                Topic = arena.Utf8(request.Topic),
                ReplyToStreamId = arena.Utf8(request.ReplyToStreamId),
                ChunkSize = request.ChunkSize == 0 ? 15000 : request.ChunkSize,
                Compress = request.Compress ? 1 : 0,
            };
            (options.DestinationIdentities, options.DestinationIdentityCount) =
                CopyStrings(arena, request.DestinationIdentities);
            (options.AttachedStreamIds, options.AttachedStreamIdCount) =
                CopyStrings(arena, request.AttachedStreamIds);
            (options.Attributes, options.AttributeCount) = CopyAttributes(arena, request.Attributes);
            Native.Check(Native.lk_room_send_text(_handle, text, ref options));
        });
    }

    public void SendBytes(byte[] data, string topic = "", string mimeType = "application/octet-stream",
        string name = "")
    {
        SendBytes(data, new ByteSendRequest { Topic = topic, MimeType = mimeType, Name = name });
    }

    public void SendBytes(byte[] data, ByteSendRequest request)
    {
        ArgumentNullException.ThrowIfNull(data);
        ArgumentNullException.ThrowIfNull(request);
        Run(() =>
        {
            using var arena = new NativeArena();
            var options = new Native.ByteSendOptions
            {
                StructSize = (nuint)Marshal.SizeOf<Native.ByteSendOptions>(),
                Topic = arena.Utf8(request.Topic),
                MimeType = arena.Utf8(request.MimeType),
                Name = arena.Utf8(request.Name),
                ChunkSize = request.ChunkSize == 0 ? 15000 : request.ChunkSize,
                Compress = request.Compress ? 1 : 0,
            };
            (options.DestinationIdentities, options.DestinationIdentityCount) =
                CopyStrings(arena, request.DestinationIdentities);
            (options.Attributes, options.AttributeCount) = CopyAttributes(arena, request.Attributes);
            Native.Check(Native.lk_room_send_bytes(_handle, data, (nuint)data.Length, ref options));
        });
    }

    public void SendFile(string path, string topic = "", string mimeType = "application/octet-stream")
    {
        SendFile(path, new FileSendRequest { Topic = topic, MimeType = mimeType });
    }

    public void SendFile(string path, FileSendRequest request)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(request);
        Run(() =>
        {
            using var arena = new NativeArena();
            var options = new Native.FileSendOptions
            {
                StructSize = (nuint)Marshal.SizeOf<Native.FileSendOptions>(),
                Topic = arena.Utf8(request.Topic),
                MimeType = arena.Utf8(request.MimeType),
                ChunkSize = request.ChunkSize == 0 ? 15000 : request.ChunkSize,
                Compress = request.Compress ? 1 : 0,
            };
            (options.DestinationIdentities, options.DestinationIdentityCount) =
                CopyStrings(arena, request.DestinationIdentities);
            (options.Attributes, options.AttributeCount) = CopyAttributes(arena, request.Attributes);
            Native.Check(Native.lk_room_send_file(_handle, path, ref options));
        });
    }

    public ChatMessageReceipt SendChatMessage(string message)
    {
        ArgumentNullException.ThrowIfNull(message);
        return Run(() =>
        {
            var id = new byte[37]; // LK_CHAT_MESSAGE_ID_BUFFER_SIZE
            Native.Check(Native.lk_room_send_chat_message(_handle, message, id, (nuint)id.Length,
                out var timestamp));
            return new ChatMessageReceipt(Encoding.UTF8.GetString(id, 0, Array.IndexOf(id, (byte)0)),
                timestamp);
        });
    }

    public void EditChatMessage(ChatMessageReceipt original, string message)
    {
        ArgumentNullException.ThrowIfNull(original);
        ArgumentNullException.ThrowIfNull(message);
        Run(() => Native.Check(Native.lk_room_edit_chat_message(_handle, original.Id,
            original.Timestamp, message)));
    }

    public void PublishDtmf(uint code, string digit)
    {
        ArgumentNullException.ThrowIfNull(digit);
        Run(() => Native.Check(Native.lk_room_publish_dtmf(_handle, code, digit)));
    }

    public RpcResponse PerformRpc(string destinationIdentity, string method, string payload,
        uint responseTimeoutMs = 10_000)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(destinationIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(method);
        ArgumentNullException.ThrowIfNull(payload);
        return Run(() =>
        {
            var options = new Native.RpcPerformOptions();
            Native.lk_rpc_perform_options_init(ref options);
            options.DestinationIdentity = Marshal.StringToCoTaskMemUTF8(destinationIdentity);
            options.Method = Marshal.StringToCoTaskMemUTF8(method);
            options.Payload = Marshal.StringToCoTaskMemUTF8(payload);
            options.ResponseTimeoutMs = responseTimeoutMs;
            try
            {
                Native.Check(Native.lk_room_perform_rpc(_handle, ref options, out var result));
                try
                {
                    return new RpcResponse(Native.lk_rpc_result_ok(result) != 0,
                        Native.ReadString(result, Native.lk_rpc_result_payload),
                        Native.lk_rpc_result_error_code(result),
                        Native.ReadString(result, Native.lk_rpc_result_error_message),
                        Native.ReadString(result, Native.lk_rpc_result_error_data));
                }
                finally { Native.lk_rpc_result_destroy(result); }
            }
            finally
            {
                Marshal.FreeCoTaskMem(options.DestinationIdentity);
                Marshal.FreeCoTaskMem(options.Method);
                Marshal.FreeCoTaskMem(options.Payload);
            }
        });
    }

    public void SetRemoteTrackSubscribed(string participantSid, string trackSid, bool subscribed)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantSid);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        Run(() => Native.Check(Native.lk_room_set_remote_track_subscribed(_handle,
            participantSid, trackSid, subscribed ? 1 : 0)));
    }

    public void UpdateRemoteTrackSettings(string participantSid, string trackSid,
        bool enabled = true, VideoQuality? quality = null, uint width = 0,
        uint height = 0, uint fps = 0, uint priority = 0)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantSid);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        Run(() =>
        {
            var settings = new Native.RemoteTrackSettings();
            Native.lk_remote_track_settings_init(ref settings);
            settings.Enabled = enabled ? 1 : 0;
            settings.HasVideoQuality = quality.HasValue ? 1 : 0;
            settings.VideoQuality = (int)(quality ?? VideoQuality.High);
            settings.VideoWidth = width;
            settings.VideoHeight = height;
            settings.VideoFps = fps;
            settings.Priority = priority;
            Native.Check(Native.lk_room_update_remote_track_settings(_handle, participantSid,
                trackSid, ref settings));
        });
    }

    public void SetTrackSubscriptionPermissions(bool allowAllParticipants) =>
        Run(() => Native.Check(Native.lk_room_set_track_subscription_permissions(_handle,
            allowAllParticipants ? 1 : 0, IntPtr.Zero, 0)));

    public void SetTrackSubscriptionPermissions(bool allowAllParticipants,
        IReadOnlyList<ParticipantTrackPermission> permissions)
    {
        ArgumentNullException.ThrowIfNull(permissions);
        Run(() =>
        {
            using var arena = new NativeArena();
            var values = new Native.ParticipantTrackPermission[permissions.Count];
            for (var i = 0; i < values.Length; i++)
            {
                var item = permissions[i];
                Native.lk_participant_track_permission_init(ref values[i]);
                values[i].ParticipantSid = arena.Utf8(item.ParticipantSid);
                values[i].ParticipantIdentity = arena.Utf8(item.ParticipantIdentity);
                values[i].AllowAll = item.AllowAll ? 1 : 0;
                if (item.AllowedTrackSids.Count != 0)
                {
                    var sids = new IntPtr[item.AllowedTrackSids.Count];
                    for (var j = 0; j < sids.Length; j++)
                        sids[j] = arena.Utf8(item.AllowedTrackSids[j]);
                    values[i].AllowedTrackSids = arena.PointerArray(sids);
                    values[i].AllowedTrackSidCount = (nuint)sids.Length;
                }
            }
            Native.Check(Native.lk_room_set_track_subscription_permissions(_handle,
                allowAllParticipants ? 1 : 0,
                values.Length == 0 ? IntPtr.Zero : arena.StructArray(values),
                (nuint)values.Length));
        });
    }
}
