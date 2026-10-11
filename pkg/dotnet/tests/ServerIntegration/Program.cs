using System.Text;
using LiveKit.Client;

var url = Require("LIVEKIT_URL");
var firstToken = Require("LIVEKIT_TOKEN_1");
var secondToken = Require("LIVEKIT_TOKEN_2");

using var first = new Room();
using var second = new Room();
var dataReceived = NewSignal<DataPacket>();
var targetedDataReceived = NewSignal<DataPacket>();
var asyncDataReceived = NewSignal<DataPacket>();
var textReceived = NewSignal<TextMessage>();
var targetedTextReceived = NewSignal<TextMessage>();
var bytesReceived = NewSignal<FileMessage>();
var fileReceived = NewSignal<FileMessage>();
var trackPublished = NewSignal<TrackPublicationEvent>();
var trackSubscribed = NewSignal<TrackStatusChange>();
var videoMetadataReceived = NewSignal<VideoFrameEvent>();
var dataTrackPublished = NewSignal<DataTrackPublicationEvent>();
var firstConnectedState = NewSignal<ConnectionState>();
var streamReceived = NewSignal<TextStreamEvent>();
var byteStreamReceived = NewSignal<ByteStreamEvent>();
var dataTrackEvent = NewSignal<DataTrackFrameReceived>();
var textProgress = NewSignal<StreamProgress>();
var textCompletion = NewSignal<StreamCompletion>();
var byteProgress = NewSignal<StreamProgress>();
var byteCompletion = NewSignal<StreamCompletion>();
var callbackErrors = new List<Exception>();
first.CallbackError += error => { lock (callbackErrors) callbackErrors.Add(error); };
second.CallbackError += error => { lock (callbackErrors) callbackErrors.Add(error); };
first.ConnectionStateChanged += state =>
{
    if (state == ConnectionState.Connected) firstConnectedState.TrySetResult(state);
};
second.DataReceived += packet => dataReceived.TrySetResult(packet);
second.DataReceived += packet =>
{
    if (packet.Topic == "targeted-data") targetedDataReceived.TrySetResult(packet);
    if (packet.Topic == "async-data") asyncDataReceived.TrySetResult(packet);
};
second.TextReceived += message => textReceived.TrySetResult(message);
second.TextReceived += message =>
{
    if (message.Topic == "targeted-text") targetedTextReceived.TrySetResult(message);
};
second.BytesReceived += message => bytesReceived.TrySetResult(message);
second.FileReceived += message =>
{
    if (message.Topic == "targeted-file") fileReceived.TrySetResult(message);
};
second.TrackPublished += value =>
{
    if (value.Track.Name == "synthetic-video") trackPublished.TrySetResult(value);
};
second.TrackSubscriptionStatusChanged += value =>
{
    if (value.Track.Name == "synthetic-video" && value.Value == 2)
        trackSubscribed.TrySetResult(value);
};
second.VideoFrameWithMetadataReceived += value =>
{
    if (value.Frame.Metadata?.FrameId == 7) videoMetadataReceived.TrySetResult(value);
};
second.DataTrackPublished += value =>
{
    if (value.Track.Name == "synthetic-data") dataTrackPublished.TrySetResult(value);
};
second.DataTrackFrameReceived += frame => dataTrackEvent.TrySetResult(frame);
second.RegisterTextStreamHandler("incremental", value =>
{
    if (value.Type == StreamEventType.Chunk) streamReceived.TrySetResult(value);
});
second.RegisterByteStreamHandler("incremental-bytes", value =>
{
    if (value.Type == StreamEventType.Chunk) byteStreamReceived.TrySetResult(value);
});
second.RegisterRpcMethod("echo", request => new RpcReply(request.Payload));
TokenFetchRequest? observedTokenRequest = null;

