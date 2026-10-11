using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed record ConnectionQualityChange(int Quality, Participant Participant);
public sealed record TrackStatusChange(TrackInfo Track, Participant Participant, int Value);
public sealed record DataChannelBufferChange(bool Reliable, ulong BufferedAmount,
    ulong HighWaterMark, ulong LowWaterMark, bool Backpressured);
public sealed record ParticipantPermissions(bool CanSubscribe, bool CanPublish,
    bool CanPublishData, IReadOnlyList<int> CanPublishSources, bool Hidden,
    bool Recorder, bool CanUpdateMetadata, bool Agent, bool CanSubscribeMetrics,
    bool CanManageAgentSession);
public sealed record ParticipantPermissionsChange(Participant Participant,
    ParticipantPermissions Previous, ParticipantPermissions Current);

public sealed partial class Room
{
    public event Action<ConnectionQualityChange>? ConnectionQualityChanged;
    public event Action<IReadOnlyList<Participant>>? ActiveSpeakersChanged;
    public event Action<IReadOnlyList<Participant>>? ParticipantsUpdated;
    public event Action<TrackStatusChange>? TrackSubscriptionPermissionChanged;
    public event Action<TrackStatusChange>? TrackSubscriptionFailed;
    public event Action<TrackStatusChange>? TrackStreamStateChanged;
    public event Action<TrackStatusChange>? TrackSubscriptionStatusChanged;
    public event Action<DataChannelBufferChange>? DataChannelBufferStatusChanged;
    public event Action<ParticipantPermissionsChange>? ParticipantPermissionsChanged;
    public event Action<TrackPublicationEvent>? LocalTrackSubscribed;

    private void OnConnectionQualityChanged(IntPtr u, IntPtr r, int quality, IntPtr participant)
    {
        if (participant == IntPtr.Zero) return;
        var copy = new ConnectionQualityChange(quality, CopyParticipant(participant));
        Queue(() => ConnectionQualityChanged?.Invoke(copy));
    }

    private static IReadOnlyList<Participant> CopyParticipantList(IntPtr pointer, nuint count)
    {
        if (count == 0 || pointer == IntPtr.Zero || count > int.MaxValue)
            return Array.Empty<Participant>();
        var values = new Participant[(int)count];
        var size = Marshal.SizeOf<Native.ParticipantInfo>();
        for (var i = 0; i < values.Length; i++)
            values[i] = CopyParticipant(IntPtr.Add(pointer, checked(i * size)));
        return values;
    }

    private void OnActiveSpeakersChanged(IntPtr u, IntPtr r, IntPtr participants, nuint count)
    {
        var copy = CopyParticipantList(participants, count);
        Queue(() => ActiveSpeakersChanged?.Invoke(copy));
    }

    private void OnParticipantsUpdated(IntPtr u, IntPtr r, IntPtr participants, nuint count)
    {
        var copy = CopyParticipantList(participants, count);
        Queue(() => ParticipantsUpdated?.Invoke(copy));
    }

    private void PublishTrackStatus(IntPtr track, IntPtr participant, int value,
        Action<TrackStatusChange>? handler)
    {
        if (handler is null) return;
        var copy = CopyTrackEvent(track, participant);
        if (copy is null) return;
        var status = new TrackStatusChange(copy.Track, copy.Participant, value);
        Queue(() => handler(status));
    }

    private void OnTrackSubscriptionPermissionChanged(IntPtr u, IntPtr r, IntPtr t,
        IntPtr p, int allowed) => PublishTrackStatus(t, p, allowed,
            TrackSubscriptionPermissionChanged);
    private void OnTrackSubscriptionFailed(IntPtr u, IntPtr r, IntPtr t,
        IntPtr p, int error) => PublishTrackStatus(t, p, error, TrackSubscriptionFailed);
    private void OnTrackStreamStateChanged(IntPtr u, IntPtr r, IntPtr t,
        IntPtr p, int state) => PublishTrackStatus(t, p, state, TrackStreamStateChanged);
    private void OnTrackSubscriptionStatusChanged(IntPtr u, IntPtr r, IntPtr t,
        IntPtr p, int status) => PublishTrackStatus(t, p, status, TrackSubscriptionStatusChanged);

    private void OnDataChannelBufferStatusChanged(IntPtr u, IntPtr r, IntPtr pointer)
    {
        if (pointer == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.DataChannelBufferStatus>(pointer);
        var copy = new DataChannelBufferChange(value.Reliable != 0, value.BufferedAmount,
            value.HighWaterMark, value.LowWaterMark, value.Backpressured != 0);
        Queue(() => DataChannelBufferStatusChanged?.Invoke(copy));
    }

    private static ParticipantPermissions CopyPermissions(IntPtr pointer)
    {
        var value = Marshal.PtrToStructure<Native.ParticipantPermissions>(pointer);
        var sources = new int[value.CanPublishSourceCount <= int.MaxValue &&
            value.CanPublishSources != IntPtr.Zero ? (int)value.CanPublishSourceCount : 0];
        for (var i = 0; i < sources.Length; i++)
            sources[i] = Marshal.ReadInt32(value.CanPublishSources, checked(i * sizeof(int)));
        return new ParticipantPermissions(value.CanSubscribe != 0, value.CanPublish != 0,
            value.CanPublishData != 0, sources, value.Hidden != 0, value.Recorder != 0,
            value.CanUpdateMetadata != 0, value.Agent != 0,
            value.CanSubscribeMetrics != 0, value.CanManageAgentSession != 0);
    }

    private void OnParticipantPermissionsChanged(IntPtr u, IntPtr r, IntPtr previous,
        IntPtr current, IntPtr participant)
    {
        if (previous == IntPtr.Zero || current == IntPtr.Zero || participant == IntPtr.Zero) return;
        var copy = new ParticipantPermissionsChange(CopyParticipant(participant),
            CopyPermissions(previous), CopyPermissions(current));
        Queue(() => ParticipantPermissionsChanged?.Invoke(copy));
    }

    private void OnLocalTrackSubscribed(IntPtr u, IntPtr r, IntPtr t, IntPtr p) =>
        PublishTrackEvent(t, p, LocalTrackSubscribed);
}
