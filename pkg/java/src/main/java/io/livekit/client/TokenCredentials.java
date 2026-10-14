package io.livekit.client;

import java.util.Objects;

/** URL and participant token returned by a token source. */
public record TokenCredentials(String serverUrl, String participantToken) {
    public TokenCredentials {
        if (Objects.requireNonNull(serverUrl).isEmpty() ||
            Objects.requireNonNull(participantToken).isEmpty())
            throw new IllegalArgumentException("Token credentials must not be empty");
    }
}