try
{
    await first.ConnectWithTokenSourceAsync(request =>
    {
        observedTokenRequest = request;
        return new TokenCredentials(url, firstToken);
    }, new TokenSourceRequestOptions
    {
        RoomName = "dotnet-request", ParticipantName = "first-request",
        ParticipantAttributes = new Dictionary<string, string> { ["source"] = "dotnet" },
    }, new ConnectionOptions
    {
        ContinualGatheringPolicy = ContinualGatheringPolicy.GatherContinually,
    }).WaitAsync(TimeSpan.FromSeconds(30));
    await second.ConnectAsync(url, secondToken).WaitAsync(TimeSpan.FromSeconds(30));
}
catch (TimeoutException error)
{
    throw new TimeoutException($"Room connection timed out: " +
        $"first={first.State}, second={second.State}", error);
}
if (!first.IsConnected || !second.IsConnected || first.Sid != second.Sid)
    throw new Exception($"Rooms did not connect to the same LiveKit room: " +
        $"first={first.State}/{first.Sid}, second={second.State}/{second.Sid}");
if (observedTokenRequest?.RoomName != "dotnet-request" ||
    observedTokenRequest.ParticipantName != "first-request" ||
    observedTokenRequest.ParticipantAttributes.GetValueOrDefault("source") != "dotnet")
    throw new Exception("Token source request options were not preserved");
if (first.GetLocalParticipant().Permissions?.CanPublish != true)
    throw new Exception("Local participant permissions snapshot was incorrect");
await firstConnectedState.Task.WaitAsync(TimeSpan.FromSeconds(8));
_ = first.GetAudioPlaybackStats();
await Poll(() => first.GetRemoteParticipants().Any(p => p.Identity == second.LocalIdentity)
    ? first : null, TimeSpan.FromSeconds(8));
if (first.GetRemoteParticipants().All(p => p.Identity != second.LocalIdentity))
    throw new Exception("First room cannot see second participant");
if (first.GetRemoteParticipants().First(p => p.Identity == second.LocalIdentity)
    .Permissions?.CanSubscribe != true)
    throw new Exception("Remote participant permissions snapshot was incorrect");
await Poll(() => second.GetRemoteParticipants().Any(p => p.Identity == first.LocalIdentity)
    ? second : null, TimeSpan.FromSeconds(8));

first.PublishData(Encoding.UTF8.GetBytes("data-probe"), topic: "dotnet");
var packet = await dataReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (packet.Topic != "dotnet" || Encoding.UTF8.GetString(packet.Data) != "data-probe")
    throw new Exception("Data payload changed in transit");
first.PublishData(Encoding.UTF8.GetBytes("targeted-data-probe"), new DataPublishRequest
{
    Topic = "targeted-data", DestinationIdentities = [second.LocalIdentity],
});
var targetedData = await targetedDataReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (Encoding.UTF8.GetString(targetedData.Data) != "targeted-data-probe" ||
    targetedData.ParticipantIdentity != first.LocalIdentity)
    throw new Exception("Targeted data publish options were not preserved");

var asyncPayload = Encoding.UTF8.GetBytes("async-data-probe");
var asyncDestinations = new[] { second.LocalIdentity };
var publishTask = first.PublishDataAsync(asyncPayload, new DataPublishRequest
{
    Topic = "async-data", DestinationIdentities = asyncDestinations,
});
Array.Fill(asyncPayload, (byte)0);
asyncDestinations[0] = first.LocalIdentity;
await publishTask.WaitAsync(TimeSpan.FromSeconds(8));
var asyncPacket = await asyncDataReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (Encoding.UTF8.GetString(asyncPacket.Data) != "async-data-probe" ||
    asyncPacket.ParticipantIdentity != first.LocalIdentity)
    throw new Exception("Async data payload or destinations changed after scheduling");

first.SendText("text-probe", "dotnet");
var text = await textReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (text.Text != "text-probe" || text.Topic != "dotnet")
    throw new Exception("Text payload changed in transit");

first.SendText("targeted-probe", new TextSendRequest
{
    Topic = "targeted-text", DestinationIdentities = [second.LocalIdentity],
    ReplyToStreamId = "parent-stream", ChunkSize = 1024, Compress = true,
    Attributes = new Dictionary<string, string> { ["origin"] = "dotnet" },
});
var targetedText = await targetedTextReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (targetedText.Text != "targeted-probe" ||
    targetedText.ParticipantIdentity != first.LocalIdentity ||
    targetedText.ReplyToStreamId != "parent-stream" ||
    targetedText.Attributes.GetValueOrDefault("origin") != "dotnet")
    throw new Exception("Targeted text options were not preserved");
