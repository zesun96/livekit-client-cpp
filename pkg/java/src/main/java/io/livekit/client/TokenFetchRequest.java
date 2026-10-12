package io.livekit.client;

import java.util.Map;

/** Request passed to a token source on initial connect or token refresh. */
public record TokenFetchRequest(boolean forceRefresh, String roomName, String participantName,
                                String participantIdentity, String participantMetadata,
                                String agentName, String agentMetadata, String deployment,
                                Map<String, String> participantAttributes) {
    public TokenFetchRequest { participantAttributes = Map.copyOf(participantAttributes); }
}
