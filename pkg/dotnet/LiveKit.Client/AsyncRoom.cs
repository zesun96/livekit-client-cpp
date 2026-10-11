using System;
using System.Threading;
using System.Threading.Tasks;

namespace LiveKit.Client;

public sealed partial class Room
{
    /// <summary>Completes when the room reaches the connected state. Cancellation only stops waiting.</summary>
    public async Task ConnectAsync(string url, string token,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void ConnectedHandler() => completion.TrySetResult();
        void DisconnectedHandler() => completion.TrySetException(
            new LiveKitException(-1, "Room disconnected before connecting"));
        Connected += ConnectedHandler;
        Disconnected += DisconnectedHandler;
        try
        {
            Connect(url, token);
            if (IsConnected) completion.TrySetResult();
            await completion.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            Connected -= ConnectedHandler;
            Disconnected -= DisconnectedHandler;
        }
    }

    /// <summary>Completes when the room reaches the disconnected state. Cancellation only stops waiting.</summary>
    public async Task DisconnectAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void DisconnectedHandler() => completion.TrySetResult();
        Disconnected += DisconnectedHandler;
        try
        {
            Disconnect();
            if (State == ConnectionState.Disconnected) completion.TrySetResult();
            await completion.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        finally { Disconnected -= DisconnectedHandler; }
    }
}
