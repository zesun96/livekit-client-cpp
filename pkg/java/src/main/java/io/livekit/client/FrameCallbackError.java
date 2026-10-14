package io.livekit.client;

/** Failure raised by a frame listener on the Java callback thread. */
public record FrameCallbackError(String frameKind, Throwable cause) {}
