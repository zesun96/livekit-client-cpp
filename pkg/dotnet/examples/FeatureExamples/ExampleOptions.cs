using LiveKit.Client;

internal sealed class ExampleOptions
{
    private readonly Dictionary<string, string> _values = new(StringComparer.Ordinal);

    internal ExampleOptions(string[] args)
    {
        for (var i = 0; i < args.Length; i += 2)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal) || i + 1 == args.Length)
                throw new ArgumentException("Options must be --name value pairs");
            _values[args[i][2..]] = args[i + 1];
        }
    }

    internal string? Get(string name) => _values.GetValueOrDefault(name);
    internal string Require(string name) => Get(name)
        ?? throw new ArgumentException($"Set --{name}");
    internal string Url => Get("url") ?? Environment.GetEnvironmentVariable("LIVEKIT_URL")
        ?? throw new ArgumentException("Set LIVEKIT_URL or --url");
    internal string Token => Get("token") ?? Environment.GetEnvironmentVariable("LIVEKIT_TOKEN")
        ?? throw new ArgumentException("Set LIVEKIT_TOKEN or --token");
    internal TimeSpan Duration(double defaultSeconds = 5)
    {
        var seconds = Get("seconds") is { } value ?
            double.Parse(value, System.Globalization.CultureInfo.InvariantCulture) : defaultSeconds;
        if (seconds <= 0 || seconds > 3600) throw new ArgumentOutOfRangeException("seconds");
        return TimeSpan.FromSeconds(seconds);
    }

    internal async Task<Room> Connect()
    {
        var room = new Room();
        try
        {
            await room.ConnectAsync(Url, Token).WaitAsync(TimeSpan.FromSeconds(30));
            return room;
        }
        catch { room.Dispose(); throw; }
    }
}
