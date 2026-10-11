using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public sealed record EncryptionStateChange(string TrackId, string ParticipantIdentity,
    int Kind, int Direction, bool Enabled, nuint KeyIndex, int State);
public sealed record SubscribedQuality(int Quality, bool Enabled);
public sealed record SubscribedCodec(string Codec, IReadOnlyList<SubscribedQuality> Qualities);
public sealed record SubscribedQualityChange(TrackInfo Track, Participant Participant,
    string TrackSid, IReadOnlyList<SubscribedQuality> Qualities,
    IReadOnlyList<SubscribedCodec> Codecs);
public sealed record TranscriptionSegment(string Id, string Text, string Language,
    ulong StartTime, ulong EndTime, bool IsFinal, long FirstReceivedTime,
    long LastReceivedTime);
public sealed record TranscriptionMessage(string ParticipantIdentity, string TrackId,
    IReadOnlyList<TranscriptionSegment> Segments);
public sealed record MetricTimestamp(long Seconds, int Nanos);
public sealed record MetricSample(long TimestampMs, MetricTimestamp? NormalizedTimestamp,
    float Value);
public sealed record TimeSeriesMetric(uint Label, uint ParticipantIdentity, uint TrackSid,
    uint Rid, IReadOnlyList<MetricSample> Samples);
public sealed record EventMetric(uint Label, uint ParticipantIdentity, uint TrackSid,
    uint Rid, long StartTimestampMs, long? EndTimestampMs,
    MetricTimestamp? NormalizedStartTimestamp, MetricTimestamp? NormalizedEndTimestamp,
    string Metadata);
public sealed record MetricsMessage(long TimestampMs, MetricTimestamp? NormalizedTimestamp,
    string ParticipantIdentity, IReadOnlyList<string> StringData,
    IReadOnlyList<TimeSeriesMetric> TimeSeries, IReadOnlyList<EventMetric> Events);

public sealed partial class Room
{
    public event Action<EncryptionStateChange>? EncryptionStateChanged;
    public event Action<SubscribedQualityChange>? SubscribedQualityUpdated;
    public event Action<TranscriptionMessage>? TranscriptionReceived;
    public event Action<MetricsMessage>? MetricsReceived;

    private static T[] ReadNativeArray<T>(IntPtr pointer, nuint count) where T : struct
    {
        if (pointer == IntPtr.Zero || count == 0 || count > 100_000) return Array.Empty<T>();
        var values = new T[(int)count];
        var size = Marshal.SizeOf<T>();
        for (var i = 0; i < values.Length; i++)
            values[i] = Marshal.PtrToStructure<T>(IntPtr.Add(pointer, checked(i * size)));
        return values;
    }

    private static MetricTimestamp CopyTimestamp(Native.MetricTimestamp value) =>
        new(value.Seconds, value.Nanos);

