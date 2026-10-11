package io.livekit.client;

/** Failure returned by the LiveKit C ABI. */
public final class LiveKitException extends RuntimeException {
    private final int status;

    public LiveKitException(int status, String message) {
        super(message);
        this.status = status;
    }

    public int status() { return status; }
}
