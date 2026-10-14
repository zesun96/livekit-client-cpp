package io.livekit.client.examples;

import io.livekit.client.*;
import java.nio.charset.StandardCharsets;
import java.io.ByteArrayOutputStream;
import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Device-free end-to-end check against a real LiveKit server. */
public final class TwoPeerSmoke {
    private static final Duration TIMEOUT = Duration.ofSeconds(15);

    private TwoPeerSmoke() {}

    public static void main(String[] args) throws Exception {
        String url = required("LIVEKIT_URL");
        String tokenA = required("LIVEKIT_TOKEN_A");
        String tokenB = required("LIVEKIT_TOKEN_B");
        CountDownLatch gotData = new CountDownLatch(1);
        CountDownLatch gotText = new CountDownLatch(1);
        CountDownLatch gotBytes = new CountDownLatch(1);
        CountDownLatch gotByteStream = new CountDownLatch(1);
        CountDownLatch gotVideo = new CountDownLatch(1);
        CountDownLatch videoSubscribed = new CountDownLatch(1);
        AtomicReference<String> callbackError = new AtomicReference<>();
        StringBuilder textChunks = new StringBuilder();
        ByteArrayOutputStream byteChunks = new ByteArrayOutputStream();
        TraceOptions previousTrace = Diagnostics.traceOptions();
        Diagnostics.setLogCaptureCapacity(128);
        Diagnostics.setTraceCaptureCapacity(128);
        Diagnostics.setTraceOptions(true, 127);
        try (Room a = new Room(); Room b = new Room()) {
            b.addListener(new Room.Listener() {
                @Override public void onDataReceived(DataPacket packet) {
                    if (packet.topic().equals("java.smoke") &&
                        Arrays.equals(packet.data(), bytes("data-ok"))) gotData.countDown();
                    else callbackError.compareAndSet(null, "Unexpected data packet");
                }
                @Override public void onTextStreamEvent(TextStreamEvent event) {
                    if (!event.topic().equals("java.stream")) return;
                    if (event.type() == 1) textChunks.append(event.content());
                    if (event.type() == 2 && textChunks.toString().equals("stream-ok"))
                        gotText.countDown();
                }
                @Override public void onBytesReceived(ByteMessage message) {
                    if (message.topic().equals("java.bytes") &&
                        Arrays.equals(message.data(), bytes("bytes-ok"))) gotBytes.countDown();
                }
                @Override public void onByteStreamEvent(ByteStreamEvent event) {
                    if (!event.topic().equals("java.byte.stream")) return;
                    if (event.type() == 1) byteChunks.writeBytes(event.content());
                    if (event.type() == 2 && Arrays.equals(byteChunks.toByteArray(), bytes("chunk-ok")))
                        gotByteStream.countDown();
                }
                @Override public void onTrackSubscribed(RemoteTrackPublication track) {
                    if (track.kind() == 2) videoSubscribed.countDown();
                }
            });
            b.addFrameListener(new Room.FrameListener() {
                @Override public void onVideoFrame(RemoteTrackPublication track,
                                                   Participant participant, VideoFrame frame) {
                    if (frame.width() == 64 && frame.height() == 64) gotVideo.countDown();
                }
            });
            b.registerTextStreamHandler("java.stream");
            b.registerByteStreamHandler("java.byte.stream");
            b.registerRpcMethod("java.echo", call -> "echo:" + call.payload());
            a.connect(url, tokenA);
            b.connect(url, tokenB);
            if (a.localIdentity().equals(b.localIdentity()))
                throw new AssertionError("Peers have the same identity");
            until(() -> a.remoteParticipants().stream()
                .anyMatch(p -> p.identity().equals(b.localIdentity())), "peer A snapshot");
            until(() -> b.remoteParticipants().stream()
                .anyMatch(p -> p.identity().equals(a.localIdentity())), "peer B snapshot");

            a.publishData(bytes("data-ok"), "java.smoke", true, b.localIdentity());
            await(gotData, "data packet");
            a.sendBytes(bytes("bytes-ok"), new DataSendOptions("java.bytes"));
            await(gotBytes, "byte message");
            try (TextStreamWriter writer = a.streamText("java.stream", b.localIdentity())) {
                writer.write("stream-");
                writer.write("ok");
            }
            await(gotText, "text stream");
            try (ByteStreamWriter writer = a.streamBytes(
                    "java.byte.stream", "application/octet-stream", "smoke.bin", b.localIdentity())) {
                writer.write(bytes("chunk-"));
                writer.write(bytes("ok"));
            }
            await(gotByteStream, "byte stream");
            RpcResult rpc = a.performRpc(b.localIdentity(), "java.echo", "ok", 5000);
            if (!rpc.ok() || !rpc.payload().equals("echo:ok"))
                throw new AssertionError("RPC response mismatch: " + rpc);

            DataTrackSchema schema = new DataTrackSchema("java.schema", 2, "",
                bytes("{\"type\":\"object\"}"));
            a.storeDataTrackSchema(schema);
            try (LocalDataTrack published = a.publishDataTrack(new DataTrackPublishOptions(
                    "java.data", 2, "", "java.schema", 2, ""))) {
                if (!published.isPublished() || !published.info().published())
                    throw new AssertionError("Local DataTrack publication state missing");
                until(() -> b.remoteDataTracks().stream()
                    .anyMatch(t -> t.sid().equals(published.sid())), "remote DataTrack");
                try (DataTrackReader reader = b.subscribeDataTrack(
                        a.localIdentity(), published.sid(), 8)) {
                    DataTrackFrame frame = null;
                    for (int attempt = 0; attempt < 20 && frame == null; attempt++) {
                        published.push(bytes("frame-ok"), 12345);
                        frame = reader.readFor(500);
                    }
                    if (frame == null || !Arrays.equals(frame.data(), bytes("frame-ok")) ||
                        !frame.hasUserTimestamp() || frame.userTimestamp() != 12345)
                        throw new AssertionError("DataTrack frame mismatch: " + frame);
                }
                DataTrackSchema remoteSchema = b.getDataTrackSchema(
                    a.localIdentity(), "java.schema", 2, "");
                if (!Arrays.equals(remoteSchema.definition(), schema.definition()))
                    throw new AssertionError("DataTrack schema mismatch");
            }

            try (VideoSource source = new VideoSource(false);
                 LocalTrack track = a.createVideoTrack("java.smoke.video", source)) {
                byte[] rgba = new byte[64 * 64 * 4];
                for (int i = 0; i < rgba.length; i += 4) {
                    rgba[i] = (byte) 255;
                    rgba[i + 3] = (byte) 255;
                }
                source.captureFrame(new VideoFrameInput(rgba, 64, 64,
                    System.nanoTime() / 1000, 0, 0, List.of(), null));
                track.publish(new TrackPublishOptions(1));
                until(() -> b.remoteTrackPublications().stream()
                    .anyMatch(t -> t.kind() == 2), "remote video publication");
                await(videoSubscribed, "remote video subscription");
                RemoteTrackPublication remoteVideo = b.remoteTrackPublications().stream()
                    .filter(t -> t.kind() == 2).findFirst().orElseThrow();
                try (VideoStream reader = b.createVideoStream(
                        a.localIdentity(), remoteVideo.sid(), 8)) {
                    var nextFrame = reader.readForAsync(5000);
                    long readerDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(4);
                    while (!nextFrame.isDone() && System.nanoTime() < readerDeadline) {
                        source.captureFrame(new VideoFrameInput(rgba, 64, 64,
                            System.nanoTime() / 1000, 0, 0, List.of(), null));
                        Thread.sleep(50);
                    }
                    VideoFrame frame = nextFrame.get(6, TimeUnit.SECONDS);
                    if (frame == null || frame.width() != 64 || frame.height() != 64)
                        throw new AssertionError("Remote video reader returned no frame");
                }
                long deadline = System.nanoTime() + TIMEOUT.toNanos();
                while (gotVideo.getCount() != 0 && System.nanoTime() < deadline) {
                    source.captureFrame(new VideoFrameInput(rgba, 64, 64,
                        System.nanoTime() / 1000, 0, 0, List.of(), null));
                    Thread.sleep(50);
                }
                await(gotVideo, "remote video frame");
            }
            if (callbackError.get() != null) throw new AssertionError(callbackError.get());
            if (!b.drainFrameCallbackErrors().isEmpty())
                throw new AssertionError("Frame callback failed");
            if (Diagnostics.drainTraceRecords().isEmpty())
                throw new AssertionError("No native trace records were delivered");
            System.out.println("Java two-peer smoke passed: data, text/byte streams, RPC, DataTrack, video");
        } finally {
            Diagnostics.setTraceOptions(previousTrace.enabled(), previousTrace.categoryMask());
            Diagnostics.setLogCaptureCapacity(0);
            Diagnostics.setTraceCaptureCapacity(0);
        }
    }

    private static void until(Check check, String label) throws Exception {
        long deadline = System.nanoTime() + TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            if (check.test()) return;
            Thread.sleep(50);
        }
        throw new AssertionError("Timed out waiting for " + label);
    }

    private static void await(CountDownLatch latch, String label) throws InterruptedException {
        if (!latch.await(TIMEOUT.toMillis(), TimeUnit.MILLISECONDS))
            throw new AssertionError("Timed out waiting for " + label);
    }

    private static byte[] bytes(String value) { return value.getBytes(StandardCharsets.UTF_8); }

    private static String required(String name) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) throw new IllegalArgumentException("Set " + name);
        return value;
    }

    @FunctionalInterface private interface Check { boolean test() throws Exception; }
}
