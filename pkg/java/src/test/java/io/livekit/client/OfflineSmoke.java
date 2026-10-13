package io.livekit.client;

/** Run after setting LIVEKIT_CLIENT_CPP_LIBRARY and -Dlivekit.java.library. */
public final class OfflineSmoke {
    public static void main(String[] args) {
        if (Room.version().isBlank()) throw new AssertionError("Empty SDK version");
        LogOptions logBefore = Diagnostics.logOptions();
        Diagnostics.setLogOptions(new LogOptions(2, 3, 3));
        if (!Diagnostics.logOptions().equals(new LogOptions(2, 3, 3)))
            throw new AssertionError("Log options did not round-trip");
        Diagnostics.setLogOptions(logBefore);
        TraceOptions traceBefore = Diagnostics.traceOptions();
        Diagnostics.setTraceOptions(true, 1);
        if (!Diagnostics.traceOptions().equals(new TraceOptions(true, 1)))
            throw new AssertionError("Trace options did not round-trip");
        Diagnostics.setTraceOptions(traceBefore.enabled(), traceBefore.categoryMask());
        try {
            new RemoteTrackSettings(true, 2, 640, 480, 0, 0);
            throw new AssertionError("Conflicting remote track settings were accepted");
        } catch (IllegalArgumentException expected) { }
        byte[] magic = {1, 2};
        E2eeOptions detailedE2ee = new E2eeOptions(new byte[32], new byte[16], true, 0,
            magic, 32, 2, 8);
        magic[0] = 9;
        if (detailedE2ee.unencryptedMagicBytes()[0] != 1 ||
            detailedE2ee.ratchetWindowSize() != 32 || detailedE2ee.keyRingSize() != 8)
            throw new AssertionError("E2EE settings were not retained");
        try (Room room = new Room()) {
            if (room.e2eeConfigured() || room.dataKeyIndex() != 0)
                throw new AssertionError("Unconfigured E2EE state is incorrect");
            try {
                room.setSharedKey(new byte[32], -1);
                throw new AssertionError("Negative E2EE key index was accepted");
            } catch (IllegalArgumentException expected) { }
            try {
                room.setParticipantKey("peer", new byte[32], 0);
                throw new AssertionError("Unconfigured participant key was accepted");
            } catch (LiveKitException expected) { }
            try {
                room.setFrameCryptorEnabled("track", 2, true);
                throw new AssertionError("Invalid frame cryptor direction was accepted");
            } catch (IllegalArgumentException expected) { }
            try {
                room.setParticipantE2eeEnabled("peer", true);
                throw new AssertionError("Unconfigured participant E2EE update was accepted");
            } catch (LiveKitException expected) { }
            try {
                room.frameCryptors();
                throw new AssertionError("Unconfigured frame cryptor snapshot was accepted");
            } catch (LiveKitException expected) { }
            java.util.concurrent.atomic.AtomicInteger subscribed =
                new java.util.concurrent.atomic.AtomicInteger();
            room.addListener(new Room.Listener() {
                @Override public void onTrackSubscribed(RemoteTrackPublication track) {
                    if (!track.sid().equals("track-1")) throw new AssertionError("Wrong track SID");
                    subscribed.incrementAndGet();
                }
            });
            room.onNativeTrack(4, new RemoteTrackPublication("remote", "track-1", "mic",
                "audio/opus", 1, 1, false, 0, 0), 0);
            if (subscribed.get() != 1) throw new AssertionError("Track listener was not called");
            java.util.concurrent.atomic.AtomicReference<AudioFrame> receivedAudio =
                new java.util.concurrent.atomic.AtomicReference<>();
            java.util.concurrent.atomic.AtomicReference<VideoFrame> receivedVideo =
                new java.util.concurrent.atomic.AtomicReference<>();
            Room.FrameListener frameListener = new Room.FrameListener() {
                @Override public void onAudioFrame(RemoteTrackPublication track,
                                                   Participant participant, AudioFrame frame) {
                    receivedAudio.set(frame);
                }
                @Override public void onVideoFrame(RemoteTrackPublication track,
                                                   Participant participant, VideoFrame frame) {
                    receivedVideo.set(frame);
                }
            };
            room.addFrameListener(frameListener);
            RemoteTrackPublication frameTrack = new RemoteTrackPublication("remote", "frame-1",
                "camera", "video/vp8", 2, 2, false, 640, 480);
            Participant frameParticipant = new Participant("p-1", "remote", "name", "");
            room.onNativeAudioFrame(frameTrack, frameParticipant, new short[] {1, 2}, 48000, 1, 2);
            room.onNativeVideoFrame(frameTrack, frameParticipant, new byte[] {3, 4}, 2, 2,
                123, 456L, 7, new byte[] {8});
            if (receivedAudio.get().samples()[1] != 2 ||
                receivedVideo.get().userTimestampUs() != 456L ||
                receivedVideo.get().userData()[0] != 8)
                throw new AssertionError("Media frame listener failed");
            room.removeFrameListener(frameListener);
            receivedAudio.set(null);
            room.onNativeAudioFrame(frameTrack, frameParticipant, new short[] {9}, 48000, 1, 1);
            if (receivedAudio.get() != null)
                throw new AssertionError("Removed frame listener was called");
            java.util.concurrent.atomic.AtomicReference<LocalTrackPublication> localTrack =
                new java.util.concurrent.atomic.AtomicReference<>();
            java.util.concurrent.atomic.AtomicInteger localUnpublished =
                new java.util.concurrent.atomic.AtomicInteger();
            java.util.concurrent.atomic.AtomicInteger localSubscribed =
                new java.util.concurrent.atomic.AtomicInteger();
            room.addListener(new Room.Listener() {
                @Override public void onLocalTrackPublished(LocalTrackPublication track) {
                    localTrack.set(track);
                }
                @Override public void onLocalTrackUnpublished(LocalTrackPublication track) {
                    localUnpublished.incrementAndGet();
                }
                @Override public void onLocalTrackSubscribed(LocalTrackPublication track) {
                    localSubscribed.incrementAndGet();
                }
            });
            RemoteTrackPublication localSnapshot = new RemoteTrackPublication("local", "local-1",
                "mic", "audio/opus", 1, 1, false, 0, 0);
            room.onNativeTrack(10, localSnapshot, 0);
            room.onNativeTrack(11, localSnapshot, 0);
            room.onNativeTrack(12, localSnapshot, 0);
            if (!"local-1".equals(localTrack.get().sid()) || localUnpublished.get() != 1 ||
                localSubscribed.get() != 1)
                throw new AssertionError("Local track lifecycle event was lost");
            java.util.concurrent.atomic.AtomicReference<SubscribedQualityChange> qualityChange =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onSubscribedQualityUpdated(SubscribedQualityChange change) {
                    qualityChange.set(change);
                }
            });
            SubscribedQuality[] incomingQualities = {new SubscribedQuality(2, true)};
            SubscribedCodec[] incomingCodecs = {new SubscribedCodec("vp8",
                java.util.List.of(incomingQualities))};
            room.onNativeSubscribedQuality(localSnapshot,
                new Participant("participant-1", "local", "name", ""), "local-1", incomingQualities,
                incomingCodecs);
            incomingQualities[0] = null;
            incomingCodecs[0] = null;
            if (!"local".equals(qualityChange.get().participant().identity()) ||
                !qualityChange.get().qualities().get(0).enabled() ||
                !"vp8".equals(qualityChange.get().codecs().get(0).codec()))
                throw new AssertionError("Subscribed quality update was lost");
            java.util.concurrent.atomic.AtomicInteger failureCode =
                new java.util.concurrent.atomic.AtomicInteger(-1);
            room.addListener(new Room.Listener() {
                @Override public void onTrackSubscriptionFailed(RemoteTrackPublication track,
                                                                 int error) {
                    failureCode.set(error);
                }
            });
            room.onNativeTrack(7, new RemoteTrackPublication("remote", "track-1", "mic",
                "audio/opus", 1, 1, false, 0, 0), 2);
            if (failureCode.get() != 2) throw new AssertionError("Subscription failure was lost");
            java.util.concurrent.atomic.AtomicReference<String> roomSid =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onRoomSidChanged(String previousSid, String sid) {
                    if (!previousSid.equals("old")) throw new AssertionError("Wrong previous SID");
                    roomSid.set(sid);
                }
            });
            room.onNativeRoomString(1, "old", "new");
            if (!"new".equals(roomSid.get())) throw new AssertionError("Room SID listener failed");
            java.util.concurrent.atomic.AtomicReference<java.util.Map<String, String>> attributes =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onParticipantAttributesChanged(Participant participant,
                                                                       java.util.Map<String, String> changes) {
                    attributes.set(changes);
                }
            });
            java.util.Map<String, String> incoming = new java.util.HashMap<>();
            incoming.put("removed", "");
            room.onNativeParticipantChange(2, new Participant("sid", "remote", "name", ""),
                "", incoming);
            incoming.clear();
            if (!"".equals(attributes.get().get("removed")))
                throw new AssertionError("Participant attribute changes were not copied");
            int[] publishSources = {1, 2};
            ParticipantPermissions updatedPermissions = new ParticipantPermissions(true, true,
                true, publishSources, false, false, true, false, false, false);
            publishSources[0] = 9;
            if (updatedPermissions.canPublishSources()[0] != 1)
                throw new AssertionError("Permission sources were not copied");
            java.util.concurrent.atomic.AtomicReference<ParticipantPermissions> permissions =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onParticipantPermissionsChanged(Participant participant,
                        ParticipantPermissions previous, ParticipantPermissions current) {
                    permissions.set(current);
                }
            });
            room.onNativePermissionsChanged(new Participant("sid", "remote", "name", ""),
                null, updatedPermissions);
            if (permissions.get() != updatedPermissions)
                throw new AssertionError("Permission listener was not called");
            java.util.concurrent.atomic.AtomicReference<java.util.List<Participant>> speakers =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onActiveSpeakersChanged(java.util.List<Participant> value) {
                    speakers.set(value);
                }
            });
            Participant[] incomingSpeakers = {new Participant("sid", "remote", "name", "")};
            room.onNativeParticipantList(0, incomingSpeakers);
            incomingSpeakers[0] = null;
            if (!"remote".equals(speakers.get().get(0).identity()))
                throw new AssertionError("Speaker list was not copied");
            java.util.concurrent.atomic.AtomicReference<SipDtmf> dtmf =
                new java.util.concurrent.atomic.AtomicReference<>();
            java.util.concurrent.atomic.AtomicReference<DataChannelBufferStatus> buffer =
                new java.util.concurrent.atomic.AtomicReference<>();
            java.util.concurrent.atomic.AtomicReference<EncryptionState> cryptor =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onSipDtmfReceived(SipDtmf value) { dtmf.set(value); }
                @Override public void onDataChannelBufferStatusChanged(DataChannelBufferStatus value) {
                    buffer.set(value);
                }
                @Override public void onEncryptionStateChanged(EncryptionState value) {
                    cryptor.set(value);
                }
            });
            room.onNativeSipDtmf(5, "5", "caller");
            room.onNativeDataChannelBufferStatus(true, 20, 100, 10, false);
            room.onNativeEncryptionState("track", "remote", 1, 0, true, 2, 3);
            if (dtmf.get().code() != 5 || buffer.get().bufferedAmount() != 20 ||
                cryptor.get().keyIndex() != 2)
                throw new AssertionError("Room status listener failed");
            java.util.concurrent.atomic.AtomicReference<RoomSnapshot> moved =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onRoomMoved(RoomSnapshot snapshot) { moved.set(snapshot); }
            });
            room.onNativeRoomSnapshot(1, "room-sid", "new-room", "metadata", true);
            if (!"new-room".equals(moved.get().name()) || !moved.get().recording())
                throw new AssertionError("Room move snapshot was lost");
            java.util.concurrent.atomic.AtomicReference<TranscriptionMessage> transcription =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onTranscriptionReceived(TranscriptionMessage message) {
                    transcription.set(message);
                }
            });
            TranscriptionSegment[] segments = {new TranscriptionSegment("segment", "hello",
                "en", 1, 2, true, 3, 4)};
            room.onNativeTranscription("remote", "track", segments);
            segments[0] = null;
            if (!"hello".equals(transcription.get().segments().get(0).text()))
                throw new AssertionError("Transcription segment was not retained");
            java.util.concurrent.atomic.AtomicReference<MetricsMessage> metrics =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onMetricsReceived(MetricsMessage message) {
                    metrics.set(message);
                }
            });
            java.util.List<MetricSample> samples = new java.util.ArrayList<>();
            samples.add(new MetricSample(10, new MetricTimestamp(1, 2), 0.5f));
            MetricsMessage metricMessage = new MetricsMessage(10, null, "remote",
                java.util.List.of("latency"),
                java.util.List.of(new TimeSeriesMetric(0, 0, 0, 0, samples)),
                java.util.List.of(new EventMetric(0, 0, 0, 0, 10, null, null, null, "ok")));
            room.onNativeMetrics(metricMessage);
            samples.clear();
            if (metrics.get().timeSeries().get(0).samples().size() != 1 ||
                !"ok".equals(metrics.get().events().get(0).metadata()))
                throw new AssertionError("Metrics event was lost or mutated");
            java.util.concurrent.atomic.AtomicReference<DataTrackPublication> dataTrack =
                new java.util.concurrent.atomic.AtomicReference<>();
            java.util.concurrent.atomic.AtomicReference<DataTrackFrame> dataFrame =
                new java.util.concurrent.atomic.AtomicReference<>();
            room.addListener(new Room.Listener() {
                @Override public void onDataTrackPublished(DataTrackPublication track) {
                    dataTrack.set(track);
                }
                @Override public void onDataTrackFrame(DataTrackPublication track,
                                                       DataTrackFrame frame) {
                    dataFrame.set(frame);
                }
            });
            room.onNativeDataTrack(0, "remote", 1, "dt-1", "telemetry", false, true,
                2, "", true, "schema", 1, "", null, false, 0);
            byte[] incomingFrame = {1, 2, 3};
            room.onNativeDataTrack(4, "remote", 1, "dt-1", "telemetry", false, true,
                2, "", true, "schema", 1, "", incomingFrame, true, 123);
            incomingFrame[0] = 9;
            if (!"schema".equals(dataTrack.get().schemaName()) ||
                dataFrame.get().data()[0] != 1 || dataFrame.get().userTimestamp() != 123)
                throw new AssertionError("DataTrack event was not copied");
            MediaDevices.listDevices();
            MediaDevices.listScreenSources();
            if (room.isConnected()) throw new AssertionError("New room is connected");
            String[] allowedTrackSids = {"TR_one"};
            ParticipantTrackPermission permission = new ParticipantTrackPermission("", "viewer",
                false, allowedTrackSids);
            allowedTrackSids[0] = "TR_other";
            if (!"TR_one".equals(permission.allowedTrackSids()[0]))
                throw new AssertionError("Subscription permission was not copied");
            room.setTrackSubscriptionPermissions(false, permission);
            try {
                room.updateRemoteTrackSettings("participant", "track", new RemoteTrackSettings());
                throw new AssertionError("Disconnected track settings update was accepted");
            } catch (LiveKitException expected) { }
            if (room.isRecording()) throw new AssertionError("New room is recording");
            AudioPlaybackStats playback = room.audioPlaybackStats();
            if (playback.queuedFrames() != 0 || playback.playedFrames() != 0)
                throw new AssertionError("New room has playback frames");
            if (room.remoteParticipants().size() != 0) throw new AssertionError("Unexpected participant");
            if (!room.remoteTrackPublications().isEmpty()) throw new AssertionError("Unexpected track");
            if (!room.remoteDataTracks().isEmpty()) throw new AssertionError("Unexpected DataTrack");
            if (room.state() != 3) throw new AssertionError("New room is not disconnected");
            try {
                room.setLocalAttributes(java.util.Map.of("offline", "test"));
            } catch (LiveKitException expected) {
                // Some SDK builds require a connected participant before updating attributes.
            }
            ConnectionOptions options = new ConnectionOptions(true, false, false, 3, 15_000,
                new IceServer[] { new IceServer(new String[] { "stun:example.invalid:3478" }, "u", "p") },
                3, 1, detailedE2ee, context -> 100L);
            if (options.reconnectPolicy().nextDelayMs(new ReconnectContext(1, 1000, 0,
                "wss://example.invalid")) != 100L)
                throw new AssertionError("Reconnect policy was not retained");
            try {
                room.connect("", "", options);
                throw new AssertionError("Empty URL was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 1) throw expected;
            }
            java.util.concurrent.atomic.AtomicBoolean tokenRequested =
                new java.util.concurrent.atomic.AtomicBoolean();
            java.util.concurrent.atomic.AtomicBoolean tokenOptionsValid =
                new java.util.concurrent.atomic.AtomicBoolean();
            TokenFetchOptions tokenOptions = new TokenFetchOptions("room", "name", "identity",
                "metadata", java.util.Map.of("role", "test"), "agent", "agent-metadata",
                "deployment");
            try {
                room.connectWithTokenSource(request -> {
                    tokenRequested.set(true);
                    tokenOptionsValid.set("room".equals(request.roomName()) &&
                        "name".equals(request.participantName()) &&
                        "identity".equals(request.participantIdentity()) &&
                        "metadata".equals(request.participantMetadata()) &&
                        "test".equals(request.participantAttributes().get("role")) &&
                        "agent".equals(request.agentName()) &&
                        "agent-metadata".equals(request.agentMetadata()) &&
                        "deployment".equals(request.deployment()));
                    throw new IllegalStateException("Offline token source");
                }, tokenOptions);
                throw new AssertionError("Failed token source was accepted");
            } catch (LiveKitException expected) {
                if (!tokenRequested.get()) throw new AssertionError("Token source was not called");
                if (!tokenOptionsValid.get())
                    throw new AssertionError("Token fetch options were not forwarded");
            }
            tokenRequested.set(false);
            try {
                room.connectWithTokenSource(request -> {
                    tokenRequested.set(true);
                    throw new IllegalStateException("Offline token source with options");
                }, tokenOptions, new ConnectionOptions(false, true, true, 2, 5000,
                    new IceServer[] {new IceServer(new String[] {"stun:example.invalid:3478"},
                        "u", "p")}, 3, 1, new E2eeOptions(new byte[32], new byte[16])));
                throw new AssertionError("Failed token source with connection options was accepted");
            } catch (LiveKitException expected) {
                if (!tokenRequested.get())
                    throw new AssertionError("Token source with connection options was not called");
            }
            try {
                room.publishDataTrack("offline", 0);
                throw new AssertionError("Disconnected DataTrack publish was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            try {
                room.publishDataTrack(new DataTrackPublishOptions("offline", 8, "custom",
                    "schema", 8, "custom-schema"));
                throw new AssertionError("Disconnected custom DataTrack publish was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            try {
                room.storeDataTrackSchema(new DataTrackSchema("offline", 7, "", new byte[] { 1 }));
            } catch (LiveKitException expected) {
                // Schema storage may require a connected participant.
            }
            try {
                room.createAudioStream("remote", "TR_offline", 4);
                throw new AssertionError("Unavailable audio stream was accepted");
            } catch (LiveKitException expected) {
                // The remote track is absent in an offline room.
            }
            try {
                room.startTrackRecording("remote", "TR_offline", "offline.wav");
                throw new AssertionError("Unavailable remote recording was accepted");
            } catch (LiveKitException expected) {
                // Recording needs a remote track.
            }
            try (TextStreamWriter writer = room.streamText("offline")) {
                if (writer.id().isEmpty()) throw new AssertionError("Text stream has no ID");
                writer.cancel("offline");
            } catch (LiveKitException expected) {
                // A disconnected participant may reject opening the stream.
            }
            try (ByteStreamWriter writer = room.streamBytes("offline", "application/octet-stream", "test")) {
                if (writer.id().isEmpty()) throw new AssertionError("Byte stream has no ID");
                writer.cancel("offline");
            } catch (LiveKitException expected) {
                // A disconnected participant may reject opening the stream.
            }
            try {
                room.createVideoStream("remote", "TR_offline", 4);
                throw new AssertionError("Unavailable video stream was accepted");
            } catch (LiveKitException expected) {
                // The remote track is absent in an offline room.
            }
            try {
                room.subscribeDataTrack("remote", "DT_offline", 8);
                throw new AssertionError("Disconnected DataTrack subscription was accepted");
            } catch (LiveKitException expected) {
                if (expected.status() != 6) throw expected;
            }
            room.registerRpcMethod("echo", invocation -> invocation.payload());
            room.unregisterRpcMethod("echo");
            room.registerTextStreamHandler("offline-text");
            room.unregisterTextStreamHandler("offline-text");
            room.registerByteStreamHandler("offline-bytes");
            room.unregisterByteStreamHandler("offline-bytes");
            try (AudioSource audio = new AudioSource(48_000, 1, 1_000);
                 VideoSource video = new VideoSource(false)) {
                audio.capture(new short[480]);
                video.captureI420(new byte[6], 2, 2, 0);
                try {
                    audio.startCapture();
                    throw new AssertionError("Synthetic audio source started device capture");
                } catch (IllegalStateException expected) { }
                try {
                    video.startCapture();
                    throw new AssertionError("Synthetic video source started device capture");
                } catch (IllegalStateException expected) { }
                try {
                    room.createAudioTrack("offline audio", audio);
                    throw new AssertionError("Disconnected audio track creation was accepted");
                } catch (LiveKitException expected) {
                    // Failed creation must release the source reference.
                }
                try {
                    room.createVideoTrack("offline video", video);
                    throw new AssertionError("Disconnected video track creation was accepted");
                } catch (LiveKitException expected) {
                    // Failed creation must release the source reference.
                }
            }
            try {
                AudioSource.createMicrophone(null, 1, true, true, true);
                throw new AssertionError("Invalid microphone queue was accepted");
            } catch (IllegalArgumentException expected) { }
            try {
                VideoSource.createScreen("", 15, true);
                throw new AssertionError("Empty screen source was accepted");
            } catch (IllegalArgumentException expected) { }
        }
        System.out.println("Java JNI offline smoke passed");
    }
}
