package io.livekit.client;

/** Audio processing switches for a microphone source. */
public record MicrophoneProcessingOptions(boolean echoCancellation, boolean autoGainControl,
                                          boolean noiseSuppression) {}
