package io.livekit.client;

import java.util.Objects;

/** ICE transport: 0 none, 1 relay, 2 no host, 3 all. Gathering: 0 once, 1 continually. */
public record ConnectionOptions(boolean autoSubscribe, boolean adaptiveStream, boolean dynacast,
                                int joinRetries, int reconnectTimeoutMs, IceServer[] iceServers,
                                int iceTransportType, int continualGatheringPolicy, E2eeOptions e2ee,
                                ReconnectPolicy reconnectPolicy) {
    public ConnectionOptions {
        if (joinRetries < 1 || reconnectTimeoutMs < 1) throw new IllegalArgumentException("Invalid retry settings");
        if (iceTransportType < 0 || iceTransportType > 3 ||
                continualGatheringPolicy < 0 || continualGatheringPolicy > 1)
            throw new IllegalArgumentException("Invalid ICE settings");
        iceServers = Objects.requireNonNull(iceServers).clone();
        for (IceServer server : iceServers) Objects.requireNonNull(server);
    }

    public ConnectionOptions(boolean autoSubscribe, boolean adaptiveStream, boolean dynacast,
                             int joinRetries, int reconnectTimeoutMs, IceServer[] iceServers,
                             int iceTransportType, int continualGatheringPolicy, E2eeOptions e2ee) {
        this(autoSubscribe, adaptiveStream, dynacast, joinRetries, reconnectTimeoutMs, iceServers,
            iceTransportType, continualGatheringPolicy, e2ee, null);
    }

    public ConnectionOptions() { this(true, false, false, 3, 15_000, new IceServer[0], 3, 1, null, null); }
    @Override public IceServer[] iceServers() { return iceServers.clone(); }
}