first.SendBytes([11, 12, 13], new ByteSendRequest
{
    Topic = "targeted-bytes", Name = "probe.bin", DestinationIdentities = [second.LocalIdentity],
    ChunkSize = 1024, Compress = true,
    Attributes = new Dictionary<string, string> { ["origin"] = "dotnet" },
});
var sentBytes = await bytesReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (sentBytes.Topic != "targeted-bytes" || sentBytes.Name != "probe.bin" ||
    !sentBytes.Data.SequenceEqual(new byte[] { 11, 12, 13 }) ||
    sentBytes.Attributes.GetValueOrDefault("origin") != "dotnet")
    throw new Exception("Targeted byte options were not preserved");
var filePath = Path.Combine(Path.GetTempPath(), "livekit-dotnet-" + Guid.NewGuid() + ".bin");
try
{
    await File.WriteAllBytesAsync(filePath, [21, 22, 23]);
    first.SendFile(filePath, new FileSendRequest
    {
        Topic = "targeted-file", DestinationIdentities = [second.LocalIdentity],
        ChunkSize = 1024, Compress = true,
        Attributes = new Dictionary<string, string> { ["origin"] = "dotnet" },
    });
    var sentFile = await fileReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
    if (sentFile.Topic != "targeted-file" ||
        !sentFile.Data.SequenceEqual(new byte[] { 21, 22, 23 }) ||
        sentFile.Attributes.GetValueOrDefault("origin") != "dotnet")
        throw new Exception($"Targeted file options were not preserved: " +
            $"topic={sentFile.Topic}, name={sentFile.Name}, data={Convert.ToHexString(sentFile.Data)}");
}
finally { File.Delete(filePath); }

var streamId = Guid.NewGuid().ToString();
using (var writer = first.StreamText(new TextStreamRequest
{
    Topic = "incremental",
    StreamId = streamId,
    DestinationIdentities = [second.LocalIdentity],
    Attributes = new Dictionary<string, string> { ["origin"] = "dotnet" },
    TotalSize = (ulong)Encoding.UTF8.GetByteCount("stream-probe"),
    ChunkSize = 1024,
    OnProgress = value => textProgress.TrySetResult(value),
    OnComplete = value => textCompletion.TrySetResult(value),
}))
{
    if (writer.Id != streamId) throw new Exception("Custom stream ID was not preserved");
    var info = writer.GetInfo();
    if (info.Kind != StreamWriterKind.Text || info.StreamId != streamId ||
        info.Topic != "incremental" || info.TotalSize != 12 ||
        info.Attributes.GetValueOrDefault("origin") != "dotnet")
        throw new Exception("Text writer info snapshot was incorrect");
    writer.Write("stream-probe");
    writer.Close();
}
var chunk = await streamReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (Encoding.UTF8.GetString(chunk.ContentBytes) != "stream-probe")
    throw new Exception("Incremental stream chunk changed in transit");
if ((await textProgress.Task.WaitAsync(TimeSpan.FromSeconds(8))).BytesSent != 12 ||
    (await textCompletion.Task.WaitAsync(TimeSpan.FromSeconds(8))).Status !=
    StreamCompletionStatus.Completed)
    throw new Exception("Text stream progress or completion callback was incorrect");
using (var writer = first.StreamBytes(new ByteStreamRequest
{
    Topic = "incremental-bytes",
    Name = "payload.bin",
    StreamId = Guid.NewGuid().ToString(),
    DestinationIdentities = [second.LocalIdentity],
    TotalSize = 3,
    ChunkSize = 1024,
    OnProgress = value => byteProgress.TrySetResult(value),
    OnComplete = value => byteCompletion.TrySetResult(value),
}))
{
    var info = writer.GetInfo();
    if (info.Kind != StreamWriterKind.Bytes || info.Name != "payload.bin" ||
        info.TotalSize != 3)
        throw new Exception("Byte writer info snapshot was incorrect");
    writer.Write([8, 9, 10]);
    writer.Close();
}
var byteChunk = await byteStreamReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (byteChunk.Name != "payload.bin" ||
    !byteChunk.Content.SequenceEqual(new byte[] { 8, 9, 10 }))
    throw new Exception("Incremental byte stream changed in transit");
