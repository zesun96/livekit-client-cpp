package io.livekit.client;

final class StreamWriterCallbacks {
    private final StreamWriterListener listener;
    private final Long totalSize;
    private long bytesSent;
    private boolean completed;

    StreamWriterCallbacks(StreamWriterListener listener, Long totalSize) {
        this.listener = listener;
        this.totalSize = totalSize;
    }

    void wrote(long bytes) {
        bytesSent += bytes;
        if (listener != null) dispatch(() -> listener.onProgress(bytesSent, totalSize));
    }

    void complete(int status, String streamId, String reason, int errorCode) {
        if (completed) return;
        completed = true;
        if (listener != null) {
            StreamCompletion result = new StreamCompletion(status, streamId, bytesSent,
                totalSize, reason, errorCode);
            dispatch(() -> listener.onComplete(result));
        }
    }

    private static void dispatch(Runnable callback) {
        try { callback.run(); }
        catch (Throwable failure) {
            Thread thread = Thread.currentThread();
            Thread.UncaughtExceptionHandler handler = thread.getUncaughtExceptionHandler();
            if (handler != null) handler.uncaughtException(thread, failure);
        }
    }
}
