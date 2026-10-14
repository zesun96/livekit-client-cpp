package io.livekit.client;

/** PCM samples copied from one remote audio frame. */
public record AudioFrame(short[] samples, int sampleRate, int channels, int samplesPerChannel) {}