    private void OnEncryptionStateChanged(IntPtr u, IntPtr r, IntPtr pointer)
    {
        if (pointer == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.EncryptionState>(pointer);
        var copy = new EncryptionStateChange(
            Marshal.PtrToStringUTF8(value.TrackId) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty,
            value.Kind, value.Direction, value.Enabled != 0, value.KeyIndex, value.State);
        Queue(() => EncryptionStateChanged?.Invoke(copy));
    }

    private static IReadOnlyList<SubscribedQuality> CopyQualities(IntPtr pointer, nuint count)
    {
        var native = ReadNativeArray<Native.SubscribedQuality>(pointer, count);
        var values = new SubscribedQuality[native.Length];
        for (var i = 0; i < values.Length; i++)
            values[i] = new SubscribedQuality(native[i].Quality, native[i].Enabled != 0);
        return values;
    }

    private void OnSubscribedQualityUpdate(IntPtr u, IntPtr r, IntPtr track,
        IntPtr participant, IntPtr pointer)
    {
        if (SubscribedQualityUpdated is null || pointer == IntPtr.Zero) return;
        var publication = CopyTrackEvent(track, participant);
        if (publication is null) return;
        var value = Marshal.PtrToStructure<Native.SubscribedQualityUpdate>(pointer);
        var codecs = ReadNativeArray<Native.SubscribedCodec>(value.Codecs, value.CodecCount);
        var copiedCodecs = new SubscribedCodec[codecs.Length];
        for (var i = 0; i < codecs.Length; i++)
            copiedCodecs[i] = new SubscribedCodec(
                Marshal.PtrToStringUTF8(codecs[i].Codec) ?? string.Empty,
                CopyQualities(codecs[i].Qualities, codecs[i].QualityCount));
        var copy = new SubscribedQualityChange(publication.Track, publication.Participant,
            Marshal.PtrToStringUTF8(value.TrackSid) ?? string.Empty,
            CopyQualities(value.Qualities, value.QualityCount), copiedCodecs);
        Queue(() => SubscribedQualityUpdated?.Invoke(copy));
    }

    private void OnTranscriptionReceived(IntPtr u, IntPtr r, IntPtr pointer)
    {
        if (TranscriptionReceived is null || pointer == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.TranscriptionReceived>(pointer);
        var native = ReadNativeArray<Native.TranscriptionSegment>(value.Segments, value.SegmentCount);
        var segments = new TranscriptionSegment[native.Length];
        for (var i = 0; i < native.Length; i++)
            segments[i] = new TranscriptionSegment(
                Marshal.PtrToStringUTF8(native[i].Id) ?? string.Empty,
                Marshal.PtrToStringUTF8(native[i].Text) ?? string.Empty,
                Marshal.PtrToStringUTF8(native[i].Language) ?? string.Empty,
                native[i].StartTime, native[i].EndTime, native[i].IsFinal != 0,
                native[i].FirstReceivedTime, native[i].LastReceivedTime);
        var copy = new TranscriptionMessage(
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty,
            Marshal.PtrToStringUTF8(value.TrackId) ?? string.Empty, segments);
        Queue(() => TranscriptionReceived?.Invoke(copy));
    }

    private void OnMetricsReceived(IntPtr u, IntPtr r, IntPtr pointer)
    {
        if (MetricsReceived is null || pointer == IntPtr.Zero) return;
        var value = Marshal.PtrToStructure<Native.MetricsReceived>(pointer);
        var nativeSeries = ReadNativeArray<Native.TimeSeriesMetric>(value.TimeSeries,
            value.TimeSeriesCount);
        var series = new TimeSeriesMetric[nativeSeries.Length];
        for (var i = 0; i < series.Length; i++)
        {
            var nativeSamples = ReadNativeArray<Native.MetricSample>(nativeSeries[i].Samples,
                nativeSeries[i].SampleCount);
            var samples = new MetricSample[nativeSamples.Length];
            for (var j = 0; j < samples.Length; j++)
                samples[j] = new MetricSample(nativeSamples[j].TimestampMs,
                    nativeSamples[j].HasNormalizedTimestamp != 0 ?
                        CopyTimestamp(nativeSamples[j].NormalizedTimestamp) : null,
                    nativeSamples[j].Value);
            series[i] = new TimeSeriesMetric(nativeSeries[i].Label,
                nativeSeries[i].ParticipantIdentity, nativeSeries[i].TrackSid,
                nativeSeries[i].Rid, samples);
        }
        var nativeEvents = ReadNativeArray<Native.EventMetric>(value.Events, value.EventCount);
        var events = new EventMetric[nativeEvents.Length];
        for (var i = 0; i < events.Length; i++)
            events[i] = new EventMetric(nativeEvents[i].Label,
                nativeEvents[i].ParticipantIdentity, nativeEvents[i].TrackSid,
                nativeEvents[i].Rid, nativeEvents[i].StartTimestampMs,
                nativeEvents[i].HasEndTimestampMs != 0 ? nativeEvents[i].EndTimestampMs : null,
                nativeEvents[i].HasNormalizedStartTimestamp != 0 ?
                    CopyTimestamp(nativeEvents[i].NormalizedStartTimestamp) : null,
                nativeEvents[i].HasNormalizedEndTimestamp != 0 ?
                    CopyTimestamp(nativeEvents[i].NormalizedEndTimestamp) : null,
                Marshal.PtrToStringUTF8(nativeEvents[i].Metadata) ?? string.Empty);
        var copy = new MetricsMessage(value.TimestampMs,
            value.HasNormalizedTimestamp != 0 ? CopyTimestamp(value.NormalizedTimestamp) : null,
            Marshal.PtrToStringUTF8(value.ParticipantIdentity) ?? string.Empty,
            ReadStringArray(value.StringData, value.StringDataCount), series, events);
        Queue(() => MetricsReceived?.Invoke(copy));
    }
}