if ((await byteProgress.Task.WaitAsync(TimeSpan.FromSeconds(8))).BytesSent != 3 ||
    (await byteCompletion.Task.WaitAsync(TimeSpan.FromSeconds(8))).Status !=
    StreamCompletionStatus.Completed)
    throw new Exception("Byte stream progress or completion callback was incorrect");
var cancelled = NewSignal<StreamCompletion>();
using (var abandoned = first.StreamText(new TextStreamRequest
{
    Topic = "incremental",
    StreamId = Guid.NewGuid().ToString(),
    DestinationIdentities = [second.LocalIdentity],
    OnComplete = value => cancelled.TrySetResult(value),
})) { }
var cancelledResult = await cancelled.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (cancelledResult.Status != StreamCompletionStatus.Cancelled)
    throw new Exception("Destroyed writer did not report cancellation");

var syncRpc = first.PerformRpc(second.LocalIdentity, "echo", "sync-probe");
if (!syncRpc.IsSuccess || syncRpc.Payload != "sync-probe")
    throw new Exception($"Synchronous RPC failed: {syncRpc.ErrorCode} {syncRpc.ErrorMessage}");
var asyncRpc = await first.PerformRpcAsync(second.LocalIdentity, "echo", "async-probe")
    .WaitAsync(TimeSpan.FromSeconds(8));
if (!asyncRpc.IsSuccess || asyncRpc.Payload != "async-probe")
    throw new Exception($"Asynchronous RPC failed: {asyncRpc.ErrorCode} {asyncRpc.ErrorMessage}");

using var source = new VideoSource();
source.CaptureI420(new byte[6], 2, 2);
if (source.Dimensions != (2u, 2u))
    throw new Exception("External video source dimensions were incorrect");
using var track = first.CreateVideoTrack("synthetic-video", source);
track.Publish(new TrackPublishRequest
{
    UserTimestampMetadata = true, FrameIdMetadata = true, UserDataMetadata = true,
    VideoEncoding = new VideoEncodingOptions(300_000, 15),
    DegradationPreference = VideoDegradationPreference.Balanced,
});
var publicationEvent = await trackPublished.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (publicationEvent.Track.Name != "synthetic-video" ||
    publicationEvent.Participant.Identity != first.LocalIdentity)
    throw new Exception("Track publication callback was incorrect");
var publication = await Poll(() => second.GetRemoteParticipants()
    .SelectMany(p => p.TrackPublications ?? [])
    .FirstOrDefault(p => p.Name == "synthetic-video"), TimeSpan.FromSeconds(8));
if ((await trackSubscribed.Task.WaitAsync(TimeSpan.FromSeconds(8))).Track.Sid != publication.Sid)
    throw new Exception("Track subscription status callback was incorrect");
using (var video = await Poll(() =>
{
    try { return second.CreateVideoStream(first.LocalIdentity, publication.Sid); }
    catch (LiveKitException) { return null; }
}, TimeSpan.FromSeconds(8)))
{
    VideoFrame? frame = null;
    var until = DateTime.UtcNow.AddSeconds(8);
    while (frame is null && DateTime.UtcNow < until)
    {
        source.CaptureI420(new byte[6], 2, 2, new VideoFrameMetadata(123, 7, [1, 2, 3]));
        frame = video.Read(300);
    }
    if (frame is null || frame.Width != 2 || frame.Height != 2 ||
        frame.Metadata?.FrameId != 7 || frame.Metadata?.UserTimestampUs != 123 ||
        !frame.Metadata.UserData!.SequenceEqual(new byte[] { 1, 2, 3 }))
        throw new Exception("Video frame or metadata was not received");
    var subscribedTrack = await Poll(() => second.GetRemoteParticipants()
        .SelectMany(p => p.TrackPublications ?? [])
        .FirstOrDefault(p => p.Sid == publication.Sid)?.SubscribedTrack,
        TimeSpan.FromSeconds(8));
    if (subscribedTrack.Sid != publication.Sid || subscribedTrack.Width != 2 ||
        subscribedTrack.Height != 2)
        throw new Exception("Subscribed track snapshot was incorrect");
    var frameEvent = await videoMetadataReceived.Task.WaitAsync(TimeSpan.FromSeconds(8));
    if (frameEvent.TrackSid != publication.Sid ||
        frameEvent.Frame.Metadata?.UserTimestampUs != 123 ||
        !frameEvent.Frame.Metadata.UserData!.SequenceEqual(new byte[] { 1, 2, 3 }))
        throw new Exception("Video metadata callback was incorrect");
    _ = second.GetRemoteRtcStats(first.LocalIdentity, publication.Sid);
}
track.UpdateDegradationPreference(VideoDegradationPreference.Balanced);
track.UpdateVideoEncoding(300_000, 15);
track.Unpublish();

