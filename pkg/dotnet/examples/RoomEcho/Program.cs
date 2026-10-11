using System;
using LiveKit.Client;

var options = new Dictionary<string, string>();
for (var i = 0; i < args.Length; i += 2)
{
    if (!args[i].StartsWith("--", StringComparison.Ordinal) || i + 1 == args.Length)
        throw new ArgumentException("Options must be --name value pairs");
    options[args[i][2..]] = args[i + 1];
}
var url = options.GetValueOrDefault("url") ?? Environment.GetEnvironmentVariable("LIVEKIT_URL")
    ?? throw new InvalidOperationException("Set LIVEKIT_URL or --url");
var token = options.GetValueOrDefault("token") ?? Environment.GetEnvironmentVariable("LIVEKIT_TOKEN")
    ?? throw new InvalidOperationException("Set LIVEKIT_TOKEN or --token");
var seconds = double.Parse(options.GetValueOrDefault("seconds") ?? "10",
    System.Globalization.CultureInfo.InvariantCulture);
if (seconds <= 0 || seconds > 3600) throw new ArgumentOutOfRangeException("seconds");

using var room = new Room();
room.Connected += () => Console.WriteLine("Connected");
room.ParticipantConnected += participant => Console.WriteLine($"Joined: {participant.Identity}");
room.DataReceived += packet => Console.WriteLine(
    $"{packet.ParticipantIdentity} [{packet.Topic}]: {System.Text.Encoding.UTF8.GetString(packet.Data)}");
room.TextReceived += message => Console.WriteLine($"{message.ParticipantIdentity}: {message.Text}");
room.ChatReceived += message => Console.WriteLine($"chat {message.ParticipantIdentity}: {message.Message}");
room.CallbackError += error => Console.Error.WriteLine(error);
room.RegisterRpcMethod("echo", request => new RpcReply(request.Payload));

await room.ConnectAsync(url, token).WaitAsync(TimeSpan.FromSeconds(30));
Console.WriteLine($"Connected: room={room.Name} identity={room.LocalIdentity}");
await room.PublishDataAsync(System.Text.Encoding.UTF8.GetBytes("hello from .NET"),
    topic: "greeting");
await Task.Delay(TimeSpan.FromSeconds(seconds));
await room.DisconnectAsync();
