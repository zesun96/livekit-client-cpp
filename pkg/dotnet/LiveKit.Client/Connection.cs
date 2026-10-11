using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LiveKit.Client;

public enum IceTransportType { None = 0, Relay = 1, NoHost = 2, All = 3 }

public sealed record IceServer(IReadOnlyList<string> Urls, string Username = "", string Password = "");

public sealed class ConnectionOptions
{
    public bool AutoSubscribe { get; init; } = true;
    public bool AdaptiveStream { get; init; }
    public bool Dynacast { get; init; }
    public uint JoinRetries { get; init; } = 3;
    public uint ReconnectTimeoutMs { get; init; } = 15_000;
    public IceTransportType IceTransportType { get; init; } = IceTransportType.All;
    public IReadOnlyList<IceServer> IceServers { get; init; } = Array.Empty<IceServer>();
    /// <summary>Return a delay in milliseconds to retry, or null to stop reconnecting.</summary>
    public Func<ReconnectContext, uint?>? ReconnectPolicy { get; init; }
}

public sealed record ReconnectContext(uint RetryCount, ulong ElapsedMs, int Reason,
    string ServerUrl);

public sealed record TokenFetchRequest(bool ForceRefresh, string RoomName,
    string ParticipantName, string ParticipantIdentity, string ParticipantMetadata,
    string AgentName, string AgentMetadata, string Deployment);

public sealed record TokenCredentials(string ServerUrl, string ParticipantToken);

public sealed partial class Room
{
    private TokenSourceRegistration? _tokenSource;
    private ReconnectPolicyRegistration? _reconnectPolicy;
    private readonly List<object> _connectionCallbackRoots = new();

    public void Connect(string url, string token, ConnectionOptions options)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(url);
        ArgumentException.ThrowIfNullOrWhiteSpace(token);
        ArgumentNullException.ThrowIfNull(options);
        Run(() =>
        {
            using var arena = new NativeArena();
            var native = BuildOptions(options, arena);
            var policy = options.ReconnectPolicy is null ? null :
                new ReconnectPolicyRegistration(options.ReconnectPolicy);
            native.ReconnectPolicy = policy?.FunctionPointer ?? IntPtr.Zero;
            if (policy is not null) _connectionCallbackRoots.Add(policy);
            Native.Check(Native.lk_room_connect_with_options(_handle, url, token, ref native));
            _reconnectPolicy = policy;
        });
    }

    public void ConnectWithTokenSource(Func<TokenFetchRequest, TokenCredentials> provider,
        ConnectionOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(provider);
        Run(() =>
        {
            if (_tokenSource is not null)
                throw new InvalidOperationException("A token source is already registered for this room");
            var registration = new TokenSourceRegistration(provider);
            using var arena = new NativeArena();
            var fetch = new Native.TokenSourceFetchOptions();
            Native.lk_token_source_fetch_options_init(ref fetch);
            options ??= new ConnectionOptions();
            var connect = BuildOptions(options, arena);
            var policy = options.ReconnectPolicy is null ? null :
                new ReconnectPolicyRegistration(options.ReconnectPolicy);
            connect.ReconnectPolicy = policy?.FunctionPointer ?? IntPtr.Zero;
            _connectionCallbackRoots.Add(registration);
            if (policy is not null) _connectionCallbackRoots.Add(policy);
            Native.Check(Native.lk_room_connect_with_token_source_and_options(_handle,
                registration.Callback, IntPtr.Zero, ref fetch, ref connect));
            _tokenSource = registration;
            _reconnectPolicy = policy;
        });
    }

    private static Native.RoomConnectOptions BuildOptions(ConnectionOptions options, NativeArena arena)
    {
        var result = new Native.RoomConnectOptions();
        Native.lk_room_connect_options_init(ref result);
        result.AutoSubscribe = options.AutoSubscribe ? 1 : 0;
        result.AdaptiveStream = options.AdaptiveStream ? 1 : 0;
        result.Dynacast = options.Dynacast ? 1 : 0;
        result.JoinRetries = options.JoinRetries;
        result.ReconnectTimeoutMs = options.ReconnectTimeoutMs;
        result.IceTransportType = (int)options.IceTransportType;
        if (options.IceServers.Count != 0)
        {
            var nativeServers = new Native.IceServer[options.IceServers.Count];
            for (var i = 0; i < nativeServers.Length; i++)
            {
                var server = options.IceServers[i];
                if (server.Urls.Count == 0)
                    throw new ArgumentException("ICE server must have at least one URL", nameof(options));
                var urls = new IntPtr[server.Urls.Count];
                for (var j = 0; j < urls.Length; j++) urls[j] = arena.Utf8(server.Urls[j]);
                nativeServers[i] = new Native.IceServer
                {
                    StructSize = (nuint)Marshal.SizeOf<Native.IceServer>(),
                    Urls = arena.PointerArray(urls),
                    UrlCount = (nuint)urls.Length,
                    Username = arena.Utf8(server.Username),
                    Password = arena.Utf8(server.Password),
                };
            }
            result.IceServers = arena.StructArray(nativeServers);
            result.IceServerCount = (nuint)nativeServers.Length;
        }
        return result;
    }
}

