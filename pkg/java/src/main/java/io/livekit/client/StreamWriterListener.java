package io.livekit.client;

/** Called after a successful write and once when the writer ends. */
public interface StreamWriterListener {
    default void onProgress(long bytesSent, Long totalSize) {}
    default void onComplete(StreamCompletion completion) {}
}
