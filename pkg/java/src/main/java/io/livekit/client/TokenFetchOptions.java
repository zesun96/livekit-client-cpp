package io.livekit.client;

import java.util.Map;
import java.util.Objects;

/** Fields passed to the token source for initial connect and refresh. */
public record TokenFetchOptions(String roomName, String participantName,
                                String participantIdentity, String participantMetadata,
                                Map<String, String> participantAttributes, String agentName,
                                String agentMetadata, String deployment) {
    public TokenFetchOptions {
        roomName = Objects.requireNonNull(roomName);
        participantName = Objects.requireNonNull(participantName);
        participantIdentity = Objects.requireNonNull(participantIdentity);
        participantMetadata = Objects.requireNonNull(participantMetadata);
        participantAttributes = Map.copyOf(participantAttributes);
        agentName = Objects.requireNonNull(agentName);
        agentMetadata = Objects.requireNonNull(agentMetadata);
        deployment = Objects.requireNonNull(deployment);
    }

    public TokenFetchOptions() { this("", "", "", "", Map.of(), "", "", ""); }
}
