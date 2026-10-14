package io.livekit.client;

/** Detached, normalized RTC stream statistics. Optional measurements return null. */
public final class RtcTrackStats {
    private final String[] text;
    private final long[] counts;
    private final double[] measurements;
    private final boolean[] available;

    RtcTrackStats(String[] text, long[] counts, double[] measurements, boolean[] available) {
        if (text.length != 6 || counts.length != 13 || measurements.length != 6 ||
            available.length != 4) throw new IllegalArgumentException("Invalid RTC stats");
        this.text = text.clone();
        this.counts = counts.clone();
        this.measurements = measurements.clone();
        this.available = available.clone();
    }

    public String id() { return text[0]; }
    public String kind() { return text[1]; }
    public String rid() { return text[2]; }
    public String codecMimeType() { return text[3]; }
    public String codecImplementation() { return text[4]; }
    public String qualityLimitationReason() { return text[5]; }
    /** C ABI direction: 0 unknown, 1 send, 2 receive. */
    public int direction() { return (int) counts[0]; }
    public double timestampMs() { return measurements[0]; }
    public long bytes() { return counts[1]; }
    public long packets() { return counts[2]; }
    public long packetsLost() { return counts[3]; }
    public Double bitrateBps() { return available[0] ? measurements[1] : null; }
    public Double roundTripTimeSeconds() { return available[1] ? measurements[2] : null; }
    public Double jitterSeconds() { return available[2] ? measurements[3] : null; }
    public Double audioLevel() { return available[3] ? measurements[4] : null; }
    public long concealedSamples() { return counts[4]; }
    public long frameWidth() { return counts[5]; }
    public long frameHeight() { return counts[6]; }
    public double framesPerSecond() { return measurements[5]; }
    public long frames() { return counts[7]; }
    public long framesDropped() { return counts[8]; }
    public long firCount() { return counts[9]; }
    public long pliCount() { return counts[10]; }
    public long nackCount() { return counts[11]; }
    public long qpSum() { return counts[12]; }
}
