using System;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

internal static partial class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void TrackQualityUpdateEvent(IntPtr userData, IntPtr room, IntPtr track,
        IntPtr participant, IntPtr update);

    [StructLayout(LayoutKind.Sequential)]
    internal struct EncryptionState
    {
        internal IntPtr TrackId;
        internal IntPtr ParticipantIdentity;
        internal int Kind;
        internal int Direction;
        internal int Enabled;
        internal nuint KeyIndex;
        internal int State;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SubscribedQuality
    {
        internal int Quality;
        internal int Enabled;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SubscribedCodec
    {
        internal IntPtr Codec;
        internal IntPtr Qualities;
        internal nuint QualityCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SubscribedQualityUpdate
    {
        internal IntPtr TrackSid;
        internal IntPtr Qualities;
        internal nuint QualityCount;
        internal IntPtr Codecs;
        internal nuint CodecCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TranscriptionSegment
    {
        internal IntPtr Id;
        internal IntPtr Text;
        internal IntPtr Language;
        internal ulong StartTime;
        internal ulong EndTime;
        internal int IsFinal;
        internal long FirstReceivedTime;
        internal long LastReceivedTime;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TranscriptionReceived
    {
        internal IntPtr ParticipantIdentity;
        internal IntPtr TrackId;
        internal IntPtr Segments;
        internal nuint SegmentCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct MetricTimestamp
    {
        internal long Seconds;
        internal int Nanos;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct MetricSample
    {
        internal long TimestampMs;
        internal int HasNormalizedTimestamp;
        internal MetricTimestamp NormalizedTimestamp;
        internal float Value;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeSeriesMetric
    {
        internal uint Label;
        internal uint ParticipantIdentity;
        internal uint TrackSid;
        internal uint Rid;
        internal IntPtr Samples;
        internal nuint SampleCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct EventMetric
    {
        internal uint Label;
        internal uint ParticipantIdentity;
        internal uint TrackSid;
        internal uint Rid;
        internal long StartTimestampMs;
        internal int HasEndTimestampMs;
        internal long EndTimestampMs;
        internal int HasNormalizedStartTimestamp;
        internal MetricTimestamp NormalizedStartTimestamp;
        internal int HasNormalizedEndTimestamp;
        internal MetricTimestamp NormalizedEndTimestamp;
        internal IntPtr Metadata;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct MetricsReceived
    {
        internal long TimestampMs;
        internal int HasNormalizedTimestamp;
        internal MetricTimestamp NormalizedTimestamp;
        internal IntPtr StringData;
        internal nuint StringDataCount;
        internal IntPtr TimeSeries;
        internal nuint TimeSeriesCount;
        internal IntPtr Events;
        internal nuint EventCount;
        internal IntPtr ParticipantIdentity;
    }
}
