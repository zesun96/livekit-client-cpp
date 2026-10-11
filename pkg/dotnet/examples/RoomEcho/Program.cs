using System;
using LiveKit.Client;

var url = Environment.GetEnvironmentVariable("LIVEKIT_URL")
    ?? throw new InvalidOperationException("Set LIVEKIT_URL");
var token = Environment.GetEnvironmentVariable("LIVEKIT_TOKEN")
    ?? throw new InvalidOperationException("Set LIVEKIT_TOKEN");

using var room = new Room();
room.Connected += () => Console.WriteLine("Connected");
room.ParticipantConnected += participant => Console.WriteLine($"Joined: {participant.Identity}");
room.DataReceived += packet => Console.WriteLine(
    $"{packet.ParticipantIdentity} [{packet.Topic}]: {System.Text.Encoding.UTF8.GetString(packet.Data)}");
room.TextReceived += message => Console.WriteLine($"{message.ParticipantIdentity}: {message.Text}");
room.ChatReceived += message => Console.WriteLine($"chat {message.ParticipantIdentity}: {message.Message}");
room.CallbackError += error => Console.Error.WriteLine(error);
room.RegisterRpcMethod("echo", request => new RpcReply(request.Payload));

room.Connect(url, token);
Console.WriteLine($"Local identity: {room.LocalIdentity}");
room.PublishData(System.Text.Encoding.UTF8.GetBytes("hello from .NET"), topic: "greeting");
room.SendText("hello from .NET", topic: "greeting");
Console.WriteLine("Press Enter to disconnect.");
Console.ReadLine();
room.Disconnect();
