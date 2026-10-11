using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace LiveKit.Client;

public sealed record TrackInfo(string Sid, string Name, string MimeType, int Kind, int Source,
    uint Width, uint Height, bool IsMuted, bool IsSimulcasted, bool SubscriptionAllowed);
public sealed record TrackPublicationEvent(TrackInfo Track, Participant Participant);
public sealed record SipDtmfMessage(uint Code, string Digit, string ParticipantIdentity);
public sealed record RoomInfo(string Sid, string Name, string Metadata, bool IsRecording);
public sealed record RoomSidChange(string PreviousSid, string Sid);
public sealed record ParticipantMetadataChange(string PreviousMetadata, Participant Participant);
public sealed record ParticipantNameChange(string Name, Participant Participant);
public sealed record ParticipantAttributesChange(IReadOnlyDictionary<string, string> Changes,
    Participant Participant);
public sealed record DataTrackPublicationEvent(RemoteDataTrack Track, Participant Participant);

public sealed partial class Room
{
    public event Action<TrackPublicationEvent>? TrackPublished;
    public event Action<TrackPublicationEvent>? TrackUnpublished;
    public event Action<TrackPublicationEvent>? TrackMuted;
    public event Action<TrackPublicationEvent>? TrackUnmuted;
    public event Action<TrackPublicationEvent>? TrackSubscribed;
    public event Action<TrackPublicationEvent>? TrackUnsubscribed;
    public event Action<TrackPublicationEvent>? LocalTrackPublished;
    public event Action<TrackPublicationEvent>? LocalTrackUnpublished;
    public event Action<ConnectionState>? ConnectionStateChanged;
    public event Action<int>? DisconnectedWithReason;
    public event Action<SipDtmfMessage>? SipDtmfReceived;
    public event Action<bool>? RecordingStatusChanged;
    public event Action? TokenRefreshed;
    public event Action<RoomInfo>? RoomUpdated;
    public event Action<RoomInfo>? RoomMoved;
    public event Action<RoomSidChange>? RoomSidChanged;
    public event Action? RoomEnded;
    public event Action<ParticipantMetadataChange>? ParticipantMetadataChanged;
    public event Action<ParticipantNameChange>? ParticipantNameChanged;
    public event Action<ParticipantAttributesChange>? ParticipantAttributesChanged;
    public event Action<DataTrackPublicationEvent>? DataTrackPublished;
    public event Action<DataTrackPublicationEvent>? DataTrackUnpublished;
    public event Action<DataTrackPublicationEvent>? LocalDataTrackPublished;
    public event Action<DataTrackPublicationEvent>? LocalDataTrackUnpublished;
    public event Action<VideoFrameEvent>? VideoFrameWithMetadataReceived;

