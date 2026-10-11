using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace LiveKit.Client;

public sealed partial class Room
{
    /// <summary>
    /// Publishes data on a worker thread. Cancellation can stop a queued call, but cannot
    /// interrupt the native publish call once it has started.
    /// </summary>
    public Task PublishDataAsync(byte[] data, bool reliable = true, string topic = "",
        CancellationToken cancellationToken = default)
        => PublishDataAsync(data, new DataPublishRequest { Reliable = reliable, Topic = topic },
            cancellationToken);

    /// <summary>
    /// Publishes a snapshot of the payload and request on a worker thread.
    /// </summary>
    public Task PublishDataAsync(byte[] data, DataPublishRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(data);
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        var payload = (byte[])data.Clone();
        var destinations = request.DestinationIdentities?.ToArray()
            ?? throw new ArgumentException("Destination identities cannot be null", nameof(request));
        var snapshot = request with { DestinationIdentities = destinations };
        return Task.Run(() => PublishData(payload, snapshot), cancellationToken);
    }
}