internal sealed class ReconnectPolicyRegistration
{
    private readonly Func<ReconnectContext, uint?> _provider;
    private readonly Native.ReconnectPolicyCallback _callback;
    internal IntPtr FunctionPointer => Marshal.GetFunctionPointerForDelegate(_callback);

    internal ReconnectPolicyRegistration(Func<ReconnectContext, uint?> provider)
    {
        _provider = provider;
        _callback = Invoke;
    }

    private int Invoke(IntPtr userData, IntPtr context, out uint delayMs)
    {
        delayMs = 0;
        try
        {
            var input = Marshal.PtrToStructure<Native.ReconnectContext>(context);
            var delay = _provider(new ReconnectContext(input.RetryCount, input.ElapsedMs,
                input.Reason, Marshal.PtrToStringUTF8(input.ServerUrl) ?? string.Empty));
            if (!delay.HasValue) return 0;
            delayMs = delay.Value;
            return 1;
        }
        catch { return 0; }
    }
}

internal sealed class NativeArena : IDisposable
{
    private readonly List<IntPtr> _allocations = new();

    internal IntPtr Utf8(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        var pointer = Marshal.StringToCoTaskMemUTF8(value);
        _allocations.Add(pointer);
        return pointer;
    }

    internal IntPtr PointerArray(IntPtr[] values)
    {
        var pointer = Marshal.AllocCoTaskMem(checked(values.Length * IntPtr.Size));
        _allocations.Add(pointer);
        for (var i = 0; i < values.Length; i++) Marshal.WriteIntPtr(pointer, i * IntPtr.Size, values[i]);
        return pointer;
    }

    internal IntPtr StructArray<T>(T[] values) where T : struct
    {
        var elementSize = Marshal.SizeOf<T>();
        var pointer = Marshal.AllocCoTaskMem(checked(values.Length * elementSize));
        _allocations.Add(pointer);
        for (var i = 0; i < values.Length; i++)
            Marshal.StructureToPtr(values[i], IntPtr.Add(pointer, i * elementSize), false);
        return pointer;
    }

    public void Dispose()
    {
        foreach (var pointer in _allocations) Marshal.FreeCoTaskMem(pointer);
        _allocations.Clear();
    }
}

internal sealed class TokenSourceRegistration : IDisposable
{
    private readonly Func<TokenFetchRequest, TokenCredentials> _provider;
    private readonly object _sync = new();
    private readonly Dictionary<int, IntPtr[]> _buffers = new();
    internal Native.TokenSourceCallback Callback { get; }

    internal TokenSourceRegistration(Func<TokenFetchRequest, TokenCredentials> provider)
    {
        _provider = provider;
        Callback = Invoke;
    }

