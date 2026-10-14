package io.livekit.client;

import java.util.List;
import java.util.Map;
import java.util.Objects;

/** Detached participant, permission, attribute and publication snapshot. */
public record ParticipantDetails(Participant participant, ParticipantPermissions permissions,
                                 Map<String, String> attributes,
                                 List<RemoteTrackPublication> publications) {
    public ParticipantDetails {
        Objects.requireNonNull(participant);
        Objects.requireNonNull(permissions);
        attributes = Map.copyOf(Objects.requireNonNull(attributes));
        publications = List.copyOf(Objects.requireNonNull(publications));
    }
}