    private static TrackPublicationEvent? CopyTrackEvent(IntPtr track, IntPtr participant)
    {
        if (track == IntPtr.Zero || participant == IntPtr.Zero) return null;
        var value = Marshal.PtrToStructure<Native.TrackPublicationInfo>(track);
        return new TrackPublicationEvent(new TrackInfo(
            Marshal.PtrToStringUTF8(value.Sid) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Name) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.MimeType) ?? string.Empty,
            value.Kind, value.Source, value.Width, value.Height, value.IsMuted != 0,
            value.IsSimulcasted != 0, value.SubscriptionAllowed != 0),
            CopyParticipant(participant));
    }

    private void PublishTrackEvent(IntPtr track, IntPtr participant,
        Action<TrackPublicationEvent>? handler)
    {
        if (handler is null) return;
        var copy = CopyTrackEvent(track, participant);
        if (copy is not null) Queue(() => handler(copy));
    }

    private void OnTrackPublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, TrackPublished);
    private void OnTrackUnpublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, TrackUnpublished);
    private void OnTrackMuted(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, TrackMuted);
    private void OnTrackUnmuted(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, TrackUnmuted);
    private void OnTrackSubscribed(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, TrackSubscribed);
    private void OnTrackUnsubscribed(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, TrackUnsubscribed);
    private void OnLocalTrackPublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, LocalTrackPublished);
    private void OnLocalTrackUnpublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, LocalTrackUnpublished);

    private void OnConnectionStateChanged(IntPtr u, IntPtr r, int state) =>
        Queue(() => ConnectionStateChanged?.Invoke((ConnectionState)state));
    private void OnDisconnectedWithReason(IntPtr u, IntPtr r, int reason) =>
        Queue(() => DisconnectedWithReason?.Invoke(reason));
    private void OnRecordingStatusChanged(IntPtr u, IntPtr r, int recording) =>
        Queue(() => RecordingStatusChanged?.Invoke(recording != 0));
    private void OnTokenRefreshed(IntPtr u, IntPtr r) => Queue(() => TokenRefreshed?.Invoke());
    private void OnRoomEos(IntPtr u, IntPtr r) => Queue(() => RoomEnded?.Invoke());

    private void OnSipDtmfReceived(IntPtr u, IntPtr r, IntPtr pointer)
    {
        if (pointer == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.SipDtmf>(pointer);
        var copy = new SipDtmfMessage(value.Code,
            Marshal.PtrToStringUTF8(value.Digit) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty);
        Queue(() => SipDtmfReceived?.Invoke(copy));
    }

    private static RoomInfo? CopyRoomInfo(IntPtr pointer)
    {
        if (pointer == IntPtr.Zero) return null;
        var value = Marshal.PtrToStructure<Native.RoomSnapshot>(pointer);
        return new RoomInfo(Marshal.PtrToStringUTF8(value.Sid) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Name) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Metadata) ?? string.Empty, value.IsRecording != 0);
    }

    private void OnRoomUpdated(IntPtr u, IntPtr r, IntPtr pointer)
    {
        var copy = CopyRoomInfo(pointer);
        if (copy is not null) Queue(() => RoomUpdated?.Invoke(copy));
    }

    private void OnRoomMoved(IntPtr u, IntPtr r, IntPtr pointer)
    {
        var copy = CopyRoomInfo(pointer);
        if (copy is not null) Queue(() => RoomMoved?.Invoke(copy));
    }

    private void OnRoomSidChanged(IntPtr u, IntPtr r, IntPtr previousSid, IntPtr sid)
    {
        var copy = new RoomSidChange(Marshal.PtrToStringUTF8(previousSid) ?? string.Empty,
            Marshal.PtrToStringUTF8(sid) ?? string.Empty);
        Queue(() => RoomSidChanged?.Invoke(copy));
    }

    private void OnParticipantMetadataChanged(IntPtr u, IntPtr r, IntPtr previous,
        IntPtr participant)
    {
        if (participant == IntPtr.Zero) return;
        var copy = new ParticipantMetadataChange(Marshal.PtrToStringUTF8(previous) ?? string.Empty,
            CopyParticipant(participant));
        Queue(() => ParticipantMetadataChanged?.Invoke(copy));
    }

    private void OnParticipantNameChanged(IntPtr u, IntPtr r, IntPtr name, IntPtr participant)
    {
        if (participant == IntPtr.Zero) return;
        var copy = new ParticipantNameChange(Marshal.PtrToStringUTF8(name) ?? string.Empty,
            CopyParticipant(participant));
        Queue(() => ParticipantNameChanged?.Invoke(copy));
    }

    private void OnParticipantAttributesChanged(IntPtr u, IntPtr r, IntPtr changes,
        nuint count, IntPtr participant)
    {
        if (participant == IntPtr.Zero || count > int.MaxValue ||
            (count != 0 && changes == IntPtr.Zero)) return;
        var values = new Dictionary<string, string>((int)count);
        var size = Marshal.SizeOf<Native.Attribute>();
        for (var i = 0; i < (int)count; i++)
        {
            var entry = Marshal.PtrToStructure<Native.Attribute>(IntPtr.Add(changes, i * size));
            values[Marshal.PtrToStringUTF8(entry.Key) ?? string.Empty] =
                Marshal.PtrToStringUTF8(entry.Value) ?? string.Empty;
        }
        var copy = new ParticipantAttributesChange(values, CopyParticipant(participant));
        Queue(() => ParticipantAttributesChanged?.Invoke(copy));
    }

    private static DataTrackPublicationEvent? CopyDataTrackEvent(IntPtr track, IntPtr participant)
    {
        if (track == IntPtr.Zero || participant == IntPtr.Zero) return null;
        var value = Marshal.PtrToStructure<Native.DataTrackInfo>(track);
        var person = CopyParticipant(participant);
        return new DataTrackPublicationEvent(new RemoteDataTrack(person.Identity,
            Marshal.PtrToStringUTF8(value.Sid) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.Name) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.SchemaName) ?? string.Empty,
            value.HasFrameEncoding != 0 ? (DataTrackFrameEncoding)value.FrameEncoding :
                DataTrackFrameEncoding.Unspecified,
            Marshal.PtrToStringUTF8(value.CustomFrameEncoding) ?? string.Empty,
            value.HasSchema != 0 ? (DataTrackSchemaEncoding)value.SchemaEncoding :
                DataTrackSchemaEncoding.Unspecified,
            Marshal.PtrToStringUTF8(value.CustomSchemaEncoding) ?? string.Empty), person);
    }

    private void PublishDataTrackEvent(IntPtr track, IntPtr participant,
        Action<DataTrackPublicationEvent>? handler)
    {
        if (handler is null) return;
        var copy = CopyDataTrackEvent(track, participant);
        if (copy is not null) Queue(() => handler(copy));
    }

    private void OnDataTrackPublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishDataTrackEvent(t, p, DataTrackPublished);
    private void OnDataTrackUnpublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishDataTrackEvent(t, p, DataTrackUnpublished);
    private void OnLocalDataTrackPublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishDataTrackEvent(t, p, LocalDataTrackPublished);
    private void OnLocalDataTrackUnpublished(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishDataTrackEvent(t, p, LocalDataTrackUnpublished);

    private void OnVideoFrameWithMetadata(IntPtr u, IntPtr r, IntPtr track,
        IntPtr participant, IntPtr frame, IntPtr metadata)
    {
        if (VideoFrameWithMetadataReceived is null || track == IntPtr.Zero ||
            participant == IntPtr.Zero || frame == IntPtr.Zero || metadata == IntPtr.Zero) return;
        if (Interlocked.Increment(ref _queuedVideoFrames) > 8)
        {
            Interlocked.Decrement(ref _queuedVideoFrames);
            return;
        }
        try
        {
            var view = Marshal.PtrToStructure<Native.VideoFrameView>(frame);
            var info = Marshal.PtrToStructure<Native.VideoFrameMetadata>(metadata);
            if (view.DataSize > int.MaxValue || info.UserDataSize > int.MaxValue)
                throw new InvalidOperationException("Video frame or metadata too large");
            var data = new byte[(int)view.DataSize];
            if (data.Length != 0) Marshal.Copy(view.Data, data, 0, data.Length);
            byte[]? userData = null;
            if (info.HasUserData != 0)
            {
                userData = new byte[(int)info.UserDataSize];
                if (userData.Length != 0) Marshal.Copy(info.UserData, userData, 0, userData.Length);
            }
            var copy = new VideoFrameEvent(
                Marshal.PtrToStringUTF8(Marshal.ReadIntPtr(track)) ?? string.Empty,
                CopyParticipant(participant),
                new VideoFrame(data, view.Width, view.Height, view.TimestampUs,
                    new VideoFrameMetadata(info.HasUserTimestampUs != 0 ? info.UserTimestampUs : null,
                        info.HasFrameId != 0 ? info.FrameId : null, userData)));
            QueueFrame(() => VideoFrameWithMetadataReceived?.Invoke(copy), false);
        }
        catch { Interlocked.Decrement(ref _queuedVideoFrames); }
    }
}
