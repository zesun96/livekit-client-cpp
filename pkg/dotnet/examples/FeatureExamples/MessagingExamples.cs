using System.Text;
using LiveKit.Client;

internal static class MessagingExamples
{
    internal static async Task RoomEvents(ExampleOptions options)
    {
        using var room = new Room();
        room.CallbackError += error => Console.Error.WriteLine(error);
        room.ConnectionStateChanged += state => Console.WriteLine($"state: {state}");
        room.ParticipantConnected += participant => Console.WriteLine($"joined: {participant.Identity}");
        room.ParticipantDisconnected += participant => Console.WriteLine($"left: {participant.Identity}");
        room.TrackPublished += value => Console.WriteLine($"track published: {value.Track.Sid}");
        room.TrackSubscribed += value => Console.WriteLine($"track subscribed: {value.Track.Sid}");
        room.TrackUnsubscribed += value => Console.WriteLine($"track unsubscribed: {value.Track.Sid}");
        room.DataReceived += value => Console.WriteLine($"data [{value.Topic}]: {value.Data.Length} bytes");
        room.ChatReceived += value => Console.WriteLine($"chat: {value.Message}");
        room.TextReceived += value => Console.WriteLine($"text [{value.Topic}]: {value.Text}");
        room.BytesReceived += value => Console.WriteLine($"bytes [{value.Topic}]: {value.Data.Length}");
        room.FileReceived += value => Console.WriteLine($"file: {value.Name}");
        room.RegisterTextStreamHandler("example-events", value =>
            Console.WriteLine($"text stream {value.Type}: {value.StreamId}"));
        room.RegisterByteStreamHandler("example-events", value =>
            Console.WriteLine($"byte stream {value.Type}: {value.StreamId}"));
        await room.ConnectAsync(options.Url, options.Token).WaitAsync(TimeSpan.FromSeconds(30));
        Console.WriteLine($"Listening as {room.LocalIdentity}");
        await Task.Delay(options.Duration(30));
        await room.DisconnectAsync();
    }

    internal static async Task DataTransfer(ExampleOptions options)
    {
        using var room = await options.Connect();
        room.SendText("hello from .NET", "text-transfer");
        room.SendBytes(Encoding.UTF8.GetBytes("livekit\0dotnet"), "byte-transfer", name: "payload.bin");
        if (options.Get("file") is { } path)
        {
            room.SendFile(path, "file-transfer");
            Console.WriteLine($"Sent file {path}");
        }
        await room.PublishDataAsync(Encoding.UTF8.GetBytes("hello"), topic: "data-transfer");
        Console.WriteLine("Sent text, bytes, and reliable data");
    }

    internal static async Task StreamWriter(ExampleOptions options)
    {
        using var room = await options.Connect();
        using var writer = room.StreamText(new TextStreamRequest
        {
            Topic = "dotnet-incremental", TotalSize = 11,
            Attributes = new Dictionary<string, string> { ["source"] = "dotnet" },
            OnComplete = result => Console.WriteLine($"stream completed: {result.Status}"),
        });
        Console.WriteLine($"Stream ID: {writer.GetInfo().StreamId}");
        foreach (var chunk in new[] { "hello", " ", "world" }) writer.Write(chunk);
        writer.Close();
        await Task.Delay(200);
    }

    internal static async Task TokenSource(ExampleOptions options)
    {
        using var room = new Room();
        await room.ConnectWithTokenSourceAsync(request =>
        {
            Console.WriteLine($"Fetch token; forced refresh: {request.ForceRefresh}");
            return new TokenCredentials(options.Url, options.Token);
        }).WaitAsync(TimeSpan.FromSeconds(30));
        Console.WriteLine($"Connected as {room.LocalIdentity}");
        await Task.Delay(options.Duration());
    }

    internal static async Task RpcReceiver(ExampleOptions options)
    {
        using var room = new Room();
        room.RegisterRpcMethod("example.echo", request => new RpcReply(request.Payload));
        await room.ConnectAsync(options.Url, options.Token).WaitAsync(TimeSpan.FromSeconds(30));
        Console.WriteLine($"RPC receiver identity: {room.LocalIdentity}");
        await Task.Delay(options.Duration(30));
    }

    internal static async Task RpcCaller(ExampleOptions options)
    {
        using var room = await options.Connect();
        var response = await room.PerformRpcAsync(options.Require("destination"),
            "example.echo", options.Get("payload") ?? "hello");
        if (!response.IsSuccess)
            throw new Exception($"RPC failed ({response.ErrorCode}): {response.ErrorMessage}");
        Console.WriteLine($"RPC response: {response.Payload}");
    }

    internal static async Task DataTrackSchema(ExampleOptions options)
    {
        using var room = await options.Connect();
        const string name = "example.telemetry.v1";
        room.StoreDataTrackSchema(name, DataTrackSchemaEncoding.JsonSchema,
            Encoding.UTF8.GetBytes("{\"type\":\"object\",\"properties\":{\"temperature\":{\"type\":\"number\"}}}"));
        using var track = room.PublishDataTrack("example-telemetry",
            DataTrackFrameEncoding.Json, name, DataTrackSchemaEncoding.JsonSchema);
        await Task.Delay(500);
        track.Push(Encoding.UTF8.GetBytes("{\"temperature\":21.5}"), 1);
        await Task.Delay(1000);
        track.Unpublish();
        Console.WriteLine("Published schema-backed DataTrack frame");
    }

    internal static async Task E2ee(ExampleOptions options)
    {
        var keyText = Environment.GetEnvironmentVariable("LIVEKIT_E2EE_KEY")
            ?? throw new ArgumentException("Set LIVEKIT_E2EE_KEY (at least 32 UTF-8 bytes)");
        var key = Encoding.UTF8.GetBytes(keyText);
        if (key.Length < 32) throw new ArgumentException("E2EE key must be at least 32 bytes");
        try
        {
            using var room = new Room();
            await room.ConnectAsync(options.Url, options.Token,
                new ConnectionOptions { E2ee = new E2eeConnectionOptions { SharedKey = key } })
                .WaitAsync(TimeSpan.FromSeconds(30));
            Console.WriteLine($"E2EE configured={room.IsE2eeConfigured}, enabled={room.IsE2eeEnabled}");
            await Task.Delay(options.Duration());
        }
        finally { System.Security.Cryptography.CryptographicOperations.ZeroMemory(key); }
    }
}
