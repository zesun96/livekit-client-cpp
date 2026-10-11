using LiveKit.Client;

var command = args.FirstOrDefault() ?? "help";
var options = new ExampleOptions(args.Skip(1).ToArray());
switch (command)
{
    case "media-devices":
        foreach (var device in MediaDevices.ListDevices())
            Console.WriteLine($"{device.Kind}{(device.IsDefault ? " [default]" : "")}: {device.Label}\n  {device.Id}");
        foreach (var source in MediaDevices.ListScreenSources())
            Console.WriteLine($"{source.Kind}: {source.Label} ({source.Width}x{source.Height})\n  {source.Id}");
        break;
    case "room-events": await MessagingExamples.RoomEvents(options); break;
    case "data-transfer": await MessagingExamples.DataTransfer(options); break;
    case "stream-writer": await MessagingExamples.StreamWriter(options); break;
    case "token-source": await MessagingExamples.TokenSource(options); break;
    case "rpc-receiver": await MessagingExamples.RpcReceiver(options); break;
    case "rpc-caller": await MessagingExamples.RpcCaller(options); break;
    case "data-track-schema": await MessagingExamples.DataTrackSchema(options); break;
    case "e2ee": await MessagingExamples.E2ee(options); break;
    case "publish-audio": await MediaExamples.PublishAudio(options); break;
    case "publish-video": await MediaExamples.PublishVideo(options); break;
    case "publish-microphone": await MediaExamples.PublishDeviceAudio(options, false); break;
    case "publish-system-audio": await MediaExamples.PublishDeviceAudio(options, true); break;
    case "publish-camera": await MediaExamples.PublishDeviceVideo(options, false); break;
    case "publish-screen": await MediaExamples.PublishDeviceVideo(options, true); break;
    case "receive-media": await MediaExamples.ReceiveMedia(options); break;
    case "receive-media-events": await MediaExamples.ReceiveMediaEvents(options); break;
    case "record-remote": await MediaExamples.RecordRemote(options); break;
    case "help":
        Console.WriteLine("Run: dotnet run --project pkg/dotnet/examples/FeatureExamples -- <scenario> [--seconds N] [--file PATH] [--destination ID] [--output-dir PATH] [--device-id ID] [--source-id ID]");
        Console.WriteLine("Scenarios: media-devices, room-events, data-transfer, stream-writer, token-source, rpc-receiver, rpc-caller, data-track-schema, e2ee, publish-audio, publish-video, publish-microphone, publish-system-audio, publish-camera, publish-screen, receive-media, receive-media-events, record-remote");
        break;
    default: throw new ArgumentException($"Unknown scenario: {command}");
}
