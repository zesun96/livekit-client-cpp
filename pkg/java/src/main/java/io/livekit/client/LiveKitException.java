package io.livekit.client;

/** Failure returned by the LiveKit C ABI. */
public final class LiveKitException extends RuntimeException {
    private final int status;
    private final int errorDomain;
    private final int errorCode;

    public LiveKitException(int status, String message) {
        this(status, 1, status, message);
    }

    public LiveKitException(int status, int errorDomain, int errorCode, String message) {
        super(message);
        this.status = status;
        this.errorDomain = errorDomain;
        this.errorCode = errorCode;
    }

    public int status() { return status; }
    /** C ABI error domain: 1 for status, 2 for DataTrack. */
    public int errorDomain() { return errorDomain; }
    /** Error code within {@link #errorDomain()}. */
    public int errorCode() { return errorCode; }
}