    private int Invoke(IntPtr userData, IntPtr options, int forceRefresh, IntPtr response)
    {
        try
        {
            var input = Marshal.PtrToStructure<Native.TokenSourceFetchOptions>(options);
            var credentials = _provider(new TokenFetchRequest(forceRefresh != 0,
                Marshal.PtrToStringUTF8(input.RoomName) ?? string.Empty,
                Marshal.PtrToStringUTF8(input.ParticipantName) ?? string.Empty,
                Marshal.PtrToStringUTF8(input.ParticipantIdentity) ?? string.Empty,
                Marshal.PtrToStringUTF8(input.ParticipantMetadata) ?? string.Empty,
                Marshal.PtrToStringUTF8(input.AgentName) ?? string.Empty,
                Marshal.PtrToStringUTF8(input.AgentMetadata) ?? string.Empty,
                Marshal.PtrToStringUTF8(input.Deployment) ?? string.Empty));
            if (credentials is null || string.IsNullOrWhiteSpace(credentials.ServerUrl) ||
                string.IsNullOrWhiteSpace(credentials.ParticipantToken)) return 1;
            lock (_sync)
            {
                var id = Environment.CurrentManagedThreadId;
                if (_buffers.Remove(id, out var old)) Free(old);
                var current = new[]
                {
                    Marshal.StringToCoTaskMemUTF8(credentials.ServerUrl),
                    Marshal.StringToCoTaskMemUTF8(credentials.ParticipantToken),
                };
                _buffers[id] = current;
                Marshal.StructureToPtr(new Native.TokenSourceResponse
                {
                    StructSize = (nuint)Marshal.SizeOf<Native.TokenSourceResponse>(),
                    ServerUrl = current[0], ParticipantToken = current[1],
                }, response, false);
            }
            return 0;
        }
        catch { return 3; }
    }

    private static void Free(IntPtr[] pointers)
    {
        foreach (var pointer in pointers) Marshal.FreeCoTaskMem(pointer);
    }

    public void Dispose()
    {
        lock (_sync)
        {
            foreach (var pointers in _buffers.Values) Free(pointers);
            _buffers.Clear();
        }
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct IceServer
    {
        internal nuint StructSize;
        internal IntPtr Urls;
        internal nuint UrlCount;
        internal IntPtr Username;
        internal IntPtr Password;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RoomConnectOptions
    {
        internal nuint StructSize;
        internal int AutoSubscribe;
        internal int AdaptiveStream;
        internal int Dynacast;
        internal uint JoinRetries;
        internal uint ReconnectTimeoutMs;
        internal IntPtr IceServers;
        internal nuint IceServerCount;
        internal int ContinualGatheringPolicy;
        internal int IceTransportType;
        internal IntPtr ReconnectPolicy;
        internal IntPtr ReconnectPolicyUserData;
        internal IntPtr E2eeOptions;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ReconnectContext
    {
        internal nuint StructSize;
        internal uint RetryCount;
        internal ulong ElapsedMs;
        internal int Reason;
        internal IntPtr ServerUrl;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int ReconnectPolicyCallback(IntPtr userData, IntPtr context, out uint delayMs);

    [StructLayout(LayoutKind.Sequential)]
    internal struct TokenSourceFetchOptions
    {
        internal nuint StructSize;
        internal IntPtr RoomName;
        internal IntPtr ParticipantName;
        internal IntPtr ParticipantIdentity;
        internal IntPtr ParticipantMetadata;
        internal IntPtr ParticipantAttributes;
        internal nuint ParticipantAttributeCount;
        internal IntPtr AgentName;
        internal IntPtr AgentMetadata;
        internal IntPtr Deployment;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TokenSourceResponse
    {
        internal nuint StructSize;
        internal IntPtr ServerUrl;
        internal IntPtr ParticipantToken;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int TokenSourceCallback(IntPtr userData, IntPtr options, int forceRefresh,
        IntPtr response);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_room_connect_options_init(ref RoomConnectOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void lk_token_source_fetch_options_init(ref TokenSourceFetchOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_connect_with_options(RoomHandle room,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string url,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string token, ref RoomConnectOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int lk_room_connect_with_token_source_and_options(RoomHandle room,
        TokenSourceCallback callback, IntPtr userData,
        ref TokenSourceFetchOptions fetchOptions, ref RoomConnectOptions connectOptions);
}
