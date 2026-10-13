package io.livekit.client;

/** Snapshot of microphone processing counters and optional measurements. */
public record MicrophoneProcessingStats(
    long captureFramesProcessed, long renderFramesProcessed, long captureProcessingErrors,
    long renderProcessingErrors, long framesDropped, boolean echoCancellationEnabled,
    Double echoReturnLossDb, Double echoReturnLossEnhancementDb,
    Double residualEchoLikelihood, Double residualEchoLikelihoodRecentMax,
    Integer delayMedianMs, Integer delayStandardDeviationMs, Integer delayMs) {}