using (var screenSource = new VideoSource(isScreencast: true))
{
    screenSource.CaptureI420(new byte[6], 2, 2);
    using var screenTrack = first.CreateVideoTrack("synthetic-screen", screenSource);
    screenTrack.PublishScreenShareVideo(new TrackPublishRequest
    {
        VideoEncoding = new VideoEncodingOptions(200_000, 15),
    });
    var screenPublication = await Poll(() => second.GetRemoteParticipants()
        .SelectMany(p => p.TrackPublications ?? [])
        .FirstOrDefault(p => p.Name == "synthetic-screen"), TimeSpan.FromSeconds(8));
    if (screenPublication.Source != 3)
        throw new Exception("Screen-share video was published with the wrong source");
    screenTrack.Unpublish();
}

first.StoreDataTrackSchema("example-schema", DataTrackSchemaEncoding.Custom,
    Encoding.UTF8.GetBytes("{}"), "example/schema-v1");
using var dataTrack = first.PublishDataTrack("synthetic-data",
    DataTrackFrameEncoding.Custom, schemaName: "example-schema",
    schemaEncoding: DataTrackSchemaEncoding.Custom,
    customFrameEncoding: "example/binary-v1",
    customSchemaEncoding: "example/schema-v1");
var localDataTrackInfo = dataTrack.GetInfo();
if (!localDataTrackInfo.IsPublished || localDataTrackInfo.Name != "synthetic-data" ||
    localDataTrackInfo.FrameEncoding != DataTrackFrameEncoding.Custom ||
    localDataTrackInfo.CustomFrameEncoding != "example/binary-v1" ||
    localDataTrackInfo.SchemaName != "example-schema")
    throw new Exception("Local DataTrack info was incorrect");
var dataTrackPublicationEvent = await dataTrackPublished.Task.WaitAsync(TimeSpan.FromSeconds(8));
if (dataTrackPublicationEvent.Track.Name != "synthetic-data" ||
    dataTrackPublicationEvent.Participant.Identity != first.LocalIdentity)
    throw new Exception("DataTrack publication callback was incorrect");
var remoteDataTrack = await Poll(() => second.GetRemoteDataTracks()
    .FirstOrDefault(t => t.Name == "synthetic-data"), TimeSpan.FromSeconds(8));
if (remoteDataTrack.FrameEncoding != DataTrackFrameEncoding.Custom ||
    remoteDataTrack.CustomFrameEncoding != "example/binary-v1" ||
    remoteDataTrack.SchemaName != "example-schema" ||
    remoteDataTrack.SchemaEncoding != DataTrackSchemaEncoding.Custom ||
    remoteDataTrack.CustomSchemaEncoding != "example/schema-v1")
    throw new Exception("Custom DataTrack encoding or schema was not preserved");
var remoteSchema = await Poll(() =>
{
    try
    {
        return second.GetDataTrackSchema(first.LocalIdentity, "example-schema",
            DataTrackSchemaEncoding.Custom, "example/schema-v1");
    }
    catch (DataTrackException) { return null; }
}, TimeSpan.FromSeconds(8));
if (remoteSchema.CustomEncoding != "example/schema-v1" ||
    Encoding.UTF8.GetString(remoteSchema.Definition) != "{}")
    throw new Exception("Custom DataTrack schema definition was not preserved");
