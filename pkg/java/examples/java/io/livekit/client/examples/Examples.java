package io.livekit.client.examples;

import io.livekit.client.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/** Feature-oriented examples. Run with LIVEKIT_URL and LIVEKIT_TOKEN. */
public final class Examples {
    private Examples() {}

    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            System.err.println("Usage: Examples <media-devices|room-echo|room-events|data-transfer|"
                + "stream-writer|data-track-schema|token-source|rpc-receiver|rpc-caller|"
                + "e2ee|publish-audio|publish-video|receive-media|receive-media-events|record-remote|"
                + "publish-microphone|publish-system-audio|publish-camera|publish-screen> [argument]");
            System.exit(2);
        }
        if (args[0].equals("media-devices")) {
            MediaDevices.listDevices().forEach(System.out::println);
            MediaDevices.listScreenSources().forEach(System.out::println);
            return;
        }
        String url = required("LIVEKIT_URL");
        String token = required("LIVEKIT_TOKEN");
        int seconds = Integer.parseInt(System.getenv().getOrDefault("LIVEKIT_EXAMPLE_SECONDS", "10"));
        if (seconds < 1) throw new IllegalArgumentException("LIVEKIT_EXAMPLE_SECONDS must be positive");
        Set<String> subscribedTracks = ConcurrentHashMap.newKeySet();
        try (Room room = new Room()) {
            room.addListener(new Room.Listener() {
                @Override public void onConnected() { System.out.println("connected"); }
                @Override public void onDisconnected(int reason) {
                    System.out.println("disconnected: " + reason);
                }
                @Override public void onParticipantConnected(Participant p) {
                    System.out.println("participant joined: " + p.identity());
                }
                @Override public void onTrackPublished(RemoteTrackPublication track) {
                    System.out.println("track published: " + track);
                }
                @Override public void onTrackSubscribed(RemoteTrackPublication track) {
                    subscribedTracks.add(track.sid());
                }
                @Override public void onTrackUnsubscribed(RemoteTrackPublication track) {
                    subscribedTracks.remove(track.sid());
                }
                @Override public void onDataReceived(DataPacket packet) {
                    System.out.println("data: " + new String(packet.data(), StandardCharsets.UTF_8));
                }
                @Override public void onTextReceived(TextMessage message) {
                    System.out.println("text: " + message.text());
                }
                @Override public void onBytesReceived(ByteMessage message) {
                    System.out.println("bytes: " + message.data().length);
                }
                @Override public void onTextStreamEvent(TextStreamEvent event) {
                    if (event.type() == 1) System.out.print(event.content());
                    if (event.type() == 2) System.out.println();
                }
                @Override public void onByteStreamEvent(ByteStreamEvent event) {
                    if (event.type() == 1)
                        System.out.println("byte stream chunk: " + event.content().length);
                }
                @Override public void onDataTrackFrame(DataTrackPublication track, DataTrackFrame frame) {
                    System.out.println("DataTrack frame: " + frame.data().length);
                }
            });
            room.registerTextStreamHandler("example.stream");
            room.registerByteStreamHandler("example.byte.stream");
            if (args[0].equals("token-source"))
                room.connectWithTokenSource(request -> new TokenCredentials(url, token));
            else if (args[0].equals("e2ee")) {
                byte[] key = required("LIVEKIT_E2EE_KEY").getBytes(StandardCharsets.UTF_8);
                if (key.length < 32) throw new IllegalArgumentException("E2EE key needs 32 UTF-8 bytes");
                room.connect(url, token, new ConnectionOptions(true, false, false, 3, 15_000,
                    new IceServer[0], 3, 1, new E2eeOptions(key,
                        "livekit-java-example".getBytes(StandardCharsets.UTF_8))));
            }
            else room.connect(url, token);
            long connectDeadline = System.nanoTime() + 10_000_000_000L;
            while (!room.isConnected() && System.nanoTime() < connectDeadline)
                Thread.sleep(50);
            if (!room.isConnected()) throw new IllegalStateException("Room did not connect");
            System.out.println("identity=" + room.localIdentity() + " room=" + room.name());
            switch (args[0]) {
                case "room-echo" -> {
                    room.sendText("hello from Java");
                    sleep(seconds);
                }
                case "room-events", "token-source" -> sleep(seconds);
                case "e2ee" -> {
                    System.out.println("E2EE configured=" + room.e2eeConfigured()
                        + " enabled=" + room.e2eeEnabled());
                    sleep(seconds);
                }
                case "data-transfer" -> {
                    room.publishData("hello data".getBytes(StandardCharsets.UTF_8), "example", true);
                    room.sendText("hello text");
                    room.sendBytes("hello bytes".getBytes(StandardCharsets.UTF_8));
                    if (args.length > 1) room.sendFile(Path.of(args[1]).toString());
                    sleep(2);
                }
                case "stream-writer" -> {
                    try (TextStreamWriter writer = room.streamText("example.stream")) {
                        writer.write("hello ");
                        writer.write("from Java");
                    }
                    sleep(2);
                }
                case "data-track-schema" -> {
                    byte[] schema = "{\"type\":\"object\"}".getBytes(StandardCharsets.UTF_8);
                    room.storeDataTrackSchema(new DataTrackSchema("example.schema", 2, "", schema));
                    try (LocalDataTrack track = room.publishDataTrack(new DataTrackPublishOptions(
                            "example.data", 2, "", "example.schema", 2, ""))) {
                        track.push("{}".getBytes(StandardCharsets.UTF_8));
                        sleep(2);
                    }
                }
                case "rpc-receiver" -> {
                    room.registerRpcMethod("example.echo", call -> call.payload());
                    sleep(seconds);
                }
                case "rpc-caller" -> {
                    if (args.length < 2) throw new IllegalArgumentException("RPC destination identity required");
                    RpcResult result = room.performRpc(args[1], "example.echo",
                        args.length > 2 ? args[2] : "hello", 5000);
                    if (!result.ok()) throw new IllegalStateException("RPC error: " + result.errorMessage());
                    System.out.println("RPC response: " + result.payload());
                }
                case "publish-audio" -> publishAudio(room, seconds);
                case "publish-video" -> publishVideo(room, seconds);
                case "receive-media" -> receiveMedia(room, subscribedTracks, seconds, false);
                case "receive-media-events" -> receiveMedia(room, subscribedTracks, seconds, true);
                case "record-remote" -> {
                    if (args.length < 2 || !Files.isDirectory(Path.of(args[1])))
                        throw new IllegalArgumentException("Existing output directory required");
                    recordRemote(room, subscribedTracks, Path.of(args[1]), seconds);
                }
                case "publish-microphone", "publish-system-audio" -> {
                    try (AudioSource source = args[0].equals("publish-microphone")
                            ? AudioSource.createMicrophone(args.length > 1 ? args[1] : null, 200,
                                true, true, true)
                            : AudioSource.createSystemAudio(args.length > 1 ? args[1] : null, 200);
                         LocalTrack track = room.createAudioTrack("java-device-audio", source)) {
                        track.publish(args[0].equals("publish-microphone") ? 2 : 4);
                        source.startCapture();
                        sleep(seconds);
                        source.stopCapture();
                    }
                }
                case "publish-camera", "publish-screen" -> {
                    try (VideoSource source = args[0].equals("publish-camera")
                            ? VideoSource.createCamera(args.length > 1 ? args[1] : null, 640, 480, 15)
                            : VideoSource.createScreen(args.length > 1 ? args[1]
                                : MediaDevices.listScreenSources().get(0).id());
                         LocalTrack track = room.createVideoTrack("java-device-video", source)) {
                        if (args[0].equals("publish-screen")) track.publishScreenShare();
                        else track.publish(1);
                        source.startCapture();
                        sleep(seconds);
                        source.stopCapture();
                    }
                }
                default -> throw new IllegalArgumentException("Unknown scenario: " + args[0]);
            }
        }
    }

    private static void publishAudio(Room room, int seconds) throws InterruptedException {
        try (AudioSource source = new AudioSource(48_000, 1, 1000);
             LocalTrack track = room.createAudioTrack("java-tone", source)) {
            source.capture(new short[960]);
            track.publish(new TrackPublishOptions(2));
            for (int i = 0; i < seconds * 50; i++) {
                short[] pcm = new short[960];
                for (int j = 0; j < pcm.length; j++)
                    pcm[j] = (short) (4000 * Math.sin(2 * Math.PI * 440 * (i * 960 + j) / 48_000));
                source.capture(pcm);
                Thread.sleep(20);
            }
        }
    }

    private static void publishVideo(Room room, int seconds) throws InterruptedException {
        try (VideoSource source = new VideoSource(false);
             LocalTrack track = room.createVideoTrack("java-bars", source)) {
            int width = 320, height = 180;
            byte[] rgba = new byte[width * height * 4];
            source.captureFrame(new VideoFrameInput(rgba, width, height,
                System.nanoTime() / 1000, 0, 0, List.of(), null));
            track.publish(new TrackPublishOptions(1));
            for (int i = 0; i < seconds * 15; i++) {
                for (int p = 0; p < width * height; p++) {
                    int offset = p * 4;
                    rgba[offset] = (byte) ((p % width + i * 4) % 256);
                    rgba[offset + 1] = (byte) (p / width);
                    rgba[offset + 2] = (byte) 120;
                    rgba[offset + 3] = (byte) 255;
                }
                source.captureFrame(new VideoFrameInput(rgba, width, height,
                    System.nanoTime() / 1000, 0, 0, List.of(), null));
                Thread.sleep(67);
            }
        }
    }

    private static void receiveMedia(Room room, Set<String> subscribedTracks,
                                     int seconds, boolean callbacks)
            throws InterruptedException {
        if (callbacks) {
            room.addFrameListener(new Room.FrameListener() {
                @Override public void onAudioFrame(RemoteTrackPublication track, Participant participant,
                                                   AudioFrame frame) {
                    System.out.println("audio frame: " + frame.samples().length);
                }
                @Override public void onVideoFrame(RemoteTrackPublication track, Participant participant,
                                                   VideoFrame frame) {
                    System.out.println("video frame: " + frame.width() + "x" + frame.height());
                }
            });
            sleep(seconds);
            return;
        }
        long deadline = System.nanoTime() + seconds * 1_000_000_000L;
        while (System.nanoTime() < deadline) {
            for (RemoteTrackPublication track : room.remoteTrackPublications()) {
                if (!subscribedTracks.contains(track.sid())) continue;
                if (track.kind() == 2) {
                    try (VideoStream stream = room.createVideoStream(
                            track.participantIdentity(), track.sid(), 8)) {
                        VideoFrame frame = stream.readFor(1000);
                        if (frame != null)
                            System.out.println("video frame: " + frame.width() + "x" + frame.height());
                    }
                } else if (track.kind() == 1) {
                    try (AudioStream stream = room.createAudioStream(
                            track.participantIdentity(), track.sid(), 8)) {
                        AudioFrame frame = stream.readFor(1000);
                        if (frame != null) System.out.println("audio frame: " + frame.samples().length);
                    }
                }
            }
            Thread.sleep(200);
        }
    }

    private static void recordRemote(Room room, Set<String> subscribedTracks,
                                     Path output, int seconds)
            throws InterruptedException {
        Map<String, TrackRecorder> recorders = new HashMap<>();
        try {
            long deadline = System.nanoTime() + seconds * 1_000_000_000L;
            while (System.nanoTime() < deadline) {
                for (RemoteTrackPublication track : room.remoteTrackPublications()) {
                    if (track.kind() != 1 && track.kind() != 2) continue;
                    if (!subscribedTracks.contains(track.sid())) continue;
                    if (recorders.containsKey(track.sid())) continue;
                    Path file = output.resolve(track.sid() + (track.kind() == 1 ? ".wav" : ""));
                    TrackRecorder recorder = room.startTrackRecording(
                        track.participantIdentity(), track.sid(), file.toString());
                    recorders.put(track.sid(), recorder);
                    System.out.println("recording " + track.sid() + " to " + file);
                }
                Thread.sleep(200);
            }
        } finally {
            for (TrackRecorder recorder : recorders.values()) {
                recorder.stop();
                System.out.println(recorder.outputPath() + ": "
                    + recorder.stats().framesWritten() + " frames");
                recorder.close();
            }
        }
    }

    private static String required(String name) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) throw new IllegalArgumentException("Set " + name);
        return value;
    }

    private static void sleep(int seconds) throws InterruptedException {
        Thread.sleep(seconds * 1000L);
    }
}
