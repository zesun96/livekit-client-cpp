using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace LiveKit.Client;

public sealed record RtcTrackStats(string Id, string Kind, string Rid, string CodecMimeType,
    int Direction, double TimestampMs, ulong Bytes, ulong Packets, long PacketsLost,
    double? BitrateBps, double? RoundTripTimeSeconds, double? JitterSeconds,
    double? AudioLevel, uint FrameWidth, uint FrameHeight, double FramesPerSecond,
    ulong Frames, ulong FramesDropped)
{
    public string CodecImplementation { get; init; } = "";
    public string QualityLimitationReason { get; init; } = "";
    public ulong ConcealedSamples { get; init; }
    public ulong FirCount { get; init; }
    public ulong PliCount { get; init; }
    public ulong NackCount { get; init; }
    public ulong QpSum { get; init; }
}

public sealed partial class LocalTrack
{
    public IReadOnlyList<RtcTrackStats> GetRtcStats() => _room.Run(() =>
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Native.Check(Native.lk_local_track_create_rtc_stats_snapshot(Handle, out var snapshot));
        try { return RtcStatsReader.Read(snapshot); }
        finally { Native.lk_rtc_stats_snapshot_destroy(snapshot); }
    });
}

public sealed partial class Room
{
    public IReadOnlyList<RtcTrackStats> GetRemoteRtcStats(string participantIdentity,
        string trackSid)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(participantIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(trackSid);
        return Run(() =>
        {
            Native.Check(Native.lk_room_create_remote_participant_snapshot(_handle,
                out var participants));
            try
            {
                for (nuint i = 0; i < Native.lk_remote_participant_list_count(participants); i++)
                {
                    Native.Check(Native.lk_remote_participant_list_at(participants, i,
                        out var participant));
                    if (Native.ReadString(participant,
                        Native.lk_remote_participant_snapshot_identity) != participantIdentity)
                        continue;
                    for (nuint j = 0; j <
                        Native.lk_remote_participant_snapshot_publication_count(participant); j++)
                    {
                        Native.Check(Native.lk_remote_participant_snapshot_publication_at(
                            participant, j, out var publication));
                        if (Native.ReadString(publication,
                            Native.lk_remote_track_publication_snapshot_sid) != trackSid)
                            continue;
                        Native.Check(Native.lk_remote_track_publication_snapshot_track(
                            publication, out var track));
                        Native.Check(Native.lk_remote_track_snapshot_create_rtc_stats_snapshot(
                            track, out var stats));
                        try { return RtcStatsReader.Read(stats); }
                        finally { Native.lk_rtc_stats_snapshot_destroy(stats); }
                    }
                    break;
                }
                throw new KeyNotFoundException("Remote track was not found");
            }
            finally { Native.lk_remote_participant_list_destroy(participants); }
        });
    }
}

internal static class RtcStatsReader
{
    internal static IReadOnlyList<RtcTrackStats> Read(IntPtr snapshot)
    {
        var count = Native.lk_rtc_stats_snapshot_count(snapshot);
        if (count > int.MaxValue) throw new LiveKitException(-1, "Too many RTC stats streams");
        var result = new List<RtcTrackStats>((int)count);
        for (nuint i = 0; i < count; i++)
        {
            var value = new Native.RtcTrackStats();
            Native.lk_rtc_track_stats_init(ref value);
            Native.Check(Native.lk_rtc_stats_snapshot_info(snapshot, i, ref value));
            result.Add(new RtcTrackStats(
                ReadString(snapshot, i, Native.lk_rtc_stats_snapshot_id),
                ReadString(snapshot, i, Native.lk_rtc_stats_snapshot_kind),
                ReadString(snapshot, i, Native.lk_rtc_stats_snapshot_rid),
                ReadString(snapshot, i, Native.lk_rtc_stats_snapshot_codec_mime_type),
                value.Direction, value.TimestampMs, value.Bytes, value.Packets, value.PacketsLost,
                value.HasBitrateBps != 0 ? value.BitrateBps : null,
                value.HasRoundTripTimeSeconds != 0 ? value.RoundTripTimeSeconds : null,
                value.HasJitterSeconds != 0 ? value.JitterSeconds : null,
                value.HasAudioLevel != 0 ? value.AudioLevel : null,
                value.FrameWidth, value.FrameHeight, value.FramesPerSecond,
                value.Frames, value.FramesDropped)
            {
                CodecImplementation = ReadString(snapshot, i,
                    Native.lk_rtc_stats_snapshot_codec_implementation),
                QualityLimitationReason = ReadString(snapshot, i,
                    Native.lk_rtc_stats_snapshot_quality_limitation_reason),
                ConcealedSamples = value.ConcealedSamples,
                FirCount = value.FirCount,
                PliCount = value.PliCount,
                NackCount = value.NackCount,
                QpSum = value.QpSum,
            });
        }
        return result;
    }

    private static string ReadString(IntPtr snapshot, nuint index, Native.IndexedStringGetter getter)
    {
        var size = getter(snapshot, index, null, 0);
        if (size == 0) return string.Empty;
        if (size > int.MaxValue) throw new LiveKitException(-1, "RTC stats string is too large");
        var buffer = new byte[(int)size];
        getter(snapshot, index, buffer, size);
        var length = Array.IndexOf(buffer, (byte)0);
        return Encoding.UTF8.GetString(buffer, 0, length < 0 ? buffer.Length : length);
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct RtcTrackStats
    {
        internal nuint StructSize;
        internal int Direction;
        internal double TimestampMs;
        internal ulong Bytes;
        internal ulong Packets;
        internal long PacketsLost;
        internal int HasBitrateBps;
        internal double BitrateBps;
        internal int HasRoundTripTimeSeconds;
        internal double RoundTripTimeSeconds;
        internal int HasJitterSeconds;
        internal double JitterSeconds;
        internal int HasAudioLevel;
        internal double AudioLevel;
        internal ulong ConcealedSamples;
        internal uint FrameWidth;
        internal uint FrameHeight;
        internal double FramesPerSecond;
        internal ulong Frames;
        internal ulong FramesDropped;
        internal ulong FirCount;
        internal ulong PliCount;
        internal ulong NackCount;
        internal ulong QpSum;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_rtc_track_stats_init(ref RtcTrackStats stats);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_local_track_create_rtc_stats_snapshot(TrackHandle track,
        out IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_track_publication_snapshot_track(IntPtr publication,
        out IntPtr track);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_remote_track_snapshot_create_rtc_stats_snapshot(IntPtr track,
        out IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_rtc_stats_snapshot_destroy(IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rtc_stats_snapshot_count(IntPtr snapshot);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_rtc_stats_snapshot_info(IntPtr snapshot, nuint index,
        ref RtcTrackStats stats);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rtc_stats_snapshot_id(IntPtr snapshot, nuint index,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rtc_stats_snapshot_kind(IntPtr snapshot, nuint index,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rtc_stats_snapshot_rid(IntPtr snapshot, nuint index,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rtc_stats_snapshot_codec_mime_type(IntPtr snapshot, nuint index,
        [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rtc_stats_snapshot_codec_implementation(IntPtr snapshot,
        nuint index, [Out] byte[]? buffer, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint lk_rtc_stats_snapshot_quality_limitation_reason(IntPtr snapshot,
        nuint index, [Out] byte[]? buffer, nuint size);
}
