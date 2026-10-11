using LiveKit.Client;

internal static class MediaExamples
{
    internal static async Task PublishAudio(ExampleOptions options)
    {
        using var source = new AudioSource();
        using var room = await options.Connect();
        source.Capture(new short[480]);
        using var track = room.CreateAudioTrack("dotnet-tone", source);
        track.Publish(new TrackPublishRequest { Source = TrackSource.Microphone });
        var count = (int)(options.Duration().TotalSeconds * 100);
        for (var frame = 0; frame < count; frame++)
        {
            var samples = new short[480];
            for (var i = 0; i < samples.Length; i++)
                samples[i] = (short)(6000 * Math.Sin(2 * Math.PI * 440 * (frame * 480 + i) / 48000));
            source.Capture(samples);
            await Task.Delay(10);
        }
        track.Unpublish();
        Console.WriteLine($"Published {count} synthetic audio frames");
    }

    internal static async Task PublishVideo(ExampleOptions options)
    {
        const int width = 160, height = 90;
        using var source = new VideoSource();
        using var room = await options.Connect();
        var frame = new byte[width * height * 3 / 2];
        Array.Fill(frame, (byte)128, width * height, width * height / 2);
        source.CaptureI420(frame, width, height);
        using var track = room.CreateVideoTrack("dotnet-video", source);
        track.Publish(new TrackPublishRequest { Source = TrackSource.Camera });
        var count = (int)(options.Duration().TotalSeconds * 30);
        for (var index = 0; index < count; index++)
        {
            Array.Fill(frame, (byte)(40 + index % 180), 0, width * height);
            source.CaptureI420(frame, width, height);
            await Task.Delay(33);
        }
        track.Unpublish();
        Console.WriteLine($"Published {count} synthetic I420 frames");
    }

    internal static async Task PublishDeviceAudio(ExampleOptions options, bool systemAudio)
    {
        using var source = systemAudio ? AudioSource.CreateSystemAudio(options.Get("device-id")) :
            AudioSource.CreateMicrophone(options.Get("device-id"));
        using var room = await options.Connect();
        using var track = room.CreateAudioTrack(systemAudio ? "dotnet-system-audio" : "dotnet-microphone", source);
        source.StartCapture();
        try
        {
            track.Publish(new TrackPublishRequest
            {
                Source = systemAudio ? TrackSource.ScreenShareAudio : TrackSource.Microphone,
            });
            Console.WriteLine($"Capturing {source.DeviceId}");
            await Task.Delay(options.Duration(10));
            track.Unpublish();
        }
        finally { source.StopCapture(); }
    }

    internal static async Task PublishDeviceVideo(ExampleOptions options, bool screen)
    {
        var sourceId = options.Get("source-id") ??
            (screen ? MediaDevices.ListScreenSources().FirstOrDefault(s => s.Kind == ScreenSourceKind.Monitor)?.Id : null);
        if (screen && sourceId is null)
            throw new ArgumentException("No monitor found; set --source-id");
        using var source = screen ? VideoSource.CreateScreen(sourceId!) :
            VideoSource.CreateCamera(options.Get("device-id"));
        using var room = await options.Connect();
        using var track = room.CreateVideoTrack(screen ? "dotnet-screen" : "dotnet-camera", source);
        source.StartCapture();
        try
        {
            track.Publish(new TrackPublishRequest
            {
                Source = screen ? TrackSource.ScreenShare : TrackSource.Camera,
            });
            await Task.Delay(options.Duration(10));
            track.Unpublish();
        }
        finally { source.StopCapture(); }
    }

    internal static async Task ReceiveMedia(ExampleOptions options)
    {
        using var room = await options.Connect();
        Console.WriteLine($"Receiving media as {room.LocalIdentity}");
        var deadline = DateTime.UtcNow + options.Duration(15);
        var opened = new HashSet<string>();
        var readers = new List<(string Sid, AudioStream? Audio, VideoStream? Video)>();
        try
        {
            while (DateTime.UtcNow < deadline)
            {
                foreach (var participant in room.GetRemoteParticipants())
                foreach (var track in participant.TrackPublications ?? [])
                {
                    if (!track.IsSubscribed || !opened.Add(track.Sid)) continue;
                    if (track.Kind == 1)
                        readers.Add((track.Sid, room.CreateAudioStream(participant.Identity, track.Sid), null));
                    else if (track.Kind == 2)
                        readers.Add((track.Sid, null, room.CreateVideoStream(participant.Identity, track.Sid)));
                }
                foreach (var (sid, audio, video) in readers)
                {
                    if (audio?.Read() is { } audioFrame)
                        Console.WriteLine($"Audio {sid}: {audioFrame.Samples.Length} samples");
                    if (video?.Read() is { } videoFrame)
                        Console.WriteLine($"Video {sid}: {videoFrame.Width}x{videoFrame.Height}");
                }
                await Task.Delay(100);
            }
        }
        finally
        {
            foreach (var (_, audio, video) in readers) { audio?.Dispose(); video?.Dispose(); }
        }
    }

    internal static async Task ReceiveMediaEvents(ExampleOptions options)
    {
        using var room = new Room();
        room.AudioFrameReceived += value => Console.WriteLine($"Audio {value.TrackSid}: {value.Frame.Samples.Length} samples");
        room.VideoFrameReceived += value => Console.WriteLine($"Video {value.TrackSid}: {value.Frame.Width}x{value.Frame.Height}");
        room.CallbackError += error => Console.Error.WriteLine(error);
        await room.ConnectAsync(options.Url, options.Token).WaitAsync(TimeSpan.FromSeconds(30));
        await Task.Delay(options.Duration(15));
    }

    internal static async Task RecordRemote(ExampleOptions options)
    {
        var outputDir = options.Require("output-dir");
        if (!Directory.Exists(outputDir)) throw new DirectoryNotFoundException(outputDir);
        using var room = await options.Connect();
        var deadline = DateTime.UtcNow + options.Duration(15);
        var recorders = new Dictionary<string, TrackRecorder>();
        try
        {
            while (DateTime.UtcNow < deadline)
            {
                foreach (var participant in room.GetRemoteParticipants())
                foreach (var track in participant.TrackPublications ?? [])
                {
                    if (!track.IsSubscribed || track.Kind is not (1 or 2) || recorders.ContainsKey(track.Sid))
                        continue;
                    var path = Path.Combine(outputDir, track.Sid + (track.Kind == 1 ? ".wav" : ""));
                    var recorder = room.StartTrackRecording(participant.Identity, track.Sid, path);
                    recorders.Add(track.Sid, recorder);
                    Console.WriteLine($"Recording {track.Sid} to {recorder.OutputPath}");
                }
                await Task.Delay(200);
            }
        }
        finally
        {
            foreach (var recorder in recorders.Values)
            {
                recorder.Stop();
                var stats = recorder.Stats;
                Console.WriteLine($"{recorder.OutputPath}: {stats.FramesWritten} frames, {stats.BytesWritten} bytes");
                recorder.Dispose();
            }
        }
    }
}