using (var reader = second.SubscribeDataTrack(first.LocalIdentity, remoteDataTrack.Sid))
{
    DataTrackFrame? frame = null;
    var until = DateTime.UtcNow.AddSeconds(8);
    while (frame is null && DateTime.UtcNow < until)
    {
        dataTrack.Push([4, 5, 6], 99);
        frame = reader.Read(300);
    }
    if (frame is null || frame.UserTimestamp != 99 ||
        !frame.Data.SequenceEqual(new byte[] { 4, 5, 6 }))
        throw new Exception("DataTrack frame was not received");
    var callbackFrame = await dataTrackEvent.Task.WaitAsync(TimeSpan.FromSeconds(8));
    if (callbackFrame.TrackSid != remoteDataTrack.Sid ||
        !callbackFrame.Frame.Data.SequenceEqual(new byte[] { 4, 5, 6 }))
        throw new Exception("DataTrack frame callback was not delivered");
}
dataTrack.Unpublish();

second.UnregisterTextStreamHandler("incremental");
second.UnregisterByteStreamHandler("incremental-bytes");
second.UnregisterRpcMethod("echo");
await first.DisconnectAsync().WaitAsync(TimeSpan.FromSeconds(8));
await second.DisconnectAsync().WaitAsync(TimeSpan.FromSeconds(8));
if (first.LastDisconnectReason != DisconnectReason.ClientInitiated)
    throw new Exception("Client disconnect reason was incorrect");
lock (callbackErrors)
    if (callbackErrors.Count != 0) throw new AggregateException(callbackErrors);

var sharedKey = System.Security.Cryptography.RandomNumberGenerator.GetBytes(32);
try
{
    var encryptedOptions = new ConnectionOptions
    {
        E2ee = new E2eeConnectionOptions
        {
            SharedKey = sharedKey, RatchetWindowSize = 16,
            FailureTolerance = 2, KeyRingSize = 4,
            KeyDerivation = E2eeKeyDerivation.HkdfSha256,
        },
    };
    using var encryptedFirst = new Room();
    using var encryptedSecond = new Room();
    await encryptedFirst.ConnectAsync(url, firstToken, encryptedOptions)
        .WaitAsync(TimeSpan.FromSeconds(30));
    await encryptedSecond.ConnectWithTokenSourceAsync(
        _ => new TokenCredentials(url, secondToken),
        new TokenSourceRequestOptions(), encryptedOptions)
        .WaitAsync(TimeSpan.FromSeconds(30));
    if (!encryptedFirst.IsConnected || !encryptedSecond.IsConnected ||
        !encryptedFirst.IsE2eeConfigured || !encryptedFirst.IsE2eeEnabled ||
        !encryptedSecond.IsE2eeConfigured || !encryptedSecond.IsE2eeEnabled)
        throw new Exception($"E2EE connection options were not applied: " +
            $"first={encryptedFirst.State}/{encryptedFirst.IsE2eeConfigured}/{encryptedFirst.IsE2eeEnabled}, " +
            $"second={encryptedSecond.State}/{encryptedSecond.IsE2eeConfigured}/{encryptedSecond.IsE2eeEnabled}");
    var exportedKey = encryptedFirst.ExportE2eeSharedKey();
    try
    {
        if (exportedKey.Length == 0)
            throw new Exception("E2EE shared key was not installed");
    }
    finally { System.Security.Cryptography.CryptographicOperations.ZeroMemory(exportedKey); }
    await encryptedFirst.DisconnectAsync().WaitAsync(TimeSpan.FromSeconds(8));
    await encryptedSecond.DisconnectAsync().WaitAsync(TimeSpan.FromSeconds(8));
}
finally { System.Security.Cryptography.CryptographicOperations.ZeroMemory(sharedKey); }
Console.WriteLine("Server .NET binding integration passed");

static string Require(string name) => Environment.GetEnvironmentVariable(name)
    ?? throw new InvalidOperationException($"Set {name}");
static TaskCompletionSource<T> NewSignal<T>() =>
    new(TaskCreationOptions.RunContinuationsAsynchronously);
static async Task<T> Poll<T>(Func<T?> value, TimeSpan timeout) where T : class
{
    var until = DateTime.UtcNow + timeout;
    while (DateTime.UtcNow < until)
    {
        var current = value();
        if (current is not null) return current;
        await Task.Delay(100);
    }
    throw new TimeoutException($"Timed out waiting for {typeof(T).Name}");
}
