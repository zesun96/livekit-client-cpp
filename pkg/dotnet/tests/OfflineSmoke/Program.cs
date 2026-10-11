using System;
using LiveKit.Client;

if (string.IsNullOrWhiteSpace(Room.Version)) throw new Exception("Missing native SDK version");

using var room = new Room();
if (room.State != ConnectionState.Disconnected || room.IsConnected)
    throw new Exception("New room should be disconnected");
if (room.GetRemoteParticipants().Count != 0)
    throw new Exception("New room should have no remote participants");
if (room.GetRemoteDataTracks().Count != 0)
    throw new Exception("New room should have no remote DataTracks");
_ = room.GetLocalParticipant();
try
{
    room.SetLocalAttributes(new Dictionary<string, string>());
    throw new Exception("Updating attributes while disconnected should fail");
}
catch (LiveKitException error) when (error.Status != 0) { }

try
{
    using var writer = room.StreamText("offline-test");
    throw new Exception("Opening a stream while disconnected should fail");
}
catch (LiveKitException error) when (error.Status != 0) { }
_ = Diagnostics.GetLogOptions();
var previousTraceOptions = Diagnostics.GetTraceOptions();
try
{
    Diagnostics.SetLogCallback(_ => { });
    Diagnostics.SetTraceOptions(new TraceOptions(true, TraceCategory.Lifecycle));
    Diagnostics.SetTraceCallback(_ => { });
    var options = Diagnostics.GetTraceOptions();
    if (!options.Enabled || options.Categories != TraceCategory.Lifecycle)
        throw new Exception("Trace options were not preserved");
}
finally
{
    Diagnostics.SetTraceCallback(null);
    Diagnostics.SetLogCallback(null);
    Diagnostics.SetTraceOptions(previousTraceOptions);
}
if (room.IsE2eeConfigured || room.IsE2eeEnabled)
    throw new Exception("New room should not have E2EE configured");
_ = MediaDevices.ListDevices();
_ = MediaDevices.ListScreenSources();

try
{
    room.PublishData([1, 2, 3]);
    throw new Exception("Publishing while disconnected should fail");
}
catch (LiveKitException error) when (error.Status != 0) { }

try
{
    room.SendText("hello", "offline-test");
    throw new Exception("Sending text while disconnected should fail");
}
catch (LiveKitException error) when (error.Status != 0) { }

if (room.PerformRpc("nobody", "offline-test", "{}").IsSuccess)
    throw new Exception("RPC while disconnected should return an error result");
var asyncRpc = await room.PerformRpcAsync("nobody", "offline-test", "{}", 1000)
    .WaitAsync(TimeSpan.FromSeconds(5));
if (asyncRpc.IsSuccess)
    throw new Exception("Async RPC while disconnected should return an error result");
room.RegisterRpcMethod("offline.echo", request => new RpcReply(request.Payload));
room.UnregisterRpcMethod("offline.echo");
room.RegisterTextStreamHandler("offline.text", _ => { });
room.RegisterByteStreamHandler("offline.bytes", _ => { });
room.UnregisterTextStreamHandler("offline.text");
room.UnregisterByteStreamHandler("offline.bytes");

using (var audio = new AudioSource())
{
    audio.Capture(new short[480]);
}

using (var video = new VideoSource())
{
    video.CaptureI420(new byte[6], 2, 2);
    video.CaptureI420(new byte[6], 2, 2,
        new VideoFrameMetadata(123, 7, [1, 2, 3]));
}

room.Dispose();
try
{
    _ = room.State;
    throw new Exception("Disposed room should reject operations");
}
catch (ObjectDisposedException) { }

Console.WriteLine("Offline .NET binding smoke passed");
