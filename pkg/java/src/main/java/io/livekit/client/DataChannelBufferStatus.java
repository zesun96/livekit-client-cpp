package io.livekit.client;

/** Detached data channel buffer and backpressure state. */
public record DataChannelBufferStatus(boolean reliable, long bufferedAmount,
                                      long highWaterMark, long lowWaterMark,
                                      boolean backpressured) {}
