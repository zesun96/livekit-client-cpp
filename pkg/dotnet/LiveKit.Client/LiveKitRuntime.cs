using System;
using System.Threading;

namespace LiveKit.Client;

/// <summary>Provides an explicit, reference-counted native runtime lifetime.</summary>
public static class LiveKitRuntime
{
    /// <summary>
    /// Initializes the native runtime. Dispose the returned lease after all native SDK
    /// objects created during its lifetime have been disposed.
    /// </summary>
    public static LiveKitRuntimeLease Acquire()
    {
        Native.Check(Native.lk_init());
        return new LiveKitRuntimeLease();
    }
}

/// <summary>Releases one native runtime reference when disposed.</summary>
public sealed class LiveKitRuntimeLease : IDisposable
{
    private int _active = 1;

    internal LiveKitRuntimeLease() { }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _active, 0) != 0)
            Native.Check(Native.lk_shutdown());
    }
}
